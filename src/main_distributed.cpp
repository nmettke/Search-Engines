// Main file, should combine Crawler and Index for the distributed design.

#include "./crawler/DelayedQueue.h"
#include "./crawler/RobotsCache.h"
#include "./crawler/UrlFilter.h"
#include "./crawler/checkpoint.h"
#include "./crawler/frontier.h"
#include "./crawler/url_dedup.h"
#include "./index/src/lib/Common.h"
#include "./index/src/lib/chunk_flusher.h"
#include "./index/src/lib/in_memory_index.h"
#include "./index/src/lib/indexQueue.h"
#include "./index/src/lib/tokenizer.h"
#include "./parser/HtmlParser.h"
#include "./utils/SSL/LinuxSSL_Crawler.hpp"
#include "./utils/hash/HashTable.h"
#include "./utils/string.hpp"
#include "./utils/threads/condition_variable.hpp"
#include "./utils/threads/lock_guard.hpp"
#include "./utils/threads/mutex.hpp"
#include "./utils/vector.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netdb.h>
#include <optional>
#include <pthread.h>
#include <stdexcept>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <time.h>
#include <unistd.h>

std::atomic<bool> shouldStop{false};

// Key constraint: there should always only be URL that belong to this machine on the frontier
Frontier *f = nullptr;
IndexQueue *q = nullptr;
InMemoryIndex mem_index;

std::atomic<size_t> numLinkThreshold{128};
vector<vector<Link>> batches;
mutex batch_lock;
std::atomic<size_t> machine_id{0};
// peer address should never be empty, it should at least include self
// peer should be in form host:port
vector<string> peer_address;
condition_variable batch_cv;

struct AnchorPosting {
    vector<string> titleWords;
    vector<string> anchorWords;
};

struct AnchorSnapshot {
    string url;
    vector<string> titleWords;
    vector<string> anchorWords;
};

static bool anchorKeyEqual(string a, string b) {
    return a == b;
} // defined to construct anchor hashtable

static uint64_t anchorKeyHash(string key) {
    return hashString(key.cstr());
} // defined to construct anchor hashtable

HashTable<string, AnchorPosting> *anchorIndex = nullptr;
mutex anchor_lock;
bool anchorEdited = false;
size_t anchorFileCount = 0;
const string anchorIndexDirectory("data/anchor_index");
const string indexDirectory("data/body_index");
const string metaDirectory("data/meta");
const size_t FLUSHANCHORSIZE = 2500;
const size_t FLUSHBODYTOKENSIZE = 25000000;

CheckpointConfig cpConfig;
Checkpoint *checkpoint = nullptr;
std::atomic<size_t> urlsCrawled{0};
UrlBloomFilter bloom(1000000, 0.0001);
UrlFilter urlFilter;
RobotsCache *robotsCache = nullptr;
DelayedQueue *delayedQueue = nullptr;
unsigned int cores = std::thread::hardware_concurrency();
static std::atomic<time_t> lastCheckpointTime{0};
static constexpr int checkpointIntervalSecs = 600; // 10 minutes
mutex crawlLogLock;
bool debug = false;

static void logCrawled(size_t count, const string &url) {
    lock_guard guard(crawlLogLock);
    if (debug) {
        std::cerr << "Crawled [" << count << "] " << url << '\n';
    }
}

static int64_t nowMillis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
// size_t anchorFlushIntervalSeconds = 30;

static bool shouldOwnUrl(const string &normalizedUrl) {
    // Decide whether machine should own url based on hash
    size_t machineCount = peer_address.size();
    if (machineCount <= 1) {
        // Skip the hashing logic if on single machine
        return true;
    }
    return (hashString(normalizedUrl.cstr()) % machineCount) == machine_id.load();
}

static void signalHandler(int) {
    shouldStop = true;
    batch_cv.notify_all();
    if (f != nullptr) {
        f->shutdown();
    }
    if (q != nullptr) {
        q->shutdown();
    }
}

static void appendAnchorTerms(const string &url, const vector<string> &words, bool isTitle) {
    // push to title list or anchor text list
    if (words.size() == 0) {
        return;
    }

    lock_guard guard(anchor_lock);
    Tuple<string, AnchorPosting> *entry = anchorIndex->Find(url, AnchorPosting());
    vector<string> &target = isTitle ? entry->value.titleWords : entry->value.anchorWords;
    for (const string &word : words) {
        target.pushBack(word);
    }
    anchorEdited = true;
}

