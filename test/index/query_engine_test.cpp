#include "index/src/lib/chunk_flusher.h"
#include "index/src/lib/disk_chunk_reader.h"
#include "index/src/lib/in_memory_index.h"
#include "index/src/lib/query_engine.h"
#include "index/src/lib/static_rank.h"
#include "index/src/lib/types.h"
#include <gtest/gtest.h>
#include <unistd.h>

namespace {

DocumentFeatures make_features(uint8_t flags, uint16_t url_length, uint16_t path_length,
                               uint8_t path_depth, uint8_t query_param_count,
                               uint8_t numeric_path_char_count, uint8_t domain_hyphen_count,
                               uint16_t outgoing_link_count, uint16_t outgoing_anchor_word_count,
                               const char *raw_tld) {
    DocumentFeatures features;
    features.flags = flags;
    features.base_domain_length = 7;
    features.url_length = url_length;
    features.path_length = path_length;
    features.path_depth = path_depth;
    features.query_param_count = query_param_count;
    features.numeric_path_char_count = numeric_path_char_count;
    features.domain_hyphen_count = domain_hyphen_count;
    features.outgoing_link_count = outgoing_link_count;
    features.outgoing_anchor_word_count = outgoing_anchor_word_count;
    features.raw_tld = raw_tld;
    return features;
}

} // namespace

TEST(QueryEngineTest, StaticRankUsesStoredDocumentFeatures) {
    const char *test_file = "query_engine_static_rank.idx";
    unlink(test_file);

    InMemoryIndex mem_index;

    mem_index.addToken({"alpha", 0});
    mem_index.finishDocument(
        {1, "https://www.example.org/article", 900, 8, 0, 0,
         make_features(kFeaturesPresent | kHttps | kSawBodyTag | kSawCloseHtmlTag, 42, 8, 1, 0, 0,
                       0, 18, 72, "org")});

    mem_index.addToken({"alpha", 2});
    mem_index.finishDocument(
        {3, "https://www.example.com/deep/reference", 320, 4, 2, 2,
         make_features(kFeaturesPresent | kHttps | kSawBodyTag | kSawCloseHtmlTag, 63, 21, 2, 1, 1,
                       0, 8, 28, "com")});

    mem_index.addToken({"alpha", 4});
    mem_index.finishDocument(
        {5, "http://spam-example.com/archive/2024/09/17/report?id=9&view=full&session=abc", 35, 0,
         4, 6,
         make_features(kFeaturesPresent | kHtmlTruncated | kHasOpenDiscardSection, 96, 47, 4, 3, 8,
                       1, 1, 2, "com")});

    flushIndexChunk(mem_index, test_file);

    DiskChunkReader reader;
    ASSERT_TRUE(reader.open(test_file));

    QueryEngine engine(reader);
    auto results = engine.search("alpha", 3);

    ASSERT_EQ(results.size(), 3u);

    auto best = reader.getDocument(results[0].doc_id);
    auto middle = reader.getDocument(results[1].doc_id);
    auto worst = reader.getDocument(results[2].doc_id);

    ASSERT_TRUE(best.has_value());
    ASSERT_TRUE(middle.has_value());
    ASSERT_TRUE(worst.has_value());

    EXPECT_EQ(best->url, "https://www.example.org/article");
    EXPECT_EQ(middle->url, "https://www.example.com/deep/reference");
    EXPECT_EQ(worst->url,
              "http://spam-example.com/archive/2024/09/17/report?id=9&view=full&session=abc");

    EXPECT_GT(results[0].score, results[1].score);
    EXPECT_GT(results[1].score, results[2].score);

    unlink(test_file);
}

TEST(QueryEngineTest, StaticRankConfigCanFlipOrdering) {
    DocumentRecord long_doc;
    long_doc.word_count = 1200;
    long_doc.title_word_count = 2;
    long_doc.seed_distance = 4;

    DocumentRecord near_seed_doc;
    near_seed_doc.word_count = 120;
    near_seed_doc.title_word_count = 2;
    near_seed_doc.seed_distance = 0;

    StaticRankScorer default_scorer;
    EXPECT_GT(default_scorer.score(near_seed_doc), default_scorer.score(long_doc));

    StaticRankConfig content_heavy_config;
    content_heavy_config.seed_distance_weight = 0.10;
    content_heavy_config.word_count_weight = 6.0;

    StaticRankScorer content_heavy_scorer(content_heavy_config);
    EXPECT_GT(content_heavy_scorer.score(long_doc), content_heavy_scorer.score(near_seed_doc));
}

TEST(QueryEngineTest, MissingFeatureFlagFallsBackToNonFeatureSignals) {
    DocumentRecord plain_doc;
    plain_doc.word_count = 300;
    plain_doc.title_word_count = 5;
    plain_doc.seed_distance = 1;
    plain_doc.features.flags = 0;
    plain_doc.features.url_length = 500;
    plain_doc.features.path_depth = 10;
    plain_doc.features.query_param_count = 8;
    plain_doc.features.outgoing_link_count = 999;

    DocumentRecord identical_plain_doc = plain_doc;
    identical_plain_doc.features.flags = kFeaturesPresent | kHttps | kSawBodyTag | kSawCloseHtmlTag;
    identical_plain_doc.features.url_length = 40;
    identical_plain_doc.features.path_depth = 1;
    identical_plain_doc.features.query_param_count = 0;
    identical_plain_doc.features.outgoing_link_count = 20;
    identical_plain_doc.features.outgoing_anchor_word_count = 70;
    identical_plain_doc.features.raw_tld = "org";

    StaticRankScorer scorer;
    double plain_score = scorer.score(plain_doc);
    double feature_score = scorer.score(identical_plain_doc);

    EXPECT_LT(plain_score, feature_score);

    DocumentRecord same_non_feature_signals = plain_doc;
    same_non_feature_signals.features.url_length = 12;
    same_non_feature_signals.features.raw_tld = "gov";

    EXPECT_DOUBLE_EQ(plain_score, scorer.score(same_non_feature_signals));
}
