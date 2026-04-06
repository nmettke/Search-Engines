// src/lib/query_engine.h
#pragma once

#include "disk_chunk_reader.h"
#include "utils/string.hpp"
#include "utils/vector.hpp"

class QueryEngine {
  public:
    explicit QueryEngine(const DiskChunkReader &reader) : reader_(reader) {}

    ::vector<DocumentRecord> search(const ::string &query) const;

  private:
    const DiskChunkReader &reader_;
};