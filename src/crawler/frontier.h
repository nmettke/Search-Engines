#pragma once

#include "FrontierItem.h"
#include "utils/string.hpp"
#include "utils/threads/condition_variable.hpp"
#include "utils/threads/lock_guard.hpp"
#include "utils/threads/mutex.hpp"
#include "utils/vector.hpp"

#include <cstddef>
#include <fstream>
#include <iostream>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

class Frontier {
  public:
    Frontier(const std::string seed_list_str);

    ~Frontier() = default;

    void push(const string &url);

    std::optional<FrontierItem> pop();

    bool contains(const string &url) const;

    std::size_t size() const;

    bool empty() const;

  private:
    std::priority_queue<FrontierItem, std::vector<FrontierItem>, FrontierItemCompare> pq;
    mutable mutex m;
};