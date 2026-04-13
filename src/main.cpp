// Main file, should combine Crawler and Index for the new distributed design
// Taken from Crawler.cpp

#include "./crawler/RobotsCache.h"
#include "./crawler/checkpoint.h"
#include "./crawler/frontier.h"
#include "./crawler/url_dedup.h"
#include "./index/src/lib/Common.h"
#include "./index/src/lib/chunk_flusher.h"
#include "./index/src/lib/disk_chunk_reader.h"
#include "./index/src/lib/in_memory_index.h"
#include "./index/src/lib/indexQueue.h"
#include "./index/src/lib/tokenizer.h"
#include "./parser/HtmlParser.h"
#include "./utils/SSL/LinuxSSL_Crawler.hpp"
#include "./utils/string.hpp"
#include "./utils/threads/condition_variable.hpp"
#include "./utils/vector.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <optional>
#include <thread>

#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

static volatile bool shouldStop = false;
Frontier *f = nullptr;
IndexQueue *q = nullptr;
InMemoryIndex mem_index;
// Distributed args
std::atomic<size_t> numLinkThreshold = 128; // how much is in batch before we push
vector<vector<Link>> batches;
mutex batch_lock;
std::atomic<size_t> machine_id; // machine id for this machine
vector<string> peer_address;    // address to send to
mutex addr_lock;
condition_variable batch_cv;

static void signalHandler(int) {
    shouldStop = true;
    batch_cv.notify_all();
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
    addr_lock.lock();
    size_t num_machine = peer_address.size();
    addr_lock.unlock();
    if (num_machine == 0) {
        num_machine = 1;
    }

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
        parsed.sourceUrl = item->link;
        parsed.seedDistance = static_cast<uint8_t>(item->getSeedDistance());
        vector<FrontierItem> discoveredLinks;

        // push to index queue
        q->push(parsed);

        for (const Link &link : parsed.links) {
            string resolved = absolutizeUrl(link.URL, item->link, parsed.base);
            if (resolved.empty()) {
                continue;
            }

            size_t hashto = hashString(resolved.cstr()) % num_machine;
            if (hashto != machine_id.load()) {
                Link routedLink(resolved);
                routedLink.anchorText = link.anchorText;
                batch_lock.lock();
                batches[hashto].pushBack(routedLink);

                if (batches[hashto].size() >= numLinkThreshold.load()) {
                    batch_cv.notify_one();
                }

                batch_lock.unlock();
                continue;
            }
            string canonical;
            if (shouldEnqueueUrl(resolved, bloom, canonical)) {
                discoveredLinks.pushBack(FrontierItem(canonical, *item));
            }
        }

        f->pushMany(discoveredLinks);
        ++urlsCrawled;
        std::cout << "Crawled [" << urlsCrawled << "] " << item->link << '\n';

        if (!shouldStop && (urlsCrawled.load() % 500) == 0) {
            checkpoint->save(*f, bloom, urlsCrawled.load());
        }
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
        auto tokenized = tokenizer.processDocument(*doc);
        for (const auto &tok : tokenized.tokens) {
            mem_index.addToken(tok);
        }
        mem_index.finishDocument(tokenized.doc_end);
        ++doc_processed;

        if (doc_processed >= 512) {
            // flush every 512 docs
            // Flush the chunk to disk using OUR flusher interface
            char chunk_path_buf[64];
            std::snprintf(chunk_path_buf, sizeof(chunk_path_buf), "chunk_%zu.idx", chunks_written);
            const string path(chunk_path_buf);
            try {
                flushIndexChunk(mem_index, path);
                std::cout << "Successfully wrote chunk with " << doc_processed
                          << "docs to: " << path << "\n"
                          << "total chuncks: " << chunks_written;
            } catch (const std::exception &e) {
                std::cerr << "Failed to write chunk: " << e.what() << "\n";
                return nullptr;
            }
            ++chunks_written;
        }
    }
    return nullptr;
}

static bool hasReadyBatch() {
    // assumes batch locked
    for (const auto &batch : batches) {
        if (batch.size() >= numLinkThreshold.load() || (shouldStop && batch.size() > 0)) {
            return true;
        }
    }
    return false;
}

