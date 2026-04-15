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
#include "./utils/STL_rewrite/deque.hpp"
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
#include <fcntl.h>
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
struct RoutedLink {
    string url;
    vector<string> anchorText;
    size_t seedDistance = 0;
};

vector<vector<RoutedLink>> batches;
mutex batch_lock;
std::atomic<size_t> machine_id{0};
// peer address should never be empty, it should at least include self
// peer should be in form host:port
vector<string> peer_address;
condition_variable batch_cv;

struct WordPosting {
    vector<string> words;
    size_t referring_count;
};

struct WordSnapshot {
    string url;
    vector<string> words;
    size_t referring_count;
};

static bool anchorKeyEqual(string a, string b) {
    return a == b;
} // defined to construct anchor hashtable

static uint64_t anchorKeyHash(string key) {
    return hashString(key.cstr());
} // defined to construct anchor hashtable

static void sanitizeText(string &text) {
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\t' || text[i] == '\n' || text[i] == '\r') {
            text[i] = ' ';
        }
    }
}

HashTable<string, WordPosting> *anchorIndex = nullptr;
mutex anchor_lock;
bool anchorIndexEdited = false;
size_t anchorFlushFileCount = 0;
const string anchorIndexDirectory("data/anchor_index");
const string indexDirectory("data/body_index");
const string metaDirectory("data/meta");
const size_t FLUSHBODYTOKENSIZE = 25000000;
static constexpr size_t maxIndexQueueItems = 1024;
static constexpr size_t crawlerThreadsPerCore = 300;
static constexpr size_t fallbackCrawlerThreadCount = 8;
static constexpr size_t maxCrawlerThreadCount = 2400;

CheckpointConfig cpConfig;
Checkpoint *checkpoint = nullptr;
std::atomic<size_t> urlsCrawled{0};
UrlBloomFilter bloom(1000000, 0.0001);
UrlFilter urlFilter;
RobotsCache *robotsCache = nullptr;
DelayedQueue *delayedQueue = nullptr;
unsigned int cores = std::thread::hardware_concurrency();
static std::atomic<time_t> lastCheckpointTime{0};
static std::atomic<time_t> lastHeartbeatTime{0};
static constexpr int checkpointIntervalSecs = 1800; // 30 minutes
static constexpr int heartbeatIntervalSecs = 300;   // 5 minutes
mutex crawlLogLock;
bool debug = false;

static std::ostream &tsOut(std::ostream &stream);

static void logCrawled(size_t count, const string &url) {
    lock_guard guard(crawlLogLock);
    if (debug) {
        tsOut(std::cerr) << "Crawled [" << count << "] " << url << '\n';
    }
}

static int64_t nowMillis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static std::string timestampPrefix() {
    std::time_t now = std::time(nullptr);
    std::tm localTime{};
    localtime_r(&now, &localTime);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S] ", &localTime);
    return std::string(buffer);
}