static void flushAnchorIndexToDisk(bool force) {
    // creates a new empty anchor index and flushes old index to disk
    vector<AnchorSnapshot> snapshot;

    anchor_lock.lock();
    if (!force && !anchorEdited) {
        // do nothing if anchor is unchanged
        anchor_lock.unlock();
        return;
    }

    for (auto it = anchorIndex->begin(); it != anchorIndex->end(); ++it) {
        snapshot.pushBack({it->key, it->value.titleWords, it->value.anchorWords});
    }
    HashTable<string, AnchorPosting> *oldIndex = anchorIndex;
    anchorIndex = new HashTable<string, AnchorPosting>(anchorKeyEqual, anchorKeyHash);
    anchorFileCount++;
    anchorEdited = false;
    anchor_lock.unlock();

    delete oldIndex;

    if (snapshot.size() == 0) {
        return;
    }

    // Old C-style way of writing format string
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%s/anchor_%zu.idx", anchorIndexDirectory.c_str(),
                  anchorFileCount);

    const string anchorIndexPath(buffer);
    string tmpPath = anchorIndexPath + ".tmp";
    FILE *fp = fopen(tmpPath.c_str(), "wb");

    if (fp == nullptr) {
        std::cerr << "Failed to open anchor index temp file: " << tmpPath << '\n';
        lock_guard guard(anchor_lock);
        for (const AnchorSnapshot &record : snapshot) {
            // File fail to open, add snapshot back to index
            Tuple<string, AnchorPosting> *entry = anchorIndex->Find(record.url, AnchorPosting());
            for (const string &word : record.titleWords) {
                entry->value.titleWords.pushBack(word);
            }
            for (const string &word : record.anchorWords) {
                entry->value.anchorWords.pushBack(word);
            }
        }
        anchorEdited = true;
        return;
    }

    // No idea if this write logic is correct. Needs checking
    fprintf(fp, "[HEADER]\n");
    fprintf(fp, "version=1\n");
    fprintf(fp, "record_count=%zu\n", snapshot.size());
    fprintf(fp, "[ANCHOR]\n");

    // writes url, title size, titles, anchor size, anchors
    for (const AnchorSnapshot &record : snapshot) {
        fprintf(fp, "%s\t%zu", record.url.c_str(), record.titleWords.size());
        for (const string &word : record.titleWords) {
            fprintf(fp, "\t%s", word.c_str());
        }
        fprintf(fp, "\t%zu", record.anchorWords.size());
        for (const string &word : record.anchorWords) {
            fprintf(fp, "\t%s", word.c_str());
        }
        fprintf(fp, "\n");
    }

    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);

    if (rename(tmpPath.c_str(), anchorIndexPath.c_str()) != 0) {
        std::cerr << "Failed to rename anchor index file to " << anchorIndexPath << '\n';
        lock_guard guard(anchor_lock);
        for (const AnchorSnapshot &record : snapshot) {
            Tuple<string, AnchorPosting> *entry = anchorIndex->Find(record.url, AnchorPosting());
            for (const string &word : record.titleWords) {
                entry->value.titleWords.pushBack(word);
            }
            for (const string &word : record.anchorWords) {
                entry->value.anchorWords.pushBack(word);
            }
        }
        anchorEdited = true;
    }
}

