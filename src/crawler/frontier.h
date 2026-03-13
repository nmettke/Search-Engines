#pragma once
#include "string.hpp"
#include "vector.hpp"
#include <fstream>
#include <iostream>
#include <optional>

class Frontier {
  public:
    Frontier() {};

    ~Frontier() = default;

    bool push(const string &url);

    std::optional<string> pop();

    bool contains(const string &url) const;

    std::size_t size() const;

    bool empty() const;

  private:
    vector<string> frontier;
};