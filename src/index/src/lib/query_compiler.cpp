// src/lib/query_compiler.cpp
#include "query_compiler.h"
#include "isr_and.h"
#include "isr_container.h"
#include "isr_or.h"
#include "isr_phrase.h"

namespace {

std::unique_ptr<ISR> collapseOrChildren(::vector<std::unique_ptr<ISR>> &children) {
    if (children.empty()) {
        return nullptr;
    }
    if (children.size() == 1) {
        return std::move(children[0]);
    }
    return std::make_unique<ISROr>(std::move(children));
}

std::unique_ptr<ISR> buildStreamPhraseISR(const DiskChunkReader &reader,
                                          const ::vector<::string> &terms, const char *prefix) {
    ::vector<std::unique_ptr<ISR>> phrase_terms;
    for (const ::string &term : terms) {
        ::string decorated = term;
        if (prefix != nullptr) {
            decorated = ::string(prefix) + term;
        }

        std::unique_ptr<ISR> node = reader.createISR(decorated);
        if (!node) {
            return nullptr;
        }
        phrase_terms.pushBack(std::move(node));
    }

    return collapseOrChildren(phrase_terms);
}

} // namespace

std::unique_ptr<ISR> QueryCompiler::compile(const ::vector<QueryToken> &tokens) {
    tokens_ = tokens;
    current_ = 0;
    if (tokens_.empty())
        return nullptr;
    return parseOr();
}

const QueryToken &QueryCompiler::peek() const { return tokens_[current_]; }
bool QueryCompiler::isAtEnd() const { return current_ >= tokens_.size(); }
void QueryCompiler::consume() {
    if (!isAtEnd())
        current_++;
}

std::unique_ptr<ISR> QueryCompiler::buildWordISR(const ::string &term) const {
    if (mode_ != QueryCompilationMode::BodyTitle) {
        // Anchor ISR (no need to decorate)
        return reader_.createISR(term);
    }

    ::vector<std::unique_ptr<ISR>> children;
    if (std::unique_ptr<ISR> body = reader_.createISR(term)) {
        children.pushBack(std::move(body));
    }
    if (std::unique_ptr<ISR> title = reader_.createISR("$" + term)) {
        children.pushBack(std::move(title));
    }
    return collapseOrChildren(children);
}

std::unique_ptr<ISR> QueryCompiler::buildPhraseISR(const ::vector<::string> &terms) const {
    if (terms.empty()) {
        return nullptr;
    }

    if (mode_ != QueryCompilationMode::BodyTitle) {
        // Build Anchor without decorator
        return buildStreamPhraseISR(reader_, terms, nullptr);
    }

    ::vector<std::unique_ptr<ISR>> children;
    if (std::unique_ptr<ISR> body_phrase = buildStreamPhraseISR(reader_, terms, nullptr)) {
        children.pushBack(std::move(body_phrase));
    }
    if (std::unique_ptr<ISR> title_phrase = buildStreamPhraseISR(reader_, terms, "$")) {
        children.pushBack(std::move(title_phrase));
    }
    return collapseOrChildren(children);
}

std::unique_ptr<ISR> QueryCompiler::parseOr() {
    ::vector<std::unique_ptr<ISR>> children;

    auto left = parseAnd();
    if (left)
        children.pushBack(std::move(left));

    while (!isAtEnd() && peek().type == QueryTokenType::OR) {
        consume();
        auto right = parseAnd();
        if (right)
            children.pushBack(std::move(right));
    }

    return collapseOrChildren(children);
}

std::unique_ptr<ISR> QueryCompiler::parseAnd() {
    ::vector<std::unique_ptr<ISR>> positives;
    ::vector<std::unique_ptr<ISR>> negatives;

    while (!isAtEnd() && peek().type != QueryTokenType::OR &&
           peek().type != QueryTokenType::R_PAREN) {
        bool is_not = false;

        if (peek().type == QueryTokenType::AND) {
            consume();
            if (isAtEnd())
                break;
        }

        if (peek().type == QueryTokenType::NOT) {
            is_not = true;
            consume();
            if (isAtEnd())
                break;
        }

        auto node = parsePrimary();
        if (!node) {
            if (!is_not)
                return nullptr; // Required positive term is missing -> AND fails entirely
            else
                continue; // Negative term missing -> safe to ignore exclusion
        } else {
            if (is_not)
                negatives.pushBack(std::move(node));
            else
                positives.pushBack(std::move(node));
        }
    }

    if (positives.empty())
        return nullptr;

    std::unique_ptr<ISR> pos_isr;
    if (positives.size() == 1) {
        pos_isr = std::move(positives[0]);
    } else {
        pos_isr =
            std::make_unique<ISRAnd>(std::move(positives), reader_.createISR(docEndToken), reader_);
    }

    if (negatives.empty()) {
        return pos_isr;
    } else {
        return std::make_unique<ISRContainer>(std::move(pos_isr), std::move(negatives),
                                              reader_.createISR(docEndToken), reader_);
    }
}

std::unique_ptr<ISR> QueryCompiler::parsePrimary() {
    if (isAtEnd())
        return nullptr;

    const auto &token = peek();

    if (token.type == QueryTokenType::WORD) {
        consume();
        return buildWordISR(token.text);
    } else if (token.type == QueryTokenType::L_PAREN) {
        consume();
        auto node = parseOr();
        if (!isAtEnd() && peek().type == QueryTokenType::R_PAREN)
            consume();
        return node;
    } else if (token.type == QueryTokenType::QUOTE) {
        consume();
        ::vector<::string> phrase_terms;

        while (!isAtEnd() && peek().type != QueryTokenType::QUOTE) {
            if (peek().type == QueryTokenType::WORD) {
                phrase_terms.pushBack(peek().text);
            }
            consume();
        }
        if (!isAtEnd() && peek().type == QueryTokenType::QUOTE)
            consume();

        return buildPhraseISR(phrase_terms);
    }

    // Invalid structual token, skip to prevent infinite loops
    consume();
    return nullptr;
}