void *CrawlerWorkerThread(void *) {
    size_t machineCount = peer_address.size();

    while (std::optional<FrontierItem> item = f->pop()) {
        if (shouldStop) {
            break;
        }

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
            delayedQueue->push(*item, check.readyAtMs);
            taskCompletionGuard.dismiss();
            continue;
        }

        string page = readURL(item->link);
        if (shouldStop) {
            break;
        }

        HtmlParser parsed(page.cstr(), page.size());
        parsed.sourceUrl = item->link;
        parsed.seedDistance = static_cast<uint8_t>(item->getSeedDistance());
        appendAnchorTerms(item->link, parsed.titleWords, true);
        q->push(parsed);

        vector<FrontierItem> discoveredLinks;
        for (const Link &link : parsed.links) {
            string normalizedOut = absolutizeUrl(link.URL, item->link, parsed.base);
            if (normalizedOut.empty() || !urlFilter.isAllowed(normalizedOut)) {
                continue;
            }

            size_t destinationMachine = hashString(normalizedOut.cstr()) % machineCount;
            if (destinationMachine != machine_id.load()) {
                // add to batch to send to another machine
                Link routedLink(normalizedOut);
                routedLink.anchorText = link.anchorText;

                batch_lock.lock();
                batches[destinationMachine].pushBack(std::move(routedLink));
                if (batches[destinationMachine].size() >= numLinkThreshold.load()) {
                    batch_cv.notify_one();
                }
                batch_lock.unlock();
                continue;
            }

            // URL belongs to this machine, add to anchor term,
            // and add to frontier if not seen
            appendAnchorTerms(normalizedOut, link.anchorText, false);
            if (bloom.checkAndInsert(normalizedOut)) {
                discoveredLinks.pushBack(FrontierItem(normalizedOut, *item));
            }
        }

        f->pushMany(discoveredLinks);
        size_t crawled = ++urlsCrawled;
        logCrawled(crawled, item->link);

        {
            time_t now = time(nullptr);
            time_t last = lastCheckpointTime.load();
            if (!shouldStop && (now - last) >= checkpointIntervalSecs) {
                if (lastCheckpointTime.compare_exchange_strong(last, now)) {
                    checkpoint->save(*f, bloom, urlsCrawled.load());
                    // flushAnchorIndexToDisk(false);
                    std::cerr << "Checkpoint saved at " << urlsCrawled.load() << " URLs\n";
                }
            }
        }
    }

    return nullptr;
}

void *DelayedQueueThread(void *) {
    while (!shouldStop) {
        sleep(1);
        if (shouldStop)
            break;
        vector<FrontierItem> ready = delayedQueue->drainReady(nowMillis());
        if (ready.size() > 0) {
            f->pushDeferred(ready);
        }
    }
    return nullptr;
}

static void sanitizeMetaText(string &text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\t' || text[i] == '\n' || text[i] == '\r') {
            text[i] = ' ';
        }
    }
}

string buildMetaLine(const HtmlParser &doc) {
    string title = "";
    for (size_t i = 0; i < doc.titleWords.size(); ++i) {
        title += doc.titleWords[i];
        title.pushBack(' ');
    }
    if (title.empty())
        title = "Untitled";
    sanitizeMetaText(title);

    string snippet = "";
    for (size_t i = 0; i < doc.words.size(); ++i) {
        if (snippet.size() > 150)
            break;
        snippet += doc.words[i];
        snippet.pushBack(' ');
    }
    if (snippet.empty())
        snippet = "No content available.";
    sanitizeMetaText(snippet);

    string meta_line = doc.sourceUrl;
    meta_line.pushBack('\t');
    meta_line += title;
    meta_line.pushBack('\t');
    meta_line += snippet;
    meta_line.pushBack('\n');

    return meta_line;
}

void flushMetaData(const vector<string> &chunk_metadata, const string &meta_path) {
    FILE *meta_fp = fopen(meta_path.c_str(), "w");
    if (meta_fp) {
        for (size_t i = 0; i < chunk_metadata.size(); ++i) {
            fwrite(chunk_metadata[i].c_str(), 1, chunk_metadata[i].size(), meta_fp);
        }
        fclose(meta_fp);
    }
}