static std::ostream &tsOut(std::ostream &stream) {
    stream << timestampPrefix();
    return stream;
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

static void appendAnchorTerms(const string &url, const vector<string> &words) {
    // push to anchor text list
    if (words.size() == 0) {
        return;
    }

    lock_guard guard(anchor_lock);
    Tuple<string, WordPosting> *entry = anchorIndex->Find(url, WordPosting());
    vector<string> &target = entry->value.words;
    for (string word : words) {
        sanitizeText(word);
        target.pushBack(word);
    }
    entry->value.referring_count++;

    anchorIndexEdited = true;
}

static void restoreWordSnapshot(HashTable<string, WordPosting> *targetIndex, bool &editedFlag,
                                const vector<WordSnapshot> &snapshot) {
    lock_guard guard(anchor_lock);
    for (const WordSnapshot &record : snapshot) {
        Tuple<string, WordPosting> *entry = targetIndex->Find(record.url, WordPosting());
        for (const string &word : record.words) {
            entry->value.words.pushBack(word);
        }
        entry->value.referring_count += record.referring_count;
    }
    editedFlag = true;
}

static bool flushWordSnapshotToDisk(const vector<WordSnapshot> &snapshot, size_t fileCount,
                                    HashTable<string, WordPosting> *targetIndex, bool &editedFlag) {
    if (snapshot.size() == 0) {
        return true;
    }

    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%s/anchor_%zu.idx", anchorIndexDirectory.c_str(),
                  fileCount);

    const string indexPath(buffer);
    string tmpPath = indexPath + ".tmp";
    FILE *fp = fopen(tmpPath.c_str(), "wb");

    if (fp == nullptr) {
        tsOut(std::cerr) << "Failed to open anchor index temp file: " << tmpPath << '\n';
        restoreWordSnapshot(targetIndex, editedFlag, snapshot);
        return false;
    }

    fprintf(fp, "[HEADER]\n");
    fprintf(fp, "version=1\n");
    fprintf(fp, "record_count=%zu\n", snapshot.size());
    fprintf(fp, "[Words]\n");

    for (const WordSnapshot &record : snapshot) {
        fprintf(fp, "%s", record.url.c_str());
        fprintf(fp, "\t%zu\t%zu\t", record.referring_count, record.words.size());
        for (const string &word : record.words) {
            fprintf(fp, "%s ", word.c_str());
        }
        fprintf(fp, "\n");
    }

    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);

    if (rename(tmpPath.c_str(), indexPath.c_str()) != 0) {
        tsOut(std::cerr) << "Failed to rename anchor index file to " << indexPath << '\n';
        restoreWordSnapshot(targetIndex, editedFlag, snapshot);
        return false;
    }

    return true;
}

static void flushAnchorIndexToDisk(bool force) {
    vector<WordSnapshot> anchorSnapshot;
    anchor_lock.lock();
    if (!force && !anchorIndexEdited) {
        anchor_lock.unlock();
        return;
    }

    for (auto it = anchorIndex->begin(); it != anchorIndex->end(); ++it) {
        anchorSnapshot.pushBack({it->key, it->value.words, it->value.referring_count});
    }

    HashTable<string, WordPosting> *oldAnchorIndex = anchorIndex;
    anchorIndex = new HashTable<string, WordPosting>(anchorKeyEqual, anchorKeyHash);
    anchorIndexEdited = false;

    anchor_lock.unlock();

    delete oldAnchorIndex;

    if (anchorSnapshot.size() == 0) {
        return;
    }

    anchorFlushFileCount++;
    flushWordSnapshotToDisk(anchorSnapshot, anchorFlushFileCount, anchorIndex, anchorIndexEdited);
}

