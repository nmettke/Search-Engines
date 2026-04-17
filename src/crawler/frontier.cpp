#include "frontier.h"

#include <dirent.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>

namespace {

thread_local string frontierActiveHostKey;
constexpr std::size_t frontierReservoirSweepChunkSize = 512;
constexpr std::size_t frontierReservoirPromotionPercent = 25;

constexpr std::size_t frontierDiskBackBackupPercent = 70; // when do we push to disk
constexpr std::size_t frontierReservoirRefillPercent = 50; // when to we refill reservoir
constexpr std::size_t frontierDiskBackRefillPercent = 30; // when do we refill disk back
constexpr const char *frontierDiskBackChunkDir = "data/disk_chunk_backup";
constexpr const char *frontierDiskBackChunkPrefix = "frontier_disk_back_chunk";

bool frontierHostKeyEqual(const string a, const string b) { return a == b; }

std::uint64_t frontierHostKeyHash(const string key) {
    std::uint64_t hash = 0;
    const char *p = key.cstr();
    while (p != nullptr && *p != '\0') {
        hash = hash * 131u + static_cast<unsigned char>(*p);
        ++p;
    }
    return hash;
}

std::size_t resolveFrontierMaxQueuedItems(std::size_t configuredMaxQueuedItems) {
    if (configuredMaxQueuedItems != 0) {
        return configuredMaxQueuedItems;
    }

    // std::size_t parsed = 2000000; // I have chosen to hard code this for simplicity
    std::size_t parsed = 20000; // I have chosen to hard code this for simplicity
    return parsed;
}
} // namespace

std::int64_t Frontier::nowMillis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

string &Frontier::activeHostKey() { return frontierActiveHostKey; }

string Frontier::extractHostKey(const string &url) {
    size_t schemeEnd = url.find("://");
    size_t hostStart = schemeEnd == string::npos ? 0 : schemeEnd + 3;
    size_t hostEnd = url.size();

    for (size_t i = hostStart; i < url.size(); ++i) {
        char c = url[i];
        if (c == '/' || c == ':' || c == '?' || c == '#') {
            hostEnd = i;
            break;
        }
    }

    return string(url.cstr() + hostStart, url.cstr() + hostEnd);
}

Frontier::Frontier(const string &seed_list_str, bool autoCloseWhenDrainedArg,
                   size_t maxQueuedItemsArg)
    : hostQueues(frontierHostKeyEqual, frontierHostKeyHash), closed(false),
      autoCloseWhenDrained(autoCloseWhenDrainedArg), pending(0), queued(0),
      maxQueuedItems(resolveFrontierMaxQueuedItems(maxQueuedItemsArg)) {
    recoverDiskBackedChunkCount();

    std::ifstream seedList(seed_list_str.c_str());
    if (!seedList.is_open()) {
        throw std::runtime_error("seedList could not be opened");
    }

    std::string line;
    while (std::getline(seedList, line)) {
        pushInternal(FrontierItem(string(line.c_str())), true);
    }

    if (pending == 0 && autoCloseWhenDrained) {
        closed = true;
    }
}

Frontier::Frontier(vector<FrontierItem> items, bool autoCloseWhenDrainedArg,
                   size_t maxQueuedItemsArg)
    : hostQueues(frontierHostKeyEqual, frontierHostKeyHash), closed(false),
      autoCloseWhenDrained(autoCloseWhenDrainedArg), pending(0), queued(0),
      maxQueuedItems(resolveFrontierMaxQueuedItems(maxQueuedItemsArg)) {
    recoverDiskBackedChunkCount();

    for (size_t i = 0; i < items.size(); ++i) {
        if (!items[i].link.empty()) {
            pushInternal(items[i], true);
        }
    }

    if (pending == 0 && autoCloseWhenDrained) {
        closed = true;
    }
}

void Frontier::scheduleHost(const string &hostKey, HostQueue &host, std::int64_t nowMs) {
    if (host.items.empty() || host.inFlight || host.inReadyQueue) {
        return;
    }

    if (host.blockedUntilMs > nowMs) {
        if (!host.inSleepingQueue) {
            sleepingHosts.push({host.blockedUntilMs, host.sleepGeneration, hostKey});
            host.inSleepingQueue = true;
        }
        return;
    }

    if (host.blockedUntilMs != 0 && host.blockedUntilMs <= nowMs) {
        host.blockedUntilMs = 0;
    }
    host.inSleepingQueue = false;

    readyHosts.pushBack(hostKey);
    host.inReadyQueue = true;
}

