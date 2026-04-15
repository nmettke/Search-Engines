#include "frontier.h"

#include <time.h>

namespace {

thread_local std::string frontierActiveHostKey;

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

Frontier::Frontier(const string &seed_list_str, bool autoCloseWhenDrainedArg)
    : closed(false), autoCloseWhenDrained(autoCloseWhenDrainedArg), pending(0), queued(0) {
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

Frontier::Frontier(vector<FrontierItem> items, bool autoCloseWhenDrainedArg)
    : closed(false), autoCloseWhenDrained(autoCloseWhenDrainedArg), pending(0), queued(0) {
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

void Frontier::pushInternal(const FrontierItem &item, bool countTowardsPending) {
    if (item.link.empty()) {
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
            pushInternal(item, false);
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
