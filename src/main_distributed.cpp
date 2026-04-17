// Main file, should combine Crawler and Index for the distributed design.

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
#include <cstdint>
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
std::atomic<size_t> nextIndexChunkId{0};

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

struct PeerReceiverState {
    mutex lock;
    condition_variable cv;
    int pendingSocketFd = -1;
    int activeSocketFd = -1;
    bool stop = false;
};

struct PeerThreadArgs {
    size_t peerIndex = 0;
};

struct IndexThreadArgs {
    size_t threadIndex = 0;
};

PeerReceiverState *peerReceiverStates = nullptr;
PeerThreadArgs *peerSenderThreadArgs = nullptr;
PeerThreadArgs *peerReceiverThreadArgs = nullptr;

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
            continue;
        }
        unsigned char c = (unsigned char)text[i];
        bool is_ctrl = (c < 0x20) || (c == 0x7F); // C0 controls + DEL
        bool is_quote = (c == '"' || c == '\'');
        bool is_backslash = (c == '\\');
        if (is_ctrl || is_quote || is_backslash) {
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
const size_t FLUSHBODYTOKENSIZE = 15000000;
static constexpr size_t maxIndexQueueItems = 102400;
static constexpr size_t crawlerThreadsPerCore = 512;
static constexpr size_t fallbackCrawlerThreadCount = 8;
static constexpr size_t maxCrawlerThreadCount = 2400;
static constexpr size_t maxPeerBatchLinks = 2000000; // drop if larger than this

CheckpointConfig cpConfig;
Checkpoint *checkpoint = nullptr;
std::atomic<size_t> urlsCrawled{0};
UrlBloomFilter bloom(150000000, 0.0001);
UrlFilter urlFilter;
RobotsCache *robotsCache = nullptr;
unsigned int cores = std::thread::hardware_concurrency();
static std::atomic<time_t> lastCheckpointTime{0};
static std::atomic<time_t> lastHeartbeatTime{0};
static constexpr int checkpointIntervalSecs = 1600; // 1 hour 
static constexpr int heartbeatIntervalSecs = 30;   // 5 minutes
mutex crawlLogLock;
bool debug = false;
bool memDebug = false;
std::atomic<size_t> droppedPeerBatchLinks{0};

static std::ostream &tsOut(std::ostream &stream);

static void logCrawled(size_t count, const string &url) {
    lock_guard<mutex> guard(crawlLogLock);
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

    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S] ", &localTime);
    return std::string(buffer);
}

static std::ostream &tsOut(std::ostream &stream) {
    stream << timestampPrefix();
    return stream;
}

// Add the byte count sentinel
static std::size_t stringBytes(const string &value) { return value.capacity() + 1; } 

static std::size_t stringVectorBytes(const vector<string> &values) {
    // Size of vector + size of each actual string
    std::size_t total = values.capacity() * sizeof(string);
    for (const string &value : values) {
        total += stringBytes(value);
    }
    return total;
}

static std::size_t routedLinkBytes(const RoutedLink &link) {
    return sizeof(RoutedLink) + stringBytes(link.url) + stringVectorBytes(link.anchorText);
}

static string formatMiB(std::size_t bytes) {
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%.2f",
                  static_cast<double>(bytes) / (1024.0 * 1024.0)); // Bytes to MB
    return string(buffer);
}

