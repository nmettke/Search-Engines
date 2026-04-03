// File stub for crawler orchestrator file
#include "Crawler.h"
#include "url_dedup.h"
#include <thread>

Frontier f("src/crawler/seedList.txt");
UrlBloomFilter bloom(1000000, 0.0001);
unsigned int cores = std::thread::hardware_concurrency();

void *WorkerThread(void *arg) {
    while (std::optional<FrontierItem> item = f.pop()) {
        struct TaskCompletionGuard {
            Frontier &frontier;
            ~TaskCompletionGuard() { frontier.taskDone(); }
        } taskCompletionGuard{f};

        string page = readURL(item->link);
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

        f.pushMany(discoveredLinks);
        std::cout << "Crawled " << item->link << '\n';
    }

    return nullptr;
}

int main() {
    size_t ThreadCount = cores * 3;
    vector<pthread_t> threads(ThreadCount);

    for (int i = 0; i < ThreadCount; i++) {
        pthread_create(&threads[i], nullptr, WorkerThread, nullptr);
    }

    for (int i = 0; i < ThreadCount; i++) {
        pthread_join(threads[i], nullptr);
    }
}