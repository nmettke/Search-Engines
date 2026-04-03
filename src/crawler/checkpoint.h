#pragma once

#include "frontier.h"
#include "url_dedup.h"
#include <cstddef>
#include <string>
#include <vector>

struct CheckpointConfig {
    std::string directory = ".";
    std::size_t interval = 500;
};

class Checkpoint {
  public:
    explicit Checkpoint(const CheckpointConfig &config);

    bool save(const Frontier &frontier, const UrlBloomFilter &bloom, std::size_t urlsCrawled);
    bool load(std::vector<FrontierItem> &items, UrlBloomFilter &bloom, std::size_t &urlsCrawled);
    bool shouldCheckpoint(std::size_t urlsCrawled) const;

  private:
    CheckpointConfig config_;
    std::size_t lastCheckpointAt_ = 0;

    std::string filePath() const;
    std::string tmpPath() const;
};
