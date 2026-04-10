#include "index/src/lib/url_features.h"
#include <gtest/gtest.h>

TEST(UrlParseTest, HostQueryWithoutPathSeparatesHostAndPath) {
    ParsedUrl parsed = parseUrl("https://example.com?x=1&y=2");
    EXPECT_EQ(parsed.host, "example.com");
    EXPECT_EQ(parsed.path, "?x=1&y=2");
}

TEST(UrlParseTest, ExtractsRawFeatureInputs) {
    ParsedUrl parsed = parseUrl("https://sub-domain.example.com/a/b/c123?x=1&y=2");
    EXPECT_EQ(parsed.tld, "com");
    EXPECT_EQ(urlBaseDomainLength(parsed), 7u);
    EXPECT_EQ(urlPathLength(parsed), 17u);
    EXPECT_EQ(urlPathDepth(parsed), 3u);
    EXPECT_EQ(urlQueryParamCount(parsed), 2u);
    EXPECT_EQ(urlNumericPathCharCount(parsed), 3u);
    EXPECT_EQ(urlDomainHyphenCount(parsed), 1u);
}

TEST(UrlParseTest, DetectsHttpsPrefixWithoutAllocation) {
    EXPECT_TRUE(urlHasHttps("https://example.com"));
    EXPECT_FALSE(urlHasHttps("http://example.com"));
    EXPECT_FALSE(urlHasHttps("https:/broken"));
}