void *IndexWorkerThread(void *) {
    Tokenizer tokenizer;
    size_t docsProcessed = 0;
    size_t chunksWritten = 0;
    size_t tokensProcessed = 0;

    vector<string> chunk_metadata;

    while (std::optional<HtmlParser> doc = q->pop()) {
        chunk_metadata.pushBack(buildMetaLine(*doc));

        auto tokenized = tokenizer.processDocument(*doc);
        for (const auto &tok : tokenized.tokens) {
            mem_index.addToken(tok);
        }
        mem_index.finishDocument(tokenized.doc_end);
        ++docsProcessed;
        tokensProcessed += tokenized.tokens.size();

        if (tokensProcessed >= FLUSHBODYTOKENSIZE) {
            // if (docsProcessed >= 500) {
            std::cout << "Start building index chunk \n";
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%s/chunk_%zu.idx", indexDirectory.c_str(),
                          chunksWritten);
            const string path(buffer);

            char meta_buffer[64];
            std::snprintf(meta_buffer, sizeof(meta_buffer), "%s/chunk_%zu.meta",
                          metaDirectory.c_str(), chunksWritten);
            const string meta_path(meta_buffer);

            try {
                flushIndexChunk(mem_index, path);
                flushMetaData(chunk_metadata, meta_path);
                flushAnchorIndexToDisk(false);
                std::cout << "Successfully wrote chunk with " << docsProcessed
                          << " docs to: " << path << '\n';
            } catch (const std::exception &e) {
                std::cerr << "Failed to write chunk: " << e.what() << '\n';
                return nullptr;
            }

            mem_index = InMemoryIndex();
            docsProcessed = 0;
            tokensProcessed = 0;
            ++chunksWritten;
        }
    }

    // Write final partial chunk
    if (docsProcessed > 0) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%s/chunk_%zu.idx", indexDirectory.c_str(),
                      chunksWritten);
        const string path(buffer);

        char meta_buffer[64];
        std::snprintf(meta_buffer, sizeof(meta_buffer), "%s/chunk_%zu.meta", metaDirectory.c_str(),
                      chunksWritten);
        const string meta_path(meta_buffer);

        try {
            flushIndexChunk(mem_index, path);
            flushMetaData(chunk_metadata, meta_path);
            flushAnchorIndexToDisk(false);
            std::cout << "Successfully wrote final chunk with " << docsProcessed
                      << " docs to: " << path << '\n';
        } catch (const std::exception &e) {
            std::cerr << "Failed to write final chunk: " << e.what() << '\n';
        }
    }

    return nullptr;
}

static bool hasReadyBatch() {
    for (const auto &batch : batches) {
        if (batch.size() >= numLinkThreshold.load() || (shouldStop && batch.size() > 0)) {
            return true;
        }
    }
    return false;
}

static constexpr size_t sendBatchRetryCount = 3;
static constexpr int sendBatchRetryBaseDelayMs = 100;

