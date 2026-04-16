// src/lib/query_compiler.h
#pragma once
#include "disk_chunk_reader.h"
#include "isr.h"
#include "query_tokenizer.h"
#include "utils/vector.hpp"
#include <memory>

enum class QueryCompilationMode { Anchor, BodyTitle };

class QueryCompiler {
  public:
    explicit QueryCompiler(const DiskChunkReader &reader,
                           QueryCompilationMode mode = QueryCompilationMode::Anchor)
        : reader_(reader), mode_(mode), current_(0) {}

    std::unique_ptr<ISR> compile(const ::vector<QueryToken> &tokens);

  private:
    const DiskChunkReader &reader_;
    QueryCompilationMode mode_;
    ::vector<QueryToken> tokens_;
    size_t current_;

    const QueryToken &peek() const;
    bool isAtEnd() const;
    void consume();
    std::unique_ptr<ISR> buildWordISR(const ::string &term) const;
    std::unique_ptr<ISR> buildPhraseISR(const ::vector<::string> &terms) const;

    std::unique_ptr<ISR> parseOr();
    std::unique_ptr<ISR> parseAnd();
    std::unique_ptr<ISR> parsePrimary();
};
