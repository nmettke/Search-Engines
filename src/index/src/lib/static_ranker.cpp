#include "static_ranker.h"
#include "content_features.h"
#include "url_features.h"

namespace {
constexpr double weights[] = {1.5, 2.0, 1.0, 0.5, 2.5, 2.0, 1.0, 0.5};

bool hasPersistedFeatures(const DocumentRecord &doc) {
    return (doc.features.flags & kFeaturesPresent) != 0;
}
}

void computeStaticFeatures(const DocumentRecord &doc, double *features) {
    if (hasPersistedFeatures(doc)) {
        features[FEATURE_TLD] = urlTldScoreForTld(doc.features.raw_tld);
        features[FEATURE_PATH_DEPTH] = urlPathDepthScore(doc.features.path_depth);
        features[FEATURE_URL_LENGTH] = urlLengthScore(doc.features.url_length);
        features[FEATURE_HTTPS] = urlHttpsScore((doc.features.flags & kHttps) != 0);
        features[FEATURE_WORD_COUNT] = contentWordCountScore(doc.word_count);
        features[FEATURE_TITLE] = contentTitleScore(doc.title_word_count);
        features[FEATURE_QUERY_PARAM] = urlQueryParamScore(doc.features.query_param_count);
        features[FEATURE_NUMERIC_DENSITY] = urlNumericDensityScore(
            doc.features.numeric_path_char_count, doc.features.path_length);
        return;
    }

    ParsedUrl parsed = parseUrl(doc.url);
    features[FEATURE_TLD] = urlTldScore(parsed);
    features[FEATURE_PATH_DEPTH] = urlPathDepthScore(parsed);
    features[FEATURE_URL_LENGTH] = urlLengthScore(doc.url);
    features[FEATURE_HTTPS] = urlHttpsScore(doc.url);
    features[FEATURE_WORD_COUNT] = contentWordCountScore(doc.word_count);
    features[FEATURE_TITLE] = contentTitleScore(doc.title_word_count);
    features[FEATURE_QUERY_PARAM] = urlQueryParamScore(doc.url);
    features[FEATURE_NUMERIC_DENSITY] = urlNumericDensityScore(parsed);
}

double computeStaticScore(const double *features, size_t num_features) {
    double score = 0.0;
    for (size_t i = 0; i < num_features; ++i) {
        score += weights[i] * features[i];
    }
    return score;
}

double computeStaticScore(const DocumentRecord &doc) {
    double features[NUM_STATIC_FEATURES] = {};
    computeStaticFeatures(doc, features);
    return computeStaticScore(features, NUM_STATIC_FEATURES);
}

RankedDocument rankDocument(const DocumentRecord &doc) {
    RankedDocument ranked;
    ranked.document = doc;
    ranked.static_score = computeStaticScore(doc);
    ranked.query_score = 0.0;
    ranked.total_score = ranked.static_score;
    return ranked;
}
