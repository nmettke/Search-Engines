#include "Crawler.h"
#include "checkpoint.h"
#include "url_dedup.h"
#include "utils/threads/lock_guard.hpp"
#include "utils/threads/mutex.hpp"
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>
#include <time.h>
#include <unistd.h>

static std::atomic<bool> shouldStop{false};
Frontier *f = nullptr;

static void signalHandler(int) {
    shouldStop = true;
    if (f)
        f->shutdown();
}

static int64_t nowMillis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

Checkpoint *checkpoint;
std::atomic<size_t> urlsCrawled{0};
UrlBloomFilter bloom(1000000, 0.0001);
RobotsCache *robotsCache = nullptr;
unsigned int cores = std::thread::hardware_concurrency();
mutex crawlLogLock;

static void logCrawled(size_t count, const string &url) {
    lock_guard<mutex> guard(crawlLogLock);
    (void)count;
    (void)url;
}

void *WorkerThread(void *arg) {
    while (std::optional<FrontierItem> item = f->pop()) {
        if (shouldStop)
            break;

        struct TaskCompletionGuard {
            Frontier &frontier;
            bool active = true;
            ~TaskCompletionGuard() {
                if (active)
                    frontier.taskDone();
            }
            void dismiss() { active = false; }
        } taskCompletionGuard{*f};

        RobotCheckResult check = robotsCache->checkAndReserve(item->link);

        if (check.status == RobotCheckStatus::DISALLOWED) {
            std::cerr << "Blocked by robots.txt: " << item->link << '\n';
            continue;
        }

        if (check.status == RobotCheckStatus::DELAYED) {
            f->snoozeCurrent(*item, check.readyAtMs);
            taskCompletionGuard.dismiss();
            continue;
        }

        string page = readURL(item->link);
        if (shouldStop)
            break;

        if (page.empty())
            continue;

        HtmlParser parsed(page.cstr(), page.size());
        parsed.sourceUrl = item->link;
        parsed.seedDistance = static_cast<uint8_t>(item->getSeedDistance());

        if (parsed.isBroken() || !parsed.isEnglish()) {
            continue;
        }

        vector<FrontierItem> discoveredLinks;

        for (const Link &link : parsed.links) {
            string resolved = absolutizeUrl(link.URL, item->link, parsed.base);
            string canonical;
            if (shouldEnqueueUrl(resolved, bloom, canonical)) {
                discoveredLinks.pushBack(FrontierItem(canonical, *item));
            }
        }

        f->pushMany(discoveredLinks);
        size_t crawled = ++urlsCrawled;
        logCrawled(crawled, item->link);
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

    if (checkpoint->load(recoveredItems, bloom, loadedCount) && recoveredItems.size() > 0) {
        urlsCrawled = loadedCount;
        f = new Frontier(recoveredItems);
        std::cerr << "Recovered from checkpoint at " << loadedCount << " URLs\n";
    } else {
        if (loadedCount > 0 && recoveredItems.size() == 0) {
            std::cerr << "Checkpoint has empty frontier; starting fresh from seed list\n";
        }
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
