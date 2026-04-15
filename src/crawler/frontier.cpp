#include "frontier.h"

#include <cstdlib>
#include <time.h>

namespace {

thread_local string frontierActiveHostKey;
constexpr std::size_t frontierReservoirSweepChunkSize = 256;
constexpr std::size_t frontierReservoirPromotionPercent = 25;
constexpr std::size_t frontierReservoirTrimChunkSize = 256;

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

    std::size_t parsed = 2000000; // I have chosen to hard code this for simplicity
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

std::size_t Frontier::computeReservoirPromotionCount(std::size_t scannedCount) {
    if (scannedCount == 0) {
        return 0;
    }

    std::size_t count =
        (scannedCount * frontierReservoirPromotionPercent + 99) / 100;
    if (count == 0) {
        count = 1;
    }
    if (count > scannedCount) {
        count = scannedCount;
    }
    return count;
}

Frontier::Frontier(const string &seed_list_str, bool autoCloseWhenDrainedArg,
                   size_t maxQueuedItemsArg)
    : hostQueues(frontierHostKeyEqual, frontierHostKeyHash), closed(false),
      autoCloseWhenDrained(autoCloseWhenDrainedArg), pending(0), queued(0),
      maxQueuedItems(resolveFrontierMaxQueuedItems(maxQueuedItemsArg)) {
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
    for (size_t i = 0; i < items.size(); ++i) {
        if (!items[i].link.empty()) {
            pushInternal(items[i], true);
        }
    }

    if (pending == 0 && autoCloseWhenDrained) {
        closed = true;
    }
}

void Frontier::scheduleHostUnlocked(const string &hostKey, HostQueue &host, std::int64_t nowMs) {
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

void Frontier::promoteSleepingHostsUnlocked(std::int64_t nowMs) {
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

std::size_t Frontier::trimReservoirTailUnlocked(std::size_t minimumSlotsNeeded) {
    if (minimumSlotsNeeded == 0 || reservoir.empty()) {
        return 0;
    }

    std::size_t trimCount = minimumSlotsNeeded;
    if (reservoir.size() > frontierReservoirTrimChunkSize &&
        trimCount < frontierReservoirTrimChunkSize) {
        trimCount = frontierReservoirTrimChunkSize;
    }
    if (trimCount > reservoir.size()) {
        trimCount = reservoir.size();
    }

    for (std::size_t i = 0; i < trimCount; ++i) {
        reservoir.popBack();
        --queued;
        if (pending > 0) {
            --pending;
        }
    }

    if (reservoir.empty()) {
        reservoirSweepCursor = 0;
    } else if (reservoirSweepCursor > reservoir.size()) {
        reservoirSweepCursor = reservoir.size();
    }

    return trimCount;
}

std::size_t Frontier::trimScheduledQueuesUnlocked(std::size_t minimumSlotsNeeded) {
    if (minimumSlotsNeeded == 0) {
        return 0;
    }

    std::size_t removed = 0;
    for (HostTable::Iterator hostIt = hostQueues.begin();
         hostIt != hostQueues.end() && removed < minimumSlotsNeeded; ++hostIt) {
        BufferedQueue<FrontierItem> &items = hostIt->value.items;
        while (!items.empty() && removed < minimumSlotsNeeded) {
            items.eraseAt(items.size() - 1);
            ++removed;
            --queued;
            if (pending > 0) {
                --pending;
            }
        }
    }

    return removed;
}

bool Frontier::makeRoomForUnlocked(bool /*preserveIncomingWhenFull*/) {
    if (maxQueuedItems == 0 || queued < maxQueuedItems) {
        return true;
    }

    std::size_t minimumSlotsNeeded = queued - maxQueuedItems + 1;
    trimReservoirTailUnlocked(minimumSlotsNeeded);
    if (queued < maxQueuedItems) {
        return true;
    }

    trimScheduledQueuesUnlocked(minimumSlotsNeeded);
    if (queued < maxQueuedItems) {
        return true;
    }

    return false;
}

void Frontier::enqueueScheduledItemUnlocked(const FrontierItem &item, std::int64_t nowMs) {
    string hostKey = extractHostKey(item.link);
    Tuple<string, HostQueue> *hostTuple = hostQueues.Find(hostKey, HostQueue());
    HostQueue &host = hostTuple->value;
    host.items.pushBack(item);
    scheduleHostUnlocked(hostKey, host, nowMs);
}

void Frontier::considerReservoirCandidateUnlocked(vector<PromotionCandidate> &winners,
                                                  const PromotionCandidate &candidate,
                                                  std::size_t maxWinners) {
    if (maxWinners == 0) {
        return;
    }

    if (winners.size() < maxWinners) {
        winners.pushBack(candidate);
        return;
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

void Frontier::removeReservoirEntryUnlocked(std::size_t index) {
    if (reservoir.empty() || index >= reservoir.size()) {
        return;
    }

    if (index + 1 != reservoir.size()) {
        reservoir[index] = std::move(reservoir[reservoir.size() - 1]);
    }
    reservoir.popBack();

    if (reservoir.empty()) {
        reservoirSweepCursor = 0;
    } else if (reservoirSweepCursor >= reservoir.size()) {
        reservoirSweepCursor %= reservoir.size();
    }
}

void Frontier::promoteReservoirUnlocked(std::int64_t nowMs) {
    if (!readyHosts.empty() || reservoir.empty()) {
        return;
    }

    while (readyHosts.empty() && !reservoir.empty()) {
        if (reservoirSweepCursor >= reservoir.size()) {
            reservoirSweepCursor = 0;
        }

        const std::size_t scannedCount =
            reservoir.size() < frontierReservoirSweepChunkSize ? reservoir.size()
                                                               : frontierReservoirSweepChunkSize;
        const std::size_t winnersToKeep = computeReservoirPromotionCount(scannedCount);
        if (winnersToKeep == 0) {
            return;
        }

        vector<PromotionCandidate> winners;
        winners.reserve(winnersToKeep);

        for (std::size_t offset = 0; offset < scannedCount; ++offset) {
            std::size_t index = (reservoirSweepCursor + offset) % reservoir.size();
            considerReservoirCandidateUnlocked(winners, {index, reservoir[index].getScore()},
                                              winnersToKeep);
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

        if (scannedChunkTouchesReservoirTail) {
            while (!winners.empty()) {
                std::size_t maxIndexPos = 0;
                for (std::size_t i = 1; i < winners.size(); ++i) {
                    if (winners[i].index > winners[maxIndexPos].index) {
                        maxIndexPos = i;
                    }
                }

                removeReservoirEntryUnlocked(winners[maxIndexPos].index);
                if (maxIndexPos + 1 != winners.size()) {
                    winners[maxIndexPos] = winners[winners.size() - 1];
                }
                winners.popBack();
            }
        } else {
            while (!winners.empty()) {
                removeReservoirEntryUnlocked(winners[winners.size() - 1].index);
                winners.popBack();
            }
        }

        if (!reservoir.empty()) {
            reservoirSweepCursor = nextSweepCursor % reservoir.size();
        } else {
            reservoirSweepCursor = 0;
        }

        for (std::size_t i = 0; i < promotedItems.size(); ++i) {
            enqueueScheduledItemUnlocked(promotedItems[i], nowMs);
        }
        nowMs = nowMillis();
    }
}

void Frontier::pushInternal(const FrontierItem &item, bool countTowardsPending,
                            bool preserveIncomingWhenFull) {
    if (item.link.empty()) {
        return;
    }

    if (!makeRoomForUnlocked(preserveIncomingWhenFull)) {
        return;
    }

    if (countTowardsPending) {
        ++pending;
    }
    ++queued;

    std::int64_t nowMs = nowMillis();
    if (preserveIncomingWhenFull) {
        enqueueScheduledItemUnlocked(item, nowMs);
        return;
    }

    reservoir.pushBack(item);
}

vector<FrontierItem> Frontier::snapshot() const {
    vector<FrontierItem> result;
    lock_guard<mutex> guard(m);
    HostTable &queues = const_cast<HostTable &>(hostQueues);
    for (HostTable::Iterator it = queues.begin(); it != queues.end(); ++it) {
        it->value.items.forEach([&result](const FrontierItem &item) {
            result.pushBack(item);
        });
    }
    for (std::size_t i = 0; i < reservoir.size(); ++i) {
        result.pushBack(reservoir[i]);
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
            pushInternal(item, false, true);
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
        if (!makeRoomForUnlocked(true)) {
            hostKey = string();
            cv.notify_all();
            return;
        }
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
        scheduleHostUnlocked(hostKey, host, now);
    }
    hostKey = string();
    cv.notify_all();
}

std::optional<FrontierItem> Frontier::pop() {
    m.lock();

    while (true) {
        std::int64_t now = nowMillis();
        promoteSleepingHostsUnlocked(now);
        promoteReservoirUnlocked(now);

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

void Frontier::releaseActiveHostUnlocked(std::int64_t nowMs) {
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
    scheduleHostUnlocked(hostKey, host, nowMs);
    hostKey = string();
}

void Frontier::taskDone() {
    lock_guard<mutex> guard(m);
    if (pending == 0) {
        releaseActiveHostUnlocked(nowMillis());
        return;
    }

    --pending;
    releaseActiveHostUnlocked(nowMillis());

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

bool Frontier::contains(const string &url) const {
    lock_guard<mutex> guard(m);
    string hostKey = extractHostKey(url);
    Tuple<string, HostQueue> *hostTuple = hostQueues.Find(hostKey);
    if (hostTuple != nullptr) {
        bool found = false;
        hostTuple->value.items.forEach([&found, &url](const FrontierItem &item) {
            if (!found && item.link == url) {
                found = true;
            }
        });
        if (found) {
            return true;
        }
    }

    for (std::size_t i = 0; i < reservoir.size(); ++i) {
        if (reservoir[i].link == url) {
            return true;
        }
    }
    return false;
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
