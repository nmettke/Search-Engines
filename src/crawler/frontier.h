#pragma once
#include "condition_variable.hpp"
#include "lock_guard.hpp"
#include "mutex.hpp"
#include "string.hpp"
#include "vector.hpp"
#include <fstream>
#include <iostream>
#include <optional>
#include <queue>
#include <stdexcept>
class Frontier {
  public:
    Frontier(const std::string seed_list_str);

    ~Frontier() = default;

    void push(const string &url);

    std::optional<string> pop();

    bool contains(const string &url) const;

    std::size_t size() const;

    bool empty() const;

  private:
    std::priority_queue<string> pq;
    mutex m;
};