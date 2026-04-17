// src/lib/in_memory_index.h
#pragma once

#include "types.h"
#include "utils/string.hpp"
#include <unordered_map>
#include <unordered_set>

class InMemoryIndex {
  public:
    void addToken(const TokenOutput &token);
    void finishDocument(const DocEndOutput &doc_end);
    std::size_t documentCount() const;
    std::size_t approxMemoryBytes() const;

    const std::unordered_map<::string, PostingList> &postings() const { return _index; }
    const ::vector<DocumentRecord> &documents() const { return _documents; }
    uint64_t totalLocations() const { return _total_locations; }

  private:
    std::unordered_map<::string, PostingList> _index;
    ::vector<DocumentRecord> _documents;
    std::unordered_set<::string> _terms_seen;
    uint64_t _total_locations = 0;
};
