#pragma once

#include "FrontierItem.h"
#include "utils/PriorityQueue.hpp"
#include "utils/string.hpp"
#include "utils/threads/condition_variable.hpp"
#include "utils/threads/lock_guard.hpp"
#include "utils/threads/mutex.hpp"
#include "utils/vector.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>

class Frontier {
  public:
    Frontier(const string &seed_list_str);

    Frontier(vector<FrontierItem> items);

    ~Frontier() = default;

    void push(const string &url);
    void pushMany(const vector<string> &urls);

    // Push items that were previously popped and then deferred because of crawl delay... these
    // items pending count was never decremented, so this method must not increment it.
    void pushDeferred(const vector<FrontierItem> &items);

    std::optional<FrontierItem> pop();

    void taskDone();
    void shutdown();

    bool contains(const string &url) const;

    size_t size() const;

    bool empty() const;

    vector<FrontierItem> snapshot() const;

  private:
    PriorityQueue<FrontierItem, FrontierItemCompare> pq;
    mutable mutex m;
    condition_variable cv;
    bool closed;
    std::size_t pending;
};