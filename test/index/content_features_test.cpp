#include "index/src/lib/content_features.h"
#include <gtest/gtest.h>

TEST(ContentWordCountScoreTest, ZeroWords) { EXPECT_DOUBLE_EQ(contentWordCountScore(0), 0.0); }

TEST(ContentWordCountScoreTest, TwentyFiveWords) {
    EXPECT_NEAR(contentWordCountScore(25), 0.15, 0.001);
}

TEST(ContentWordCountScoreTest, FiftyWords) { EXPECT_DOUBLE_EQ(contentWordCountScore(50), 0.3); }

TEST(ContentWordCountScoreTest, TwoHundredWords) {
    EXPECT_DOUBLE_EQ(contentWordCountScore(200), 1.0);
}

TEST(ContentWordCountScoreTest, SixThousandWords) {
    EXPECT_DOUBLE_EQ(contentWordCountScore(6000), 0.75);
}

TEST(ContentTitleScoreTest, NoTitle) { EXPECT_DOUBLE_EQ(contentTitleScore(0), 0.0); }

TEST(ContentTitleScoreTest, FiveWords) { EXPECT_DOUBLE_EQ(contentTitleScore(5), 1.0); }

TEST(ContentTitleScoreTest, TwentyWords) { EXPECT_DOUBLE_EQ(contentTitleScore(20), 0.3); }
