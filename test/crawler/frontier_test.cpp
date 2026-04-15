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
