#include "isr_and.h"
#include <algorithm>

ISRAnd::ISRAnd(::vector<std::unique_ptr<ISR>> terms, std::unique_ptr<ISRWord> doc_end_isr,
               const DiskChunkReader &reader)
    : terms_(std::move(terms)), doc_end_isr_(std::move(doc_end_isr)), reader_(reader) {

    if (terms_.empty() || !doc_end_isr_) {
        is_exhausted_ = true;
        current_loc_ = ISRSentinel;
    } else {
        is_exhausted_ = false;
        advanceToNextMatch();
    }
}

uint32_t ISRAnd::next() {
    if (is_exhausted_)
        return ISRSentinel;

    for (auto &term : terms_) {
        term->seek(current_loc_ + 1);
    }

    return advanceToNextMatch();
}

uint32_t ISRAnd::seek(uint32_t target) {
    if (is_exhausted_)
        return ISRSentinel;
    if (current_loc_ >= target)
        return current_loc_;

    for (auto &term : terms_) {
        term->seek(target);
    }

    return advanceToNextMatch();
}

uint32_t ISRAnd::advanceToNextMatch() {
    while (true) {
        uint32_t max_loc = 0;
        for (auto &term : terms_) {
            if (term->done()) {
                is_exhausted_ = true;
                return ISRSentinel;
            }
            max_loc = std::max(max_loc, term->currentLocation());
        }

        uint32_t doc_end_loc = doc_end_isr_->seek(max_loc);
        if (doc_end_loc == ISRSentinel) {
            is_exhausted_ = true;
            return ISRSentinel;
        }

        uint32_t doc_id = doc_end_isr_->currentIndex() - 1;
        auto target_doc = reader_.getDocument(doc_id);
        if (!target_doc.has_value()) {
            is_exhausted_ = true;
            return ISRSentinel;
        }

        uint32_t doc_start_loc = target_doc->start_location;
        bool all_in_doc = true;

        for (auto &term : terms_) {
            uint32_t loc = term->seek(doc_start_loc);
            if (loc == ISRSentinel) {
                is_exhausted_ = true;
                return ISRSentinel;
            }

            if (loc > doc_end_loc) {
                all_in_doc = false;
                break;
            }
        }

        if (all_in_doc) {
            current_loc_ = doc_end_loc;
            current_doc_ = target_doc;
            return current_loc_;
        }

        for (auto &term : terms_) {
            term->seek(doc_end_loc + 1);
        }
    }
}