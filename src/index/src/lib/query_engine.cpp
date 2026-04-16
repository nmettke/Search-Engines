#include "query_engine.h"
#include "../../../utils/string.hpp"
#include "../../../utils/vector.hpp"
#include "query_compiler.h"
#include "query_profile.h"
#include "query_tokenizer.h"
#include <algorithm>
#include <limits>
#include <memory>

namespace {

constexpr uint32_t kMaxDocId = std::numeric_limits<uint32_t>::max();

struct CompiledQuery {
    QueryProfile profile;
    std::unique_ptr<ISR> body_root;
    std::unique_ptr<ISR> anchor_root;
    bool can_match = false;
};

class TopKHeap {
  public:
    explicit TopKHeap(size_t k) : k_(k) { heap_.reserve(k + 1); }

    void push(const ScoredDocument &item) {
        if (k_ == 0) {
            return;
        }

        if (heap_.size() < k_) {
            heap_.pushBack(item);
            heapifyUp(static_cast<int>(heap_.size() - 1));
        } else if (item.score > heap_[0].score) {
            heap_[0] = item;
            heapifyDown(0);
        }
    }

    double minScore() const {
        if (heap_.size() < k_ || heap_.empty()) {
            return -std::numeric_limits<double>::infinity();
        }
        return heap_[0].score;
    }

    vector<ScoredDocument> extractSorted() {
        // returns full sorted results
        vector<ScoredDocument> sorted_results;
        while (!heap_.empty()) {
            sorted_results.pushBack(heap_[0]);
            heap_[0] = heap_.back();
            heap_.popBack();
            if (!heap_.empty()) {
                heapifyDown(0);
            }
        }
        for (size_t i = 0; i < sorted_results.size() / 2; ++i) {
            std::swap(sorted_results[i], sorted_results[sorted_results.size() - 1 - i]);
        }
        return sorted_results;
    }

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
        int size = static_cast<int>(heap_.size());
        while (true) {
            int left = 2 * index + 1;
            int right = 2 * index + 2;
            int smallest = index;

            if (left < size && heap_[left].score < heap_[smallest].score) {
                smallest = left;
            }
            if (right < size && heap_[right].score < heap_[smallest].score) {
                smallest = right;
            }

            if (smallest != index) {
                std::swap(heap_[index], heap_[smallest]);
                index = smallest;
            } else {
                break;
            }
        }
    }
};

double currentMinCompetitive(const TopKHeap &top_k, const std::atomic<double> *shared_min_score) {
    // Min considering local topK heap and global topKheap (other chunks)
    double min_competitive = top_k.minScore();
    if (shared_min_score != nullptr) {
        min_competitive = std::max(min_competitive, shared_min_score->load());
    }
    return min_competitive;
}

double computeMaxStaticScore(const DiskChunkReader &reader, const StaticRankScorer &scorer) {
    double max_score = 0.0;
    for (uint32_t doc_id = 0; doc_id < reader.header().num_documents; ++doc_id) {
        if (std::optional<DocumentRecord> doc = reader.getDocument(doc_id)) {
            max_score = std::max(max_score, scorer.score(*doc));
        }
    }
    return max_score;
}

CompiledQuery compileQuery(const DiskChunkReader &body_reader, const DiskChunkReader *anchor_reader,
                           const string &query) {
    CompiledQuery compiled;

    ::vector<QueryToken> tokens = QueryTokenizer::tokenize(query);
    if (tokens.empty()) {
        return compiled;
    }

    compiled.profile = buildQueryProfile(tokens, body_reader, anchor_reader);
    if (compiled.profile.empty()) {
        return compiled;
    }

    QueryCompiler body_compiler(body_reader, QueryCompilationMode::BodyTitle);
    compiled.body_root = body_compiler.compile(tokens);

    if (anchor_reader != nullptr) {
        QueryCompiler anchor_compiler(*anchor_reader, QueryCompilationMode::Anchor);
        compiled.anchor_root = anchor_compiler.compile(tokens);
    }

    compiled.can_match = (compiled.body_root != nullptr || compiled.anchor_root != nullptr);

    return compiled;
}

