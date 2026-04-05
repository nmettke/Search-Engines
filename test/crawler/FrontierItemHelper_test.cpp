#include "crawler/FrontierItemHelper.h"
#include <gtest/gtest.h>

// -------------------------
// stringToSuffix tests
// -------------------------

TEST(FrontierItemHelperStringToSuffixTest, KnownSuffixes) {
    EXPECT_EQ(stringToSuffix("com"), Suffix::COM);
    EXPECT_EQ(stringToSuffix("edu"), Suffix::EDU);
    EXPECT_EQ(stringToSuffix("gov"), Suffix::GOV);
    EXPECT_EQ(stringToSuffix("org"), Suffix::ORG);
    EXPECT_EQ(stringToSuffix("net"), Suffix::NET);
    EXPECT_EQ(stringToSuffix("mil"), Suffix::MIL);
    EXPECT_EQ(stringToSuffix("int"), Suffix::INT);
}

TEST(FrontierItemHelperStringToSuffixTest, TechTLDs) {
    EXPECT_EQ(stringToSuffix("io"), Suffix::IO);
    EXPECT_EQ(stringToSuffix("dev"), Suffix::DEV);
    EXPECT_EQ(stringToSuffix("app"), Suffix::APP);
}

TEST(FrontierItemHelperStringToSuffixTest, CountryCodeTLDs) {
    EXPECT_EQ(stringToSuffix("uk"), Suffix::CCTLD);
    EXPECT_EQ(stringToSuffix("de"), Suffix::CCTLD);
    EXPECT_EQ(stringToSuffix("fr"), Suffix::CCTLD);
    EXPECT_EQ(stringToSuffix("jp"), Suffix::CCTLD);
    EXPECT_EQ(stringToSuffix("ru"), Suffix::CCTLD);
    EXPECT_EQ(stringToSuffix("br"), Suffix::CCTLD);
    EXPECT_EQ(stringToSuffix("cn"), Suffix::CCTLD);
    EXPECT_EQ(stringToSuffix("au"), Suffix::CCTLD);
    EXPECT_EQ(stringToSuffix("ca"), Suffix::CCTLD);
}

TEST(FrontierItemHelperStringToSuffixTest, UnknownLongSuffixFallsBackToOther) {
    EXPECT_EQ(stringToSuffix("xyz"), Suffix::OTHER);
    EXPECT_EQ(stringToSuffix("info"), Suffix::OTHER);
    EXPECT_EQ(stringToSuffix(""), Suffix::OTHER);
}

// -------------------------
// suffixScore tests
// -------------------------

TEST(FrontierItemHelperSuffixScoreTest, ReturnsExpectedScores) {
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::EDU), 4.0);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::GOV), 4.0);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::ORG), 3.0);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::COM), 3.0);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::NET), 2.5);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::MIL), 2.5);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::INT), 2.5);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::IO), 2.0);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::DEV), 2.0);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::APP), 2.0);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::CCTLD), 2.0);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::OTHER), 1.0);
}

// -------------------------
// baseLengthScore tests
// -------------------------

TEST(FrontierItemHelperBaseLengthScoreTest, VeryShortIsPenalized) {
    EXPECT_DOUBLE_EQ(baseLengthScore(0), -1.0);
    EXPECT_DOUBLE_EQ(baseLengthScore(1), -1.0);
    EXPECT_DOUBLE_EQ(baseLengthScore(2), -1.0);
}

TEST(FrontierItemHelperBaseLengthScoreTest, ShortIsSlightlyPenalized) {
    EXPECT_DOUBLE_EQ(baseLengthScore(3), -0.3);
    EXPECT_DOUBLE_EQ(baseLengthScore(4), -0.3);
}

TEST(FrontierItemHelperBaseLengthScoreTest, SweetSpotEarnsBonus) {
    EXPECT_DOUBLE_EQ(baseLengthScore(5), 0.5);
    EXPECT_DOUBLE_EQ(baseLengthScore(10), 0.5);
    EXPECT_DOUBLE_EQ(baseLengthScore(15), 0.5);
}

