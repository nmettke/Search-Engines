// src/lib/query_tokenizer.h
#pragma once

#include <string>
#include <vector>

enum class QueryTokenType { WORD, OR, AND, NOT, L_PAREN, R_PAREN, QUOTE };

struct QueryToken {
    QueryTokenType type;
    std::string text;
};

class QueryTokenizer {
  public:
    static std::vector<QueryToken> tokenize(const std::string &query);
};
