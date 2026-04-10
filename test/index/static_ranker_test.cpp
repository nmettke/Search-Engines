#include "index/src/lib/static_ranker.h"
#include <gtest/gtest.h>

static double scoreFromRaw(const char *url, uint32_t wc, uint16_t twc) {
    DocumentRecord doc;
    doc.url = string(url);
    doc.word_count = wc;
    doc.title_word_count = twc;
    return computeStaticScore(doc);
}

TEST(StaticRankerTest, HighQualityDocScoresHigherThanLowQuality) {
    double high = scoreFromRaw("https://mit.edu/research", 500, 5);
    double low = scoreFromRaw("http://example.xyz/a/b/c/d/e/f/g/h?x=1&y=2&z=3", 10, 0);
    EXPECT_GT(high, low);
}

TEST(StaticRankerTest, FeatureArrayPopulatedCorrectly) {
    DocumentRecord doc;
    doc.url = "https://mit.edu/research";
    doc.word_count = 500;
    doc.title_word_count = 5;

    double features[NUM_STATIC_FEATURES] = {};
    computeStaticFeatures(doc, features);

    EXPECT_DOUBLE_EQ(features[FEATURE_TLD], 1.0);
    EXPECT_DOUBLE_EQ(features[FEATURE_HTTPS], 1.0);
    EXPECT_DOUBLE_EQ(features[FEATURE_WORD_COUNT], 1.0);
    EXPECT_DOUBLE_EQ(features[FEATURE_TITLE], 1.0);
    EXPECT_DOUBLE_EQ(features[FEATURE_QUERY_PARAM], 1.0);
}

TEST(StaticRankerTest, RankedDocumentUsesStaticScoreAsTotalScore) {
    DocumentRecord doc;
    doc.url = "https://example.com/page";
    doc.word_count = 300;
    doc.title_word_count = 7;

    RankedDocument ranked = rankDocument(doc);
    EXPECT_DOUBLE_EQ(ranked.static_score, ranked.total_score);
    EXPECT_DOUBLE_EQ(ranked.query_score, 0.0);
}

TEST(StaticRankerTest, PersistedRawTldDrivesTldScoreWithoutReparsingUrl) {
    DocumentRecord doc;
    doc.url = "https://example.xyz/path";
    doc.word_count = 300;
    doc.title_word_count = 7;
    doc.features.flags = kFeaturesPresent | kHttps;
    doc.features.raw_tld = "edu";
    doc.features.url_length = 24;
    doc.features.path_length = 5;
    doc.features.path_depth = 1;
    doc.features.query_param_count = 0;
    doc.features.numeric_path_char_count = 0;

    double features[NUM_STATIC_FEATURES] = {};
    computeStaticFeatures(doc, features);

    EXPECT_DOUBLE_EQ(features[FEATURE_TLD], 1.0);
}
