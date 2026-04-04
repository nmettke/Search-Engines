#include "Crawler.h"
#include "checkpoint.h"
#include "url_dedup.h"
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
size_t urlsCrawled = 0;
UrlBloomFilter bloom(1000000, 0.0001);
unsigned int cores = std::thread::hardware_concurrency();

void *WorkerThread(void *arg) {
    while (std::optional<FrontierItem> item = f->pop()) {
        if (shouldStop)
            break;

        struct TaskCompletionGuard {
            Frontier &frontier;
            ~TaskCompletionGuard() { frontier.taskDone(); }
        } taskCompletionGuard{*f};

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
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    cpConfig.directory = "src/crawler";
    cpConfig.interval = 500;
    checkpoint = new Checkpoint(cpConfig);

    vector<FrontierItem> recoveredItems;
    urlsCrawled = 0;

    if (checkpoint->load(recoveredItems, bloom, urlsCrawled)) {
        f = new Frontier(recoveredItems);
        std::cerr << "Recovered from checkpoint at " << urlsCrawled << " URLs\n";
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

    delete f;
    delete checkpoint;
    return 0;
}
