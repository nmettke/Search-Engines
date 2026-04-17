// src/lib/query_tokenizer.h
#pragma once

#include "../../../utils/string.hpp"
#include "../../../utils/vector.hpp"

enum class QueryTokenType { WORD, OR, AND, NOT, L_PAREN, R_PAREN, QUOTE };

struct QueryToken {
    QueryTokenType type;
    ::string text;
};

class QueryTokenizer {
  public:
    static ::vector<QueryToken> tokenize(const ::string &query);
};