static void logMemDebugHeartbeat() {
    if (!memDebug) {
        return;
    }

    tsOut(std::cout) << " frontier approx memory = " << formatMiB(f->approxMemoryBytes())
                     << " MiB\n";
    tsOut(std::cout) << " bloom filter approx memory = " << formatMiB(bloom.memoryUsageBytes())
                     << " MiB\n";

    IndexQueue::Stats queueStats = q->stats();
    tsOut(std::cout) << " index queue size = " << queueStats.itemCount << " pages, "
                     << formatMiB(queueStats.approxBytes) << " MiB\n";

    lock_guard<mutex> guard(batch_lock);
    for (size_t i = 0; i < batches.size(); ++i) {
        if (i == machine_id.load()) {
            continue;
        }

        std::size_t total = batches[i].capacity() * sizeof(RoutedLink);
        for (const RoutedLink &link : batches[i]) {
            total += routedLinkBytes(link);
        }
        tsOut(std::cout) << " send-to-peer batch[" << i << "] = " << batches[i].size()
                         << " urls, " << formatMiB(total)
                         << " MiB\n";
    }
    tsOut(std::cout) << " dropped send-to-peer links = " << droppedPeerBatchLinks.load() << '\n';
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
    if (peerReceiverStates != nullptr) {
        for (size_t i = 0; i < peer_address.size(); ++i) {
            if (i == machine_id.load()) {
                continue;
            }
            peerReceiverStates[i].cv.notify_one();
        }
    }
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

    lock_guard<mutex> guard(anchor_lock);
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
    lock_guard<mutex> guard(anchor_lock);
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

    char buffer[128] = {};
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

    size_t fileCount = anchorFlushFileCount;
    if (flushWordSnapshotToDisk(anchorSnapshot, fileCount, anchorIndex, anchorIndexEdited)) {
        anchorFlushFileCount++;
    }
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
            tsOut(std::cout) << " frontier size = " << f -> size() << '\n';
            logMemDebugHeartbeat();
            std::cout.flush();
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
            f->snoozeCurrent(*item, check.readyAtMs);
            taskCompletionGuard.dismiss();
            continue;
        }

        string page = readURL(item->link);
        if (shouldStop) {
            break;
        }

        if (page.empty()) {
            continue;
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
                if (batches[destinationMachine].size() < maxPeerBatchLinks) {
                    batches[destinationMachine].pushBack(std::move(routedLink));
                } else {
                    ++droppedPeerBatchLinks;
                }
                if (batches[destinationMachine].size() >= numLinkThreshold.load()) {
                    batch_cv.notify_all();
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

string buildMetaLine(const HtmlParser &doc) {
    static constexpr size_t MAX_TOTAL_LEN = 300;

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
        if (snippet.size() >= MAX_TOTAL_LEN)
            break;
        snippet += doc.words[i];
        snippet.pushBack(' ');
    }
    if (snippet.empty())
        snippet = "No content available.";
    sanitizeText(snippet);

    // strip leading special /non-printable bytes from snippet
    static constexpr size_t MAX_STRIP_SCAN = 10;
    size_t lead = 0;
    while (lead < snippet.size() && lead < MAX_STRIP_SCAN) {
        unsigned char c = (unsigned char)snippet[lead];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            break;
        }
        if (c < 0x80) {
            ++lead;
        } else if ((c & 0xE0) == 0xC0) {
            lead += 2;
        } else if ((c & 0xF0) == 0xE0) {
            lead += 3;
        } else if ((c & 0xF8) == 0xF0) {
            lead += 4;
        } else {
            ++lead;
        }
        if (lead > snippet.size()) {
            lead = snippet.size();
        }
    }
    if (lead > 0) {
        snippet = snippet.substr(lead);
    }
    if (snippet.empty()) {
        snippet = "No content available.";
    }

    // attempt to try to remove the beginning snippets. pretty whacky.
    size_t tpos = 0;
    size_t stripped_with_letter = 0;
    while (tpos < snippet.size() && tpos < 32) {
        size_t tok_start = tpos;
        size_t tok_end = tok_start;
        while (tok_end < snippet.size() && snippet[tok_end] != ' ') {
            ++tok_end;
        }
        size_t tok_len = tok_end - tok_start;
        if (tok_len < 3 || tok_len > 6)
            break;

        bool all_hex = true;
        bool has_digit = false;
        bool has_hex_letter = false;
        for (size_t i = tok_start; i < tok_end; ++i) {
            unsigned char c = (unsigned char)snippet[i];
            if (c >= '0' && c <= '9') {
                has_digit = true;
            } else if ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
                has_hex_letter = true;
            } else {
                all_hex = false;
                break;
            }
        }
        if (!all_hex)
            break;

        bool looks_like_codepoint = has_digit && has_hex_letter;
        bool trailing_digits = has_digit && !has_hex_letter && stripped_with_letter > 0;
        if (!looks_like_codepoint && !trailing_digits)
            break;

        if (looks_like_codepoint)
            ++stripped_with_letter;
        tpos = tok_end;
        if (tpos < snippet.size() && snippet[tpos] == ' ')
            ++tpos;
    }
    if (tpos > 0) {
        snippet = snippet.substr(tpos);
    }
    if (snippet.empty()) {
        snippet = "No content available.";
    }

    // cap each at 300
    if (title.size() > MAX_TOTAL_LEN) {
        title = title.substr(0, MAX_TOTAL_LEN);
    }
    if (snippet.size() > MAX_TOTAL_LEN) {
        snippet = snippet.substr(0, MAX_TOTAL_LEN);
    }

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