TEST(FrontierItemHelperBaseLengthScoreTest, LongIsNeutral) {
    EXPECT_DOUBLE_EQ(baseLengthScore(16), 0.0);
    EXPECT_DOUBLE_EQ(baseLengthScore(25), 0.0);
}

TEST(FrontierItemHelperBaseLengthScoreTest, VeryLongIsPenalized) {
    EXPECT_DOUBLE_EQ(baseLengthScore(26), -0.5);
    EXPECT_DOUBLE_EQ(baseLengthScore(30), -0.5);
}

// -------------------------
// extractRest tests
// -------------------------

TEST(FrontierItemHelperExtractRestTest, RemovesHttpsScheme) {
    string rest;
    extractRest("https://www.google.com/maps", rest);

    EXPECT_STREQ(rest.cstr(), "www.google.com/maps");
}

TEST(FrontierItemHelperExtractRestTest, RemovesHttpScheme) {
    string rest;
    extractRest("http://example.org/page", rest);

    EXPECT_STREQ(rest.cstr(), "example.org/page");
}

TEST(FrontierItemHelperExtractRestTest, LeavesStringUnchangedWhenNoScheme) {
    string rest;
    extractRest("umich.edu/academics", rest);

    EXPECT_STREQ(rest.cstr(), "umich.edu/academics");
}

TEST(FrontierItemHelperExtractRestTest, HostOnlyUrlWithoutScheme) {
    string rest;
    extractRest("google.com", rest);

    EXPECT_STREQ(rest.cstr(), "google.com");
}

// -------------------------
// splitHostAndPath tests
// -------------------------

TEST(FrontierItemHelperSplitHostAndPathTest, SplitsHostAndPath) {
    string host;
    string path;

    splitHostAndPath("www.google.com/maps/about", host, path);

    EXPECT_STREQ(host.cstr(), "www.google.com");
    EXPECT_STREQ(path.cstr(), "/maps/about");
}

TEST(FrontierItemHelperSplitHostAndPathTest, HostOnlyHasEmptyPath) {
    string host;
    string path;

    splitHostAndPath("www.google.com", host, path);

    EXPECT_STREQ(host.cstr(), "www.google.com");
    EXPECT_STREQ(path.cstr(), "");
}

TEST(FrontierItemHelperSplitHostAndPathTest, RootPathIsCaptured) {
    string host;
    string path;

    splitHostAndPath("www.google.com/", host, path);

    EXPECT_STREQ(host.cstr(), "www.google.com");
    EXPECT_STREQ(path.cstr(), "/");
}

// -------------------------
// parseHost tests
// -------------------------

TEST(FrontierItemHelperParseHostTest, EmptyHostGivesOtherAndZeroLength) {
    Suffix suffix = Suffix::COM;
    size_t baseLength = 999;

    parseHost("", suffix, baseLength);

    EXPECT_EQ(suffix, Suffix::OTHER);
    EXPECT_EQ(baseLength, 0);
}

TEST(FrontierItemHelperParseHostTest, HostWithoutDotUsesWholeHostAsBaseLength) {
    Suffix suffix = Suffix::COM;
    size_t baseLength = 0;

    parseHost("localhost", suffix, baseLength);

    EXPECT_EQ(suffix, Suffix::OTHER);
    EXPECT_EQ(baseLength, 9);
}

TEST(FrontierItemHelperParseHostTest, SimpleDomainParsesCorrectly) {
    Suffix suffix = Suffix::OTHER;
    size_t baseLength = 0;

    parseHost("google.com", suffix, baseLength);

    EXPECT_EQ(suffix, Suffix::COM);
    EXPECT_EQ(baseLength, 6);
}

TEST(FrontierItemHelperParseHostTest, SubdomainParsesCorrectly) {
    Suffix suffix = Suffix::OTHER;
    size_t baseLength = 0;

    parseHost("www.google.com", suffix, baseLength);

    EXPECT_EQ(suffix, Suffix::COM);
    EXPECT_EQ(baseLength, 6);
}

