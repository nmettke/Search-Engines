#include "crawler/FrontierItem.h"
#include <gtest/gtest.h>

// seedDistanceWeight = 1.0 (matches FrontierItem.cpp)
// Each hop from seed costs exactly 1.0 in getScore().

TEST(FrontierItemTest, SeedHasDistanceZeroAndChildHasDistanceOne) {
    FrontierItem seed(string("https://umich.edu"));
    // Use identical URL so TLD/baseLength/pathDepth are the same.
    // Only seedDistance differs: seed=0, child=1.
    // seedDistanceWeight = 1.0, so score gap is exactly 1.0.
    FrontierItem child(string("https://umich.edu"), seed);

    EXPECT_DOUBLE_EQ(seed.getScore() - child.getScore(), 1.0);
}

TEST(FrontierItemTest, SeedDistanceAccumulatesAcrossThreeGenerations) {
    FrontierItem seed(string("https://umich.edu"));
    FrontierItem child(string("https://umich.edu"), seed);
    FrontierItem grandchild(string("https://umich.edu"), child);

    // Each generation adds exactly 1.0 to the score gap (seedDistanceWeight = 1.0).
    EXPECT_DOUBLE_EQ(seed.getScore() - child.getScore(), 1.0);
    EXPECT_DOUBLE_EQ(seed.getScore() - grandchild.getScore(), 2.0);
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

    // brokenSourceWeight = 3.0, seedDistanceWeight = 1.0
    // childOfBroken: broken penalty (3.0) + distance (1.0) = 4.0 below cleanSeed
    // childOfClean:  only distance (1.0) below cleanSeed
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

    // Both built without a parent — neither should have a seedDistance penalty.
    // Score difference should only come from TLD/baseLength/path differences, not distance.
    // umich.edu: EDU(4.0) - 0.15*5(baseLength) - 0(path) - 0(distance) = 3.25
    // google.com: COM(3.0) - 0.15*6 - 0 - 0 = 2.10
    EXPECT_DOUBLE_EQ(a.getScore(), 4.0 - 0.15 * 5);
    EXPECT_DOUBLE_EQ(b.getScore(), 3.0 - 0.15 * 6);
}
