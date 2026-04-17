// src/lib/in_memory_index.cpp
#include "in_memory_index.h"

namespace {

std::size_t stringBytes(const ::string &value) {
    return value.capacity() + 1;
}

std::size_t vectorBytes(const ::vector<uint32_t> &values) {
    return values.capacity() * sizeof(uint32_t);
}

std::size_t postingListBytes(const PostingList &postingList) {
    return vectorBytes(postingList.locations);
}

std::size_t documentRecordBytes(const DocumentRecord &record) {
    return stringBytes(record.url) + stringBytes(record.features.raw_tld);
}

} // namespace

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
                                        doc_end.word_count, doc_end.title_word_count,
                                        doc_end.seed_distance, doc_end.features});
    _terms_seen.clear();
}

std::size_t InMemoryIndex::documentCount() const { return _documents.size(); }

std::size_t InMemoryIndex::approxMemoryBytes() const {
    std::size_t total = sizeof(InMemoryIndex);

    total += _index.bucket_count() * sizeof(void *);
    for (const auto &entry : _index) {
        total += sizeof(decltype(entry));
        total += stringBytes(entry.first);
        total += postingListBytes(entry.second);
    }

    total += _documents.capacity() * sizeof(DocumentRecord);
    for (const DocumentRecord &record : _documents) {
        total += documentRecordBytes(record);
    }

    total += _terms_seen.bucket_count() * sizeof(void *);
    for (const ::string &term : _terms_seen) {
        total += sizeof(::string);
        total += stringBytes(term);
    }

    return total;
}