TEST(FrontierItemHelperParseHostTest, EduDomainParsesCorrectly) {
    Suffix suffix = Suffix::OTHER;
    size_t baseLength = 0;

    parseHost("cs.umich.edu", suffix, baseLength);

    EXPECT_EQ(suffix, Suffix::EDU);
    EXPECT_EQ(baseLength, 5);
}

TEST(FrontierItemHelperParseHostTest, IoDomainParsesCorrectly) {
    Suffix suffix = Suffix::OTHER;
    size_t baseLength = 0;

    parseHost("github.io", suffix, baseLength);

    EXPECT_EQ(suffix, Suffix::IO);
    EXPECT_EQ(baseLength, 6);
}

TEST(FrontierItemHelperParseHostTest, CcTldParsesCorrectly) {
    Suffix suffix = Suffix::OTHER;
    size_t baseLength = 0;

    parseHost("bbc.co.uk", suffix, baseLength);

    EXPECT_EQ(suffix, Suffix::CCTLD);
    // secondLastDot is at index of '.', so baseLength = "co" = 2
    EXPECT_EQ(baseLength, 2);
}

TEST(FrontierItemHelperParseHostTest, UnknownTldParsesAsOther) {
    Suffix suffix = Suffix::COM;
    size_t baseLength = 0;

    parseHost("startup.xyz", suffix, baseLength);

    EXPECT_EQ(suffix, Suffix::OTHER);
    EXPECT_EQ(baseLength, 7);
}

// -------------------------
// computePathDepth tests
// -------------------------

TEST(FrontierItemHelperComputePathDepthTest, EmptyPathHasDepthZero) {
    EXPECT_EQ(computePathDepth(""), 0);
}

TEST(FrontierItemHelperComputePathDepthTest, RootPathHasDepthZero) {
    EXPECT_EQ(computePathDepth("/"), 0);
}

TEST(FrontierItemHelperComputePathDepthTest, SingleSegmentHasDepthOne) {
    EXPECT_EQ(computePathDepth("/about"), 1);
}

TEST(FrontierItemHelperComputePathDepthTest, SingleSegmentTrailingSlashStillHasDepthOne) {
    EXPECT_EQ(computePathDepth("/about/"), 1);
}

TEST(FrontierItemHelperComputePathDepthTest, TwoSegmentsHasDepthTwo) {
    EXPECT_EQ(computePathDepth("/maps/about"), 2);
}

TEST(FrontierItemHelperComputePathDepthTest, TwoSegmentsTrailingSlashStillHasDepthTwo) {
    EXPECT_EQ(computePathDepth("/maps/about/"), 2);
}

// -------------------------
// countQueryParams tests
// -------------------------

TEST(FrontierItemHelperCountQueryParamsTest, NoQueryStringReturnsZero) {
    EXPECT_EQ(countQueryParams("/about"), 0);
    EXPECT_EQ(countQueryParams(""), 0);
    EXPECT_EQ(countQueryParams("/"), 0);
}

TEST(FrontierItemHelperCountQueryParamsTest, SingleParamReturnsOne) {
    EXPECT_EQ(countQueryParams("/search?q=hello"), 1);
}

TEST(FrontierItemHelperCountQueryParamsTest, MultipleParamsCountedCorrectly) {
    EXPECT_EQ(countQueryParams("/page?a=1&b=2&c=3"), 3);
}

TEST(FrontierItemHelperCountQueryParamsTest, QueryStringOnlyNoPath) {
    EXPECT_EQ(countQueryParams("?key=value"), 1);
}

TEST(FrontierItemHelperCountQueryParamsTest, EmptyQueryStringReturnsZero) {
    EXPECT_EQ(countQueryParams("/path?"), 0);
    EXPECT_EQ(countQueryParams("?"), 0);
}

// -------------------------
// isLowValuePath tests
// -------------------------

