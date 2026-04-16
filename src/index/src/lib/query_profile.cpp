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

// Update the rare and common count
void updateRareCount(const ::string &term, const DiskChunkReader &body_reader,
                     const DiskChunkReader *anchor_reader, size_t &rare_count,
                     size_t &common_count) {
    uint64_t total_locations = body_reader.header().total_locations;
    uint64_t collection_frequency = 0;

    if (std::optional<TermInfo> body_info = body_reader.getTermInfo(term)) {
        collection_frequency += body_info->collection_frequency;
    }

    if (anchor_reader != nullptr) {
        if (std::optional<TermInfo> anchor_info = anchor_reader->getTermInfo(term)) {
            collection_frequency += anchor_info->collection_frequency;
            total_locations += anchor_reader->header().total_locations;
        }
    }

    if (collection_frequency == 0 || total_locations == 0) {
        return;
    }

    double estimated_period =
        static_cast<double>(total_locations) / static_cast<double>(collection_frequency);

    if (estimated_period >= 50000.0) {
        ++rare_count;
    } else if (estimated_period <= 250.0) {
        ++common_count;
    }
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

        for (const ::string &term : profile_.unique_terms) {
            updateRareCount(term, body_reader, anchor_reader, profile_.rare_word_count,
                            profile_.common_word_count);
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
        profile_.flattened_terms.pushBack(term);
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
