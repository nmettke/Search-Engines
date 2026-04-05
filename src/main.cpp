// Main file, should combine Crawler and Index for the new distributed design
// Taken from Crawler.cpp

#include "./crawler/Crawler.h"
#include "./crawler/checkpoint.h"
#include "./crawler/url_dedup.h"
#include "./index/src/lib/chunk_flusher.h"
#include "./index/src/lib/disk_chunk_reader.h"
#include "./index/src/lib/in_memory_index.h"
#include "./index/src/lib/indexQueue.h"
#include "./index/src/lib/tokenizer.h"

#include <csignal>
#include <iostream>
#include <optional>
#include <thread>

static volatile bool shouldStop = false;
Frontier *f = nullptr;
IndexQueue *q = nullptr;
InMemoryIndex index;

static void signalHandler(int) {
    shouldStop = true;
    if (f)
        f->shutdown();
    if (q)
        q->shutdown();
}

CheckpointConfig cpConfig;
Checkpoint *checkpoint;
std::atomic<size_t> urlsCrawled{0};
UrlBloomFilter bloom(1000000, 0.0001);
unsigned int cores = std::thread::hardware_concurrency();

void *CrawlerWorkerThread(void *arg) {
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

        // push to index queue
        q->push(parsed);

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

void *IndexWorkerThread(void *arg) {
    // index worker thread to take from queue and process
    // should only be one worker thread
    Tokenizer tokenizer;
    size_t doc_processed = 0;
    size_t chunks_written = 0;

    // Tokenize and build the In-Memory Index
    while (std::optional<HtmlParser> doc = q->pop()) {
        auto tokenized = tokenizer.processDocument(doc);
        for (const auto &tok : tokenized.tokens) {
            index.addToken(tok);
        }
        index.finishDocument(tokenized.doc_end);
        ++doc_processed;

        if (doc_processed >= 512) {
            // flush every 512 docs
            // Flush the chunk to disk using OUR flusher interface
            const std::string path = "chunk_" << chunks_written << ".idx";
            try {
                flushIndexChunk(index, path);
                std::cout << "Successfully wrote chunk with " << doc_processed
                          << "docs to: " << path << "\n"
                          << "total chuncks: " << chunks_written;
            } catch (const std::exception &e) {
                std::cerr << "Failed to write chunk: " << e.what() << "\n";
                return 1;
            }
            ++chunks_written;
        }
    }
    return nullptr;
}

int main() {
    initSSL();
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    cpConfig.directory = "src/crawler";
    checkpoint = new Checkpoint(cpConfig);

    vector<FrontierItem> recoveredItems;
    size_t loadedCount = 0;
    q = new IndexQueue();

    if (checkpoint->load(recoveredItems, bloom, loadedCount, q)) {
        urlsCrawled = loadedCount;
        f = new Frontier(recoveredItems);
        std::cerr << "Recovered from checkpoint at " << loadedCount << " URLs\n";
    } else {
        f = new Frontier("src/crawler/seedList.txt");
        std::cerr << "Starting fresh from seed list\n";
    }

    // Background checkpoint thread — saves every 10 minutes
    auto CheckpointThread = [](void *) -> void * {
        const int intervalSeconds = 600;
        while (!shouldStop) {
            for (int i = 0; i < intervalSeconds && !shouldStop; ++i)
                sleep(1);
            if (!shouldStop)
                checkpoint->save(*f, bloom, urlsCrawled, q);
        }
        return nullptr;
    };

    pthread_t cpThread;
    pthread_create(&cpThread, nullptr, CheckpointThread, nullptr);

    size_t CrawlerThreadCount = cores * 3;
    size_t IndexThreadCount = 1;
    vector<pthread_t> crawlerThreads(CrawlerThreadCount);
    vector<pthread_t> indexThreads(IndexThreadCount);

    for (size_t i = 0; i < CrawlerThreadCount; i++) {
        pthread_create(&crawlerThreads[i], nullptr, CrawlerWorkerThread, nullptr);
    }

    for (size_t i = 0; i < IndexThreadCount; i++) {
        pthread_create(&indexThreads[i], nullptr, IndexWorkerThread, nullptr);
    }

    for (size_t i = 0; i < CrawlerThreadCount; i++) {
        pthread_join(crawlerThreads[i], nullptr);
    }

    for (size_t i = 0; i < IndexThreadCount; i++) {
        pthread_join(indexThreads[i], nullptr);
    }

    pthread_join(cpThread, nullptr);
    checkpoint->save(*f, bloom, urlsCrawled, q);

    if (shouldStop)
        std::cerr << "Graceful shutdown after SIGINT\n";

    delete q;
    delete f;
    delete checkpoint;
    return 0;
}
