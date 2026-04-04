// src/lib/isr_phrase.cpp
#include "isr_phrase.h"
#include <algorithm>

ISRPhrase::ISRPhrase(std::vector<std::unique_ptr<ISR>> terms)
    : terms_(std::move(terms)), current_loc_(0), is_exhausted_(false) {
    if (terms_.empty()) {
        is_exhausted_ = true;
        current_loc_ = ISRSentinel;
    } else {
        advanceToNextMatch();
    }
}

uint32_t ISRPhrase::next() {
    if (is_exhausted_)
        return ISRSentinel;

    terms_[0]->next();
    return advanceToNextMatch();
}

uint32_t ISRPhrase::seek(uint32_t target) {
    if (is_exhausted_)
        return ISRSentinel;
    if (current_loc_ >= target)
        return current_loc_;

    terms_[0]->seek(target);
    return advanceToNextMatch();
}

uint32_t ISRPhrase::advanceToNextMatch() {
    while (true) {
        uint32_t max_implied_start = 0;
        bool any_exhausted = false;

        for (size_t i = 0; i < terms_.size(); ++i) {
            if (terms_[i]->done()) {
                any_exhausted = true;
                break;
            }

            uint32_t implied_start = terms_[i]->currentLocation() - static_cast<uint32_t>(i);
            max_implied_start = std::max(max_implied_start, implied_start);
        }

        if (any_exhausted) {
            is_exhausted_ = true;
            return ISRSentinel;
        }

        bool match = true;

        for (size_t i = 0; i < terms_.size(); ++i) {
            uint32_t target_loc = max_implied_start + static_cast<uint32_t>(i);
            uint32_t actual_loc = terms_[i]->seek(target_loc);

            if (actual_loc == ISRSentinel) {
                is_exhausted_ = true;
                return ISRSentinel;
            }

            if (actual_loc != target_loc) {
                match = false;
                break;
            }
        }

        if (match) {
            current_loc_ = max_implied_start + static_cast<uint32_t>(terms_.size() - 1);
            return current_loc_;
        }
    }
}