static bool sendBatchToPeer(const string &peer, const vector<Link> &batch) {
    // Common network connection code to send formatted payload of batched links
    if (batch.size() == 0) {
        return true;
    }

    size_t colon = peer.find(':');
    if (colon == string::npos || colon == 0 || colon + 1 >= peer.size()) {
        std::cerr << "Invalid peer address: " << peer << '\n';
        return false;
    }

    string host = peer.substr(0, colon);
    string port = peer.substr(colon + 1);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *result = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) {
        std::cerr << "Failed to resolve peer " << peer << '\n';
        return false;
    }

    int socketFd = -1;
    for (addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        socketFd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (socketFd < 0) {
            continue;
        }

        if (connect(socketFd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(socketFd);
        socketFd = -1;
    }

    freeaddrinfo(result);

    if (socketFd < 0) {
        std::cerr << "Failed to connect to peer " << peer << '\n';
        return false;
    }

    string payload;
    payload.reserve(batch.size() * 96); // heuristic. can be changed

    // Write in format URL \t anchor \t ... \n
    for (const Link &link : batch) {
        payload += link.URL;
        for (const string &word : link.anchorText) {
            payload.pushBack('\t');
            payload += word;
        }
        payload.pushBack('\n');
    }

    const char *data = payload.data();
    size_t remaining = payload.size();
    while (remaining > 0) {
        ssize_t sent = send(socketFd, data, remaining, 0);
        if (sent < 0) {
            std::cerr << "Failed while sending to peer " << peer << ": " << std::strerror(errno)
                      << '\n';
            close(socketFd);
            return false;
        }

        data += sent;
        remaining -= static_cast<size_t>(sent);
    }

    close(socketFd);

    if (debug) {
        std::cout << "Send Batch Successful\n";
    }

    return true;
}

static bool sendBatchToPeerWithRetry(const string &peer, const vector<Link> &batch) {
    for (size_t attempt = 0; attempt <= sendBatchRetryCount; ++attempt) {
        if (sendBatchToPeer(peer, batch)) {
            return true;
        }

        if (attempt == sendBatchRetryCount) {
            std::cout << "Send Batch Failed; Give up\n";
            break;
        }

        if (debug) {
            std::cout << "Send Batch Failed; Retrying\n";
        }

        const int delayMs = sendBatchRetryBaseDelayMs * (2 * attempt);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }

    return false;
}

void *SendToMachineThread(void *) {
    while (true) {
        vector<vector<Link>> readyBatches;

        batch_lock.lock();
        while (!shouldStop && !hasReadyBatch()) {
            batch_cv.wait(batch_lock);
        }

        if (shouldStop && !hasReadyBatch()) {
            batch_lock.unlock();
            break;
        }

        // We consider batch ready if it is big enough or when process
        // stopped and there are remaining batches
        readyBatches = vector<vector<Link>>(batches.size());
        for (size_t i = 0; i < batches.size(); ++i) {
            if (batches[i].size() >= numLinkThreshold.load() ||
                (shouldStop && batches[i].size() > 0)) {
                readyBatches[i] = std::move(batches[i]);
                batches[i] = vector<Link>();
            }
        }

        batch_lock.unlock();

        for (size_t i = 0; i < readyBatches.size(); ++i) {
            if (readyBatches[i].size() == 0) {
                continue;
            }

            if (i == machine_id.load()) {
                // batch of the local machine should always be empty, so
                // shouldn't happen but still safeguarding
                vector<string> localUrls;
                for (const Link &link : readyBatches[i]) {
                    appendAnchorTerms(link.URL, link.anchorText, false);
                    if (bloom.checkAndInsert(link.URL)) {
                        localUrls.pushBack(link.URL);
                    }
                }
                f->pushMany(localUrls);
                continue;
            }

            if (!sendBatchToPeerWithRetry(peer_address[i], readyBatches[i])) {
                // Put failed batch back without notifying batch_cv so it only
                // retries once real new URLs make the batch ready again.
                batch_lock.lock();
                for (Link &link : readyBatches[i]) {
                    batches[i].pushBack(std::move(link));
                }
                batch_lock.unlock();
            }
        }
    }

    return nullptr;
}

static size_t findCharFrom(const string &value, char c, size_t start) {
    // find char c starting from start pointer
    for (size_t i = start; i < value.size(); ++i) {
        if (value[i] == c) {
            return i;
        }
    }
    return string::npos;
}

static int openListeningSocket() {
    // Common network call for creating a socket to listen to incoming batches
    if (peer_address.size() <= 1 || machine_id.load() >= peer_address.size()) {
        return -1;
    }

    const string &selfPeer = peer_address[machine_id.load()];
    if (selfPeer.empty()) {
        return -1;
    }

    size_t colon = selfPeer.find(':');
    if (colon == string::npos || colon + 1 >= selfPeer.size()) {
        std::cerr << "Invalid listen address for machine " << machine_id.load() << ": " << selfPeer
                  << '\n';
        return -1;
    }

    string port = selfPeer.substr(colon + 1);
    if (port.empty()) {
        std::cerr << "Invalid listen address for machine " << machine_id.load() << ": " << selfPeer
                  << '\n';
        return -1;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo *result = nullptr;
    // Bind to 0.0.0.0:8081
    if (getaddrinfo(nullptr, port.c_str(), &hints, &result) != 0) {
        std::cerr << "Failed to resolve listen port for " << selfPeer << '\n';
        return -1;
    }

    int listenFd = -1;
    for (addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        listenFd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listenFd < 0) {
            continue;
        }

        int opt = 1;
        setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        if (bind(listenFd, rp->ai_addr, rp->ai_addrlen) == 0 && listen(listenFd, 32) == 0) {
            break;
        }

        close(listenFd);
        listenFd = -1;
    }

    freeaddrinfo(result);

    if (listenFd >= 0) {
        std::cerr << "Listening for batches on 0.0.0.0:" << port << " (configured as " << selfPeer
                  << ")\n";
    }
    return listenFd;
}

void *ReceiveFromMachineThread(void *) {
    int listenFd = openListeningSocket();
    if (listenFd < 0) {
        return nullptr;
    }

    while (!shouldStop) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenFd, &readSet);

        timeval timeout{};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ready = select(listenFd + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "Listener select failed: " << std::strerror(errno) << '\n';
            break;
        }
        if (ready == 0) {
            continue;
        }

        int clientFd = accept(listenFd, nullptr, nullptr);
        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "Accept failed: " << std::strerror(errno) << '\n';
            continue;
        }

        // read payload into a string
        string payload;
        char buffer[4096];
        while (true) {
            ssize_t received = recv(clientFd, buffer, sizeof(buffer), 0);
            if (received == 0) {
                break;
            }
            if (received < 0) {
                if (errno == EINTR) {
                    continue;
                }
                std::cerr << "Receive failed: " << std::strerror(errno) << '\n';
                break;
            }
            payload.append(buffer, static_cast<size_t>(received));
        }

        close(clientFd);

        if (debug) {
            std::cout << "Receive batch\n";
        }

        if (payload.empty()) {
            continue;
        }

        // decode payload based on our encoding method into vector of urls and anchors
        vector<FrontierItem> discoveredLinks;
        size_t lineStart = 0;
        while (lineStart < payload.size()) {
            size_t lineEnd = findCharFrom(payload, '\n', lineStart);
            size_t lineLength = lineEnd != string::npos ? lineLength = lineEnd - lineStart
                                                        : lineLength = payload.size() - lineStart;

            string line = payload.substr(lineStart, lineLength);

            if (!line.empty()) {
                // parse line
                size_t fieldEnd = findCharFrom(line, '\t', 0);
                string rawUrl = fieldEnd == string::npos ? line : line.substr(0, fieldEnd);
                vector<string> anchorWords;

                // add anchor text if tab char was found
                while (fieldEnd != string::npos) {
                    size_t fieldStart = fieldEnd + 1;
                    fieldEnd = findCharFrom(line, '\t', fieldStart);
                    size_t fieldLength =
                        fieldEnd == string::npos ? line.size() - fieldStart : fieldEnd - fieldStart;
                    if (fieldLength > 0) {
                        anchorWords.pushBack(line.substr(fieldStart, fieldLength));
                    }
                }

                // add the links to our frontier and anchor
                string normalizedOut = normalizeUrl(rawUrl);
                if (!normalizedOut.empty() && urlFilter.isAllowed(normalizedOut) &&
                    shouldOwnUrl(normalizedOut)) {
                    appendAnchorTerms(normalizedOut, anchorWords, false);
                    if (bloom.checkAndInsert(normalizedOut)) {
                        discoveredLinks.pushBack(FrontierItem(normalizedOut));
                    }
                }
            }

            if (lineEnd == string::npos) {
                break;
            }

            lineStart = lineEnd + 1;
        }

        if (discoveredLinks.size() > 0) {
            f->pushMany(discoveredLinks);
        }
    }

    close(listenFd);
    return nullptr;
}

