#include "index/src/lib/document_features.h"
#include <gtest/gtest.h>

TEST(DocumentFeaturesTest, SourceUrlBeatsHtmlBase) {
    HtmlParser doc;
    doc.base = "http://base.example.com/wrong";
    doc.sourceUrl = "https://crawl.example.com/right";
    doc.words = {"hello"};

    DocumentFeatures features = extractDocumentFeatures(doc);

    EXPECT_EQ(doc.documentUrl(), doc.sourceUrl);
    EXPECT_EQ(features.url_length, doc.sourceUrl.size());
    EXPECT_TRUE((features.flags & kFeaturesPresent) != 0);
    EXPECT_TRUE((features.flags & kHttps) != 0);
}

TEST(DocumentFeaturesTest, UrlStructureFieldsAreExtracted) {
    HtmlParser doc;
    doc.sourceUrl = "https://www.alpha-beta.example.com/a1/b22?x=1&y=2";
    doc.words = {"hello", "world"};

    DocumentFeatures features = extractDocumentFeatures(doc);

    EXPECT_EQ(features.raw_tld, "com");
    EXPECT_EQ(features.base_domain_length, 7u);
    EXPECT_EQ(features.url_length, doc.sourceUrl.size());
    EXPECT_EQ(features.path_length, 15u);
    EXPECT_EQ(features.path_depth, 2u);
    EXPECT_EQ(features.query_param_count, 2u);
    EXPECT_EQ(features.numeric_path_char_count, 3u);
    EXPECT_EQ(features.domain_hyphen_count, 1u);
}

TEST(DocumentFeaturesTest, AlphaCountsIncludeTitleAndBody) {
    HtmlParser doc;
    doc.words = {"Alpha"};
    doc.titleWords = {u8"東京"};

    DocumentFeatures features = extractDocumentFeatures(doc);

    EXPECT_EQ(features.latin_alpha_count, 5u);
    EXPECT_EQ(features.total_alpha_count, 7u);
}

TEST(DocumentFeaturesTest, OutgoingCountsUseLinksAndAnchorText) {
    HtmlParser doc;

    Link first("https://example.com/one");
    first.anchorText = {"alpha", "beta"};

    Link second("https://example.com/two");
    second.anchorText = {"gamma", "delta", "epsilon"};

    Link third("https://example.com/three");

    doc.links.pushBack(first);
    doc.links.pushBack(second);
    doc.links.pushBack(third);

    DocumentFeatures features = extractDocumentFeatures(doc);

    EXPECT_EQ(features.outgoing_link_count, 3u);
    EXPECT_EQ(features.outgoing_anchor_word_count, 5u);
}

TEST(DocumentFeaturesTest, FlagsReflectStructuralSignals) {
    const char *buffer = "<html><body><script>bad";
    HtmlParser doc(buffer, 23);
    doc.sourceUrl = "http://example.com";

    DocumentFeatures features = extractDocumentFeatures(doc);

    EXPECT_TRUE((features.flags & kSawBodyTag) != 0);
    EXPECT_TRUE((features.flags & kHasOpenDiscardSection) != 0);
    EXPECT_FALSE((features.flags & kSawCloseHtmlTag) != 0);
    EXPECT_FALSE((features.flags & kHtmlTruncated) != 0);
}

TEST(DocumentFeaturesTest, TruncatedMarkupSetsFlag) {
    const char *buffer = "<html><body><div";
    HtmlParser doc(buffer, 16);

    DocumentFeatures features = extractDocumentFeatures(doc);

    EXPECT_TRUE((features.flags & kSawBodyTag) != 0);
    EXPECT_TRUE((features.flags & kHtmlTruncated) != 0);
}
