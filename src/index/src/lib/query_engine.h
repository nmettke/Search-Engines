// src/lib/query_engine.h
#pragma once

#include "../../../utils/string.hpp"
#include "../../../utils/vector.hpp"
#include "disk_chunk_reader.h"
#include "static_rank.h"
#include "types.h"
#include <cstdint>

struct ScoredDocument {
    string url;
    double score;
};

class QueryEngine {
  public:
    explicit QueryEngine(const DiskChunkReader &reader, StaticRankConfig rank_config = {})
        : reader_(reader), scorer_(rank_config) {}

    vector<ScoredDocument> search(const string &query, size_t K = 500) const;

  private:
    const DiskChunkReader &reader_;
    StaticRankScorer scorer_;
};
