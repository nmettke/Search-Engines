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
#include <string>
#include <vector>

class Frontier {
  public:
    Frontier(const std::string seed_list_str);

    ~Frontier() = default;

    void push(const string &url);
    void pushMany(const vector<string> &urls);

    std::optional<FrontierItem> pop();

    void taskDone();

    bool contains(const string &url) const;

    std::size_t size() const;

    bool empty() const;

  private:
    PriorityQueue<FrontierItem, FrontierItemCompare> pq;
    mutable mutex m;
    condition_variable cv;
    bool closed;
    std::size_t pending;
};