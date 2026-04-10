#include "index/src/lib/url_features.h"
#include <gtest/gtest.h>

TEST(UrlTldScoreTest, EduDomain) { EXPECT_DOUBLE_EQ(urlTldScore("https://mit.edu/research"), 1.0); }

TEST(UrlTldScoreTest, ComDomain) { EXPECT_DOUBLE_EQ(urlTldScore("https://example.com"), 0.75); }

TEST(UrlTldScoreTest, UnknownTld) { EXPECT_DOUBLE_EQ(urlTldScore("https://example.xyz"), 0.15); }

TEST(UrlPathDepthScoreTest, DepthThree) {
    double score = urlPathDepthScore("https://example.com/a/b/c");
    EXPECT_GT(score, 0.0);
    EXPECT_LT(score, 1.0);
}

TEST(UrlQueryParamScoreTest, ThreeParams) {
    EXPECT_NEAR(urlQueryParamScore("https://example.com/page?a=1&b=2&c=3"), 0.55, 0.001);
}

TEST(UrlNumericDensityScoreTest, RootOnly) {
    EXPECT_DOUBLE_EQ(urlNumericDensityScore("https://example.com"), 1.0);
}

TEST(UrlParseTest, HostQueryWithoutPathSeparatesHostAndPath) {
    ParsedUrl parsed = parseUrl("https://example.com?x=1&y=2");
    EXPECT_EQ(parsed.host, "example.com");
    EXPECT_EQ(parsed.path, "?x=1&y=2");
}
