#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

std::string normalizeUrl(const std::string &rawUrl);

class UrlBloomFilter {
  public:
    UrlBloomFilter(std::size_t expectedItems, double falsePositiveRate);

    bool probablyContains(const std::string &key) const;
    void insert(const std::string &key);

  private:
    std::size_t bitCount;
    std::uint32_t hashCount;
    std::vector<bool> bits;

    static std::pair<std::uint64_t, std::uint64_t> hashKey(const std::string &key);
    bool getBit(std::size_t index) const;
    void setBit(std::size_t index);
};

bool shouldEnqueueUrl(const std::string &rawUrl, UrlBloomFilter &bloom, std::string &canonicalOut);
