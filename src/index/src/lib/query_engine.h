// src/lib/query_engine.h
#pragma once

#include "disk_chunk_reader.h"
#include "tokenizer.h"
#include <string>
#include <vector>

class QueryEngine {
  public:
    // Initialize with a reference to your open reader
    explicit QueryEngine(const DiskChunkReader &reader) : reader_(reader) {}

    // Executes an AND query for multiple terms
    std::vector<DocumentRecord> search(const std::vector<std::string> &terms) const;

  private:
    const DiskChunkReader &reader_;
};