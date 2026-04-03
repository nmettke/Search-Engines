#include "utils/string.hpp"
#include <gtest/gtest.h>

TEST(StringConstructorTest, DefaultConstructor) {
    string s;
    EXPECT_EQ(s.size(), 0);
    EXPECT_STREQ(s.cstr(), "");
}

TEST(StringConstructorTest, CStringConstructor) {
    string s("hello");
    EXPECT_EQ(s.size(), 5);
    EXPECT_STREQ(s.cstr(), "hello");
}

TEST(StringConstructorTest, CopyConstructor) {
    string a("hello");
    string b(a);

    EXPECT_EQ(b.size(), 5);
    EXPECT_STREQ(b.cstr(), "hello");
}

TEST(StringAssignmentTest, AssignmentOperator) {
    string a("hello");
    string b("world");

    b = a;

    EXPECT_EQ(b.size(), 5);
    EXPECT_STREQ(b.cstr(), "hello");
}

TEST(StringAppendTest, OperatorPlusEquals) {
    string a("hello");
    string b("world");

    a += b;

    EXPECT_EQ(a.size(), 10);
    EXPECT_STREQ(a.cstr(), "helloworld");
}

TEST(StringAppendTest, OperatorPlus) {
    string a("hello");
    string b("world");

    string c = a + b;

    EXPECT_STREQ(c.cstr(), "helloworld");
    EXPECT_STREQ(a.cstr(), "hello");
    EXPECT_STREQ(b.cstr(), "world");
}

TEST(StringAppendTest, LiteralPlusString) {
    string s("world");

    string result = "hello " + s;

    EXPECT_STREQ(result.cstr(), "hello world");
}

TEST(StringAppendTest, StringPlusLiteral) {
    string s("hello");

    string result = s + " world";

    EXPECT_STREQ(result.cstr(), "hello world");
}

TEST(StringPushPopTest, PushBack) {
    string s("abc");

    s.pushBack('d');

    EXPECT_EQ(s.size(), 4);
    EXPECT_STREQ(s.cstr(), "abcd");
}

TEST(StringPushPopTest, PopBack) {
    string s("abcd");

    s.popBack();

    EXPECT_EQ(s.size(), 3);
    EXPECT_STREQ(s.cstr(), "abc");
}

TEST(StringComparisonTest, Equality) {
    string a("test");
    string b("test");

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(StringComparisonTest, Ordering) {
    string a("abc");
    string b("abd");

    EXPECT_TRUE(a < b);
    EXPECT_TRUE(b > a);
}

TEST(StringFindTest, SubstringFound) {
    string s("hello world");

    size_t pos = s.find("world");

    EXPECT_EQ(pos, 6);
}

TEST(StringFindTest, SubstringNotFound) {
    string s("hello");

    size_t pos = s.find("xyz");

    EXPECT_EQ(pos, string::npos);
}

TEST(StringAppendTest, AppendRawBuffer) {
    string s("hello");

    s.append(" world", 6);

    EXPECT_STREQ(s.cstr(), "hello world");
}

TEST(StringEdgeCases, EmptyStringBehavior) {
    string s;

    EXPECT_EQ(s.size(), 0);

    s.pushBack('a');

    EXPECT_EQ(s.size(), 1);
    EXPECT_STREQ(s.cstr(), "a");
}

TEST(StringCapacityTest, LargeGrowthMaintainsCorrectContent) {
    string s;

    for (int i = 0; i < 10000; i++) {
        s.pushBack('a');
    }

    EXPECT_EQ(s.size(), 10000);

    for (size_t i = 0; i < s.size(); i++) {
        EXPECT_EQ(s[i], 'a');
    }

    EXPECT_EQ(s.cstr()[s.size()], '\0');
}