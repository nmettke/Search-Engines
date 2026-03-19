include(GoogleTest)

# -------------------------
# Vector Tests
# -------------------------

add_executable(vector_tests
    utils/vector_test.cpp
)

target_link_libraries(vector_tests
    PRIVATE
        utils
        GTest::gtest_main
)

gtest_discover_tests(vector_tests
    TEST_PREFIX "vector::"
)


# -------------------------
# String Tests
# -------------------------

add_executable(string_tests
    utils/string_test.cpp
)

target_link_libraries(string_tests
    PRIVATE
        utils
        GTest::gtest_main
)

gtest_discover_tests(string_tests
    TEST_PREFIX "string::"
)

# -------------------------
# FrontierItemHelper Tests
# -------------------------

add_executable(FrontierItemHelper_tests
    crawler/FrontierItemHelper_test.cpp
)

target_link_libraries(FrontierItemHelper_tests
    PRIVATE
        crawler
        GTest::gtest_main
)

gtest_discover_tests(FrontierItemHelper_tests
    TEST_PREFIX "FrontierItemHelper::"
)