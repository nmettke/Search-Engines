#pragma once

#include "frontier.h"
#include "url_dedup.h"
#include "utils/string.hpp"
#include "utils/threads/mutex.hpp"
#include <cstddef>

struct CheckpointConfig {
    string directory = ".";
};

class Checkpoint {
  public:
    explicit Checkpoint(const CheckpointConfig &config);

    bool save(const Frontier &frontier, const UrlBloomFilter &bloom, size_t urlsCrawled);
    bool load(vector<FrontierItem> &items, UrlBloomFilter &bloom, size_t &urlsCrawled);

  private:
    CheckpointConfig config_;
    mutable mutex saveMutex_;

    string filePath() const;
    string tmpPath() const;
};
