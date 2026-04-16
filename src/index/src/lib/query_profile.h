#pragma once

#include "disk_chunk_reader.h"
#include "query_tokenizer.h"
#include "utils/vector.hpp"

struct QueryPhrase {
    ::vector<::string> terms;
};

struct QueryProfile {
    ::vector<::string> flattened_terms; // list of query word ignoring structure and repeat
    ::vector<::string> unique_terms;    // dedupped words
    ::vector<QueryPhrase> phrases;
    size_t rare_word_count = 0;   // computed from term freq in query
    size_t common_word_count = 0; // computed from term freq in query

    bool empty() const { return unique_terms.empty(); }
};

QueryProfile buildQueryProfile(const ::vector<QueryToken> &tokens,
                               const DiskChunkReader &body_reader,
                               const DiskChunkReader *anchor_reader);
