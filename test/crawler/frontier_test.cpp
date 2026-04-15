#include "crawler/frontier.h"

#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <time.h>

TEST(FrontierTest, DifferentHostsCanProceedWhileFirstHostIsInFlight) {
    Frontier frontier(vector<FrontierItem>{
                         FrontierItem(string("https://a.example/page-1")),
                         FrontierItem(string("https://a.example/page-2")),
                         FrontierItem(string("https://b.example/page-1")),
                     },
                     false);

    std::optional<FrontierItem> first = frontier.pop();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(std::string(first->link.cstr()), "https://a.example/page-1");

    std::future<std::optional<FrontierItem>> secondFuture =
        std::async(std::launch::async, [&frontier]() {
            std::optional<FrontierItem> item = frontier.pop();
            frontier.taskDone();
            return item;
        });

    std::optional<FrontierItem> second = secondFuture.get();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(std::string(second->link.cstr()), "https://b.example/page-1");

    frontier.taskDone();
}

TEST(FrontierTest, SnoozedHostRequeuesAndBecomesReadyLater) {
    Frontier frontier(vector<FrontierItem>{FrontierItem(string("https://a.example/page-1"))}, false);

    std::optional<FrontierItem> first = frontier.pop();
    ASSERT_TRUE(first.has_value());

    auto start = std::chrono::steady_clock::now();
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    std::int64_t readyAtMs =
        static_cast<std::int64_t>(ts.tv_sec) * 1000 + static_cast<std::int64_t>(ts.tv_nsec / 1000000) + 40;
    frontier.snoozeCurrent(*first, readyAtMs);

    std::optional<FrontierItem> second = frontier.pop();
    auto finish = std::chrono::steady_clock::now();

    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(std::string(second->link.cstr()), "https://a.example/page-1");
    EXPECT_GE(std::chrono::duration_cast<std::chrono::milliseconds>(finish - start).count(), 25);

    frontier.taskDone();
}

TEST(FrontierTest, BetterUrlEvictsWorstQueuedItemWhenFrontierIsFull) {
    FrontierItem good = FrontierItem::withSeedDistance(string("https://a.example/root"), 0);
    FrontierItem worse = FrontierItem::withSeedDistance(string("https://b.example/deep/page"), 6);
    Frontier frontier(vector<FrontierItem>{good, worse}, false, 2);

    FrontierItem betterIncoming =
        FrontierItem::withSeedDistance(string("https://c.example/root"), 1);
    frontier.push(betterIncoming);

    EXPECT_EQ(frontier.size(), 2u);
    EXPECT_TRUE(frontier.contains(good.link));
    EXPECT_TRUE(frontier.contains(betterIncoming.link));
    EXPECT_FALSE(frontier.contains(worse.link));
}

TEST(FrontierTest, WorseUrlIsDroppedWhenFrontierIsFull) {
    FrontierItem good = FrontierItem::withSeedDistance(string("https://a.example/root"), 0);
    FrontierItem medium = FrontierItem::withSeedDistance(string("https://b.example/root"), 1);
    Frontier frontier(vector<FrontierItem>{good, medium}, false, 2);

    FrontierItem worseIncoming =
        FrontierItem::withSeedDistance(string("https://c.example/deep/page"), 8);
    frontier.push(worseIncoming);

    EXPECT_EQ(frontier.size(), 2u);
    EXPECT_TRUE(frontier.contains(good.link));
    EXPECT_TRUE(frontier.contains(medium.link));
    EXPECT_FALSE(frontier.contains(worseIncoming.link));
}

TEST(FrontierTest, SnoozedItemMakesRoomWhenFrontierIsFull) {
    FrontierItem first = FrontierItem::withSeedDistance(string("https://a.example/root"), 0);
    FrontierItem queued = FrontierItem::withSeedDistance(string("https://b.example/queued"), 1);
    Frontier frontier(vector<FrontierItem>{first}, false, 1);

    std::optional<FrontierItem> popped = frontier.pop();
    ASSERT_TRUE(popped.has_value());
    frontier.push(queued);
    ASSERT_TRUE(frontier.contains(queued.link));

    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    std::int64_t readyAtMs =
        static_cast<std::int64_t>(ts.tv_sec) * 1000 + static_cast<std::int64_t>(ts.tv_nsec / 1000000);
    frontier.snoozeCurrent(*popped, readyAtMs);

    EXPECT_EQ(frontier.size(), 1u);
    EXPECT_TRUE(frontier.contains(popped->link));
    EXPECT_FALSE(frontier.contains(queued.link));

    std::optional<FrontierItem> resumed = frontier.pop();
    ASSERT_TRUE(resumed.has_value());
    EXPECT_EQ(std::string(resumed->link.cstr()), std::string(popped->link.cstr()));
    frontier.taskDone();
}
