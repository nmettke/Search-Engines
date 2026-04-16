#pragma once

#include "disk_chunk_reader.h"
#include "query_profile.h"
#include "types.h"
#include <limits>

struct DynamicRankConfig {
    double static_weight = 0.3;

    double anchor_stream_weight = 2.8;
    double title_stream_weight = 1.75;
    double body_stream_weight = 1.0;

    double all_words_bonus = 2.10;
    double most_words_bonus = 1.20;
    double some_words_bonus = 0.35;

    double exact_phrase_bonus = 2.40;
    double short_span_bonus = 1.35;
    double ordered_span_bonus = 0.90;
    double short_ordered_span_bonus = 0.75;

    double double_bonus = 0.55;
    double triple_bonus = 0.90;
    double near_top_bonus = 0.45;
    double occurrence_bonus = 0.12;
    double too_frequent_penalty = 0.80; // if occurence greater than capped occurence
    double most_words_percent = 0.6;

    size_t near_top_limit_anchor = 8;
    size_t near_top_limit_title = 3;
    size_t near_top_limit_body = 24;
    size_t short_span_multiplier_anchor = 2;
    size_t short_span_multiplier_title = 2;
    size_t short_span_multiplier_body = 4;
    size_t max_gap_for_double = 2;
    size_t max_counted_occurrences = 6; // capped number of occurence of each term
};

class DynamicRankScorer {
  public:
    explicit DynamicRankScorer(DynamicRankConfig config = {}) : config_(config) {}

    double score(const DiskChunkReader &body_reader, const DiskChunkReader *anchor_reader,
                 const QueryProfile &profile, uint32_t doc_id, const DocumentRecord &body_doc,
                 double static_score,
                 double min_competitive_score = -std::numeric_limits<double>::infinity()) const;

    double maxDynamicScore(const QueryProfile &profile, bool include_body_title,
                           bool include_anchor) const;
    double staticWeight() const { return config_.static_weight; }
    const DynamicRankConfig &config() const { return config_; }

  private:
    DynamicRankConfig config_;
};
