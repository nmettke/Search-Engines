#include <gtest/gtest.h>
#include <string>
#include "vector.hpp"

TEST(VectorTest, DefaultConstructor)
{
    vector<int> v;
    EXPECT_EQ(v.size(), 0);
    EXPECT_EQ(v.capacity(), 0);
}

TEST(VectorTest, ResizeConstructor)
{
    vector<int> v(5);

    EXPECT_EQ(v.size(), 5);
    EXPECT_EQ(v.capacity(), 5);

    for (size_t i = 0; i < v.size(); ++i)
        EXPECT_EQ(v[i], 0);
}

TEST(VectorTest, FillConstructor)
{
    vector<int> v(4, 7);

    EXPECT_EQ(v.size(), 4);

    for (size_t i = 0; i < v.size(); ++i)
        EXPECT_EQ(v[i], 7);
}

TEST(VectorTest, PushBack)
{
    vector<int> v;

    v.pushBack(1);
    v.pushBack(2);
    v.pushBack(3);

    EXPECT_EQ(v.size(), 3);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[1], 2);
    EXPECT_EQ(v[2], 3);
}

TEST(VectorTest, PopBack)
{
    vector<int> v;

    v.pushBack(10);
    v.pushBack(20);
    v.pushBack(30);

    v.popBack();

    EXPECT_EQ(v.size(), 2);
    EXPECT_EQ(v[0], 10);
    EXPECT_EQ(v[1], 20);
}

TEST(VectorTest, CopyConstructor)
{
    vector<int> a;

    for (int i = 0; i < 5; ++i)
        a.pushBack(i);

    vector<int> b(a);

    EXPECT_EQ(b.size(), a.size());

    for (size_t i = 0; i < a.size(); ++i)
        EXPECT_EQ(a[i], b[i]);
}

TEST(VectorTest, CopyAssignment)
{
    vector<int> a;
    vector<int> b;

    for (int i = 0; i < 4; ++i)
        a.pushBack(i * 2);

    b = a;

    EXPECT_EQ(b.size(), a.size());

    for (size_t i = 0; i < a.size(); ++i)
        EXPECT_EQ(b[i], a[i]);
}

TEST(VectorTest, MoveConstructor)
{
    vector<int> a;

    a.pushBack(1);
    a.pushBack(2);
    a.pushBack(3);

    vector<int> b(std::move(a));

    EXPECT_EQ(b.size(), 3);
    EXPECT_EQ(b[0], 1);
    EXPECT_EQ(b[1], 2);
    EXPECT_EQ(b[2], 3);

    EXPECT_EQ(a.size(), 0);
}

TEST(VectorTest, MoveAssignment)
{
    vector<int> a;
    vector<int> b;

    a.pushBack(5);
    a.pushBack(6);

    b = std::move(a);

    EXPECT_EQ(b.size(), 2);
    EXPECT_EQ(b[0], 5);
    EXPECT_EQ(b[1], 6);

    EXPECT_EQ(a.size(), 0);
}

TEST(VectorTest, Reserve)
{
    vector<int> v;

    v.reserve(20);

    EXPECT_GE(v.capacity(), 20);
}

TEST(VectorTest, EmplaceBack)
{
    vector<std::string> v;

    v.emplaceBack("hello");
    v.emplaceBack("world");

    EXPECT_EQ(v.size(), 2);
    EXPECT_EQ(v[0], "hello");
    EXPECT_EQ(v[1], "world");
}

TEST(VectorTest, Iterators)
{
    vector<int> v;

    for (int i = 0; i < 5; ++i)
        v.pushBack(i);

    int expected = 0;

    for (int *it = v.begin(); it != v.end(); ++it)
    {
        EXPECT_EQ(*it, expected);
        expected++;
    }
}

TEST(VectorTest, ManyPushBack)
{
    vector<int> v;

    for (int i = 0; i < 1000; ++i)
        v.pushBack(i);

    EXPECT_EQ(v.size(), 1000);

    for (int i = 0; i < 1000; ++i)
        EXPECT_EQ(v[i], i);
}

TEST(VectorEdgeCases, ReservePreservesData)
{
    vector<int> v;

    for (int i = 0; i < 20; ++i)
        v.pushBack(i);

    size_t oldSize = v.size();

    v.reserve(100);

    EXPECT_EQ(v.size(), oldSize);

    for (int i = 0; i < 20; ++i)
        EXPECT_EQ(v[i], i);
}

TEST(VectorEdgeCases, CopyIsDeep)
{
    vector<int> a;

    for (int i = 0; i < 5; ++i)
        a.pushBack(i);

    vector<int> b = a;

    b[0] = 999;

    EXPECT_EQ(a[0], 0);
    EXPECT_EQ(b[0], 999);
}

struct Counted
{
    static int destructCount;

    int value;

    Counted(int v) : value(v) {}

    ~Counted()
    {
        destructCount++;
    }
};

int Counted::destructCount = 0;

TEST(VectorEdgeCases, DestructorsCalled)
{
    Counted::destructCount = 0;

    {
        vector<Counted> v;

        for (int i = 0; i < 5; ++i)
            v.emplaceBack(i);

        EXPECT_EQ(v.size(), 5);
    }

    EXPECT_EQ(Counted::destructCount, 5);
}

TEST(VectorEdgeCases, SelfAssignment)
{
    vector<int> v;

    for (int i = 0; i < 10; ++i)
        v.pushBack(i);

    v = v;

    EXPECT_EQ(v.size(), 10);

    for (int i = 0; i < 10; ++i)
        EXPECT_EQ(v[i], i);
}