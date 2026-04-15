#pragma once

#include "FrontierItem.h"
#include "utils/string.hpp"
#include "utils/threads/condition_variable.hpp"
#include "utils/threads/lock_guard.hpp"
#include "utils/threads/mutex.hpp"
#include "utils/vector.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>

class Frontier {
  public:
    Frontier(const string &seed_list_str, bool autoCloseWhenDrained = true,
             size_t maxQueuedItems = 0);

    Frontier(vector<FrontierItem> items, bool autoCloseWhenDrained = true,
             size_t maxQueuedItems = 0);

    ~Frontier() = default;

    void push(const string &url);
    void push(const FrontierItem &item);
    void pushMany(const vector<string> &urls);
    void pushMany(const vector<FrontierItem> &items);

    // Push items that were previously popped and then deferred because of crawl delay... these
    // items pending count was never decremented, so this method must not increment it.
    void pushDeferred(const vector<FrontierItem> &items);
    void snoozeCurrent(const FrontierItem &item, std::int64_t readyAtMs);

    std::optional<FrontierItem> pop();

    void taskDone();
    void shutdown();

    bool contains(const string &url) const;

    size_t size() const;

    bool empty() const;

    bool hasInFlightWork() const;

    vector<FrontierItem> snapshot() const;

  private:
    struct HostQueue {
        std::deque<FrontierItem> items;
        std::int64_t blockedUntilMs = 0;
        std::uint64_t sleepGeneration = 0;
        bool inFlight = false;
        bool inReadyQueue = false;
        bool inSleepingQueue = false;
    };

    struct SleepingHost {
        std::int64_t readyAtMs = 0;
        std::uint64_t generation = 0;
        std::string hostKey;
    };

    struct SleepingHostCompare {
        bool operator()(const SleepingHost &a, const SleepingHost &b) const {
            return a.readyAtMs > b.readyAtMs;
        }
    };

    static std::int64_t nowMillis();
    static std::string extractHostKey(const string &url);

    void pushInternal(const FrontierItem &item, bool countTowardsPending,
                      bool preserveIncomingWhenFull = false);
    bool makeRoomForUnlocked(const FrontierItem &incoming, bool preserveIncomingWhenFull);
    bool evictWorstQueuedItemUnlocked(const FrontierItem *incoming);
    void scheduleHostUnlocked(const std::string &hostKey, HostQueue &host, std::int64_t nowMs);
    void promoteSleepingHostsUnlocked(std::int64_t nowMs);
    void releaseActiveHostUnlocked(std::int64_t nowMs);
    static std::string &activeHostKey();

    std::unordered_map<std::string, HostQueue> hostQueues;
    std::deque<std::string> readyHosts;
    std::priority_queue<SleepingHost, std::vector<SleepingHost>, SleepingHostCompare> sleepingHosts;
    mutable mutex m;
    condition_variable cv;
    bool closed;
    bool autoCloseWhenDrained;
    std::size_t pending;
    std::size_t queued;
    std::size_t maxQueuedItems;
};