void *IndexWorkerThread(void *arg) {
    const size_t threadIndex = static_cast<IndexThreadArgs *>(arg)->threadIndex;
    Tokenizer tokenizer;
    InMemoryIndex mem_index;
    size_t docsProcessed = 0;
    size_t tokensProcessed = 0;
    time_t lastHeartbeat = time(nullptr);

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

        if (memDebug) {
            time_t now = time(nullptr);
            if ((now - lastHeartbeat) >= heartbeatIntervalSecs) {
                tsOut(std::cout) << "index thread " << threadIndex
                                 << " still alive; docs processed = " << docsProcessed
                                 << ", current in-memory index = " << mem_index.documentCount()
                                 << " docs, " << formatMiB(mem_index.approxMemoryBytes())
                                 << " MiB\n";
                lastHeartbeat = now;
            }
        }

        if (tokensProcessed >= FLUSHBODYTOKENSIZE) {
            const size_t chunkId = nextIndexChunkId.fetch_add(1);
            char buffer[64] = {};
            std::snprintf(buffer, sizeof(buffer), "%s/chunk_%zu.idx", indexDirectory.c_str(),
                          chunkId);
            const string path(buffer);

            char meta_buffer[64] = {};
            std::snprintf(meta_buffer, sizeof(meta_buffer), "%s/chunk_%zu.meta",
                          metaDirectory.c_str(), chunkId);
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
        }

        if (docsProcessed > 0 && docsProcessed % 10000 == 0) {
            tsOut(std::cout) << "Processed " << docsProcessed << " documents\n";
        }
    }

    // Write final partial chunk
    if (docsProcessed > 0) {
        const size_t chunkId = nextIndexChunkId.fetch_add(1);
        char buffer[64] = {};
        std::snprintf(buffer, sizeof(buffer), "%s/chunk_%zu.idx", indexDirectory.c_str(),
                      chunkId);
        const string path(buffer);

        char meta_buffer[64] = {};
        std::snprintf(meta_buffer, sizeof(meta_buffer), "%s/chunk_%zu.meta",
                      metaDirectory.c_str(), chunkId);
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

static constexpr size_t sendBatchRetryCount = 5;
static constexpr int sendBatchRetryBaseDelayMs = 1000;
static constexpr int handshakeTimeoutSecs = 5;

static size_t findCharFrom(const string &value, char c, size_t start) {
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

struct ParsedPeerAddress {
    string host;
    string port;
};

static ParsedPeerAddress parsePeerAddress(const string &peer) {
    size_t colon = peer.find(':');
    return ParsedPeerAddress{peer.substr(0, colon), peer.substr(colon + 1)};
}

static bool hasReadyBatchForPeer(size_t peerIndex) {
    return batches[peerIndex].size() >= numLinkThreshold.load() ||
           (shouldStop && batches[peerIndex].size() > 0);
}

static bool sendAllBytes(int socketFd, const char *data, size_t totalBytes, const string &peer) {
    size_t remaining = totalBytes;
    while (remaining > 0) {
        ssize_t sent = send(socketFd, data, remaining, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            tsOut(std::cerr) << "Failed while sending to peer " << peer << ": "
                             << std::strerror(errno) << '\n';
            return false;
        }
        if (sent == 0) {
            tsOut(std::cerr) << "Peer " << peer << " closed while sending\n";
            return false;
        }

        data += sent;
        remaining -= static_cast<size_t>(sent);
    }

    return true;
}

static bool recvAllBytes(int socketFd, unsigned char *data, size_t totalBytes, size_t peerIndex) {
    size_t remaining = totalBytes;
    while (remaining > 0) {
        ssize_t received = recv(socketFd, data, remaining, 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (!shouldStop) {
                tsOut(std::cerr) << "Receive failed from peer " << peerIndex << ": "
                                 << std::strerror(errno) << '\n';
            }
            return false;
        }
        if (received == 0) {
            return false;
        }

        data += received;
        remaining -= static_cast<size_t>(received);
    }

    return true;
}

static bool sendFrame(int socketFd, const string &peer, const string &payload) {
    unsigned char header[8];
    uint64_t value = static_cast<uint64_t>(payload.size());

    // encode header (payload size)
    for (int i = 7; i >= 0; --i) {
        header[i] = static_cast<unsigned char>(value & 0xffU);
        value >>= 8;
    }

    // We need to let peer know payload size
    if (!sendAllBytes(socketFd, reinterpret_cast<const char *>(header), sizeof(header), peer)) {
        return false;
    }

    if (payload.size() == 0) {
        return true;
    }

    return sendAllBytes(socketFd, payload.data(), payload.size(), peer);
}

static bool recvFrame(int socketFd, size_t peerIndex, string &payload) {
    unsigned char header[8];
    if (!recvAllBytes(socketFd, header, sizeof(header), peerIndex)) {
        return false;
    }

    // decode the message size
    uint64_t frameBytes = 0;
    for (size_t i = 0; i < 8; ++i) {
        frameBytes = (frameBytes << 8) | static_cast<uint64_t>(header[i]);
    }

    payload = string();
    char buffer[4096];
    uint64_t remaining = frameBytes;
    while (remaining > 0) {
        size_t chunkSize =
            remaining > sizeof(buffer) ? sizeof(buffer) : static_cast<size_t>(remaining);
        ssize_t received = recv(socketFd, buffer, chunkSize, 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (!shouldStop) {
                tsOut(std::cerr) << "Receive failed from peer " << peerIndex << ": "
                                 << std::strerror(errno) << '\n';
            }
            return false;
        }
        if (received == 0) {
            return false;
        }

        payload.append(buffer, static_cast<size_t>(received));
        remaining -= static_cast<uint64_t>(received);
    }

    return true;
}

static int connectToPeer(const string &peer) {
    ParsedPeerAddress parsedPeer = parsePeerAddress(peer);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo *result = nullptr;
    if (getaddrinfo(parsedPeer.host.c_str(), parsedPeer.port.c_str(), &hints, &result) != 0) {
        tsOut(std::cerr) << "Failed to resolve peer " << peer << '\n';
        return -1;
    }

    int socketFd = -1;
    for (addrinfo *rp = result; rp != nullptr; rp = rp->ai_next) {
        socketFd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (socketFd < 0) {
            continue;
        }

        int keepAliveOpt = 1;
        setsockopt(socketFd, SOL_SOCKET, SO_KEEPALIVE, &keepAliveOpt, sizeof(keepAliveOpt));
        if (connect(socketFd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;
        }

        close(socketFd);
        socketFd = -1;
    }

    freeaddrinfo(result);

    if (socketFd < 0) {
        tsOut(std::cerr) << "Failed to connect to peer " << peer << '\n';
    }

    return socketFd;
}

static string buildHandshakePayload() {
    return string("machine_id\t") + std::to_string(machine_id.load()).c_str() + "\n";
}

static bool parseHandshakePeerIndex(const string &payload, size_t &peerIndex) {
    static const string prefix("machine_id\t");
    if (payload.size() <= prefix.size() || payload.substr(0, prefix.size()) != prefix) {
        tsOut(std::cerr) << "Rejected peer connection with invalid handshake payload\n";
        return false;
    }

    string rawPeerIndex = payload.substr(prefix.size());
    if (!rawPeerIndex.empty() && rawPeerIndex.back() == '\n') {
        rawPeerIndex.pop_back();
    }
    if (!rawPeerIndex.empty() && rawPeerIndex.back() == '\r') {
        rawPeerIndex.pop_back();
    }

    if (!parseSizeField(rawPeerIndex, peerIndex)) {
        tsOut(std::cerr) << "Rejected peer connection with malformed machine id in handshake\n";
        return false;
    }
    if (peerIndex >= peer_address.size() || peerIndex == machine_id.load()) {
        tsOut(std::cerr) << "Rejected peer connection with invalid machine id " << peerIndex
                         << '\n';
        return false;
    }
    return true;
}

static bool ensureConnectedPeerSocket(size_t peerIndex, int &socketFd) {
    if (socketFd >= 0) {
        return true;
    }

    socketFd = connectToPeer(peer_address[peerIndex]);
    if (socketFd < 0) {
        return false;
    }

    if (!sendFrame(socketFd, peer_address[peerIndex], buildHandshakePayload())) {
        tsOut(std::cerr) << "Failed to send handshake to peer " << peerIndex << '\n';
        close(socketFd);
        socketFd = -1;
        return false;
    }

    if (debug) {
        tsOut(std::cout) << "Connected outbound socket to peer " << peerIndex << '\n';
    }

    return true;
}

static bool sendBatchToPeerWithRetry(size_t peerIndex, int &socketFd,
                                     const vector<RoutedLink> &batch) {
    // build payload
    string payload;
    payload.reserve(batch.size() * 96);
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

    for (size_t attempt = 0; attempt <= sendBatchRetryCount; ++attempt) {
        if (ensureConnectedPeerSocket(peerIndex, socketFd) &&
            sendFrame(socketFd, peer_address[peerIndex], payload)) {
            // tsOut(std::cout) << "Sent batch frame to peer " << peerIndex << " with "
            //                  << batch.size() << " links and " << payload.size() << " bytes\n";
            return true;
        }

        tsOut(std::cerr) << "Failed to send batch frame to peer " << peerIndex << " on attempt "
                         << (attempt + 1) << "/" << (sendBatchRetryCount + 1) << " with "
                         << batch.size() << " links and " << payload.size() << " bytes\n";

        if (socketFd >= 0) {
            close(socketFd);
            socketFd = -1;
        }

        if (attempt == sendBatchRetryCount) {
            tsOut(std::cout) << "Send batch to peer " << peerIndex << " failed after retries\n";
            break;
        }

        if (debug) {
            tsOut(std::cout) << "Retrying peer " << peerIndex << " batch send\n";
        }

        const int delayMs = sendBatchRetryBaseDelayMs * (1 << attempt);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }

    return false;
}

void *SendToPeerThread(void *arg) {
    const size_t peerIndex = static_cast<PeerThreadArgs *>(arg)->peerIndex;
    int socketFd = -1;

    while (true) {
        vector<RoutedLink> readyBatch;

        batch_lock.lock();
        while (!hasReadyBatchForPeer(peerIndex) && !shouldStop) {
            batch_cv.wait(batch_lock);
        }

        if (!hasReadyBatchForPeer(peerIndex)) {
            batch_lock.unlock();
            break;
        }

        readyBatch = std::move(batches[peerIndex]);
        batches[peerIndex] = vector<RoutedLink>();
        batch_lock.unlock();

        if (!sendBatchToPeerWithRetry(peerIndex, socketFd, readyBatch)) {
            // batch_lock.lock();
            // for (const RoutedLink &link : readyBatch) {
            //     batches[peerIndex].pushBack(link);
            // }
            // batch_lock.unlock();
            // batch_cv.notify_all();
            tsOut(std::cerr << "Send batch to peer " << peerIndex << " failed after retry; dropped");
        }
    }

    if (socketFd >= 0) {
        close(socketFd);
    }
    return nullptr;
}

static int openListeningSocket() {
    if (peer_address.size() <= 1 || machine_id.load() >= peer_address.size()) {
        return -1;
    }

    const string &selfPeer = peer_address[machine_id.load()];
    if (selfPeer.empty()) {
        return -1;
    }

    ParsedPeerAddress parsedSelfPeer = parsePeerAddress(selfPeer);

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo *result = nullptr;
    if (getaddrinfo(nullptr, parsedSelfPeer.port.c_str(), &hints, &result) != 0) {
        tsOut(std::cerr) << "Failed to resolve listen address " << selfPeer << '\n';
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
        tsOut(std::cerr) << "Listening for batches on " << selfPeer << '\n';
    }
    return listenFd;
}

static void processReceivedBatch(const string &payload) {
    if (payload.empty()) {
        return;
    }

    vector<FrontierItem> discoveredLinks;
    size_t lineStart = 0;
    while (lineStart < payload.size()) {
        size_t lineEnd = findCharFrom(payload, '\n', lineStart);
        size_t lineLength =
            lineEnd != string::npos ? lineEnd - lineStart : payload.size() - lineStart;

        string line = payload.substr(lineStart, lineLength);
        if (!line.empty()) {
            size_t fieldEnd = findCharFrom(line, '\t', 0);
            string rawUrl = fieldEnd == string::npos ? line : line.substr(0, fieldEnd);
            vector<string> anchorWords;
            size_t receivedSeedDistance = 0;

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

            while (fieldEnd != string::npos) {
                size_t fieldStart = fieldEnd + 1;
                fieldEnd = findCharFrom(line, '\t', fieldStart);
                size_t fieldLength =
                    fieldEnd == string::npos ? line.size() - fieldStart : fieldEnd - fieldStart;
                if (fieldLength > 0) {
                    anchorWords.pushBack(line.substr(fieldStart, fieldLength));
                }
            }

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

void *AcceptPeerConnectionsThread(void *) {
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

        sockaddr_storage clientAddr{};
        socklen_t clientAddrLen = sizeof(clientAddr);
        int clientFd = accept(listenFd, reinterpret_cast<sockaddr *>(&clientAddr), &clientAddrLen);
        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            tsOut(std::cerr) << "Accept failed: " << std::strerror(errno) << '\n';
            continue;
        }

        int keepAliveOpt = 1;
        setsockopt(clientFd, SOL_SOCKET, SO_KEEPALIVE, &keepAliveOpt, sizeof(keepAliveOpt));

        timeval handshakeTimeout{};
        handshakeTimeout.tv_sec = handshakeTimeoutSecs;
        handshakeTimeout.tv_usec = 0;
        setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &handshakeTimeout, sizeof(handshakeTimeout));

        string handshakePayload;
        if (!recvFrame(clientFd, peer_address.size(), handshakePayload)) {
            tsOut(std::cerr) << "Rejected peer connection with missing handshake\n";
            close(clientFd);
            continue;
        }

        timeval noTimeout{};
        noTimeout.tv_sec = 0;
        noTimeout.tv_usec = 0;
        setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &noTimeout, sizeof(noTimeout));

        size_t remotePeerIndex = 0;
        if (!parseHandshakePeerIndex(handshakePayload, remotePeerIndex)) {
            close(clientFd);
            continue;
        }

        if (debug) {
            tsOut(std::cout) << "Accepted persistent connection from peer " << remotePeerIndex
                             << '\n';
        }

        PeerReceiverState &state = peerReceiverStates[remotePeerIndex];
        state.lock.lock();
        if (state.pendingSocketFd >= 0) {
            close(state.pendingSocketFd);
        }
        state.pendingSocketFd = clientFd;
        state.cv.notify_one();
        state.lock.unlock();
    }

    close(listenFd);
    return nullptr;
}

void *ReceiveFromPeerThread(void *arg) {
    const size_t peerIndex = static_cast<PeerThreadArgs *>(arg)->peerIndex;
    PeerReceiverState &state = peerReceiverStates[peerIndex];

    while (true) {
        int socketFd = -1;

        state.lock.lock();
        while (state.pendingSocketFd < 0 && !state.stop && !shouldStop) {
            state.cv.wait(state.lock);
        }

        if (state.pendingSocketFd < 0 && (state.stop || shouldStop)) {
            state.lock.unlock();
            break;
        }

        socketFd = state.pendingSocketFd;
        state.pendingSocketFd = -1;
        state.activeSocketFd = socketFd;
        state.lock.unlock();

        if (debug) {
            tsOut(std::cout) << "Receiver thread attached to peer " << peerIndex << '\n';
        }

        while (!shouldStop) {
            string payload;
            if (!recvFrame(socketFd, peerIndex, payload)) {
                tsOut(std::cerr) << "Receiver lost frame stream from peer " << peerIndex << '\n';
                break;
            }

            if (debug) {
                tsOut(std::cout) << "Received batch frame from peer " << peerIndex << " with "
                                 << payload.size() << " bytes\n";
            }
            processReceivedBatch(payload);
        }

        shutdown(socketFd, SHUT_RDWR);
        close(socketFd);

        state.lock.lock();
        if (state.activeSocketFd == socketFd) {
            state.activeSocketFd = -1;
        }
        state.lock.unlock();
    }

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
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    urlFilter.loadBlacklist("src/crawler/blackList.txt");

    cpConfig.directory = "src/crawler";
    checkpoint = new Checkpoint(cpConfig);
    q = new IndexQueue(maxIndexQueueItems);
    robotsCache = new RobotsCache();
    anchorIndex = new HashTable<string, WordPosting>(anchorKeyEqual, anchorKeyHash);

    const char *rawPeers = std::getenv("SEARCH_PEERS");
    if (rawPeers == nullptr || rawPeers[0] == '\0') {
        peer_address = vector<string>(1);
        peer_address[0] = "";
    } else {
        size_t start = 0;
        const string peers(rawPeers);
        while (start < peers.size()) {
            size_t end = findCharFrom(peers, ',', start);
            size_t len = end == string::npos ? peers.size() - start : end - start;
            string peer = peers.substr(start, len);
            if (!peer.empty()) {
                peer_address.pushBack(peer);
            }
            if (end == string::npos) {
                break;
            }
            start = end + 1;
        }
        if (peer_address.size() == 0) {
            peer_address = vector<string>(1);
            peer_address[0] = "";
        }
    }

    if (argc < 3) {
        tsOut(std::cerr) << "Usage: " << argv[0]
                         << " <machine_id> <batch_threshold> [debug] [mem-debug]\n";
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
    for (int i = 3; i < argc; ++i) {
        const string arg(argv[i]);
        if (arg == "debug") {
            debug = true;
            continue;
        }
        if (arg == "mem-debug") {
            memDebug = true;
            continue;
        }

        tsOut(std::cerr) << "Optional arguments must be 'debug' or 'mem-debug'\n";
        return 1;
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
    size_t indexThreadCount = 4;
    vector<pthread_t> crawlerThreads(crawlerThreadCount);
    vector<pthread_t> indexThreads(indexThreadCount);
    vector<IndexThreadArgs> indexThreadArgs(indexThreadCount);
    pthread_t acceptThread{};
    pthread_t checkpointThread{};
    pthread_t *peerSenderThreads = nullptr;
    pthread_t *peerReceiverThreads = nullptr;
    bool *peerSenderStarted = nullptr;
    bool *peerReceiverStarted = nullptr;
    bool acceptStarted = false;
    bool checkpointStarted = false;

    lastCheckpointTime = time(nullptr);
    lastHeartbeatTime = lastCheckpointTime.load();

    for (size_t i = 0; i < crawlerThreadCount; ++i) {
        pthread_create(&crawlerThreads[i], nullptr, CrawlerWorkerThread, nullptr);
    }

    for (size_t i = 0; i < indexThreadCount; ++i) {
        indexThreadArgs[i].threadIndex = i;
        pthread_create(&indexThreads[i], nullptr, IndexWorkerThread, &indexThreadArgs[i]);
    }

    pthread_create(&checkpointThread, nullptr, CheckpointThread, nullptr);
    checkpointStarted = true;

    if (peer_address.size() > 1) {
        // For local frontier testing, the default configuration leaves SEARCH_PEERS unset,
        // which gives us a single-machine run and bypasses all socket code.
        const size_t peerCount = peer_address.size();
        peerReceiverStates = new PeerReceiverState[peerCount];
        peerSenderThreadArgs = new PeerThreadArgs[peerCount];
        peerReceiverThreadArgs = new PeerThreadArgs[peerCount];
        peerSenderThreads = new pthread_t[peerCount];
        peerReceiverThreads = new pthread_t[peerCount];
        peerSenderStarted = new bool[peerCount]();
        peerReceiverStarted = new bool[peerCount]();

        for (size_t i = 0; i < peerCount; ++i) {
            peerSenderThreadArgs[i].peerIndex = i;
            peerReceiverThreadArgs[i].peerIndex = i;
            if (i == machine_id.load()) {
                continue;
            }

            pthread_create(&peerSenderThreads[i], nullptr, SendToPeerThread,
                           &peerSenderThreadArgs[i]);
            peerSenderStarted[i] = true;

            pthread_create(&peerReceiverThreads[i], nullptr, ReceiveFromPeerThread,
                           &peerReceiverThreadArgs[i]);
            peerReceiverStarted[i] = true;
        }

        pthread_create(&acceptThread, nullptr, AcceptPeerConnectionsThread, nullptr);
        acceptStarted = true;
    }

    // pthread_create(&anchorThread, nullptr, AnchorFlushThread, nullptr);

    for (size_t i = 0; i < crawlerThreads.size(); ++i) {
        pthread_join(crawlerThreads[i], nullptr);
    }

    shouldStop = true;
    q->shutdown();
    batch_cv.notify_all();

    if (peerReceiverStates != nullptr) {
        // making sure all socket correctly closed
        for (size_t i = 0; i < peer_address.size(); ++i) {
            if (i == machine_id.load()) {
                continue;
            }

            PeerReceiverState &state = peerReceiverStates[i];
            state.lock.lock();
            state.stop = true;
            if (state.activeSocketFd >= 0) {
                shutdown(state.activeSocketFd, SHUT_RDWR);
            }
            if (state.pendingSocketFd >= 0) {
                shutdown(state.pendingSocketFd, SHUT_RDWR);
                close(state.pendingSocketFd);
                state.pendingSocketFd = -1;
            }
            state.cv.notify_one();
            state.lock.unlock();
        }
    }

    if (checkpointStarted) {
        pthread_join(checkpointThread, nullptr);
    }

    for (size_t i = 0; i < indexThreads.size(); ++i) {
        pthread_join(indexThreads[i], nullptr);
    }

    if (acceptStarted) {
        pthread_join(acceptThread, nullptr);
    }

    if (peerSenderThreads != nullptr) {
        for (size_t i = 0; i < peer_address.size(); ++i) {
            if (i == machine_id.load()) {
                continue;
            }
            if (peerSenderStarted[i]) {
                pthread_join(peerSenderThreads[i], nullptr);
            }
        }
    }

    if (peerReceiverThreads != nullptr) {
        for (size_t i = 0; i < peer_address.size(); ++i) {
            if (i == machine_id.load()) {
                continue;
            }
            if (peerReceiverStarted[i]) {
                pthread_join(peerReceiverThreads[i], nullptr);
            }
        }
    }

    // pthread_join(anchorThread, nullptr);

    checkpoint->save(*f, bloom, urlsCrawled.load());

    // force flush Anchor even if Anchor was unchanged
    flushAnchorIndexToDisk(true);

    if (shouldStop) {
        tsOut(std::cerr) << "Graceful shutdown after SIGINT\n";
    }

    delete anchorIndex;
    delete robotsCache;
    delete f;
    delete q;
    delete checkpoint;
    delete[] peerSenderThreads;
    delete[] peerReceiverThreads;
    delete[] peerSenderStarted;
    delete[] peerReceiverStarted;
    delete[] peerSenderThreadArgs;
    delete[] peerReceiverThreadArgs;
    delete[] peerReceiverStates;

    cleanupSSL();
    return 0;
}
