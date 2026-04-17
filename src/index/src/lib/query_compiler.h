// src/lib/query_compiler.h
#pragma once
#include "../../../utils/vector.hpp"
#include "disk_chunk_reader.h"
#include "isr.h"
#include "query_tokenizer.h"
#include <memory>

class QueryCompiler {
  public:
    explicit QueryCompiler(const DiskChunkReader &reader) : reader_(reader), current_(0) {}

    std::unique_ptr<ISR> compile(const ::vector<QueryToken> &tokens);

    std::unique_ptr<ISR> compileAnchor(const ::vector<QueryToken> &tokens);

  private:
    const DiskChunkReader &reader_;
    ::vector<QueryToken> tokens_;
    size_t current_;
    bool is_anchor_ = false;

    const QueryToken &peek() const;
    bool isAtEnd() const;
    void consume();

    std::unique_ptr<ISR> parseOr();
    std::unique_ptr<ISR> parseAnd();
    std::unique_ptr<ISR> parsePrimary();

    std::unique_ptr<ISR> createISR(const string &term);
};