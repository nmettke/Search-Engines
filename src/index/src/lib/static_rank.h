#pragma once

#include "../../../utils/string.hpp"
#include "types.h"
#include <cstdint>

struct StaticRankConfig {
    double seed_distance_weight = 2.40;
    double seed_distance_scale = 1.50;

    double word_count_weight = 1.50;
    double word_count_pivot = 350.0;
    double title_word_count_weight = 0.70;
    double title_word_count_pivot = 6.0;

    uint32_t short_doc_threshold = 80;
    double short_doc_penalty = 0.60;
    uint32_t very_long_doc_threshold = 5000;
    double very_long_doc_penalty = 0.20;

    double https_bonus = 0.35;
    double saw_body_bonus = 0.25;
    double saw_close_html_bonus = 0.25;
    double truncated_penalty = 0.75;
    double open_discard_penalty = 0.50;

    double url_length_weight = 0.45;
    double url_length_scale = 80.0;
    double path_length_weight = 0.70;
    double path_length_scale = 30.0;
    double path_depth_weight = 0.70;
    double path_depth_scale = 2.0;
    double query_param_weight = 0.55;
    double query_param_scale = 1.0;
    double numeric_path_weight = 0.45;
    double numeric_path_scale = 2.0;
    double domain_hyphen_weight = 0.35;
    double domain_hyphen_scale = 1.0;

    double base_domain_bonus = 0.20;
    uint8_t min_base_domain_length = 4;
    uint8_t max_base_domain_length = 20;

    double outgoing_link_weight = 0.50;
    double outgoing_link_pivot = 20.0;
    double outgoing_anchor_weight = 0.40;
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
