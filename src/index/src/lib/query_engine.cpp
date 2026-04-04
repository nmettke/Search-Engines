// src/lib/query_engine.cpp
#include "query_engine.h"
#include <algorithm>
#include <cstdint>

std::vector<DocumentRecord> QueryEngine::search(const std::vector<std::string> &terms) const {
    std::vector<DocumentRecord> results;
    if (terms.empty())
        return results;

    // Initialize Term ISRs
    std::vector<std::unique_ptr<ISRWord>> term_isrs;
    for (const auto &term : terms) {
        auto isr = reader_.createISR(term);
        if (!isr || isr->done()) {
            return results;
        }
        term_isrs.push_back(std::move(isr));
    }

    // Initialize #DocEnd ISR
    auto doc_end_isr = reader_.createISR(docEndToken);
    if (!doc_end_isr || doc_end_isr->done())
        return results;

    // DAAT Leapfrog Loop
    while (true) {
        // Find the maximum location among all terms
        uint32_t max_loc = 0;
        for (auto &isr : term_isrs) {
            if (!isr || isr->done())
                return results;

            max_loc = std::max(max_loc, isr->currentLocation());
        }

        // Find the END of the document containing max_loc
        uint32_t doc_end_loc = doc_end_isr->seek(max_loc);
        if (doc_end_loc == ISRSentinel)
            break;

        uint32_t doc_id = doc_end_isr->currentIndex() - 1;
        auto target_doc = reader_.getDocument(doc_id);
        if (!target_doc.has_value())
            break;
        uint32_t doc_start_loc = target_doc->start_location;

        bool all_in_doc = true;
        for (auto &isr : term_isrs) {
            uint32_t loc = isr->seek(doc_start_loc);

            if (loc == ISRSentinel)
                return results;

            if (loc > doc_end_loc) {
                all_in_doc = false;
                break;
            }
        }

        if (all_in_doc) {
            results.push_back(*target_doc);
        }

        for (auto &isr : term_isrs) {
            isr->seek(doc_end_loc);
        }
    }

    return results;
}