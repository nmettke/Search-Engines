#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "utils/string.hpp"


string normalizeUrl(const string &rawUrl);

class UrlBloomFilter {
  public:
    UrlBloomFilter(std::size_t expectedItems, double falsePositiveRate);

    bool probablyContains(const string &key) const;
    void insert(const string &key);

  private:
    std::size_t bitCount;
    std::uint32_t hashCount;
    std::vector<bool> bits;

    static std::pair<std::uint64_t, std::uint64_t> hashKey(const string &key);
    bool getBit(std::size_t index) const;
    void setBit(std::size_t index);
};

bool shouldEnqueueUrl(const string &rawUrl, UrlBloomFilter &bloom, string &canonicalOut);