void *CheckpointThread(void *) {
    while (!shouldStop) {
        sleep(1);
        if (shouldStop) {
            break;
        }

        time_t now = time(nullptr);
        time_t lastHeartbeat = lastHeartbeatTime.load();
        if ((now - lastHeartbeat) >= heartbeatIntervalSecs) {
            tsOut(std::cout) << "still alive; documents processed = " << urlsCrawled.load() << '\n';
            lastHeartbeatTime = now;
        }

        time_t last = lastCheckpointTime.load();
        if ((now - last) < checkpointIntervalSecs) {
            continue;
        }

        const size_t crawled = urlsCrawled.load();
        CheckpointSnapshot snapshot;
        const int64_t snapshotStart = nowMillis();
        if (!checkpoint->createSnapshot(*f, bloom, crawled, snapshot)) {
            continue;
        }
        const int64_t snapshotEnd = nowMillis();

        tsOut(std::cout) << "Starting checkpoint at " << crawled << " URLs\n";
        if (!checkpoint->writeSnapshot(snapshot)) {
            tsOut(std::cerr) << "Checkpoint write failed at " << crawled << " URLs\n";
            continue;
        }
        const int64_t writeEnd = nowMillis();
        tsOut(std::cerr) << "Checkpoint snapshot took " << (snapshotEnd - snapshotStart)
                         << " ms; write took " << (writeEnd - snapshotEnd) << " ms\n";

        lastCheckpointTime = now;
    }

    return nullptr;
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
            // std::cerr << "Blocked by robots.txt: " << item->link << '\n';
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

        if (parsed.isBroken() || !parsed.isEnglish()) {
            continue;
        }

        q->push(parsed);

        vector<FrontierItem> discoveredLinks;
        for (const Link &link : parsed.links) {
            string resolved = absolutizeUrl(link.URL, item->link, parsed.base);
            string canonicalOut;
            if (!passesUrlQualityChecks(resolved, canonicalOut) ||
                !urlFilter.isAllowed(canonicalOut)) {
                continue;
            }

            size_t destinationMachine = hashString(canonicalOut.cstr()) % machineCount;
            if (destinationMachine != machine_id.load()) {
                // add to batch to send to another machine
                RoutedLink routedLink;
                routedLink.url = canonicalOut;
                routedLink.seedDistance = item->getSeedDistance() + 1;
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
            appendAnchorTerms(canonicalOut, link.anchorText);
            if (bloom.checkAndInsert(canonicalOut)) {
                discoveredLinks.pushBack(FrontierItem(canonicalOut, *item));
            }
        }

        f->pushMany(discoveredLinks);
        size_t crawled = ++urlsCrawled;
        logCrawled(crawled, item->link);
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

string buildMetaLine(const HtmlParser &doc) {
    string title = "";
    for (size_t i = 0; i < doc.titleWords.size(); ++i) {
        title += doc.titleWords[i];
        title.pushBack(' ');
    }
    if (title.empty())
        title = "Untitled";
    sanitizeText(title);

    string snippet = "";
    for (size_t i = 0; i < doc.words.size(); ++i) {
        if (snippet.size() > 150)
            break;
        snippet += doc.words[i];
        snippet.pushBack(' ');
    }
    if (snippet.empty())
        snippet = "No content available.";
    sanitizeText(snippet);

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
                tsOut(std::cout) << "Successfully wrote chunk with " << docsProcessed
                                 << " docs to: " << path << '\n';
            } catch (const std::exception &e) {
                tsOut(std::cerr) << "Failed to write chunk: " << e.what() << '\n';
                return nullptr;
            }

            mem_index = InMemoryIndex();
            chunk_metadata = vector<string>();
            docsProcessed = 0;
            tokensProcessed = 0;
            ++chunksWritten;
        }

        if (docsProcessed > 0 && docsProcessed % 10000 == 0) {
            tsOut(std::cout) << "Processed " << docsProcessed << " documents\n";
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
            tsOut(std::cout) << "Successfully wrote final chunk with " << docsProcessed
                             << " docs to: " << path << '\n';
        } catch (const std::exception &e) {
            tsOut(std::cerr) << "Failed to write final chunk: " << e.what() << '\n';
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

static constexpr size_t sendBatchRetryCount = 5;
static constexpr int sendBatchRetryBaseDelayMs = 1000;
static constexpr int sendBatchConnectTimeoutSecs = 10;
static constexpr int sendBatchSendTimeoutSecs = 15;
static constexpr int receiveBatchRecvTimeoutSecs = 30;
static constexpr size_t receiveWorkerThreadCount = 32;
static constexpr size_t maxQueuedReceiveClientFds = 5120;

// Bounded handoff from the accept loop to a fixed pool of receive workers.
// Replaces "spawn a detached pthread per accept()" so connection bursts or
// stalled peers can no longer create unbounded threads/FDs.
static deque<int> clientFdQueue;
static mutex receiveQueueMutex;
static condition_variable receiveQueueCv;

// Non-blocking connect with a bounded timeout. Returns true iff the socket is
// fully connected. On return, the socket is restored to blocking mode so the
// caller can use ordinary send()/recv() with SO_SNDTIMEO / SO_RCVTIMEO.
// The caller owns fd in all cases (this helper never closes it).
static bool connectWithTimeout(int fd, const sockaddr *addr, socklen_t addrlen, int timeoutSecs) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return false;
    }

    int ret = connect(fd, addr, addrlen);
    if (ret == 0) {
        // Connected immediately (localhost-ish). Restore blocking mode.
        fcntl(fd, F_SETFL, flags);
        return true;
    }
    if (errno != EINPROGRESS) {
        return false;
    }

    fd_set writeSet;
    FD_ZERO(&writeSet);
    FD_SET(fd, &writeSet);

    timeval timeout{};
    timeout.tv_sec = timeoutSecs;
    timeout.tv_usec = 0;

    int selectResult = select(fd + 1, nullptr, &writeSet, nullptr, &timeout);
    if (selectResult <= 0) {
        // 0 = timeout, <0 = error
        return false;
    }

    int sockErr = 0;
    socklen_t len = sizeof(sockErr);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &sockErr, &len) < 0 || sockErr != 0) {
        return false;
    }

    // Restore blocking mode so subsequent send() respects SO_SNDTIMEO.
    fcntl(fd, F_SETFL, flags);
    return true;
}

