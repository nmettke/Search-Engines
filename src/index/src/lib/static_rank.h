#pragma once

#include "../../../utils/string.hpp"
#include "types.h"
#include <cstdint>

struct StaticRankConfig {
    double seed_distance_weight = 3.30;
    double seed_distance_scale = 1.00;

    double word_count_weight = 0.90;
    double word_count_pivot = 500.0;
    double title_word_count_weight = 0.45;
    double title_word_count_pivot = 6.0;

    uint32_t short_doc_threshold = 45;
    double short_doc_penalty = 0.20;
    uint32_t very_long_doc_threshold = 5000;
    double very_long_doc_penalty = 0.20;

    double https_bonus = 0.35;
    double saw_body_bonus = 0.25;
    double saw_close_html_bonus = 0.25;
    double truncated_penalty = 0.75;
    double open_discard_penalty = 0.50;

    double url_length_weight = 0.55;
    double url_length_scale = 60.0;
    double path_length_weight = 0.95;
    double path_length_scale = 20.0;
    double path_depth_weight = 1.15;
    double path_depth_scale = 1.20;
    double query_param_weight = 0.95;
    double query_param_scale = 0.80;
    double numeric_path_weight = 0.75;
    double numeric_path_scale = 1.00;
    double domain_hyphen_weight = 0.45;
    double domain_hyphen_scale = 1.0;

    double base_domain_bonus = 0.35;
    uint8_t min_base_domain_length = 3;
    uint8_t max_base_domain_length = 20;

    double outgoing_link_weight = 0.30;
    double outgoing_link_pivot = 20.0;
    double outgoing_anchor_weight = 0.20;
    double outgoing_anchor_pivot = 80.0;

    double gov_tld_bonus = 0.35;
    double edu_tld_bonus = 0.35;
    double org_tld_bonus = 0.20;
    double com_tld_bonus = 0.10;
    double net_tld_bonus = 0.05;
};

class StaticRankScorer {
  public:
    explicit StaticRankScorer(StaticRankConfig config = {}) : config_(config) {}

    double score(const DocumentRecord &doc) const;
    const StaticRankConfig &config() const { return config_; }

  private:
    StaticRankConfig config_;

    double scoreProvenance(const DocumentRecord &doc) const;
    double scoreContent(const DocumentRecord &doc) const;
    double scoreQuality(const DocumentFeatures &features) const;
    double scoreUrl(const DocumentFeatures &features) const;
    double scoreLinks(const DocumentFeatures &features) const;
    double scoreTld(const ::string &raw_tld) const;
};