void Frontier::promoteSleepingHosts(std::int64_t nowMs) {
    while (!sleepingHosts.empty() && sleepingHosts.top().readyAtMs <= nowMs) {
        SleepingHost sleeping = sleepingHosts.extractTop();

        Tuple<string, HostQueue> *hostTuple = hostQueues.Find(sleeping.hostKey);
        if (hostTuple == nullptr) {
            continue;
        }

        HostQueue &host = hostTuple->value;
        if (host.sleepGeneration != sleeping.generation || host.items.empty() || host.inFlight ||
            host.inReadyQueue) {
            continue;
        }

        if (host.blockedUntilMs > nowMs) {
            continue;
        }

        host.blockedUntilMs = 0;
        host.inSleepingQueue = false;
        readyHosts.pushBack(sleeping.hostKey);
        host.inReadyQueue = true;
    }
}

void Frontier::enqueueScheduledItem(const FrontierItem &item, std::int64_t nowMs) {
    string hostKey = extractHostKey(item.link);
    Tuple<string, HostQueue> *hostTuple = hostQueues.Find(hostKey, HostQueue());
    HostQueue &host = hostTuple->value;
    host.items.pushBack(item);
    scheduleHost(hostKey, host, nowMs);
}

void Frontier::promoteReservoir(std::int64_t nowMs) {
    const std::size_t reservoirRefillChunkSize = maxQueuedItems * frontierReservoirRefillPercent / 100;
    const std::size_t diskBackRefillSize = maxQueuedItems * frontierDiskBackRefillPercent / 100;

    if (disk_back_reservoir.size() < diskBackRefillSize) {
        loadDiskBackedChunkFromDisk();
    }

    if (reservoir.size() < reservoirRefillChunkSize) {
        refillReservoirFromDiskBacked();
    }


    if (!readyHosts.empty() || reservoir.empty()) {
        return;
    }

    // When the runnable host list is empty, pull a bounded chunk from the reservoir,
    // score only that chunk, and promote the strongest URLs into host queues.
    while (readyHosts.empty() && !reservoir.empty()) {
        if (reservoirSweepCursor >= reservoir.size()) {
            reservoirSweepCursor = 0;
        }

        const std::size_t scannedCount = reservoir.size() < frontierReservoirSweepChunkSize
                                             ? reservoir.size()
                                             : frontierReservoirSweepChunkSize;
        std::size_t winnersToKeep = (scannedCount * frontierReservoirPromotionPercent + 99) / 100;
        if (winnersToKeep == 0) {
            winnersToKeep = 1;
        }
        if (winnersToKeep > scannedCount) {
            winnersToKeep = scannedCount;
        }
        if (winnersToKeep == 0) {
            return;
        }

        vector<PromotionCandidate> winners;
        winners.reserve(winnersToKeep);

        for (std::size_t offset = 0; offset < scannedCount; ++offset) {
            std::size_t index = (reservoirSweepCursor + offset) % reservoir.size();
            PromotionCandidate candidate{index, reservoir[index].getScore()};

            if (winners.size() < winnersToKeep) {
                winners.pushBack(candidate);
                continue;
            }

            std::size_t worstWinnerIndex = 0;
            for (std::size_t i = 1; i < winners.size(); ++i) {
                if (winners[i].score < winners[worstWinnerIndex].score) {
                    worstWinnerIndex = i;
                }
            }

            if (candidate.score > winners[worstWinnerIndex].score) {
                winners[worstWinnerIndex] = candidate;
            }
        }

        if (winners.empty()) {
            return;
        }

        vector<FrontierItem> promotedItems;
        promotedItems.reserve(winners.size());
        for (std::size_t i = 0; i < winners.size(); ++i) {
            promotedItems.pushBack(reservoir[winners[i].index]);
        }

        std::size_t nextSweepCursor = reservoirSweepCursor + scannedCount;
        if (!reservoir.empty()) {
            nextSweepCursor %= reservoir.size();
        }

        const bool scannedChunkTouchesReservoirTail =
            scannedCount == reservoir.size() ||
            reservoirSweepCursor + scannedCount > reservoir.size();

        // Swap-remove is safe in arbitrary order unless this chunk reaches the
        // physical tail of the reservoir. In that case, repeatedly exposing new
        // tail elements can corrupt saved winner indices, so remove from largest
        // index to smallest.
        if (scannedChunkTouchesReservoirTail) {
            while (!winners.empty()) {
                std::size_t maxIndexPos = 0;
                for (std::size_t i = 1; i < winners.size(); ++i) {
                    if (winners[i].index > winners[maxIndexPos].index) {
                        maxIndexPos = i;
                    }
                }

                std::size_t index = winners[maxIndexPos].index;
                if (index + 1 != reservoir.size()) {
                    reservoir[index] = std::move(reservoir[reservoir.size() - 1]);
                }
                reservoir.popBack();
                if (maxIndexPos + 1 != winners.size()) {
                    winners[maxIndexPos] = winners[winners.size() - 1];
                }
                winners.popBack();
            }
        } else {
            while (!winners.empty()) {
                std::size_t index = winners[winners.size() - 1].index;
                if (index + 1 != reservoir.size()) {
                    reservoir[index] = std::move(reservoir[reservoir.size() - 1]);
                }
                reservoir.popBack();
                winners.popBack();
            }
        }

        if (!reservoir.empty()) {
            reservoirSweepCursor = nextSweepCursor % reservoir.size();
        } else {
            reservoirSweepCursor = 0;
        }

        for (std::size_t i = 0; i < promotedItems.size(); ++i) {
            enqueueScheduledItem(promotedItems[i], nowMs);
        }
        nowMs = nowMillis();
    }
}

