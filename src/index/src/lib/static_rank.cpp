#include "static_rank.h"

namespace {

bool hasFlag(const DocumentFeatures &features, DocumentFeatureFlag flag) {
    return (features.flags & flag) != 0;
}

double saturatingRatio(double value, double pivot) {
    if (value <= 0.0 || pivot <= 0.0) {
        return 0.0;
    }
    return value / (value + pivot);
}

double inversePenalty(double value, double scale) {
    if (value <= 0.0 || scale <= 0.0) {
        return 1.0;
    }
    return 1.0 / (1.0 + (value / scale));
}

} // namespace

double StaticRankScorer::score(const DocumentRecord &doc) const {
    double score = scoreProvenance(doc) + scoreContent(doc);

    const DocumentFeatures &features = doc.features;
    if (!hasFlag(features, kFeaturesPresent)) {
        return score;
    }

    score += scoreQuality(features);
    score += scoreUrl(features);
    score += scoreLinks(features);
    return score;
}

double StaticRankScorer::scoreProvenance(const DocumentRecord &doc) const {
    return config_.seed_distance_weight *
           inversePenalty(static_cast<double>(doc.seed_distance), config_.seed_distance_scale);
}

double StaticRankScorer::scoreContent(const DocumentRecord &doc) const {
    double score = 0.0;

    score += config_.word_count_weight *
             saturatingRatio(static_cast<double>(doc.word_count), config_.word_count_pivot);
    score +=
        config_.title_word_count_weight *
        saturatingRatio(static_cast<double>(doc.title_word_count), config_.title_word_count_pivot);

    if (doc.word_count < config_.short_doc_threshold) {
        score -= config_.short_doc_penalty;
    }
    if (doc.word_count > config_.very_long_doc_threshold) {
        score -= config_.very_long_doc_penalty;
    }

    return score;
}

double StaticRankScorer::scoreQuality(const DocumentFeatures &features) const {
    double score = 0.0;

    if (hasFlag(features, kHttps)) {
        score += config_.https_bonus;
    }
    if (hasFlag(features, kSawBodyTag)) {
        score += config_.saw_body_bonus;
    }
    if (hasFlag(features, kSawCloseHtmlTag)) {
        score += config_.saw_close_html_bonus;
    }
    if (hasFlag(features, kHtmlTruncated)) {
        score -= config_.truncated_penalty;
    }
    if (hasFlag(features, kHasOpenDiscardSection)) {
        score -= config_.open_discard_penalty;
    }

    return score;
}

double StaticRankScorer::scoreUrl(const DocumentFeatures &features) const {
    double score = 0.0;

    score += config_.url_length_weight *
             inversePenalty(static_cast<double>(features.url_length), config_.url_length_scale);
    score += config_.path_length_weight *
             inversePenalty(static_cast<double>(features.path_length), config_.path_length_scale);
    score += config_.path_depth_weight *
             inversePenalty(static_cast<double>(features.path_depth), config_.path_depth_scale);
    score +=
        config_.query_param_weight *
        inversePenalty(static_cast<double>(features.query_param_count), config_.query_param_scale);
    score += config_.numeric_path_weight *
             inversePenalty(static_cast<double>(features.numeric_path_char_count),
                            config_.numeric_path_scale);
    score += config_.domain_hyphen_weight *
             inversePenalty(static_cast<double>(features.domain_hyphen_count),
                            config_.domain_hyphen_scale);

    if (features.base_domain_length >= config_.min_base_domain_length &&
        features.base_domain_length <= config_.max_base_domain_length) {
        score += config_.base_domain_bonus;
    }

    score += scoreTld(features.raw_tld);
    return score;
}

double StaticRankScorer::scoreLinks(const DocumentFeatures &features) const {
    double score = 0.0;

    score += config_.outgoing_link_weight *
             saturatingRatio(static_cast<double>(features.outgoing_link_count),
                             config_.outgoing_link_pivot);
    score += config_.outgoing_anchor_weight *
             saturatingRatio(static_cast<double>(features.outgoing_anchor_word_count),
                             config_.outgoing_anchor_pivot);
    return score;
}

double StaticRankScorer::scoreTld(const ::string &raw_tld) const {
    if (raw_tld == "gov") {
        return config_.gov_tld_bonus;
    }
    if (raw_tld == "edu") {
        return config_.edu_tld_bonus;
    }
    if (raw_tld == "org") {
        return config_.org_tld_bonus;
    }
    if (raw_tld == "com") {
        return config_.com_tld_bonus;
    }
    if (raw_tld == "net") {
        return config_.net_tld_bonus;
    }
    return 0.0;
}
