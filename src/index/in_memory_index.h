// This header includes code needed to build an index in-memory
#pragma once

#include "types.h"
#include <string>
#include <unordered_map>
#include <unordered_set>

class InMemoryIndex {
  public:
    void addToken(const TokenOutput &token);
    void finishDocument(const DocEndOutput &doc_end);

    const std::unordered_map<std::string, PostingList> &postings() const { return _index; }
    const std::vector<DocumentRecord> &documents() const { return _documents; }
    uint64_t totalLocations() const { return _total_locations; }

  private:
    std::unordered_map<std::string, PostingList> _index;
    std::vector<DocumentRecord> _documents;
    std::unordered_set<std::string> _terms_seen;
    uint64_t _total_locations = 0;
};
