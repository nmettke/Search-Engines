// src/lib/query_compiler.cpp
#include "query_compiler.h"
#include "isr_and.h"
#include "isr_container.h"
#include "isr_or.h"
#include "isr_phrase.h"
#include <memory>

std::unique_ptr<ISR> QueryCompiler::compile(const ::vector<QueryToken> &tokens) {
    tokens_ = tokens;
    current_ = 0;
    if (tokens_.empty())
        return nullptr;
    return parseOr();
}

std::unique_ptr<ISR> QueryCompiler::compileAnchor(const ::vector<QueryToken> &tokens) {
    is_anchor_ = true;
    return compile(tokens);
}

const QueryToken &QueryCompiler::peek() const { return tokens_[current_]; }
bool QueryCompiler::isAtEnd() const { return current_ >= tokens_.size(); }
void QueryCompiler::consume() {
    if (!isAtEnd())
        current_++;
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

    if (children.empty())
        return nullptr;
    if (children.size() == 1)
        return std::move(children[0]);

    return std::make_unique<ISROr>(std::move(children));
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
        return createISR(token.text);
    } else if (token.type == QueryTokenType::L_PAREN) {
        consume();
        auto node = parseOr();
        if (!isAtEnd() && peek().type == QueryTokenType::R_PAREN)
            consume();
        return node;
    } else if (token.type == QueryTokenType::QUOTE) {
        consume();
        ::vector<std::unique_ptr<ISR>> phrase_terms;
        bool missing_term = false;

        while (!isAtEnd() && peek().type != QueryTokenType::QUOTE) {
            if (peek().type == QueryTokenType::WORD) {
                auto node = createISR(peek().text);
                if (node)
                    phrase_terms.pushBack(std::move(node));
                else
                    missing_term = true; // Phrase broken
            }
            consume();
        }
        if (!isAtEnd() && peek().type == QueryTokenType::QUOTE)
            consume();

        if (missing_term || phrase_terms.empty())
            return nullptr;
        if (phrase_terms.size() == 1)
            return std::move(phrase_terms[0]);

        return std::make_unique<ISRPhrase>(std::move(phrase_terms));
    }

    // Invalid structual token, skip to prevent infinite loops
    consume();
    return nullptr;
}

std::unique_ptr<ISR> QueryCompiler::createISR(const string &term) {
    if (is_anchor_) {
        return reader_.createISR(term);
    }

    auto body_isr = reader_.createISR(term);
    auto title_isr = reader_.createISR("$" + term);

    if (body_isr && title_isr) {
        ::vector<std::unique_ptr<ISR>> children;
        children.pushBack(std::move(body_isr));
        children.pushBack(std::move(title_isr));
        return std::make_unique<ISROr>(std::move(children));
    } else if (body_isr) {
        return body_isr;
    } else {
        return title_isr;
    }
}