TEST(FrontierItemHelperIsLowValuePathTest, CleanPathReturnsFalse) {
    EXPECT_FALSE(isLowValuePath("/about"));
    EXPECT_FALSE(isLowValuePath("/news/2024/article"));
    EXPECT_FALSE(isLowValuePath(""));
}

TEST(FrontierItemHelperIsLowValuePathTest, AuthPathsReturnTrue) {
    EXPECT_TRUE(isLowValuePath("/login"));
    EXPECT_TRUE(isLowValuePath("/logout"));
    EXPECT_TRUE(isLowValuePath("/signup"));
    EXPECT_TRUE(isLowValuePath("/register"));
    EXPECT_TRUE(isLowValuePath("/signin"));
    EXPECT_TRUE(isLowValuePath("/signout"));
}

TEST(FrontierItemHelperIsLowValuePathTest, FeedPathsReturnTrue) {
    EXPECT_TRUE(isLowValuePath("/rss"));
    EXPECT_TRUE(isLowValuePath("/feed"));
}

TEST(FrontierItemHelperIsLowValuePathTest, ApiPathReturnsTrue) {
    EXPECT_TRUE(isLowValuePath("/api/v1/users"));
}

TEST(FrontierItemHelperIsLowValuePathTest, WordPressInfraPathsReturnTrue) {
    EXPECT_TRUE(isLowValuePath("/wp-admin/options.php"));
    EXPECT_TRUE(isLowValuePath("/wp-login.php"));
    EXPECT_TRUE(isLowValuePath("/wp-includes/js/jquery.js"));
    EXPECT_TRUE(isLowValuePath("/cgi-bin/search"));
}

TEST(FrontierItemHelperIsLowValuePathTest, TransactionalPathsReturnTrue) {
    EXPECT_TRUE(isLowValuePath("/cart"));
    EXPECT_TRUE(isLowValuePath("/checkout"));
    EXPECT_TRUE(isLowValuePath("/unsubscribe"));
    EXPECT_TRUE(isLowValuePath("/print"));
}

TEST(FrontierItemHelperIsLowValuePathTest, QueryStringContainingPatternDoesNotFlag) {
    EXPECT_FALSE(isLowValuePath("/page?next=login"));
    EXPECT_FALSE(isLowValuePath("/search?redirect=signup&ref=home"));
}

// -------------------------
// Integration tests
// -------------------------

TEST(FrontierItemHelperIntegrationTest, FullParsingPipelineForTypicalUrl) {
    string rest;
    string host;
    string path;
    Suffix suffix = Suffix::OTHER;
    size_t baseLength = 0;

    extractRest("https://www.google.com/maps/about/", rest);
    splitHostAndPath(rest, host, path);
    parseHost(host, suffix, baseLength);

    EXPECT_STREQ(rest.cstr(), "www.google.com/maps/about/");
    EXPECT_STREQ(host.cstr(), "www.google.com");
    EXPECT_STREQ(path.cstr(), "/maps/about/");
    EXPECT_EQ(suffix, Suffix::COM);
    EXPECT_EQ(baseLength, 6);
    // trailing slash stripped: "/maps/about" has depth 2
    EXPECT_EQ(computePathDepth(path), 2);
}

TEST(FrontierItemHelperIntegrationTest, FullParsingPipelineForHostOnlyUrl) {
    string rest;
    string host;
    string path;
    Suffix suffix = Suffix::OTHER;
    size_t baseLength = 0;

    extractRest("https://umich.edu", rest);
    splitHostAndPath(rest, host, path);
    parseHost(host, suffix, baseLength);

    EXPECT_STREQ(rest.cstr(), "umich.edu");
    EXPECT_STREQ(host.cstr(), "umich.edu");
    EXPECT_STREQ(path.cstr(), "");
    EXPECT_EQ(suffix, Suffix::EDU);
    EXPECT_EQ(baseLength, 5);
    EXPECT_EQ(computePathDepth(path), 0);
}