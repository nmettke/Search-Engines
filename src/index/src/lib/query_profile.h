#pragma once

#include "disk_chunk_reader.h"
#include "query_tokenizer.h"
#include "utils/vector.hpp"

struct QueryPhrase {
    ::vector<::string> terms;
};

struct QueryProfile {
    ::vector<::string> unique_terms; // dedupped words
    ::vector<QueryPhrase> phrases;

    bool empty() const { return unique_terms.empty(); }
};

QueryProfile buildQueryProfile(const ::vector<QueryToken> &tokens,
                               const DiskChunkReader &body_reader,
                               const DiskChunkReader *anchor_reader);
