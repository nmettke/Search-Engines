#pragma once

#include "utils/STL_rewrite/pair.hpp"
#include "utils/string.hpp"
#include "utils/threads/mutex.hpp"
#include "utils/vector.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>

::string normalizeUrl(const ::string &rawUrl);
::string absolutizeUrl(const ::string &rawUrl, const ::string &sourceUrl,
                       const ::string &baseUrl = "");

class UrlBloomFilter {
  public:
    struct Snapshot {
        std::size_t bitCount = 0;
        std::uint32_t hashCount = 0;
        vector<uint8_t> packedBits;
    };

    UrlBloomFilter(std::size_t expectedItems, double falsePositiveRate);
    bool serializeToStream(FILE *f) const;
    static UrlBloomFilter deserializeFromStream(FILE *f);
    Snapshot snapshot() const;
    std::size_t memoryUsageBytes() const;

    bool probablyContains(const ::string &key) const;
    void insert(const ::string &key);
    bool checkAndInsert(const ::string &key);

  private:
    std::size_t bitCount;
    std::uint32_t hashCount;
    vector<bool> bits;
    mutable mutex m_;

    UrlBloomFilter(std::size_t bitCount, std::uint32_t hashCount, vector<bool> bits);

    static ::pair<std::uint64_t, std::uint64_t> hashKey(const ::string &key);
    bool getBit(std::size_t index) const;
    void setBit(std::size_t index);
};

bool shouldEnqueueUrl(const ::string &rawUrl, UrlBloomFilter &bloom, ::string &canonicalOut);
bool passesUrlQualityChecks(const ::string &rawUrl, ::string &canonicalOut);
