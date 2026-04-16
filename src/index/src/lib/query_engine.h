#pragma once

#include "../../../utils/string.hpp"
#include "../../../utils/vector.hpp"
#include "disk_chunk_reader.h"
#include "dynamic_rank.h"
#include "static_rank.h"
#include "types.h"
#include <atomic>
#include <cstdint>

struct ScoredDocument {
    uint32_t doc_id;
    double score;
};

class QueryEngine {
  public:
    explicit QueryEngine(const DiskChunkReader &body_reader, StaticRankConfig rank_config = {},
                         DynamicRankConfig dynamic_rank_config = {});
    QueryEngine(const DiskChunkReader &body_reader, const DiskChunkReader &anchor_reader,
                StaticRankConfig rank_config = {}, DynamicRankConfig dynamic_rank_config = {});

    vector<ScoredDocument> search(const string &query, size_t K = 500) const;
    vector<ScoredDocument> search(const string &query, size_t K,
                                  const std::atomic<double> *shared_min_score) const;

  private:
    const DiskChunkReader &body_reader_;
    const DiskChunkReader *anchor_reader_ = nullptr;
    StaticRankScorer static_scorer_;
    DynamicRankScorer dynamic_scorer_;
    double max_static_score_ = 0.0;
};
