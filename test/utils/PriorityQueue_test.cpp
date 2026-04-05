#include "utils/PriorityQueue.hpp"
#include "utils/string.hpp"
#include <functional>
#include <gtest/gtest.h>
#include <stdexcept>

TEST(PriorityQueueTest, DefaultConstructor) {
    PriorityQueue<int> pq;
    EXPECT_TRUE(pq.empty());
    EXPECT_EQ(pq.size(), 0);
}

TEST(PriorityQueueTest, PushSingleElement) {
    PriorityQueue<int> pq;
    pq.push(10);

    EXPECT_FALSE(pq.empty());
    EXPECT_EQ(pq.size(), 1);
    EXPECT_EQ(pq.top(), 10);
}

TEST(PriorityQueueTest, PushMultipleElementsMaxHeapBehavior) {
    PriorityQueue<int> pq;
    pq.push(3);
    pq.push(10);
    pq.push(5);
    pq.push(8);

    EXPECT_EQ(pq.size(), 4);
    EXPECT_EQ(pq.top(), 10);
}

TEST(PriorityQueueTest, PopRemovesTopElement) {
    PriorityQueue<int> pq;
    pq.push(3);
    pq.push(10);
    pq.push(5);

    pq.pop();

    EXPECT_EQ(pq.size(), 2);
    EXPECT_EQ(pq.top(), 5);
}

TEST(PriorityQueueTest, ExtractTopReturnsAndRemovesTop) {
    PriorityQueue<int> pq;
    pq.push(7);
    pq.push(2);
    pq.push(9);
    pq.push(4);

    int top = pq.extractTop();

    EXPECT_EQ(top, 9);
    EXPECT_EQ(pq.size(), 3);
    EXPECT_EQ(pq.top(), 7);
}

TEST(PriorityQueueTest, PopSingleElementMakesEmpty) {
    PriorityQueue<int> pq;
    pq.push(42);

    pq.pop();

    EXPECT_TRUE(pq.empty());
    EXPECT_EQ(pq.size(), 0);
}

TEST(PriorityQueueTest, ExtractTopSingleElementMakesEmpty) {
    PriorityQueue<int> pq;
    pq.push(42);

    int top = pq.extractTop();

    EXPECT_EQ(top, 42);
    EXPECT_TRUE(pq.empty());
    EXPECT_EQ(pq.size(), 0);
}

TEST(PriorityQueueTest, TopThrowsOnEmptyQueue) {
    PriorityQueue<int> pq;
    EXPECT_THROW(pq.top(), std::out_of_range);
}

TEST(PriorityQueueTest, PopThrowsOnEmptyQueue) {
    PriorityQueue<int> pq;
    EXPECT_THROW(pq.pop(), std::out_of_range);
}

TEST(PriorityQueueTest, ExtractTopThrowsOnEmptyQueue) {
    PriorityQueue<int> pq;
    EXPECT_THROW(pq.extractTop(), std::out_of_range);
}

TEST(PriorityQueueTest, ClearRemovesAllElements) {
    PriorityQueue<int> pq;
    pq.push(1);
    pq.push(2);
    pq.push(3);

    pq.clear();

    EXPECT_TRUE(pq.empty());
    EXPECT_EQ(pq.size(), 0);
}

TEST(PriorityQueueTest, EmplaceInsertsCustomString) {
    PriorityQueue<string> pq;
    pq.emplace("banana");
    pq.emplace("apple");
    pq.emplace("pear");

    EXPECT_EQ(pq.size(), 3);
    EXPECT_TRUE(pq.top() == string("pear"));
}

TEST(PriorityQueueTest, CustomComparatorMinHeap) {
    PriorityQueue<int, std::greater<int>> pq;
    pq.push(10);
    pq.push(3);
    pq.push(7);
    pq.push(1);

    EXPECT_EQ(pq.top(), 1);

    pq.pop();
    EXPECT_EQ(pq.top(), 3);
}

struct Item {
    string name;
    int priority;

    Item(const string &name_in, int priority_in) : name(name_in), priority(priority_in) {}
};

struct ItemCompare {
    bool operator()(const Item &a, const Item &b) const { return a.priority < b.priority; }
};

TEST(PriorityQueueTest, WorksWithCustomType) {
    PriorityQueue<Item, ItemCompare> pq;
    pq.emplace("low", 1);
    pq.emplace("high", 10);
    pq.emplace("mid", 5);

    EXPECT_TRUE(pq.top().name == string("high"));
    EXPECT_EQ(pq.top().priority, 10);

    pq.pop();

    EXPECT_TRUE(pq.top().name == string("mid"));
    EXPECT_EQ(pq.top().priority, 5);
}

TEST(PriorityQueueTest, MaintainsHeapOrderAcrossManyPops) {
    PriorityQueue<int> pq;
    pq.push(4);
    pq.push(1);
    pq.push(9);
    pq.push(2);
    pq.push(7);
    pq.push(8);

    EXPECT_EQ(pq.extractTop(), 9);
    EXPECT_EQ(pq.extractTop(), 8);
    EXPECT_EQ(pq.extractTop(), 7);
    EXPECT_EQ(pq.extractTop(), 4);
    EXPECT_EQ(pq.extractTop(), 2);
    EXPECT_EQ(pq.extractTop(), 1);
    EXPECT_TRUE(pq.empty());
}