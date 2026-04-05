// src/lib/query_compiler.h
#pragma once
#include "disk_chunk_reader.h"
#include "isr.h"
#include "query_tokenizer.h"
#include <memory>
#include <vector>

class QueryCompiler {
  public:
    explicit QueryCompiler(const DiskChunkReader &reader) : reader_(reader), current_(0) {}

    std::unique_ptr<ISR> compile(const std::vector<QueryToken> &tokens);

  private:
    const DiskChunkReader &reader_;
    std::vector<QueryToken> tokens_;
    size_t current_;

    const QueryToken &peek() const;
    bool isAtEnd() const;
    void consume();

    std::unique_ptr<ISR> parseOr();
    std::unique_ptr<ISR> parseAnd();
    std::unique_ptr<ISR> parsePrimary();
};