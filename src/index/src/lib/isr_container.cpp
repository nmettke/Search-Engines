// src/lib/isr_container.cpp
#include "isr_container.h"

ISRContainer::ISRContainer(std::unique_ptr<ISR> positive_isr,
                           ::vector<std::unique_ptr<ISR>> negative_isrs,
                           std::unique_ptr<ISRWord> doc_end_isr, const DiskChunkReader &reader)
    : positive_isr_(std::move(positive_isr)), negative_isrs_(std::move(negative_isrs)),
      doc_end_isr_(std::move(doc_end_isr)), reader_(reader), current_loc_(0), is_exhausted_(false) {

    if (!positive_isr_ || positive_isr_->done() || !doc_end_isr_) {
        is_exhausted_ = true;
        current_loc_ = ISRSentinel;
    } else {
        advanceToNextMatch();
    }
}

uint32_t ISRContainer::next() {
    if (is_exhausted_)
        return ISRSentinel;
    positive_isr_->next();
    return advanceToNextMatch();
}

uint32_t ISRContainer::seek(uint32_t target) {
    if (is_exhausted_)
        return ISRSentinel;
    if (current_loc_ >= target)
        return current_loc_;

    positive_isr_->seek(target);
    return advanceToNextMatch();
}

uint32_t ISRContainer::advanceToNextMatch() {
    while (!positive_isr_->done()) {
        uint32_t pos_loc = positive_isr_->currentLocation();

        uint32_t doc_end_loc = doc_end_isr_->seek(pos_loc);
        if (doc_end_loc == ISRSentinel) {
            is_exhausted_ = true;
            return ISRSentinel;
        }

        uint32_t doc_id = doc_end_isr_->currentIndex() - 1;
        auto target_doc = reader_.getDocument(doc_id);
        if (!target_doc) {
            is_exhausted_ = true;
            return ISRSentinel;
        }
        uint32_t doc_start_loc = target_doc->start_location;

        bool is_excluded = false;

        for (auto &neg_isr : negative_isrs_) {
            if (!neg_isr->done()) {
                uint32_t neg_loc = neg_isr->seek(doc_start_loc);

                if (neg_loc != ISRSentinel && neg_loc <= doc_end_loc) {
                    is_excluded = true;
                    break;
                }
            }
        }

        if (!is_excluded) {
            current_loc_ = pos_loc;
            return current_loc_;
        } else {
            positive_isr_->seek(doc_end_loc + 1);
        }
    }

    is_exhausted_ = true;
    return ISRSentinel;
}