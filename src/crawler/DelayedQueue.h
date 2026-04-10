#pragma once

#include "FrontierItem.h"
#include "utils/PriorityQueue.hpp"
#include "utils/threads/lock_guard.hpp"
#include "utils/threads/mutex.hpp"
#include "utils/vector.hpp"

#include <cstdint>

// queue of urls that cant be crawled yet because of time delay. ordered by readyAtMs (when we can
// crawl again)
class DelayedQueue {
  public:
    DelayedQueue() = default;
    ~DelayedQueue() = default;

    DelayedQueue(const DelayedQueue &) = delete;
    DelayedQueue &operator=(const DelayedQueue &) = delete;

    void push(const FrontierItem &item, int64_t readyAtMs);

    vector<FrontierItem> drainReady(int64_t nowMs);

    size_t size() const;
    bool empty() const;

  private:
    struct Entry {
        FrontierItem item;
        int64_t readyAtMs;

        Entry() : item(string("")), readyAtMs(0) {}
        Entry(const FrontierItem &i, int64_t t) : item(i), readyAtMs(t) {}
    };

    struct EntryCompare {
        bool operator()(const Entry &a, const Entry &b) const { return a.readyAtMs > b.readyAtMs; }
    };

    PriorityQueue<Entry, EntryCompare> heap;
    mutable mutex m;
};
