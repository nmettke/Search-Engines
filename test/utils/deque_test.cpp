#include "utils/STL_rewrite/deque.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <utility>

namespace {

template <typename T> static void expectBackSequenceAndDrain(deque<T> &d, int first, int last) {
    for (int expected = first; expected <= last; ++expected) {
        ASSERT_FALSE(d.empty());
        EXPECT_EQ(d.back(), expected);
        d.pop_back();
    }

    EXPECT_TRUE(d.empty());
    EXPECT_EQ(d.size(), 0);
}

struct Payload {
    int id;
    std::string name;
};

struct LifetimeTracked {
    static int alive;

    int value;

    LifetimeTracked(int value) : value(value) { ++alive; }
    LifetimeTracked(const LifetimeTracked &other) : value(other.value) { ++alive; }
    LifetimeTracked(LifetimeTracked &&other) noexcept : value(other.value) { ++alive; }
    ~LifetimeTracked() { --alive; }
};

int LifetimeTracked::alive = 0;

} // namespace

TEST(DequeBasics, DefaultConstructorStartsEmpty) {
    deque<int> d;

    EXPECT_TRUE(d.empty());
    EXPECT_EQ(d.size(), 0);
}

TEST(DequeBasics, PushFrontSingleElementMakesBackVisible) {
    deque<int> d;

    d.push_front(42);

    EXPECT_FALSE(d.empty());
    EXPECT_EQ(d.size(), 1);
    EXPECT_EQ(d.back(), 42);
}

TEST(DequeBasics, PopBackRemovesOldestElement) {
    deque<int> d;

    d.push_front(1);
    d.push_front(2);
    d.push_front(3);

    ASSERT_EQ(d.back(), 1);
    d.pop_back();

    ASSERT_EQ(d.size(), 2);
    EXPECT_EQ(d.back(), 2);
}

TEST(DequeBasics, ReuseAfterBecomingEmptyWorks) {
    deque<int> d;

    d.push_front(1);
    d.pop_back();

    EXPECT_TRUE(d.empty());
    EXPECT_EQ(d.size(), 0);

    d.push_front(7);
    d.push_front(8);

    expectBackSequenceAndDrain(d, 7, 8);
}

TEST(DequeOrdering, PushFrontMaintainsBackInFifoOrder) {
    deque<int> d;

    for (int i = 1; i <= 6; ++i) {
        d.push_front(i);
    }

    expectBackSequenceAndDrain(d, 1, 6);
}

TEST(DequeEdgeCases, GrowthPreservesExistingContents) {
    deque<int> d;

    for (int i = 1; i <= 8; ++i) {
        d.push_front(i);
    }

    d.push_front(9);

    ASSERT_EQ(d.size(), 9);

    for (int expected = 1; expected <= 9; ++expected) {
        ASSERT_FALSE(d.empty());
        EXPECT_EQ(d.back(), expected);
        d.pop_back();
    }

    EXPECT_TRUE(d.empty());
}

TEST(DequeEdgeCases, MultipleGrowthStepsPreserveOrder) {
    deque<int> d;

    for (int i = 1; i <= 100; ++i) {
        d.push_front(i);
    }

    ASSERT_EQ(d.size(), 100);
    expectBackSequenceAndDrain(d, 1, 100);
}

TEST(DequeObjectSemantics, CopyConstructorCreatesIndependentCopy) {
    deque<int> original;
    for (int i = 1; i <= 5; ++i) {
        original.push_front(i);
    }

    deque<int> copy(original);
    original.pop_back();

    ASSERT_EQ(original.size(), 4);
    ASSERT_EQ(copy.size(), 5);
    EXPECT_EQ(original.back(), 2);
    EXPECT_EQ(copy.back(), 1);
}

TEST(DequeObjectSemantics, CopyAssignmentCreatesIndependentCopy) {
    deque<int> source;
    for (int i = 1; i <= 4; ++i) {
        source.push_front(i);
    }

    deque<int> target;
    target.push_front(99);
    target = source;
    source.pop_back();

    ASSERT_EQ(target.size(), 4);
    EXPECT_EQ(target.back(), 1);
    ASSERT_EQ(source.size(), 3);
    EXPECT_EQ(source.back(), 2);
}

TEST(DequeObjectSemantics, SelfCopyAssignmentPreservesContents) {
    deque<int> d;
    for (int i = 1; i <= 4; ++i) {
        d.push_front(i);
    }

    d = d;

    ASSERT_EQ(d.size(), 4);
    expectBackSequenceAndDrain(d, 1, 4);
}

TEST(DequeObjectSemantics, MoveConstructorTransfersContents) {
    deque<int> source;
    for (int i = 1; i <= 5; ++i) {
        source.push_front(i);
    }

    deque<int> moved(std::move(source));

    EXPECT_TRUE(source.empty());
    EXPECT_EQ(source.size(), 0);
    ASSERT_EQ(moved.size(), 5);
    expectBackSequenceAndDrain(moved, 1, 5);
}

TEST(DequeObjectSemantics, MoveAssignmentTransfersContents) {
    deque<int> source;
    for (int i = 1; i <= 5; ++i) {
        source.push_front(i);
    }

    deque<int> target;
    target.push_front(100);
    target = std::move(source);

    EXPECT_TRUE(source.empty());
    EXPECT_EQ(source.size(), 0);
    ASSERT_EQ(target.size(), 5);
    expectBackSequenceAndDrain(target, 1, 5);
}

TEST(DequeValueTypes, EmplaceFrontConstructsPayloadInPlace) {
    deque<Payload> d;

    d.emplace_front(Payload{1, "first"});
    d.emplace_front(Payload{2, "second"});

    ASSERT_EQ(d.size(), 2);
    EXPECT_EQ(d.back().id, 1);
    EXPECT_EQ(d.back().name, "first");
    d.pop_back();
    EXPECT_EQ(d.back().id, 2);
    EXPECT_EQ(d.back().name, "second");
}

TEST(DequeValueTypes, SupportsMoveOnlyElements) {
    deque<std::unique_ptr<int>> d;

    d.push_front(std::make_unique<int>(1));
    d.push_front(std::make_unique<int>(2));
    d.push_front(std::make_unique<int>(3));

    ASSERT_EQ(d.size(), 3);
    ASSERT_NE(d.back(), nullptr);
    EXPECT_EQ(*d.back(), 1);
    d.pop_back();
    ASSERT_NE(d.back(), nullptr);
    EXPECT_EQ(*d.back(), 2);
    d.pop_back();
    ASSERT_NE(d.back(), nullptr);
    EXPECT_EQ(*d.back(), 3);
}

TEST(DequeValueTypes, ConstBackAccessWorks) {
    deque<int> mutable_deque;
    mutable_deque.push_front(5);
    mutable_deque.push_front(6);

    const deque<int> &d = mutable_deque;

    ASSERT_EQ(d.size(), 2);
    EXPECT_EQ(d.back(), 5);
}

TEST(DequeValueTypes, ReleasesElementsAfterPopAndDestruction) {
    LifetimeTracked::alive = 0;

    {
        deque<LifetimeTracked> d;
        for (int i = 1; i <= 16; ++i) {
            d.push_front(LifetimeTracked{i});
        }

        ASSERT_GT(LifetimeTracked::alive, 0);

        for (int i = 0; i < 5; ++i) {
            d.pop_back();
        }

        ASSERT_EQ(d.size(), 11);
    }

    EXPECT_EQ(LifetimeTracked::alive, 0);
}