uint32_t currentDocId(ISR *root, ISRWord *doc_end) {
    if (root == nullptr || root->done() || doc_end == nullptr) {
        return kMaxDocId;
    }

    uint32_t loc = root->currentLocation();
    uint32_t doc_end_loc = doc_end->seek(loc);
    if (doc_end_loc == ISRSentinel) {
        return kMaxDocId;
    }
    return doc_end->currentIndex() - 1;
}

void advancePastCurrentDoc(ISR *root, ISRWord *doc_end) {
    if (root == nullptr || root->done()) {
        return;
    }

    uint32_t doc_end_loc = doc_end->seek(root->currentLocation());
    if (doc_end_loc == ISRSentinel) {
        root->seek(ISRSentinel);
        return;
    }
    root->seek(doc_end_loc + 1);
}

} // namespace

QueryEngine::QueryEngine(const DiskChunkReader &body_reader, StaticRankConfig rank_config,
                         DynamicRankConfig dynamic_rank_config)
    : body_reader_(body_reader), static_scorer_(rank_config), dynamic_scorer_(dynamic_rank_config),
      max_static_score_(computeMaxStaticScore(body_reader_, static_scorer_)) {}

QueryEngine::QueryEngine(const DiskChunkReader &body_reader, const DiskChunkReader &anchor_reader,
                         StaticRankConfig rank_config, DynamicRankConfig dynamic_rank_config)
    : body_reader_(body_reader), anchor_reader_(&anchor_reader), static_scorer_(rank_config),
      dynamic_scorer_(dynamic_rank_config),
      max_static_score_(computeMaxStaticScore(body_reader_, static_scorer_)) {}

vector<ScoredDocument> QueryEngine::search(const string &query, size_t K) const {
    return search(query, K, nullptr);
}

vector<ScoredDocument> QueryEngine::search(const string &query, size_t K,
                                           const std::atomic<double> *shared_min_score) const {
    TopKHeap top_k(K);
    CompiledQuery compiled = compileQuery(body_reader_, anchor_reader_, query);
    if (!compiled.can_match) {
        return top_k.extractSorted();
    }

    std::unique_ptr<ISRWord> body_doc_end = body_reader_.createISR(docEndToken);
    if (!body_doc_end) {
        return top_k.extractSorted();
    }

    std::unique_ptr<ISRWord> anchor_doc_end;
    if (anchor_reader_ != nullptr && compiled.anchor_root != nullptr) {
        anchor_doc_end = anchor_reader_->createISR(docEndToken);
    }

    double min_competitive = currentMinCompetitive(top_k, shared_min_score);
    double doc_upper_bound =
        dynamic_scorer_.staticWeight() * max_static_score_ +
        dynamic_scorer_.maxDynamicScore(compiled.profile, true, anchor_reader_ != nullptr);

    while (true) {
        uint32_t body_doc_id = currentDocId(compiled.body_root.get(), body_doc_end.get());
        uint32_t anchor_doc_id = currentDocId(compiled.anchor_root.get(),
                                              anchor_doc_end ? anchor_doc_end.get() : nullptr);
        uint32_t current_doc_id = std::min(body_doc_id, anchor_doc_id);

        if (current_doc_id == kMaxDocId) {
            break;
        }

        min_competitive = std::max(min_competitive, currentMinCompetitive(top_k, shared_min_score));

        if (doc_upper_bound > min_competitive) {
            if (std::optional<DocumentRecord> doc = body_reader_.getDocument(current_doc_id)) {
                double static_score = static_scorer_.score(*doc);
                double score =
                    dynamic_scorer_.score(body_reader_, anchor_reader_, compiled.profile,
                                          current_doc_id, *doc, static_score, min_competitive);
                if (score > min_competitive) {
                    top_k.push({current_doc_id, score});
                    min_competitive = currentMinCompetitive(top_k, shared_min_score);
                }
            }
        }

        if (body_doc_id == current_doc_id) {
            advancePastCurrentDoc(compiled.body_root.get(), body_doc_end.get());
        }
        if (anchor_doc_id == current_doc_id && anchor_doc_end) {
            advancePastCurrentDoc(compiled.anchor_root.get(), anchor_doc_end.get());
        }
    }

    return top_k.extractSorted();
}
