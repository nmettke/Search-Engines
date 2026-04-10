#pragma once

#include "types.h"
#include <cstddef>

constexpr size_t NUM_STATIC_FEATURES = 8;

enum StaticFeature {
    FEATURE_TLD,
    FEATURE_PATH_DEPTH,
    FEATURE_URL_LENGTH,
    FEATURE_HTTPS,
    FEATURE_WORD_COUNT,
    FEATURE_TITLE,
    FEATURE_QUERY_PARAM,
    FEATURE_NUMERIC_DENSITY
};

void computeStaticFeatures(const DocumentRecord &doc, double *features);
double computeStaticScore(const double *features, size_t num_features);
double computeStaticScore(const DocumentRecord &doc);
RankedDocument rankDocument(const DocumentRecord &doc);
