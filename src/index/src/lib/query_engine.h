// src/lib/query_engine.h
#pragma once

#include "disk_chunk_reader.h"
#include <string>
#include <vector>

class QueryEngine {
  public:
    explicit QueryEngine(const DiskChunkReader &reader) : reader_(reader) {}

    std::vector<DocumentRecord> search(const std::string &query) const;

  private:
    const DiskChunkReader &reader_;
};