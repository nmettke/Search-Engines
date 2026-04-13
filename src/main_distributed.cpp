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

CheckpointConfig cpConfig;
Checkpoint *checkpoint = nullptr;
std::atomic<size_t> urlsCrawled{0};
UrlBloomFilter bloom(1000000, 0.0001);
UrlFilter urlFilter;
RobotsCache *robotsCache = nullptr;
DelayedQueue *delayedQueue = nullptr;
unsigned int cores = std::thread::hardware_concurrency();

static int64_t nowMillis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
size_t anchorFlushIntervalSeconds = 30;

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

        vector<string> discoveredLinks;
        for (const Link &link : parsed.links) {
            string normalizedOut = normalizeUrl(link.URL);
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
                discoveredLinks.pushBack(normalizedOut);
            }
        }

        f->pushMany(discoveredLinks);
        ++urlsCrawled;
        std::cout << "Crawled [" << urlsCrawled << "] " << item->link << '\n';

        if (!shouldStop && (urlsCrawled.load() % 500) == 0) {
            checkpoint->save(*f, bloom, urlsCrawled.load());
            flushAnchorIndexToDisk(false);
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

void *IndexWorkerThread(void *) {
    Tokenizer tokenizer;
    size_t docsProcessed = 0;
    size_t chunksWritten = 0;

    while (std::optional<HtmlParser> doc = q->pop()) {
        auto tokenized = tokenizer.processDocument(*doc);
        for (const auto &tok : tokenized.tokens) {
            mem_index.addToken(tok);
        }
        mem_index.finishDocument(tokenized.doc_end);
        ++docsProcessed;

        if (docsProcessed >= 512) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%s/chunk_%zu.idx", indexDirectory.c_str(),
                          chunksWritten);
            const string path(buffer);

            try {
                flushIndexChunk(mem_index, path);
                std::cout << "Successfully wrote chunk with " << docsProcessed
                          << " docs to: " << path << '\n';
            } catch (const std::exception &e) {
                std::cerr << "Failed to write chunk: " << e.what() << '\n';
                return nullptr;
            }

            mem_index = InMemoryIndex();
            docsProcessed = 0;
            ++chunksWritten;
        }
    }

    // Write final partial chunk
    if (docsProcessed > 0) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%s/chunk_%zu.idx", indexDirectory.c_str(),
                      chunksWritten);
        const string path(buffer);

        try {
            flushIndexChunk(mem_index, path);
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
    return true;
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

            if (!sendBatchToPeer(peer_address[i], readyBatches[i])) {
                // We add batch back to memory if send failed
                batch_lock.lock();
                for (const Link &link : readyBatches[i]) {
                    batches[i].pushBack(link);
                }
                batch_lock.unlock();
                batch_cv.notify_one();
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

    string host = selfPeer.substr(0, colon);
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
    if (getaddrinfo(host.empty() ? nullptr : host.c_str(), port.c_str(), &hints, &result) != 0) {
        std::cerr << "Failed to resolve listen address " << selfPeer << '\n';
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
        std::cerr << "Listening for batches on " << selfPeer << '\n';
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

        if (payload.empty()) {
            continue;
        }

        // decode payload based on our encoding method into vector of urls and anchors
        vector<string> discoveredLinks;
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
                        discoveredLinks.pushBack(normalizedOut);
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

static size_t parseEnv(const char *name, size_t fallback) {
    // Try parsing env as unsigned long int, else return fallback
    const char *raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') {
        return fallback;
    }

    char *end = nullptr;
    unsigned long parsed = std::strtoul(raw, &end, 10);
    if (end == raw || (end != nullptr && *end != '\0')) {
        return fallback;
    }

    return static_cast<size_t>(parsed);
}

int main() {
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

    peer_address = {""};
    machine_id = parseEnv("SEARCH_MACHINE_ID", 0);
    numLinkThreshold = parseEnv("SEARCH_BATCH_THRESHOLD", numLinkThreshold.load());
    anchorFlushIntervalSeconds = parseEnv("SEARCH_ANCHOR_FLUSH_SECS", anchorFlushIntervalSeconds);

    if (anchorFlushIntervalSeconds == 0) {
        std::cerr << "Anchor flush is zero; halting\n";
        return 1;
    }

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

        f = new Frontier(std::move(ownedRecoveredItems));
        std::cerr << "Recovered from checkpoint at " << urlsCrawled.load() << " URLs\n";
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
