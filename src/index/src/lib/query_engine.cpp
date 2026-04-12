// src/lib/query_engine.cpp
#include "query_engine.h"
#include "query_compiler.h"
#include "query_tokenizer.h"
#include "utils/string.hpp"
#include "utils/vector.hpp"

class TopKHeap {
  private:
    ::vector<ScoredDocument> heap_;
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

    ::vector<ScoredDocument> extractSorted() {
        ::vector<ScoredDocument> sorted_results;
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

double QueryEngine::calculate_score(const DocumentRecord &doc, const ::string &query) const {
    // TODO: Implement scoring logic
    return 1.0;
}

::vector<ScoredDocument> QueryEngine::search(const ::string &query, size_t K) const {
    TopKHeap top_k(K);

    auto tokens = QueryTokenizer::tokenize(query);
    if (tokens.empty())
        return top_k.extractSorted();

    QueryCompiler compiler(reader_);
    auto root = compiler.compile(tokens);
    if (!root)
        return top_k.extractSorted();

    auto doc_end_isr = reader_.createISR(docEndToken);
    if (!doc_end_isr)
        return top_k.extractSorted();

    while (!root->done()) {
        uint32_t match_loc = root->currentLocation();

        uint32_t doc_end_loc = doc_end_isr->seek(match_loc);
        if (doc_end_loc == ISRSentinel)
            break;

        uint32_t doc_id = doc_end_isr->currentIndex() - 1;
        auto doc = reader_.getDocument(doc_id);

        if (doc) {
            double score = calculate_score(*doc, query);
            top_k.push({*doc, score});
        }

        root->seek(doc_end_loc + 1);
    }

    return top_k.extractSorted();
}