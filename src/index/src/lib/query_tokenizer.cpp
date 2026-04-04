// src/lib/query_tokenizer.cpp
#include "query_tokenizer.h"
#include "tokenizer.h"
#include <cctype>

std::string to_lower(std::string &s) {
    for (char &c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    }
    return s;
}

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
            // check if this a NOT operator or a hyphen
            // It is a NOT operator if it is at the start of the query,
            // or preceded by a space, parenthesis, or quote.
            bool is_not_operator = false;
            if (i == 0 || std::isspace(query[i - 1]) || query[i - 1] == '(' ||
                query[i - 1] == '"') {
                is_not_operator = true;
            }

            if (is_not_operator) {
                tokens.push_back({QueryTokenType::NOT, "NOT"});
                i++;
                continue;
            }
        }

        if (c != '(' && c != ')' && c != '"' && c != '|' && c != '&' && (!std::isspace(c))) {
            std::string word;

            while (i < query.length()) {
                char wc = query[i];
                if (std::isspace(wc) || wc == '(' || wc == ')' || wc == '"' || wc == '|' ||
                    wc == '&') {
                    break;
                }

                // check for hyphen in the middle of a word
                if (wc == '-' && i + 1 < query.length() && !std::isspace(query[i + 1])) {
                    word += wc;
                    i++;
                    continue;
                } else if (wc == '-') {
                    break;
                }

                word += wc;
                i++;
            }

            std::string lower_word = to_lower(word);

            if (lower_word == "or") {
                tokens.push_back({QueryTokenType::OR, "OR"});
            } else if (lower_word == "and") {
                tokens.push_back({QueryTokenType::AND, "AND"});
            } else if (lower_word == "not") {
                tokens.push_back({QueryTokenType::NOT, "NOT"});
            } else {
                Tokenizer doc_tokenizer;
                std::vector<std::string> processed = doc_tokenizer.processToken(word);

                if (!processed.empty()) {
                    tokens.push_back({QueryTokenType::WORD, processed[0]});
                }
            }
        }
    }

    return tokens;
}