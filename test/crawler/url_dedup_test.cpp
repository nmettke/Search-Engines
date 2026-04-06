#include "crawler/url_dedup.h"
#include <gtest/gtest.h>

TEST(UrlDedupNormalizeTest, NormalizesSchemeAndHostAndStripsFragment) {
    string normalized = normalizeUrl(" HTTPS://Example.COM/path?q=1#section ");
    EXPECT_STREQ(normalized.cstr(), "https://example.com/path?q=1");
}

TEST(UrlDedupNormalizeTest, RejectsInvalidOrEmptyInput) {
    EXPECT_STREQ(normalizeUrl("ftp://example.com/path").cstr(), "");
    EXPECT_STREQ(normalizeUrl("example.com/path").cstr(), "");
    EXPECT_STREQ(normalizeUrl("   ").cstr(), "");
}

TEST(UrlDedupShouldEnqueueTest, AcceptsFirstUrlAndWritesCanonical) {
    UrlBloomFilter bloom(1000, 0.01);
    string canonical;

    bool inserted = shouldEnqueueUrl("https://example.com/path", bloom, canonical);

    EXPECT_TRUE(inserted);
    EXPECT_STREQ(canonical.cstr(), "https://example.com/path");
}

TEST(UrlDedupShouldEnqueueTest, RejectsDuplicateAcrossEquivalentForms) {
    UrlBloomFilter bloom(1000, 0.01);
    string canonical;

    EXPECT_TRUE(shouldEnqueueUrl("https://example.com/path", bloom, canonical));
    EXPECT_FALSE(shouldEnqueueUrl("HTTPS://Example.COM/path", bloom, canonical));
    EXPECT_FALSE(shouldEnqueueUrl("https://example.com/path#section", bloom, canonical));
}

TEST(UrlDedupShouldEnqueueTest, RejectsInvalidUrls) {
    UrlBloomFilter bloom(1000, 0.01);
    string canonical;

    EXPECT_FALSE(shouldEnqueueUrl("ftp://example.com/path", bloom, canonical));
    EXPECT_FALSE(shouldEnqueueUrl("   ", bloom, canonical));
}

TEST(UrlDedupBloomFilterTest, ContainsAfterInsert) {
    UrlBloomFilter bloom(1000, 0.01);
    string canonical;

    EXPECT_TRUE(shouldEnqueueUrl("https://example.com/other", bloom, canonical));
    EXPECT_TRUE(bloom.probablyContains("https://example.com/other"));
    EXPECT_FALSE(bloom.probablyContains(""));
}
