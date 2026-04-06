// src/lib/query_engine.cpp
#include "query_engine.h"
#include "query_compiler.h"
#include "query_tokenizer.h"

::vector<DocumentRecord> QueryEngine::search(const ::string &query) const {
    ::vector<DocumentRecord> results;

    auto tokens = QueryTokenizer::tokenize(query);
    if (tokens.empty())
        return results;

    QueryCompiler compiler(reader_);
    auto root = compiler.compile(tokens);
    if (!root)
        return results;

    auto doc_end_isr = reader_.createISR(docEndToken);
    if (!doc_end_isr)
        return results;

    while (!root->done()) {
        uint32_t match_loc = root->currentLocation();

        uint32_t doc_end_loc = doc_end_isr->seek(match_loc);
        if (doc_end_loc == ISRSentinel)
            break;

        uint32_t doc_id = doc_end_isr->currentIndex() - 1;
        auto doc = reader_.getDocument(doc_id);
        if (doc) {
            results.pushBack(*doc);
        }

        // seek past this document so we don't return same match again
        root->seek(doc_end_loc + 1);
    }

    return results;
}