// New frontier backup logic
void Frontier::doBackUp() {
    if (disk_back_reservoir.size() < maxQueuedItems) {
        return;
    }

    std::size_t backupCount = disk_back_reservoir.size() * frontierDiskBackBackupPercent;

    vector<FrontierItem> backup;
    backup.reserve(backupCount);
    for (std::size_t i = 0; i < backupCount; ++i) {
        backup.pushBack(disk_back_reservoir[i]);
    }

    const std::size_t nextChunkIndex = diskBackedChunksOnDisk + 1;
    
    if (!writeDiskChunk(backup, nextChunkIndex)) {
        return;
    }

    vector<FrontierItem> remainingItems;
    remainingItems.reserve(disk_back_reservoir.size() - backupCount);
    for (std::size_t i = backupCount; i < disk_back_reservoir.size(); ++i) {
        remainingItems.pushBack(std::move(disk_back_reservoir[i]));
    }

    disk_back_reservoir = std::move(remainingItems);
    diskBackedChunksOnDisk = nextChunkIndex;
}

void Frontier::refillReservoirFromDiskBacked() {
    if (reservoir.size() < maxQueuedItems || disk_back_reservoir.empty()) {
        return;
    }
    std::size_t refillCount = maxQueuedItems * frontierReservoirRefillPercent;

    if (refillCount > disk_back_reservoir.size()) {
        refillCount = disk_back_reservoir.size();
    }

    vector<FrontierItem> remainingItems;
    remainingItems.reserve(disk_back_reservoir.size() - refillCount);
    for (std::size_t i = refillCount; i < disk_back_reservoir.size(); ++i) {
        remainingItems.pushBack(std::move(disk_back_reservoir[i]));
    }

    for (std::size_t i = 0; i < refillCount; ++i) {
        reservoir.pushBack(std::move(disk_back_reservoir[i]));
    }

    disk_back_reservoir = std::move(remainingItems);
}

bool Frontier::loadDiskBackedChunkFromDisk() {
    if (diskBackedChunksOnDisk == 0) {
        return false;
    }

    string chunkPath;
    if (!findDiskChunkPath(diskBackedChunksOnDisk, chunkPath)) {
        return false;
    }

    vector<FrontierItem> chunkItems;
    if (!readDiskChunk(chunkPath, chunkItems)) {
        return false;
    }

    for (std::size_t i = 0; i < chunkItems.size(); ++i) {
        disk_back_reservoir.pushBack(std::move(chunkItems[i]));
    }

    queued += chunkItems.size();
    pending += chunkItems.size();
    std::remove(chunkPath.c_str());
    --diskBackedChunksOnDisk;
    return true;
}

// Used on restart, recover the number of disk back chunk we have on disk
void Frontier::recoverDiskBackedChunkCount() {
    DIR *dir = opendir(frontierDiskBackChunkDir);
    if (dir == nullptr) {
        return;
    }

    std::size_t maxChunkIndex = 0;
    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        std::size_t chunkIndex = 0;
        std::size_t chunkItemCount = 0;
        if (std::sscanf(entry->d_name, "frontier_disk_back_chunk_%zu_%zu.dat", &chunkIndex,
                        &chunkItemCount) != 2 ||
            chunkIndex == 0 || chunkItemCount == 0) {
            continue;
        }

        if (chunkIndex > maxChunkIndex) {
            maxChunkIndex = chunkIndex;
        }
    }
    closedir(dir);

    diskBackedChunksOnDisk = maxChunkIndex;
}

