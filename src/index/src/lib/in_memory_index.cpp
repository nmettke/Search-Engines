// src/lib/in_memory_index.cpp
#include "in_memory_index.h"

void InMemoryIndex::addToken(const TokenOutput &token) {
    // insert to posting list
    auto &posting_list = _index[token.term];
    posting_list.locations.push_back(token.location);
    ++posting_list.collection_frequency;

    // term has not been seen in this doc
    if (_terms_seen.insert(token.term).second) {
        ++posting_list.doc_frequency;
    }

    // Update the last total token count
    if (static_cast<uint64_t>(token.location) + 1 > _total_locations) {
        _total_locations = static_cast<uint64_t>(token.location) + 1;
    }
}

void InMemoryIndex::finishDocument(const DocEndOutput &doc_end) {
    addToken(TokenOutput{docEndToken, doc_end.location});
    _documents.push_back(DocumentRecord{doc_end.url, doc_end.doc_start_loc, doc_end.location,
                                        doc_end.word_count, doc_end.title_word_count});
    _terms_seen.clear();
}