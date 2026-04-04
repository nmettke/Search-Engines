// src/lib/query_tokenizer.cpp
#include "query_tokenizer.h"
#include "tokenizer.h"
#include <cctype>

std::vector<QueryToken> QueryTokenizer::tokenize(const std::string &query) {
    std::vector<QueryToken> tokens;
    size_t i = 0;

    while (i < query.length()) {
        char c = query[i];

        if (std::isspace(c)) {
            i++;
            continue;
        }

        if (c == '(') {
            tokens.push_back({QueryTokenType::L_PAREN, "("});
            i++;
        } else if (c == ')') {
            tokens.push_back({QueryTokenType::R_PAREN, ")"});
            i++;
        } else if (c == '"') {
            tokens.push_back({QueryTokenType::QUOTE, "\""});
            i++;
        } else if (c == '|') {
            if (i + 1 < query.length() && query[i + 1] == '|')
                i++; // handle "||"
            tokens.push_back({QueryTokenType::OR, "OR"});
            i++;
        } else if (c == '&') {
            if (i + 1 < query.length() && query[i + 1] == '&')
                i++; // handle "&&"
            tokens.push_back({QueryTokenType::AND, "AND"});
            i++;
        } else if (c == '-') {
            tokens.push_back({QueryTokenType::NOT, "NOT"});
            i++;
        } else {
            std::string word;
            while (i < query.length() && !std::isspace(query[i]) && query[i] != '(' &&
                   query[i] != ')' && query[i] != '"' && query[i] != '|' && query[i] != '&') {
                word += query[i];
                i++;
            }

            for (char &wc : word) {
                if (wc >= 'A' && wc <= 'Z')
                    wc = static_cast<char>(wc + 32);
            }

            if (word == "or") {
                tokens.push_back({QueryTokenType::OR, "OR"});
            } else if (word == "and") {
                tokens.push_back({QueryTokenType::AND, "AND"});
            } else if (word == "not") {
                tokens.push_back({QueryTokenType::NOT, "NOT"});
            } else {
                std::string stemmed = PorterStemmer::stem(word);
                if (!stemmed.empty()) {
                    tokens.push_back({QueryTokenType::WORD, stemmed});
                }
            }
        }
    }

    return tokens;
}