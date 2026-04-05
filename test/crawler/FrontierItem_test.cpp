#include "crawler/FrontierItem.h"
#include <cmath>
#include <gtest/gtest.h>

// seedDistanceWeight = 2.0 (matches FrontierItem.cpp)
// Each hop from seed costs 2.0 * log2(1 + distance).
// Distance 1: 2.0 * log2(2) = 2.0
// Distance 2: 2.0 * log2(3) ~= 3.17

// -------------------------
// Existing tests (updated for new scoring)
// -------------------------

TEST(FrontierItemTest, SeedHasDistanceZeroAndChildHasDistanceOne) {
    FrontierItem seed(string("https://umich.edu"));
    // Same URL so TLD/baseLength/pathDepth/queryParams/lowValuePath are identical.
    // Only seedDistance differs: seed=0, child=1.
    // Gap = seedDistanceWeight * (log2(2) - log2(1)) = 2.0 * 1.0 = 2.0
    FrontierItem child(string("https://umich.edu"), seed);

    EXPECT_DOUBLE_EQ(seed.getScore() - child.getScore(), 2.0);
}

TEST(FrontierItemTest, SeedDistanceAccumulatesAcrossThreeGenerations) {
    FrontierItem seed(string("https://umich.edu"));
    FrontierItem child(string("https://umich.edu"), seed);
    FrontierItem grandchild(string("https://umich.edu"), child);

    // seed - child = 2.0 * log2(2) = 2.0
    EXPECT_DOUBLE_EQ(seed.getScore() - child.getScore(), 2.0);

    // seed - grandchild = 2.0 * log2(3)
    EXPECT_DOUBLE_EQ(seed.getScore() - grandchild.getScore(), 2.0 * log2(3.0));
}

TEST(FrontierItemTest, ChildScoreIsLowerThanSeedScore) {
    FrontierItem seed(string("https://umich.edu"));
    FrontierItem child(string("https://umich.edu/about"), seed);

    EXPECT_GT(seed.getScore(), child.getScore());
}

TEST(FrontierItemTest, LinkFieldIsPreservedByParentConstructor) {
    FrontierItem seed(string("https://umich.edu"));
    FrontierItem child(string("https://cs.umich.edu"), seed);

    EXPECT_STREQ(child.link.cstr(), "https://cs.umich.edu");
}

TEST(FrontierItemTest, BrokenParentPassesBrokenPenaltyToChild) {
    FrontierItem cleanSeed(string("https://umich.edu"));
    FrontierItem brokenSeed(string("https://umich.edu"));
    brokenSeed.markBroken();

    FrontierItem childOfClean(string("https://umich.edu/about"), cleanSeed);
    FrontierItem childOfBroken(string("https://umich.edu/about"), brokenSeed);

    // brokenSourceWeight = 3.0 — same URL, same path, same distance, only broken flag differs
    EXPECT_DOUBLE_EQ(childOfClean.getScore() - childOfBroken.getScore(), 3.0);
}

TEST(FrontierItemTest, CheckpointRoundTripPreservesScore) {
    FrontierItem seed(string("https://umich.edu"));
    FrontierItem child(string("https://umich.edu/about"), seed);

    string line = child.serializeToLine();
    FrontierItem restored = FrontierItem::deserializeFromLine(line);

    EXPECT_DOUBLE_EQ(child.getScore(), restored.getScore());
    EXPECT_STREQ(child.link.cstr(), restored.link.cstr());
}

TEST(FrontierItemTest, StringOnlyConstructorAlwaysHasDistanceZero) {
    FrontierItem a(string("https://umich.edu"));
    FrontierItem b(string("https://google.com"));

    // umich.edu: EDU(4.0) + baseLengthScore(5)=0.5 + sqrt(0) + 2.0*log2(1) = 4.5
    // google.com: COM(3.0) + baseLengthScore(6)=0.5 + 0 + 0 = 3.5
    EXPECT_DOUBLE_EQ(a.getScore(), 4.5);
    EXPECT_DOUBLE_EQ(b.getScore(), 3.5);
}

// -------------------------
// Ranking comparison tests
// -------------------------

TEST(FrontierItemTest, EduBeatsComForSameDepth) {
    FrontierItem edu(string("https://umich.edu"));
    FrontierItem com(string("https://google.com"));

    EXPECT_GT(edu.getScore(), com.getScore());
}

TEST(FrontierItemTest, ComBeatsUnknownTld) {
    FrontierItem com(string("https://google.com"));
    FrontierItem unknown(string("https://example.xyz"));

    // COM(3.0)+0.5=3.5 vs OTHER(1.0)+baseLengthScore(7)=0.5 => 1.5
    EXPECT_GT(com.getScore(), unknown.getScore());
}