// code to create thread to periodically flush anchor.
// Since we are already flushing every few hundred docs, we skip this

// void *AnchorFlushThread(void *) {
//     while (!shouldStop) {
//         for (size_t i = 0; i < anchorFlushIntervalSeconds && !shouldStop; ++i) {
//             sleep(1);
//         }
//         flushAnchorIndexToDisk(false);
//     }
//     return nullptr;
// }

// static size_t parseEnv(const char *name, size_t fallback) {
//     // Try parsing env as unsigned long int, else return fallback
//     const char *raw = std::getenv(name);
//     if (raw == nullptr || raw[0] == '\0') {
//         return fallback;
//     }
//
//     char *end = nullptr;
//     unsigned long parsed = std::strtoul(raw, &end, 10);
//     if (end == raw || (end != nullptr && *end != '\0')) {
//         return fallback;
//     }
//
//     return static_cast<size_t>(parsed);
// }

static bool parseSizeArg(const char *raw, size_t &value) {
    if (raw == nullptr || raw[0] == '\0') {
        return false;
    }

    char *end = nullptr;
    unsigned long parsed = std::strtoul(raw, &end, 10);
    if (end == raw || (end != nullptr && *end != '\0')) {
        return false;
    }

    value = static_cast<size_t>(parsed);
    return true;
}

