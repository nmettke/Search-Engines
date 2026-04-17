#include "crawler/frontier.h"

#include <dirent.h>
#include <chrono>
#include <cstdio>
#include <future>
#include <gtest/gtest.h>
#include <set>
#include <string>
#include <thread>
#include <time.h>

namespace {

constexpr const char *frontierDiskBackChunkDir = ".";

void clearFrontierDiskChunks() {
    DIR *dir = opendir(frontierDiskBackChunkDir);
    if (dir == nullptr) {
        return;
    }

    struct dirent *entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        std::size_t chunkIndex = 0;
        std::size_t chunkItemCount = 0;
        if (std::sscanf(entry->d_name, "frontier_disk_back_chunk_%zu_%zu.dat", &chunkIndex,
                        &chunkItemCount) == 2) {
            std::string path = std::string(frontierDiskBackChunkDir) + "/" + entry->d_name;
            std::remove(path.c_str());
        }
    }

    closedir(dir);
}

class FrontierTest : public ::testing::Test {
  protected:
    void SetUp() override { clearFrontierDiskChunks(); }
    void TearDown() override { clearFrontierDiskChunks(); }
};

} // namespace

TEST_F(FrontierTest, DifferentHostsCanProceedWhileFirstHostIsInFlight) {
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

TEST_F(FrontierTest, SnoozedHostRequeuesAndBecomesReadyLater) {
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

TEST_F(FrontierTest, SnapshotOnlyCapturesInMemoryLayersAfterDiskSpill) {
    Frontier frontier(vector<FrontierItem>{}, false, 2);
    vector<FrontierItem> items{
        FrontierItem::withSeedDistance(string("https://a.example/root"), 0),
        FrontierItem::withSeedDistance(string("https://b.example/root"), 1),
        FrontierItem::withSeedDistance(string("https://c.example/root"), 2),
        FrontierItem::withSeedDistance(string("https://d.example/root"), 3),
        FrontierItem::withSeedDistance(string("https://e.example/root"), 4),
        FrontierItem::withSeedDistance(string("https://f.example/root"), 5),
    };

    frontier.pushMany(items);

    EXPECT_EQ(frontier.size(), items.size());

    vector<FrontierItem> snapshot = frontier.snapshot();
    EXPECT_EQ(snapshot.size(), 4u);
}

TEST_F(FrontierTest, FrontierRecoversSnapshotPlusPersistentDiskChunks) {
    vector<FrontierItem> items{
        FrontierItem::withSeedDistance(string("https://a.example/root"), 0),
        FrontierItem::withSeedDistance(string("https://b.example/root"), 1),
        FrontierItem::withSeedDistance(string("https://c.example/root"), 2),
        FrontierItem::withSeedDistance(string("https://d.example/root"), 3),
        FrontierItem::withSeedDistance(string("https://e.example/root"), 4),
        FrontierItem::withSeedDistance(string("https://f.example/root"), 5),
    };

    vector<FrontierItem> snapshot;
    {
        Frontier frontier(vector<FrontierItem>{}, false, 2);
        frontier.pushMany(items);
        snapshot = frontier.snapshot();
        EXPECT_EQ(frontier.size(), items.size());
        EXPECT_EQ(snapshot.size(), 4u);
    }

    Frontier recovered(snapshot, false, 2);
    EXPECT_EQ(recovered.size(), snapshot.size());

    std::set<std::string> poppedLinks;
    for (std::size_t i = 0; i < items.size(); ++i) {
        std::optional<FrontierItem> next = recovered.pop();
        ASSERT_TRUE(next.has_value());
        poppedLinks.insert(std::string(next->link.cstr()));
        recovered.taskDone();
    }

    EXPECT_TRUE(recovered.empty());
    EXPECT_EQ(poppedLinks.size(), items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        EXPECT_EQ(poppedLinks.count(std::string(items[i].link.cstr())), 1u);
    }
}

TEST_F(FrontierTest, ReservoirItemsAppearInContainsAndSnapshotBeforePromotion) {
    FrontierItem first = FrontierItem::withSeedDistance(string("https://a.example/root"), 0);
    FrontierItem second = FrontierItem::withSeedDistance(string("https://b.example/root"), 1);
    Frontier frontier(vector<FrontierItem>{first, second}, false);

    EXPECT_EQ(frontier.size(), 2u);
    EXPECT_TRUE(frontier.contains(first.link));
    EXPECT_TRUE(frontier.contains(second.link));

    vector<FrontierItem> snapshot = frontier.snapshot();
    EXPECT_EQ(snapshot.size(), 2u);
}

TEST_F(FrontierTest, ReservoirSweepPromotesBestItemFromChunkFirst) {
    FrontierItem best = FrontierItem::withSeedDistance(string("https://a.example/root"), 0);
    FrontierItem medium = FrontierItem::withSeedDistance(string("https://b.example/root"), 2);
    FrontierItem worse = FrontierItem::withSeedDistance(string("https://c.example/deep/page"), 6);
    FrontierItem worst = FrontierItem::withSeedDistance(string("https://d.example/deeper/page"), 8);
    Frontier frontier(vector<FrontierItem>{worse, medium, worst, best}, false);

    std::optional<FrontierItem> popped = frontier.pop();
    ASSERT_TRUE(popped.has_value());
    EXPECT_EQ(std::string(popped->link.cstr()), std::string(best.link.cstr()));

    frontier.taskDone();
}
