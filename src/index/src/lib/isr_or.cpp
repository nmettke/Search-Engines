// src/lib/isr_or.cpp
#include "isr_or.h"
#include <algorithm>

ISROr::ISROr(std::vector<std::unique_ptr<ISR>> terms)
    : terms_(std::move(terms)), current_loc_(0), is_exhausted_(false) {
    if (terms_.empty()) {
        is_exhausted_ = true;
        current_loc_ = ISRSentinel;
    } else {
        advanceToNextMatch();
    }
}

uint32_t ISROr::next() {
    if (is_exhausted_)
        return ISRSentinel;

    for (auto &term : terms_) {
        if (!term->done() && term->currentLocation() == current_loc_) {
            term->next();
        }
    }
    return advanceToNextMatch();
}

uint32_t ISROr::seek(uint32_t target) {
    if (is_exhausted_)
        return ISRSentinel;
    if (current_loc_ >= target)
        return current_loc_;

    for (auto &term : terms_) {
        if (!term->done() && term->currentLocation() < target) {
            term->seek(target);
        }
    }
    return advanceToNextMatch();
}

uint32_t ISROr::advanceToNextMatch() {
    uint32_t min_loc = ISRSentinel;

    for (const auto &term : terms_) {
        if (!term->done()) {
            min_loc = std::min(min_loc, term->currentLocation());
        }
    }

    if (min_loc == ISRSentinel) {
        is_exhausted_ = true;
    }

    current_loc_ = min_loc;
    return current_loc_;
}