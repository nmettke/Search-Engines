#include "Crawler.h"
#include "checkpoint.h"
#include "url_dedup.h"
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

static volatile bool shouldStop = false;
Frontier *f = nullptr;

static void signalHandler(int) {
    shouldStop = true;
    if (f)
        f->shutdown();
}

CheckpointConfig cpConfig;
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

        if (!shouldStop && checkpoint->shouldCheckpoint(urlsCrawled)) {
            checkpoint->save(*f, bloom, urlsCrawled);
        }
    }

    return nullptr;
}

int main() {
    initSSL();
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    cpConfig.directory = "src/crawler";
    cpConfig.interval = 500;
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

    size_t ThreadCount = cores * 3;
    vector<pthread_t> threads(ThreadCount);

    for (size_t i = 0; i < ThreadCount; i++) {
        pthread_create(&threads[i], nullptr, WorkerThread, nullptr);
    }

    for (size_t i = 0; i < ThreadCount; i++) {
        pthread_join(threads[i], nullptr);
    }

    checkpoint->save(*f, bloom, urlsCrawled);

    if (shouldStop)
        std::cerr << "Graceful shutdown after SIGINT\n";

    delete robotsCache;
    delete f;
    delete checkpoint;
    cleanupSSL();
    return 0;
}
