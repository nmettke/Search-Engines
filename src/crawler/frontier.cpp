#include "frontier.h"

#include <cstdlib>
#include <time.h>

namespace {

thread_local std::string frontierActiveHostKey;

std::size_t resolveFrontierMaxQueuedItems(std::size_t configuredMaxQueuedItems) {
    if (configuredMaxQueuedItems != 0) {
        return configuredMaxQueuedItems;
    }

    const char *raw = std::getenv("SEARCH_FRONTIER_MAX_ITEMS");
    if (raw == nullptr || raw[0] == '\0') {
        return 0;
    }

    char *end = nullptr;
    unsigned long parsed = std::strtoul(raw, &end, 10);
    if (end == raw || (end != nullptr && *end != '\0')) {
        std::cerr << "Ignoring invalid SEARCH_FRONTIER_MAX_ITEMS value: " << raw << '\n';
        return 0;
    }
    parsed = 2000000;
    return static_cast<std::size_t>(parsed);
}

} // namespace

std::int64_t Frontier::nowMillis() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

std::string &Frontier::activeHostKey() { return frontierActiveHostKey; }

std::string Frontier::extractHostKey(const string &url) {
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

    return std::string(url.cstr() + hostStart, url.cstr() + hostEnd);
}

Frontier::Frontier(const string &seed_list_str, bool autoCloseWhenDrainedArg,
                   size_t maxQueuedItemsArg)
    : closed(false), autoCloseWhenDrained(autoCloseWhenDrainedArg), pending(0), queued(0),
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
    : closed(false), autoCloseWhenDrained(autoCloseWhenDrainedArg), pending(0), queued(0),
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

void Frontier::scheduleHostUnlocked(const std::string &hostKey, HostQueue &host, std::int64_t nowMs) {
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

    readyHosts.push_back(hostKey);
    host.inReadyQueue = true;
}

void Frontier::promoteSleepingHostsUnlocked(std::int64_t nowMs) {
    while (!sleepingHosts.empty() && sleepingHosts.top().readyAtMs <= nowMs) {
        SleepingHost sleeping = sleepingHosts.top();
        sleepingHosts.pop();

        auto it = hostQueues.find(sleeping.hostKey);
        if (it == hostQueues.end()) {
            continue;
        }

        HostQueue &host = it->second;
        if (host.sleepGeneration != sleeping.generation || host.items.empty() || host.inFlight ||
            host.inReadyQueue) {
            continue;
        }

        if (host.blockedUntilMs > nowMs) {
            continue;
        }

        host.blockedUntilMs = 0;
        host.inSleepingQueue = false;
        readyHosts.push_back(sleeping.hostKey);
        host.inReadyQueue = true;
    }
}

bool Frontier::evictWorstQueuedItemUnlocked(const FrontierItem *incoming) {
    if (queued == 0) {
        return false;
    }

    auto worstHostIt = hostQueues.end();
    std::deque<FrontierItem>::iterator worstItemIt;
    double worstScore = 0.0;
    bool foundWorst = false;

    for (auto hostIt = hostQueues.begin(); hostIt != hostQueues.end(); ++hostIt) {
        std::deque<FrontierItem> &items = hostIt->second.items;
        for (auto itemIt = items.begin(); itemIt != items.end(); ++itemIt) {
            double candidateScore = itemIt->getScore();
            if (!foundWorst || candidateScore < worstScore) {
                foundWorst = true;
                worstScore = candidateScore;
                worstHostIt = hostIt;
                worstItemIt = itemIt;
            }
        }
    }

    if (!foundWorst) {
        return false;
    }

    if (incoming != nullptr && !(incoming->getScore() > worstScore)) {
        return false;
    }

    worstHostIt->second.items.erase(worstItemIt);
    --queued;
    if (pending > 0) {
        --pending;
    }

    return true;
}

