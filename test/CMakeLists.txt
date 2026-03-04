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

gtest_discover_tests(vector_tests)


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

gtest_discover_tests(string_tests)