TEST(FrontierItemTest, IoTldGetsCredit) {
    FrontierItem ioSite(string("https://github.io"));
    FrontierItem unknownSite(string("https://somesite.xyz"));

    // github.io: IO(2.0) + baseLengthScore(6)=0.5 = 2.5
    // somesite.xyz: OTHER(1.0) + baseLengthScore(8)=0.5 = 1.5
    EXPECT_GT(ioSite.getScore(), unknownSite.getScore());
}

TEST(FrontierItemTest, CcTldGetsCredit) {
    // bbc.de: CCTLD(2.0) + baseLengthScore(3)=-0.3 = 1.7
    // bbc.xyz: OTHER(1.0) + baseLengthScore(3)=-0.3 = 0.7
    // Same domain name, only TLD differs — ccTLD scores higher.
    FrontierItem cctld(string("https://bbc.de"));
    FrontierItem other(string("https://bbc.xyz"));

    EXPECT_GT(cctld.getScore(), other.getScore());
}

TEST(FrontierItemTest, LongDomainNameBeatsVeryShortDomainName) {
    // "stackoverflow.com": baseLength=13, in [5,15] => +0.5 bonus
    // "g.com": baseLength=1, < 3 => -1.0 penalty
    FrontierItem longDomain(string("https://stackoverflow.com"));
    FrontierItem shortDomain(string("https://g.com"));

    EXPECT_GT(longDomain.getScore(), shortDomain.getScore());
}

TEST(FrontierItemTest, ShallowPathBeatsDeepPath) {
    FrontierItem shallow(string("https://example.com/about"));
    FrontierItem deep(string("https://example.com/a/b/c/d/e"));

    EXPECT_GT(shallow.getScore(), deep.getScore());
}

TEST(FrontierItemTest, CleanPathBeatsQueryHeavyPath) {
    FrontierItem clean(string("https://example.com/page"));
    FrontierItem queryHeavy(string("https://example.com/page?a=1&b=2&c=3"));

    // 3 query params => -0.5 * 3 = -1.5 penalty
    EXPECT_GT(clean.getScore(), queryHeavy.getScore());
}

TEST(FrontierItemTest, ShortUrlBeatsLongUrl) {
    // Build a URL > 100 chars to trigger the URL length penalty
    // "https://example.com/" = 20 chars; pad path to 90 more chars
    string longUrl("https://example.com/");
    for (int i = 0; i < 90; ++i) {
        longUrl.pushBack('a');
    }
    // longUrl.size() == 110, excess = 10, penalty = 0.02 * 10 = 0.2

    FrontierItem shortItem(string("https://example.com/about"));
    FrontierItem longItem(longUrl);

    EXPECT_GT(shortItem.getScore(), longItem.getScore());
}

TEST(FrontierItemTest, CleanPathBeatsLowValuePath) {
    FrontierItem clean(string("https://example.com/about"));
    FrontierItem lowValue(string("https://example.com/login"));

    // lowValuePath adds -3.0 penalty
    EXPECT_GT(clean.getScore(), lowValue.getScore());
}

TEST(FrontierItemTest, TrailingSlashEquivalence) {
    // "https://example.com" and "https://example.com/" should have equal scores:
    // Both parse to pathDepth=0 after the trailing slash fix.
    FrontierItem noSlash(string("https://example.com"));
    FrontierItem withSlash(string("https://example.com/"));

    EXPECT_DOUBLE_EQ(noSlash.getScore(), withSlash.getScore());
}

TEST(FrontierItemTest, CloserSeedBeatsMoreDistantSeed) {
    FrontierItem seed(string("https://umich.edu"));
    FrontierItem child(string("https://umich.edu/about"), seed);
    FrontierItem grandchild(string("https://umich.edu/about/more"), child);

    EXPECT_GT(seed.getScore(), child.getScore());
    EXPECT_GT(child.getScore(), grandchild.getScore());
}

TEST(FrontierItemTest, CheckpointRoundTripPreservesNewFields) {
    // Use a URL with query params and low-value path to exercise new fields
    FrontierItem seed(string("https://umich.edu"));
    FrontierItem item(string("https://umich.edu/login?redirect=home&token=abc"), seed);

    string line = item.serializeToLine();
    FrontierItem restored = FrontierItem::deserializeFromLine(line);

    EXPECT_DOUBLE_EQ(item.getScore(), restored.getScore());
    EXPECT_STREQ(item.link.cstr(), restored.link.cstr());
}
