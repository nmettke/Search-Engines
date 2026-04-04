// src/lib/query_engine.cpp
#include "query_engine.h"
#include "isr_and.h"
#include <algorithm>
#include <cstdint>

std::vector<DocumentRecord> QueryEngine::search(const std::vector<std::string> &terms) const {
    std::vector<DocumentRecord> results;
    if (terms.empty())
        return results;

    std::vector<std::unique_ptr<ISR>> term_isrs;
    for (const auto &term : terms) {
        auto isr = reader_.createISR(term);
        if (!isr)
            return results; // Instant fail if a term doesn't exist
        term_isrs.push_back(std::move(isr));
    }

    auto doc_end_isr = reader_.createISR(docEndToken);
    if (!doc_end_isr)
        return results;

    // Build the execution tree root
    ISRAnd root(std::move(term_isrs), std::move(doc_end_isr), reader_);

    // Execute the pipeline!
    while (!root.done()) {
        if (root.currentDocument().has_value()) {
            results.push_back(*root.currentDocument());
        }
        root.next();
    }

    return results;
}