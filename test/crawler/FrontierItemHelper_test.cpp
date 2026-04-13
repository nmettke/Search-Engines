#include "crawler/FrontierItemHelper.h"
#include <gtest/gtest.h>

TEST(FrontierItemHelperStringToSuffixTest, KnownSuffixes) {
    EXPECT_EQ(stringToSuffix("com"), Suffix::COM);
    EXPECT_EQ(stringToSuffix("edu"), Suffix::EDU);
    EXPECT_EQ(stringToSuffix("gov"), Suffix::GOV);
    EXPECT_EQ(stringToSuffix("org"), Suffix::ORG);
    EXPECT_EQ(stringToSuffix("net"), Suffix::NET);
    EXPECT_EQ(stringToSuffix("mil"), Suffix::MIL);
    EXPECT_EQ(stringToSuffix("int"), Suffix::INT);
}

TEST(FrontierItemHelperStringToSuffixTest, UnknownSuffixFallsBackToOther) {
    EXPECT_EQ(stringToSuffix("io"), Suffix::OTHER);
    EXPECT_EQ(stringToSuffix("xyz"), Suffix::OTHER);
    EXPECT_EQ(stringToSuffix(""), Suffix::OTHER);
}

TEST(FrontierItemHelperSuffixScoreTest, ReturnsExpectedScores) {
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::EDU), 4.0);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::GOV), 4.0);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::ORG), 3.0);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::COM), 3.0);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::NET), 2.0);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::MIL), 2.0);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::INT), 2.0);
    EXPECT_DOUBLE_EQ(suffixScore(Suffix::OTHER), 0.0);
}

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

TEST(FrontierItemHelperParseHostTest, UnknownTldParsesAsOther) {
    Suffix suffix = Suffix::COM;
    size_t baseLength = 0;

    parseHost("startup.io", suffix, baseLength);

    EXPECT_EQ(suffix, Suffix::OTHER);
    EXPECT_EQ(baseLength, 7);
}

TEST(FrontierItemHelperParseHostTest, MultiPartCountryCodeShowsCurrentHeuristicBehavior) {
    Suffix suffix = Suffix::COM;
    size_t baseLength = 0;

    parseHost("www.bbc.co.uk", suffix, baseLength);

    EXPECT_EQ(suffix, Suffix::ORG);
    EXPECT_EQ(baseLength, 3);
}

TEST(FrontierItemHelperComputePathDepthTest, EmptyPathHasDepthZero) {
    EXPECT_EQ(computePathDepth(""), 0);
}

TEST(FrontierItemHelperComputePathDepthTest, RootPathHasDepthOne) {
    EXPECT_EQ(computePathDepth("/"), 1);
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