bool Frontier::makeRoomForUnlocked(const FrontierItem &incoming, bool preserveIncomingWhenFull) {
    if (maxQueuedItems == 0 || queued < maxQueuedItems) {
        return true;
    }

    return evictWorstQueuedItemUnlocked(preserveIncomingWhenFull ? nullptr : &incoming);
}

void Frontier::pushInternal(const FrontierItem &item, bool countTowardsPending,
                            bool preserveIncomingWhenFull) {
    if (item.link.empty()) {
        return;
    }

    if (!makeRoomForUnlocked(item, preserveIncomingWhenFull)) {
        return;
    }

    std::string hostKey = extractHostKey(item.link);
    HostQueue &host = hostQueues[hostKey];
    host.items.push_back(item);
    ++queued;
    if (countTowardsPending) {
        ++pending;
    }

    scheduleHostUnlocked(hostKey, host, nowMillis());
}

vector<FrontierItem> Frontier::snapshot() const {
    vector<FrontierItem> result;
    lock_guard guard(m);
    for (const auto &entry : hostQueues) {
        for (const FrontierItem &item : entry.second.items) {
            result.pushBack(item);
        }
    }
    return result;
}

void Frontier::push(const string &url) {
    lock_guard guard(m);
    if (closed || url.empty()) {
        return;
    }

    pushInternal(FrontierItem(url), true);
    cv.notify_one();
}

void Frontier::push(const FrontierItem &item) {
    lock_guard guard(m);
    if (closed || item.link.empty()) {
        return;
    }

    pushInternal(item, true);
    cv.notify_one();
}

void Frontier::pushMany(const vector<string> &urls) {
    lock_guard guard(m);
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
    lock_guard guard(m);
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
    lock_guard guard(m);
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
    lock_guard guard(m);

    std::string &hostKey = activeHostKey();
    if (hostKey.empty()) {
        return;
    }

    auto it = hostQueues.find(hostKey);
    if (it == hostQueues.end()) {
        hostKey.clear();
        return;
    }

    HostQueue &host = it->second;
    host.inFlight = false;

    if (!item.link.empty()) {
        if (!makeRoomForUnlocked(item, true)) {
            hostKey.clear();
            cv.notify_all();
            return;
        }
        host.items.push_front(item);
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
    hostKey.clear();
    cv.notify_all();
}

std::optional<FrontierItem> Frontier::pop() {
    m.lock();

    while (true) {
        std::int64_t now = nowMillis();
        promoteSleepingHostsUnlocked(now);

        while (!readyHosts.empty()) {
            std::string hostKey = readyHosts.front();
            readyHosts.pop_front();

            auto it = hostQueues.find(hostKey);
            if (it == hostQueues.end()) {
                continue;
            }

            HostQueue &host = it->second;
            host.inReadyQueue = false;
            if (host.inFlight || host.items.empty()) {
                continue;
            }

            FrontierItem item = host.items.front();
            host.items.pop_front();
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
    std::string &hostKey = activeHostKey();
    if (hostKey.empty()) {
        return;
    }

    auto it = hostQueues.find(hostKey);
    if (it == hostQueues.end()) {
        hostKey.clear();
        return;
    }

    HostQueue &host = it->second;
    host.inFlight = false;
    scheduleHostUnlocked(hostKey, host, nowMs);
    hostKey.clear();
}

void Frontier::taskDone() {
    lock_guard guard(m);
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
    lock_guard guard(m);
    closed = true;
    cv.notify_all();
}

bool Frontier::contains(const string &url) const {
    lock_guard guard(m);
    std::string hostKey = extractHostKey(url);
    auto it = hostQueues.find(hostKey);
    if (it == hostQueues.end()) {
        return false;
    }

    for (const FrontierItem &item : it->second.items) {
        if (item.link == url) {
            return true;
        }
    }
    return false;
}

size_t Frontier::size() const {
    lock_guard guard(m);
    return queued;
}

bool Frontier::empty() const {
    lock_guard guard(m);
    return queued == 0;
}

bool Frontier::hasInFlightWork() const {
    lock_guard guard(m);
    return pending > queued;
}
