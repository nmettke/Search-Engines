// src/lib/query_engine.h
#pragma once

#include "disk_chunk_reader.h"
#include "types.h"
#include "utils/string.hpp"
#include "utils/vector.hpp"

struct ScoredDocument {
    DocumentRecord doc;
    double score;
};

class QueryEngine {
  public:
    explicit QueryEngine(const DiskChunkReader &reader) : reader_(reader) {}

    ::vector<ScoredDocument> search(const ::string &query, size_t K = 500) const;

  private:
    const DiskChunkReader &reader_;

    double calculate_score(const DocumentRecord &doc, const ::string &query) const;
};