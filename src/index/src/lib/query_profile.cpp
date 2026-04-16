#include "query_profile.h"

namespace {

bool containsTerm(const ::vector<::string> &terms, const ::string &term) {
    for (const ::string &candidate : terms) {
        if (candidate == term) {
            return true;
        }
    }
    return false;
}

// Retain the queryCompiler structure to traverse the query and add words to the profile
// code is mostly taken from the compiler (since this allows us to handle adding complicated query)
// for example NOT (A or B) both should not be added to the term counts
class QueryProfileBuilder {
  public:
    explicit QueryProfileBuilder(const ::vector<QueryToken> &tokens) : tokens_(tokens) {}

    QueryProfile collect(const DiskChunkReader &body_reader, const DiskChunkReader *anchor_reader) {
        current_ = 0;
        profile_ = QueryProfile{};
        if (!tokens_.empty()) {
            parseOr(true);
        }

        return profile_;
    }

  private:
    const ::vector<QueryToken> &tokens_;
    QueryProfile profile_;
    size_t current_ = 0;

    bool isAtEnd() const { return current_ >= tokens_.size(); }

    // mirror peek and consume in compiler
    const QueryToken &peek() const { return tokens_[current_]; }

    void consume() {
        if (!isAtEnd()) {
            ++current_;
        }
    }

    void addWord(const ::string &term) {
        if (!containsTerm(profile_.unique_terms, term)) {
            profile_.unique_terms.pushBack(term);
        }
    }

    void addPhrase(const ::vector<::string> &terms) {
        if (terms.empty()) {
            return;
        }

        QueryPhrase phrase;
        for (const ::string &term : terms) {
            phrase.terms.pushBack(term);
        }
        profile_.phrases.pushBack(std::move(phrase));
    }

    void parseOr(bool positive) {
        parseAnd(positive);
        while (!isAtEnd() && peek().type == QueryTokenType::OR) {
            consume();
            parseAnd(positive);
        }
    }

    void parseAnd(bool positive) {
        while (!isAtEnd() && peek().type != QueryTokenType::OR &&
               peek().type != QueryTokenType::R_PAREN) {
            bool child_positive = positive;

            if (peek().type == QueryTokenType::AND) {
                consume();
                continue;
            }

            if (peek().type == QueryTokenType::NOT) {
                child_positive = false;
                consume();
                if (isAtEnd()) {
                    return;
                }
            }

            parsePrimary(child_positive);
        }
    }

    void parsePrimary(bool positive) {
        if (isAtEnd()) {
            return;
        }

        const QueryToken &token = peek();
        if (token.type == QueryTokenType::WORD) {
            if (positive) {
                addWord(token.text);
            }
            consume();
            return;
        }

        if (token.type == QueryTokenType::L_PAREN) {
            consume();
            parseOr(positive);
            if (!isAtEnd() && peek().type == QueryTokenType::R_PAREN) {
                consume();
            }
            return;
        }

        if (token.type == QueryTokenType::QUOTE) {
            consume();
            ::vector<::string> phrase_terms;
            while (!isAtEnd() && peek().type != QueryTokenType::QUOTE) {
                if (peek().type == QueryTokenType::WORD && positive) {
                    addWord(peek().text);
                    phrase_terms.pushBack(peek().text);
                }
                consume();
            }
            if (!isAtEnd() && peek().type == QueryTokenType::QUOTE) {
                consume();
            }
            if (positive) {
                addPhrase(phrase_terms);
            }
            return;
        }

        consume();
    }
};

} // namespace

QueryProfile buildQueryProfile(const ::vector<QueryToken> &tokens,
                               const DiskChunkReader &body_reader,
                               const DiskChunkReader *anchor_reader) {
    QueryProfileBuilder builder(tokens);
    return builder.collect(body_reader, anchor_reader);
}
