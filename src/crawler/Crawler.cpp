#include "Crawler.h"
#include "checkpoint.h"
#include "url_dedup.h"
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>
#include <unistd.h>

static std::atomic<bool> shouldStop{false};
Frontier *f = nullptr;

static void signalHandler(int) {
    shouldStop = true;
    if (f)
        f->shutdown();
}

Checkpoint *checkpoint;
std::atomic<size_t> urlsCrawled{0};
UrlBloomFilter bloom(1000000, 0.0001);
RobotsCache *robotsCache = nullptr;
unsigned int cores = std::thread::hardware_concurrency();

void *WorkerThread(void *arg) {
    while (std::optional<FrontierItem> item = f->pop()) {
        if (shouldStop)
            break;

        struct TaskCompletionGuard {
            Frontier &frontier;
            ~TaskCompletionGuard() { frontier.taskDone(); }
        } taskCompletionGuard{*f};

        if (!robotsCache->isAllowed(item->link)) {
            std::cerr << "Blocked by robots.txt: " << item->link << '\n';
            continue;
        }

        string page = readURL(item->link);
        if (shouldStop)
            break;

        HtmlParser parsed(page.cstr(), page.size());
        vector<string> discoveredLinks;

        for (const Link &link : parsed.links) {
            if (link.URL.find("http") != link.URL.npos) {
                string canonical;
                if (shouldEnqueueUrl(link.URL, bloom, canonical)) {
                    discoveredLinks.pushBack(canonical);
                }
            }
        }

        f->pushMany(discoveredLinks);
        ++urlsCrawled;
        std::cout << "Crawled [" << urlsCrawled << "] " << item->link << '\n';
    }

    return nullptr;
}

// Runs on its own thread, saves checkpoint every 10 minutes
void *CheckpointThread(void *arg) {
    const int intervalSeconds = 600;
    while (!shouldStop) {
        for (int i = 0; i < intervalSeconds && !shouldStop; ++i) {
            sleep(1);
        }
        if (!shouldStop) {
            checkpoint->save(*f, bloom, urlsCrawled);
        }
    }
    return nullptr;
}

int main() {
    initSSL();
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    CheckpointConfig cpConfig;
    cpConfig.directory = "src/crawler";
    checkpoint = new Checkpoint(cpConfig);
    robotsCache = new RobotsCache();

    vector<FrontierItem> recoveredItems;
    size_t loadedCount = 0;

    if (checkpoint->load(recoveredItems, bloom, loadedCount)) {
        urlsCrawled = loadedCount;
        f = new Frontier(recoveredItems);
        std::cerr << "Recovered from checkpoint at " << loadedCount << " URLs\n";
    } else {
        f = new Frontier("src/crawler/seedList.txt");
        std::cerr << "Starting fresh from seed list\n";
    }

    // Start checkpoint thread
    pthread_t cpThread;
    pthread_create(&cpThread, nullptr, CheckpointThread, nullptr);

    size_t ThreadCount = cores * 3;
    vector<pthread_t> threads(ThreadCount);

    for (size_t i = 0; i < ThreadCount; i++) {
        pthread_create(&threads[i], nullptr, WorkerThread, nullptr);
    }

    for (size_t i = 0; i < ThreadCount; i++) {
        pthread_join(threads[i], nullptr);
    }

    // Stop checkpoint thread
    pthread_join(cpThread, nullptr);

    // Final save on exit
    checkpoint->save(*f, bloom, urlsCrawled);

    if (shouldStop)
        std::cerr << "Graceful shutdown after SIGINT\n";

    delete robotsCache;
    delete f;
    delete checkpoint;
    cleanupSSL();
    return 0;
}