string Frontier::diskChunkPath(std::size_t chunkIndex, std::size_t chunkItemCount) const {
    char suffixBuffer[64];
    std::snprintf(suffixBuffer, sizeof(suffixBuffer), "_%zu_%zu.dat", chunkIndex, chunkItemCount);

    string path = string(frontierDiskBackChunkDir);
    path += "/";
    path += frontierDiskBackChunkPrefix;
    path += suffixBuffer;
    return path;
}

bool Frontier::findDiskChunkPath(std::size_t chunkIndex, string &path) const {
    DIR *dir = opendir(frontierDiskBackChunkDir);
    if (dir == nullptr) {
        return false;
    }

    bool found = false;
    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        std::size_t candidateChunkIndex = 0;
        std::size_t candidateChunkItemCount = 0;
        if (std::sscanf(entry->d_name, "frontier_disk_back_chunk_%zu_%zu.dat",
                        &candidateChunkIndex, &candidateChunkItemCount) != 2) {
            continue;
        }

        if (candidateChunkIndex == chunkIndex) {
            path = string(frontierDiskBackChunkDir);
            path += "/";
            path += string(entry->d_name);
            found = true;
            break;
        }
    }

    closedir(dir);
    return found;
}

bool Frontier::writeDiskChunk(const vector<FrontierItem> &items, std::size_t chunkIndex) const {
    FILE *f = std::fopen(diskChunkPath(chunkIndex, items.size()).c_str(), "wb");
    if (f == nullptr) {
        std::cerr << "Frontier: failed to open disk-back chunk for write\n";
        return false;
    }

    for (std::size_t i = 0; i < items.size(); ++i) {
        string line = items[i].serializeToLine();
        std::fprintf(f, "%s\n", line.c_str());
    }

    const bool writeOk = std::fflush(f) == 0;
    std::fclose(f);
    return writeOk;
}

bool Frontier::readDiskChunk(const string &path, vector<FrontierItem> &items) const {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        std::cerr << "Frontier: failed to open disk-back chunk for read\n";
        return false;
    }

    char lineBuf[8192];
    while (std::fgets(lineBuf, sizeof(lineBuf), f) != nullptr) {
        const std::size_t len = std::strlen(lineBuf);
        if (len > 0 && lineBuf[len - 1] == '\n') {
            lineBuf[len - 1] = '\0';
        }
        if (lineBuf[0] != '\0') {
            items.pushBack(FrontierItem::deserializeFromLine(string(lineBuf)));
        }
    }

    std::fclose(f);
    return true;
}

void Frontier::pushInternal(const FrontierItem &item, bool countTowardsPending) {
    if (item.link.empty()) {
        return;
    }

    if (countTowardsPending) {
        ++pending;
    }
    ++queued;

    if (reservoir.size() < maxQueuedItems) {
        reservoir.pushBack(item);
        return;
    }

    doBackUp();
    disk_back_reservoir.pushBack(item);
}

vector<FrontierItem> Frontier::snapshot() const {
    vector<FrontierItem> result;
    lock_guard<mutex> guard(m);
    HostTable &queues = const_cast<HostTable &>(hostQueues);
    for (HostTable::Iterator it = queues.begin(); it != queues.end(); ++it) {
        it->value.items.forEach([&result](const FrontierItem &item) { result.pushBack(item); });
    }
    for (std::size_t i = 0; i < reservoir.size(); ++i) {
        result.pushBack(reservoir[i]);
    }
    for (std::size_t i = 0; i < disk_back_reservoir.size(); ++i) {
        result.pushBack(disk_back_reservoir[i]);
    }
    return result;
}

void Frontier::push(const string &url) {
    lock_guard<mutex> guard(m);
    if (closed || url.empty()) {
        return;
    }

    pushInternal(FrontierItem(url), true);
    cv.notify_one();
}

void Frontier::push(const FrontierItem &item) {
    lock_guard<mutex> guard(m);
    if (closed || item.link.empty()) {
        return;
    }

    pushInternal(item, true);
    cv.notify_one();
}

