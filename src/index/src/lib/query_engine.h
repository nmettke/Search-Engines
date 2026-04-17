// src/lib/query_engine.h
#pragma once

#include "../../../utils/string.hpp"
#include "../../../utils/vector.hpp"
#include "disk_chunk_reader.h"
#include "isr.h"
#include "static_rank.h"
#include "types.h"
#include <cstdint>

const uint32_t MAX_DOC_ID = 0xFFFFFFFF;

struct ScoredDocument {
    uint32_t doc_id;
    double score;
};

class QueryEngine {
  public:
    explicit QueryEngine(const DiskChunkReader &body_reader, const DiskChunkReader &anchor_reader,
                         StaticRankConfig rank_config = {})
        : body_reader_(body_reader), anchor_reader_(anchor_reader), scorer_(rank_config) {}

    vector<ScoredDocument> search(const string &query, size_t K = 500) const;

  private:
    const DiskChunkReader &body_reader_;
    const DiskChunkReader &anchor_reader_;

    StaticRankScorer scorer_;

    double calculate_span_score(uint32_t doc_id, ISR *body_root, ISR *anchor_root,
                                const DocumentRecord &doc) const;
};