int main(int argc, char **argv) {
    initSSL();
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    urlFilter.loadBlacklist("src/crawler/blackList.txt");

    cpConfig.directory = "src/crawler";
    checkpoint = new Checkpoint(cpConfig);
    q = new IndexQueue();
    robotsCache = new RobotsCache();
    delayedQueue = new DelayedQueue();
    anchorIndex = new HashTable<string, AnchorPosting>(anchorKeyEqual, anchorKeyHash);

    peer_address = {
        "34.29.237.224:8081",   // 0  Ashmit
        "136.112.148.251:8081", // 1
        "35.224.103.74:8081",   // 2
        "136.116.201.85:8081",  // 3
        "34.173.238.38:8081",   // 4
        "35.188.109.235:8081",  // 5
        "34.68.9.152:8081",     // 6
        "136.112.239.151:8081", // 7  Will
        "34.170.242.178:8081",  // 8
        "34.172.238.52:8081",   // 9
        "35.226.71.48:8081",    // 10
        "35.193.171.172:8081",  // 11
        "34.31.154.209:8081",   // 12 Andrew
        "35.239.255.145:8081",  // 13
        "34.61.8.145:8081",     // 14
        "34.133.73.6:8081",     // 15
        "34.135.5.27:8081",     // 16
        "34.70.193.99:8081",    // 17 Anthony
        "34.123.110.125:8081",  // 18
        "104.154.225.51:8081",  // 19
        "35.226.221.84:8081",   // 20
        "136.119.115.108:8081", // 21
        "34.67.230.213:8081",   // 22
        "34.55.218.240:8081",   // 23
        "34.68.198.19:8081",    // 24
        "136.114.215.122:8081", // 25 Satvik
        "34.55.5.248:8081",     // 26
        "34.63.33.31:8081",     // 27
        "34.30.203.75:8081",    // 28
        "35.188.188.95:8081",   // 29
        "34.171.171.179:8081",  // 30
        "104.197.40.88:8081",   // 31
        "34.58.160.109:8081",   // 32 Vasu
        "34.134.205.94:8081",   // 33
        "34.170.70.78:8081",    // 34
        "34.136.157.247:8081",  // 35
        "34.135.152.6:8081",    // 36
        "104.154.42.191:8081",  // 37
        "35.238.14.169:8081",   // 38
        "34.45.219.149:8081",   // 39 Nate
        "34.9.132.225:8081",    // 40
        "34.9.191.70:8081",     // 41
        "136.116.245.6:8081",   // 42
        "34.9.119.101:8081",    // 43
    };

    // machine_id = parseEnv("SEARCH_MACHINE_ID", 0);
    // numLinkThreshold = parseEnv("SEARCH_BATCH_THRESHOLD", numLinkThreshold.load());
    // anchorFlushIntervalSeconds = parseEnv("SEARCH_ANCHOR_FLUSH_SECS",
    // anchorFlushIntervalSeconds);

    if (argc != 3 && argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <machine_id> <batch_threshold> [debug]\n";
        return 1;
    }

    size_t parsedMachineId = 0;
    size_t parsedBatchThreshold = 0;
    if (!parseSizeArg(argv[1], parsedMachineId) || !parseSizeArg(argv[2], parsedBatchThreshold)) {
        std::cerr << "machine_id and batch_threshold must both be unsigned integers\n";
        return 1;
    }

    machine_id = parsedMachineId;
    numLinkThreshold = parsedBatchThreshold;
    if (argc == 4) {
        if (string(argv[3]) != "debug") {
            std::cerr << "Optional third argument must be 'debug'\n";
            return 1;
        }
        debug = true;
    }

    // if (anchorFlushIntervalSeconds == 0) {
    //     std::cerr << "Warning: Anchor flush is zero;\n";
    // }

    if (machine_id.load() >= peer_address.size()) {
        std::cerr << "Machine id " << machine_id.load()
                  << " is out of range for configured peers; Halting\n";
        return 1;
    }

    batches = vector<vector<Link>>(peer_address.size());

    vector<FrontierItem> recoveredItems;
    urlsCrawled = 0;

    size_t recoveredUrlCount = 0;

    if (checkpoint->load(recoveredItems, bloom, recoveredUrlCount)) {
        // When we start from checkpoint, we also check that all URL should be parsed by this
        // machine
        urlsCrawled = recoveredUrlCount;

        vector<FrontierItem> ownedRecoveredItems;
        for (const FrontierItem &item : recoveredItems) {
            string normalizeOut = normalizeUrl(item.link);
            if (normalizeOut.empty() || !urlFilter.isAllowed(normalizeOut) ||
                !shouldOwnUrl(normalizeOut)) {
                continue;
            }

            if (normalizeOut == item.link) {
                ownedRecoveredItems.pushBack(item);
            } else {
                ownedRecoveredItems.pushBack(FrontierItem(normalizeOut));
            }
        }

        if (ownedRecoveredItems.size() > 0) {
            f = new Frontier(std::move(ownedRecoveredItems));
            std::cerr << "Recovered from checkpoint at " << urlsCrawled.load() << " URLs\n";
        } else {
            std::cerr << "Checkpoint has empty frontier for this machine; starting fresh from seed "
                         "list\n";

            std::ifstream seedList("src/crawler/seedList.txt");
            if (!seedList.is_open()) {
                throw std::runtime_error("seed list could not be opened");
            }

            vector<FrontierItem> ownedSeedItems;
            std::string line;
            while (std::getline(seedList, line)) {
                string normalizeOut = normalizeUrl(string(line.c_str()));
                if (normalizeOut.empty() || !urlFilter.isAllowed(normalizeOut) ||
                    !shouldOwnUrl(normalizeOut)) {
                    continue;
                }
                ownedSeedItems.pushBack(FrontierItem(normalizeOut));
            }

            f = new Frontier(std::move(ownedSeedItems));
            urlsCrawled = 0;
            std::cerr << "Starting fresh from seed list\n";
        }
    } else {
        // We filter to ensure we should own link on the seed list
        std::ifstream seedList("src/crawler/seedList.txt");
        if (!seedList.is_open()) {
            throw std::runtime_error("seed list could not be opened");
        }

        vector<FrontierItem> ownedSeedItems;
        std::string line;
        while (std::getline(seedList, line)) {
            string normalizeOut = normalizeUrl(string(line.c_str()));
            if (normalizeOut.empty() || !urlFilter.isAllowed(normalizeOut) ||
                !shouldOwnUrl(normalizeOut)) {
                continue;
            }
            ownedSeedItems.pushBack(FrontierItem(normalizeOut));
        }

        f = new Frontier(std::move(ownedSeedItems));
        std::cerr << "Starting fresh from seed list\n";
    }

    size_t crawlerThreadCount = cores * 3;
    size_t indexThreadCount = 1;
    vector<pthread_t> crawlerThreads(crawlerThreadCount);
    vector<pthread_t> indexThreads(indexThreadCount);

    pthread_t senderThread{};
    pthread_t receiverThread{};
    pthread_t dqThread{};
    // pthread_t anchorThread{};
    bool senderStarted = false;
    bool receiverStarted = false;

    lastCheckpointTime = time(nullptr);

    for (size_t i = 0; i < crawlerThreadCount; ++i) {
        pthread_create(&crawlerThreads[i], nullptr, CrawlerWorkerThread, nullptr);
    }

    for (size_t i = 0; i < indexThreadCount; ++i) {
        pthread_create(&indexThreads[i], nullptr, IndexWorkerThread, nullptr);
    }

    pthread_create(&dqThread, nullptr, DelayedQueueThread, nullptr);

    if (peer_address.size() > 1) {
        // start network threads only if we have multi-machine
        pthread_create(&senderThread, nullptr, SendToMachineThread, nullptr);
        pthread_create(&receiverThread, nullptr, ReceiveFromMachineThread, nullptr);
        senderStarted = true;
        receiverStarted = true;
    }

    // pthread_create(&anchorThread, nullptr, AnchorFlushThread, nullptr);

    for (size_t i = 0; i < crawlerThreads.size(); ++i) {
        pthread_join(crawlerThreads[i], nullptr);
    }

    shouldStop = true;
    q->shutdown();
    batch_cv.notify_all();

    for (size_t i = 0; i < indexThreads.size(); ++i) {
        pthread_join(indexThreads[i], nullptr);
    }

    if (senderStarted) {
        pthread_join(senderThread, nullptr);
    }

    if (receiverStarted) {
        pthread_join(receiverThread, nullptr);
    }

    pthread_join(dqThread, nullptr);
    // pthread_join(anchorThread, nullptr);

    checkpoint->save(*f, bloom, urlsCrawled.load());

    // force flush Anchor even if Anchor was unchanged
    flushAnchorIndexToDisk(true);

    if (shouldStop) {
        std::cerr << "Graceful shutdown after SIGINT\n";
    }

    delete anchorIndex;
    delete robotsCache;
    delete delayedQueue;
    delete f;
    delete q;
    delete checkpoint;

    cleanupSSL();
    return 0;
}
