// src/lib/query_engine.cpp
#include "query_engine.h"
#include "../../../utils/string.hpp"
#include "../../../utils/vector.hpp"
#include "isr.h"
#include "query_compiler.h"
#include "query_tokenizer.h"
#include <memory>

class TopKHeap {
  private:
    vector<ScoredDocument> heap_;
    size_t k_;

    void heapifyUp(int index) {
        while (index > 0) {
            int parent = (index - 1) / 2;
            if (heap_[index].score < heap_[parent].score) {
                std::swap(heap_[index], heap_[parent]);
                index = parent;
            } else {
                break;
            }
        }
    }

    void heapifyDown(int index) {
        int size = heap_.size();
        while (true) {
            int left = 2 * index + 1;
            int right = 2 * index + 2;
            int smallest = index;

            if (left < size && heap_[left].score < heap_[smallest].score)
                smallest = left;
            if (right < size && heap_[right].score < heap_[smallest].score)
                smallest = right;

            if (smallest != index) {
                std::swap(heap_[index], heap_[smallest]);
                index = smallest;
            } else {
                break;
            }
        }
    }

  public:
    TopKHeap(size_t k) : k_(k) { heap_.reserve(k + 1); }

    void push(const ScoredDocument &item) {
        if (heap_.size() < k_) {
            heap_.push_back(item);
            heapifyUp(heap_.size() - 1);
        } else if (item.score > heap_[0].score) {
            heap_[0] = item;
            heapifyDown(0);
        }
    }

    vector<ScoredDocument> extractSorted() {
        vector<ScoredDocument> sorted_results;
        while (!heap_.empty()) {
            sorted_results.push_back(heap_[0]);
            heap_[0] = heap_.back();
            heap_.popBack();
            heapifyDown(0);
        }
        for (size_t i = 0; i < sorted_results.size() / 2; ++i) {
            std::swap(sorted_results[i], sorted_results[sorted_results.size() - 1 - i]);
        }
        return sorted_results;
    }
};

vector<ScoredDocument> QueryEngine::search(const string &query, size_t K) const {
    TopKHeap top_k(K);

    auto tokens = QueryTokenizer::tokenize(query);
    if (tokens.empty())
        return top_k.extractSorted();

    QueryCompiler body_compiler(body_reader_);
    QueryCompiler anchor_compiler(anchor_reader_);

    auto body_root = body_compiler.compile(tokens);
    auto anchor_root = anchor_compiler.compileAnchor(tokens);

    if (!body_root && !anchor_root)
        return top_k.extractSorted();

    auto body_doc_end = body_root ? body_reader_.createISR(docEndToken) : nullptr;
    auto anchor_doc_end = anchor_root ? anchor_reader_.createISR(docEndToken) : nullptr;

    bool body_active = body_root && body_doc_end && !body_root->done();
    bool anchor_active = anchor_root && anchor_doc_end && !anchor_root->done();

    while (body_active || anchor_active) {
        uint32_t body_doc_id = MAX_DOC_ID;
        uint32_t body_doc_end_loc = ISRSentinel;
        if (body_active) {
            body_doc_end_loc = body_doc_end->seek(body_root->currentLocation());
            if (body_doc_end_loc == ISRSentinel)
                body_active = false;
            else
                body_doc_id = body_doc_end->currentIndex() - 1;
        }

        uint32_t anchor_doc_id = MAX_DOC_ID;
        uint32_t anchor_doc_end_loc = ISRSentinel;
        if (anchor_active) {
            anchor_doc_end_loc = anchor_doc_end->seek(anchor_root->currentLocation());
            if (anchor_doc_end_loc == ISRSentinel)
                anchor_active = false;
            else
                anchor_doc_id = anchor_doc_end->currentIndex() - 1;
        }

        uint32_t current_doc_id = (body_doc_id < anchor_doc_id) ? body_doc_id : anchor_doc_id;

        if (current_doc_id == MAX_DOC_ID)
            break;

        auto doc = body_reader_.getDocument(current_doc_id);

        if (doc) {
            bool body_matched = (body_doc_id == current_doc_id);
            bool anchor_matched = (anchor_doc_id == current_doc_id);

            double score = calculate_span_score(current_doc_id, body_root.get(), anchor_root.get(),
                                                body_matched, anchor_matched, doc.value());
            top_k.push({current_doc_id, score});
        }

        if (body_active && body_doc_id == current_doc_id) {
            body_root->seek(body_doc_end_loc + 1);
            body_active = !body_root->done();
        }
        if (anchor_active && anchor_doc_id == current_doc_id) {
            anchor_root->seek(anchor_doc_end_loc + 1);
            anchor_active = !anchor_root->done();
        }
    }

    return top_k.extractSorted();
}

double QueryEngine::calculate_span_score(uint32_t doc_id, ISR *body_root, ISR *anchor_root,
                                         bool body_matched, bool anchor_matched,
                                         const DocumentRecord &doc) const {

    double static_score = static_scorer_.score(doc);
    double dynamic_score = 0.0;

    if (body_matched && body_root) {
        dynamic_score += 20.0;
    }

    if (anchor_matched && anchor_root) {
        dynamic_score += 60.0;
    }

    return dynamic_score + static_score;
}