void Frontier::pushMany(const vector<string> &urls) {
    lock_guard<mutex> guard(m);
    if (closed || urls.size() == 0) {
        return;
    }

    for (const string &url : urls) {
        if (!url.empty()) {
            pushInternal(FrontierItem(url), true);
        }
    }

    cv.notify_all();
}

void Frontier::pushMany(const vector<FrontierItem> &items) {
    lock_guard<mutex> guard(m);
    if (closed || items.size() == 0) {
        return;
    }

    for (const FrontierItem &item : items) {
        if (!item.link.empty()) {
            pushInternal(item, true);
        }
    }

    cv.notify_all();
}

void Frontier::pushDeferred(const vector<FrontierItem> &items) {
    lock_guard<mutex> guard(m);
    if (closed || items.size() == 0) {
        return;
    }

    for (const FrontierItem &item : items) {
        if (!item.link.empty()) {
            pushInternal(item, false);
        }
    }

    cv.notify_all();
}

void Frontier::snoozeCurrent(const FrontierItem &item, std::int64_t readyAtMs) {
    lock_guard<mutex> guard(m);

    string &hostKey = activeHostKey();
    if (hostKey.empty()) {
        return;
    }

    Tuple<string, HostQueue> *hostTuple = hostQueues.Find(hostKey);
    if (hostTuple == nullptr) {
        hostKey = string();
        return;
    }

    HostQueue &host = hostTuple->value;
    host.inFlight = false;

    if (!item.link.empty()) {
        host.items.pushFront(item);
        ++queued;
    }

    if (readyAtMs > host.blockedUntilMs) {
        host.blockedUntilMs = readyAtMs;
    }
    ++host.sleepGeneration;
    std::int64_t now = nowMillis();
    if (host.blockedUntilMs > now) {
        sleepingHosts.push({host.blockedUntilMs, host.sleepGeneration, hostKey});
        host.inSleepingQueue = true;
    } else {
        scheduleHost(hostKey, host, now);
    }
    hostKey = string();
    cv.notify_all();
}

std::optional<FrontierItem> Frontier::pop() {
    m.lock();

    // pop() is the handoff point between the two frontier layers:
    // wake delayed hosts, refill from the reservoir if needed, then dispatch
    // one URL from the next runnable host.
    while (true) {
        std::int64_t now = nowMillis();
        promoteSleepingHosts(now);
        promoteReservoir(now);

        while (!readyHosts.empty()) {
            string hostKey = readyHosts.popFront();

            Tuple<string, HostQueue> *hostTuple = hostQueues.Find(hostKey);
            if (hostTuple == nullptr) {
                continue;
            }

            HostQueue &host = hostTuple->value;
            host.inReadyQueue = false;
            if (host.inFlight || host.items.empty()) {
                continue;
            }

            FrontierItem item = host.items.popFront();
            --queued;
            host.inFlight = true;
            activeHostKey() = hostKey;

            m.unlock();
            return item;
        }

        if (closed) {
            m.unlock();
            return std::nullopt;
        }

        if (!sleepingHosts.empty()) {
            cv.wait_until(m, sleepingHosts.top().readyAtMs);
        } else {
            cv.wait(m);
        }
    }
}

void Frontier::releaseActiveHost(std::int64_t nowMs) {
    string &hostKey = activeHostKey();
    if (hostKey.empty()) {
        return;
    }

    Tuple<string, HostQueue> *hostTuple = hostQueues.Find(hostKey);
    if (hostTuple == nullptr) {
        hostKey = string();
        return;
    }

    HostQueue &host = hostTuple->value;
    host.inFlight = false;
    scheduleHost(hostKey, host, nowMs);
    hostKey = string();
}

void Frontier::taskDone() {
    lock_guard<mutex> guard(m);
    if (pending == 0) {
        releaseActiveHost(nowMillis());
        return;
    }

    --pending;
    releaseActiveHost(nowMillis());

    if (pending == 0) {
        if (autoCloseWhenDrained) {
            closed = true;
        }
        cv.notify_all();
        return;
    }

    cv.notify_one();
}

void Frontier::shutdown() {
    lock_guard<mutex> guard(m);
    closed = true;
    cv.notify_all();
}

size_t Frontier::size() const {
    lock_guard<mutex> guard(m);
    return queued;
}

bool Frontier::empty() const {
    lock_guard<mutex> guard(m);
    return queued == 0;
}

bool Frontier::hasInFlightWork() const {
    lock_guard<mutex> guard(m);
    return pending > queued;
}