static bool sendBatchToPeer(const string &peer, const vector<RoutedLink> &batch) {
    // Common network connection code to send formatted payload of batched links
    if (batch.size() == 0) {
        return true;
    }

    size_t colon = peer.find(':');
    if (colon == string::npos || colon == 0 || colon + 1 >= peer.size()) {
        tsOut(std::cerr) << "Invalid peer address: " << peer << '\n';
        return false;
    }

    string host = peer.substr(0, colon);
    string port = peer.substr(colon + 1);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *result = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &result) != 0) {
        tsOut(std::cerr) << "Failed to resolve peer " << peer << '\n';
        return false;
    }

    int socketFd = -1;
    for (addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        socketFd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (socketFd < 0) {
            continue;
        }

        if (connectWithTimeout(socketFd, rp->ai_addr, rp->ai_addrlen,
                               sendBatchConnectTimeoutSecs)) {
            break;
        }

        close(socketFd);
        socketFd = -1;
    }

    freeaddrinfo(result);

    if (socketFd < 0) {
        tsOut(std::cerr) << "Failed to connect to peer " << peer << '\n';
        return false;
    }

    // Bound how long send() can block on a dead peer.
    timeval sendTimeout{};
    sendTimeout.tv_sec = sendBatchSendTimeoutSecs;
    sendTimeout.tv_usec = 0;
    setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof(sendTimeout));

    string payload;
    payload.reserve(batch.size() * 96); // heuristic. can be changed

    // Write in format URL \t seedDistance \t anchor \t ... \n
    for (const RoutedLink &link : batch) {
        payload += link.url;
        payload.pushBack('\t');
        payload += std::to_string(link.seedDistance).c_str();
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
            tsOut(std::cerr) << "Failed while sending to peer " << peer << ": "
                             << std::strerror(errno) << '\n';
            close(socketFd);
            return false;
        }

        data += sent;
        remaining -= static_cast<size_t>(sent);
    }

    close(socketFd);

    if (debug) {
        tsOut(std::cout) << "Send Batch Successful\n";
    }

    return true;
}

static bool sendBatchToPeerWithRetry(const string &peer, const vector<RoutedLink> &batch) {
    for (size_t attempt = 0; attempt <= sendBatchRetryCount; ++attempt) {
        if (sendBatchToPeer(peer, batch)) {
            return true;
        }

        if (attempt == sendBatchRetryCount) {
            tsOut(std::cout) << "Send Batch Failed; Give up\n";
            break;
        }

        if (debug) {
            tsOut(std::cout) << "Send Batch Failed; Retrying\n";
        }

        const int delayMs = sendBatchRetryBaseDelayMs * (1 << attempt);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }

    return false;
}