static bool sendBatchToPeer(const string &peer, const vector<Link> &batch) {
    if (batch.size() == 0) {
        return true;
    }

    string host;
    string port;

    bool valid_addr = false;

    size_t colon = peer.find(':');
    if (colon == string::npos) {
        valid_addr = false;
    } else {
        host = string(peer.cstr(), peer.cstr() + colon);
        port = string(peer.cstr() + colon + 1);
        valid_addr = !host.empty() && !port.empty();
    }

    if (!valid_addr) {
        std::cerr << "Invalid peer address: " << peer << '\n';
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *result = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) {
        std::cerr << "Failed to resolve peer " << peer << '\n';
        return false;
    }

    int socket_fd = -1;
    for (addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        socket_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (socket_fd < 0) {
            continue;
        }

        if (connect(socket_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(socket_fd);
        socket_fd = -1;
    }

    freeaddrinfo(result);

    if (socket_fd < 0) {
        std::cerr << "Failed to connect to peer " << peer << '\n';
        return false;
    }

    string payload;
    payload.reserve(batch.size() * 64);
    for (const Link &link : batch) {
        payload += link.URL.cstr();
        payload.pushBack('\n');
    }

    const char *data = payload.data();
    size_t remaining = payload.size();
    while (remaining > 0) {
        ssize_t sent = send(socket_fd, data, remaining, 0);
        if (sent < 0) {
            std::cerr << "Failed while sending to peer " << peer << ": " << std::strerror(errno)
                      << '\n';
            close(socket_fd);
            return false;
        }

        data += sent;
        remaining -= static_cast<size_t>(sent);
    }

    close(socket_fd);
    return true;
}

void *SendToMachineThread(void *arg) {
    while (true) {
        vector<vector<Link>> ready_batches;

        batch_lock.lock();

        while (!shouldStop && !hasReadyBatch()) {
            batch_cv.wait(batch_lock);
        }

        if (shouldStop && !hasReadyBatch()) {
            batch_lock.unlock();
            break;
        }

        ready_batches = vector<vector<Link>>(batches.size());
        for (size_t i = 0; i < batches.size(); ++i) {
            if (batches[i].size() >= numLinkThreshold.load() ||
                (shouldStop && batches[i].size() > 0)) {
                ready_batches[i] = std::move(batches[i]);
            }
        }

        batch_lock.unlock();

        for (size_t i = 0; i < ready_batches.size(); ++i) {
            if (ready_batches[i].size() == 0) {
                continue;
            }

            if (i == machine_id.load()) {
                // this should never happen but we add just in case
                // batches[machine_id] should always be empty
                vector<string> local_urls;
                for (const Link &link : ready_batches[i]) {
                    string canonical;
                    if (shouldEnqueueUrl(link.URL, bloom, canonical)) {
                        local_urls.pushBack(canonical);
                    }
                }
                f->pushMany(local_urls);
                continue;
            }

            if (!sendBatchToPeer(peer_address[i], ready_batches[i])) {
                batch_lock.lock();
                for (const Link &link : ready_batches[i]) {
                    batches[i].pushBack(link);
                }
                batch_lock.unlock();
                batch_cv.notify_one();
            }
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
    q = new IndexQueue();

    // Set up distribution, placeholder for now
    machine_id = 0;
    addr_lock.lock();
    peer_address = vector<string>(1);
    peer_address[0] = "";
    addr_lock.unlock();
    batches = vector<vector<Link>>(peer_address.size());

    vector<FrontierItem> recoveredItems;
    urlsCrawled = 0;

    size_t recoveredUrlCount = 0;
    if (checkpoint->load(recoveredItems, bloom, recoveredUrlCount) && recoveredItems.size() > 0) {
        urlsCrawled = recoveredUrlCount;
        f = new Frontier(recoveredItems);
        std::cerr << "Recovered from checkpoint at " << urlsCrawled.load() << " URLs\n";
    } else {
        if (recoveredUrlCount > 0 && recoveredItems.size() == 0) {
            std::cerr << "Checkpoint has empty frontier; starting fresh from seed list\n";
        }
        f = new Frontier("src/crawler/seedList.txt");
        std::cerr << "Starting fresh from seed list\n";
    }

    size_t CrawlerThreadCount = cores * 3;
    size_t IndexThreadCount = 1;
    vector<pthread_t> crawlerThreads(CrawlerThreadCount);
    vector<pthread_t> indexThreads(IndexThreadCount);
    pthread_t senderThread; // singular sender thread for now

    for (size_t i = 0; i < CrawlerThreadCount; i++) {
        pthread_create(&crawlerThreads[i], nullptr, CrawlerWorkerThread, nullptr);
    }

    for (size_t i = 0; i < IndexThreadCount; i++) {
        pthread_create(&indexThreads[i], nullptr, IndexWorkerThread, nullptr);
    }

    pthread_create(&senderThread, nullptr, SendToMachineThread, nullptr);

    for (size_t i = 0; i < CrawlerThreadCount; i++) {
        pthread_join(crawlerThreads[i], nullptr);
    }

    shouldStop = true;
    q->shutdown();
    batch_cv.notify_all();

    for (size_t i = 0; i < IndexThreadCount; i++) {
        pthread_join(indexThreads[i], nullptr);
    }

    pthread_join(senderThread, nullptr);

    checkpoint->save(*f, bloom, urlsCrawled);

    if (shouldStop)
        std::cerr << "Graceful shutdown after SIGINT\n";

    delete f;
    delete q;
    delete checkpoint;
    return 0;
}