void *SendToMachineThread(void *) {
    while (true) {
        vector<vector<RoutedLink>> readyBatches;

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
        readyBatches = vector<vector<RoutedLink>>(batches.size());
        for (size_t i = 0; i < batches.size(); ++i) {
            if (batches[i].size() >= numLinkThreshold.load() ||
                (shouldStop && batches[i].size() > 0)) {
                readyBatches[i] = std::move(batches[i]);
                batches[i] = vector<RoutedLink>();
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
                vector<FrontierItem> localLinks;
                for (const RoutedLink &link : readyBatches[i]) {
                    string canonicalOut;
                    if (!passesUrlQualityChecks(link.url, canonicalOut) ||
                        !urlFilter.isAllowed(canonicalOut)) {
                        continue;
                    }

                    appendAnchorTerms(canonicalOut, link.anchorText);
                    if (bloom.checkAndInsert(canonicalOut)) {
                        localLinks.pushBack(
                            FrontierItem::withSeedDistance(canonicalOut, link.seedDistance));
                    }
                }
                f->pushMany(localLinks);
                continue;
            }

            if (!sendBatchToPeerWithRetry(peer_address[i], readyBatches[i])) {
                // Put failed batch back without notifying batch_cv so it only
                // retries once real new URLs make the batch ready again.
                // batch_lock.lock();
                // for (Link &link : readyBatches[i]) {
                //     batches[i].pushBack(std::move(link));
                // }
                // batch_lock.unlock();
                tsOut(std::cerr) << "Send Batch Failed; Give up\n";
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

static bool parseSizeField(const string &raw, size_t &value) {
    if (raw.empty()) {
        return false;
    }

    char *end = nullptr;
    unsigned long parsed = std::strtoul(raw.c_str(), &end, 10);
    if (end == raw.c_str() || (end != nullptr && *end != '\0')) {
        return false;
    }

    value = static_cast<size_t>(parsed);
    return true;
}

static void processReceivedBatch(int clientFd) {
    // Bound how long a stalled peer can pin this worker + FD.
    // Without this, a half-open TCP connection leaves recv() blocked
    // until the kernel's TCP keepalive expires (hours), leaking FDs
    // over a long run.
    timeval recvTimeout{};
    recvTimeout.tv_sec = receiveBatchRecvTimeoutSecs;
    recvTimeout.tv_usec = 0;
    setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(recvTimeout));

    int keepAlive = 1;
    setsockopt(clientFd, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(keepAlive));

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
            tsOut(std::cerr) << "Receive failed: " << std::strerror(errno) << '\n';
            break;
        }
        payload.append(buffer, static_cast<size_t>(received));
    }

    close(clientFd);

    if (debug) {
        tsOut(std::cout) << "Receive batch\n";
    }

    if (payload.empty()) {
        return;
    }

    // decode payload based on our encoding method into vector of urls and anchors
    vector<FrontierItem> discoveredLinks;
    size_t lineStart = 0;
    while (lineStart < payload.size()) {
        size_t lineEnd = findCharFrom(payload, '\n', lineStart);
        size_t lineLength =
            lineEnd != string::npos ? lineEnd - lineStart : payload.size() - lineStart;

        string line = payload.substr(lineStart, lineLength);

        if (!line.empty()) {
            // parse line
            size_t fieldEnd = findCharFrom(line, '\t', 0);
            string rawUrl = fieldEnd == string::npos ? line : line.substr(0, fieldEnd);
            vector<string> anchorWords;
            size_t receivedSeedDistance = 0;

            // New payload format includes seed distance as second field.
            // For backward compatibility with old payloads, if parsing fails,
            // we treat the field as the first anchor word.
            if (fieldEnd != string::npos) {
                size_t fieldStart = fieldEnd + 1;
                size_t secondFieldEnd = findCharFrom(line, '\t', fieldStart);
                size_t secondFieldLength = secondFieldEnd == string::npos
                                               ? line.size() - fieldStart
                                               : secondFieldEnd - fieldStart;
                string maybeSeedDistance = line.substr(fieldStart, secondFieldLength);
                if (parseSizeField(maybeSeedDistance, receivedSeedDistance)) {
                    fieldEnd = secondFieldEnd;
                }
            }

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
            string canonicalOut;
            if (passesUrlQualityChecks(rawUrl, canonicalOut) && urlFilter.isAllowed(canonicalOut) &&
                shouldOwnUrl(canonicalOut)) {
                appendAnchorTerms(canonicalOut, anchorWords);
                if (bloom.checkAndInsert(canonicalOut)) {
                    discoveredLinks.pushBack(
                        FrontierItem::withSeedDistance(canonicalOut, receivedSeedDistance));
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

void *ReceiveWorkerThread(void *) {
    // Pull accepted client FDs off clientFdQueue and process them. Workers
    // drain any queued FDs after shouldStop flips so in-flight batches aren't
    // lost during graceful shutdown.
    while (true) {
        int clientFd = -1;

        receiveQueueMutex.lock();
        while (!shouldStop && clientFdQueue.empty()) {
            receiveQueueCv.wait(receiveQueueMutex);
        }
        if (clientFdQueue.empty()) {
            // shouldStop && queue drained -> exit
            receiveQueueMutex.unlock();
            break;
        }
        clientFd = clientFdQueue.back();
        clientFdQueue.pop_back();
        receiveQueueMutex.unlock();

        processReceivedBatch(clientFd);
    }

    return nullptr;
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
        tsOut(std::cerr) << "Invalid listen address for machine " << machine_id.load() << ": "
                         << selfPeer << '\n';
        return -1;
    }

    string port = selfPeer.substr(colon + 1);
    if (port.empty()) {
        tsOut(std::cerr) << "Invalid listen address for machine " << machine_id.load() << ": "
                         << selfPeer << '\n';
        return -1;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo *result = nullptr;
    // Bind to 0.0.0.0:8081
    if (getaddrinfo(nullptr, port.c_str(), &hints, &result) != 0) {
        tsOut(std::cerr) << "Failed to resolve listen port for " << selfPeer << '\n';
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

        if (bind(listenFd, rp->ai_addr, rp->ai_addrlen) == 0 && listen(listenFd, 100) == 0) {
            break;
        }

        close(listenFd);
        listenFd = -1;
    }

    freeaddrinfo(result);

    if (listenFd >= 0) {
        tsOut(std::cerr) << "Listening for batches on 0.0.0.0:" << port << " (configured as "
                         << selfPeer << ")\n";
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
            tsOut(std::cerr) << "Listener select failed: " << std::strerror(errno) << '\n';
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
            tsOut(std::cerr) << "Accept failed: " << std::strerror(errno) << '\n';
            continue;
        }

        // Hand the fd to the fixed-size receive worker pool. A single
        // notify_one wakes one idle worker; if all workers are busy the fd
        // stays in the queue until one frees up.
        receiveQueueMutex.lock();
        if (clientFdQueue.size() >= maxQueuedReceiveClientFds) {
            receiveQueueMutex.unlock();
            tsOut(std::cerr) << "Receive queue full; dropping incoming batch connection\n";
            close(clientFd);
            continue;
        }
        clientFdQueue.push_front(clientFd);
        receiveQueueCv.notify_all();
        receiveQueueMutex.unlock();
    }

    close(listenFd);
    return nullptr;
}

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
    q = new IndexQueue(maxIndexQueueItems);
    robotsCache = new RobotsCache();
    delayedQueue = new DelayedQueue();
    anchorIndex = new HashTable<string, WordPosting>(anchorKeyEqual, anchorKeyHash);

    peer_address = {"10.128.0.21:8081", "10.128.0.22:8081", "10.128.0.23:8081", "10.128.0.25:8081",
                    "10.128.0.26:8081", "10.128.0.27:8081", "10.128.0.28:8081", "10.128.0.29:8081"};

    // machine_id = parseEnv("SEARCH_MACHINE_ID", 0);
    // numLinkThreshold = parseEnv("SEARCH_BATCH_THRESHOLD", numLinkThreshold.load());
    // anchorFlushIntervalSeconds = parseEnv("SEARCH_ANCHOR_FLUSH_SECS",
    // anchorFlushIntervalSeconds);

    if (argc != 3 && argc != 4) {
        tsOut(std::cerr) << "Usage: " << argv[0] << " <machine_id> <batch_threshold> [debug]\n";
        return 1;
    }

    size_t parsedMachineId = 0;
    size_t parsedBatchThreshold = 0;
    if (!parseSizeArg(argv[1], parsedMachineId) || !parseSizeArg(argv[2], parsedBatchThreshold)) {
        tsOut(std::cerr) << "machine_id and batch_threshold must both be unsigned integers\n";
        return 1;
    }

    machine_id = parsedMachineId;
    numLinkThreshold = parsedBatchThreshold;
    if (argc == 4) {
        if (string(argv[3]) != "debug") {
            tsOut(std::cerr) << "Optional third argument must be 'debug'\n";
            return 1;
        }
        debug = true;
    }

    // if (anchorFlushIntervalSeconds == 0) {
    //     std::cerr << "Warning: Anchor flush is zero;\n";
    // }

    if (machine_id.load() >= peer_address.size()) {
        tsOut(std::cerr) << "Machine id " << machine_id.load()
                         << " is out of range for configured peers; Halting\n";
        return 1;
    }

    batches = vector<vector<RoutedLink>>(peer_address.size());

    vector<FrontierItem> recoveredItems;
    urlsCrawled = 0;

    size_t recoveredUrlCount = 0;

    if (checkpoint->load(recoveredItems, bloom, recoveredUrlCount)) {
        // When we start from checkpoint, we also check that all URL should be parsed by this
        // machine
        urlsCrawled = recoveredUrlCount;

        vector<FrontierItem> ownedRecoveredItems;
        for (const FrontierItem &item : recoveredItems) {
            string canonicalOut;
            if (!passesUrlQualityChecks(item.link, canonicalOut) ||
                !urlFilter.isAllowed(canonicalOut) || !shouldOwnUrl(canonicalOut)) {
                continue;
            }

            if (canonicalOut == item.link) {
                ownedRecoveredItems.pushBack(item);
            } else {
                ownedRecoveredItems.pushBack(item.withLink(canonicalOut));
            }
        }

        if (ownedRecoveredItems.size() > 0) {
            f = new Frontier(std::move(ownedRecoveredItems), false);
            tsOut(std::cerr) << "Recovered from checkpoint at " << urlsCrawled.load() << " URLs\n";
        } else {
            tsOut(std::cerr)
                << "Checkpoint has empty frontier for this machine; starting fresh from seed "
                   "list\n";

            std::ifstream seedList("src/crawler/seedList.txt");
            if (!seedList.is_open()) {
                throw std::runtime_error("seed list could not be opened");
            }

            vector<FrontierItem> ownedSeedItems;
            std::string line;
            while (std::getline(seedList, line)) {
                string canonicalOut;
                if (!passesUrlQualityChecks(string(line.c_str()), canonicalOut) ||
                    !urlFilter.isAllowed(canonicalOut) || !shouldOwnUrl(canonicalOut)) {
                    continue;
                }
                ownedSeedItems.pushBack(FrontierItem(canonicalOut));
            }

            f = new Frontier(std::move(ownedSeedItems), false);
            urlsCrawled = 0;
            tsOut(std::cerr) << "Starting fresh from seed list\n";
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
            string canonicalOut;
            if (!passesUrlQualityChecks(string(line.c_str()), canonicalOut) ||
                !urlFilter.isAllowed(canonicalOut) || !shouldOwnUrl(canonicalOut)) {
                continue;
            }
            ownedSeedItems.pushBack(FrontierItem(canonicalOut));
        }

        f = new Frontier(std::move(ownedSeedItems), false);
        tsOut(std::cerr) << "Starting fresh from seed list\n";
    }

    size_t crawlerThreadCount = (cores == 0 ? fallbackCrawlerThreadCount
                                            : static_cast<size_t>(cores) * crawlerThreadsPerCore);
    if (crawlerThreadCount > maxCrawlerThreadCount) {
        crawlerThreadCount = maxCrawlerThreadCount;
    }
    size_t indexThreadCount = 1;
    vector<pthread_t> crawlerThreads(crawlerThreadCount);
    vector<pthread_t> indexThreads(indexThreadCount);

    pthread_t senderThread{};
    pthread_t receiverThread{};
    pthread_t dqThread{};
    pthread_t checkpointThread{};
    // pthread_t anchorThread{};
    vector<pthread_t> receiveWorkers(receiveWorkerThreadCount);
    bool senderStarted = false;
    bool receiverStarted = false;
    bool receiveWorkersStarted = false;
    bool checkpointStarted = false;

    lastCheckpointTime = time(nullptr);
    lastHeartbeatTime = lastCheckpointTime.load();

    for (size_t i = 0; i < crawlerThreadCount; ++i) {
        pthread_create(&crawlerThreads[i], nullptr, CrawlerWorkerThread, nullptr);
    }

    for (size_t i = 0; i < indexThreadCount; ++i) {
        pthread_create(&indexThreads[i], nullptr, IndexWorkerThread, nullptr);
    }

    pthread_create(&checkpointThread, nullptr, CheckpointThread, nullptr);
    checkpointStarted = true;
    pthread_create(&dqThread, nullptr, DelayedQueueThread, nullptr);

    if (peer_address.size() > 1) {
        // start network threads only if we have multi-machine
        pthread_create(&senderThread, nullptr, SendToMachineThread, nullptr);
        pthread_create(&receiverThread, nullptr, ReceiveFromMachineThread, nullptr);
        senderStarted = true;
        receiverStarted = true;

        // Fixed pool of receive workers. Replaces per-accept pthread_create
        // so connection bursts can't create unbounded threads/FDs.
        for (size_t i = 0; i < receiveWorkerThreadCount; ++i) {
            pthread_create(&receiveWorkers[i], nullptr, ReceiveWorkerThread, nullptr);
        }
        receiveWorkersStarted = true;
    }

    // pthread_create(&anchorThread, nullptr, AnchorFlushThread, nullptr);

    for (size_t i = 0; i < crawlerThreads.size(); ++i) {
        pthread_join(crawlerThreads[i], nullptr);
    }

    shouldStop = true;
    q->shutdown();
    batch_cv.notify_all();

    if (checkpointStarted) {
        pthread_join(checkpointThread, nullptr);
    }

    for (size_t i = 0; i < indexThreads.size(); ++i) {
        pthread_join(indexThreads[i], nullptr);
    }

    if (senderStarted) {
        pthread_join(senderThread, nullptr);
    }

    if (receiverStarted) {
        pthread_join(receiverThread, nullptr);
    }

    if (receiveWorkersStarted) {
        // Receiver has exited, so no new fds will be pushed. Wake every
        // receive worker so they observe shouldStop, drain any queued fds,
        // and exit. Defensive drain + close afterwards in case any fd
        // slipped in during the shutdown race.
        receiveQueueMutex.lock();
        receiveQueueCv.notify_all();
        receiveQueueMutex.unlock();

        for (size_t i = 0; i < receiveWorkers.size(); ++i) {
            pthread_join(receiveWorkers[i], nullptr);
        }

        receiveQueueMutex.lock();
        while (!clientFdQueue.empty()) {
            close(clientFdQueue.back());
            clientFdQueue.pop_back();
        }
        receiveQueueMutex.unlock();
    }

    pthread_join(dqThread, nullptr);
    // pthread_join(anchorThread, nullptr);

    checkpoint->save(*f, bloom, urlsCrawled.load());

    // force flush Anchor even if Anchor was unchanged
    flushAnchorIndexToDisk(true);

    if (shouldStop) {
        tsOut(std::cerr) << "Graceful shutdown after SIGINT\n";
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
