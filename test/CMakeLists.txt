include(GoogleTest)

set(FETCHED_GTEST_INCLUDE_DIRS
    ${googletest_SOURCE_DIR}/googletest/include
    ${googletest_SOURCE_DIR}/googletest
)

# -------------------------
# Vector Tests
# -------------------------

add_executable(vector_tests
    utils/vector_test.cpp
)

target_link_libraries(vector_tests
    PRIVATE
        utils
        GTest::gtest
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
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(string_tests
    TEST_PREFIX "string::"
)

# -------------------------
# Deque Tests
# -------------------------

add_executable(deque_tests
    utils/deque_test.cpp
)

target_link_libraries(deque_tests
    PRIVATE
        utils
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(deque_tests
    TEST_PREFIX "deque::"
)

# -------------------------
# FrontierItemHelper Tests
# -------------------------

add_executable(FrontierItemHelper_tests
    crawler/FrontierItemHelper_test.cpp
)

target_link_libraries(FrontierItemHelper_tests
    PRIVATE
        crawler_lib
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(FrontierItemHelper_tests
    TEST_PREFIX "FrontierItemHelper::"
)

# -------------------------
# Frontier Tests
# -------------------------

add_executable(frontier_tests
    crawler/frontier_test.cpp
)

target_link_libraries(frontier_tests
    PRIVATE
        crawler_lib
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(frontier_tests
    TEST_PREFIX "frontier::"
)

# -------------------------
# Url Dedup Tests
# -------------------------

add_executable(url_dedup_tests
    crawler/url_dedup_test.cpp
)

target_link_libraries(url_dedup_tests
    PRIVATE
        crawler_lib
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(url_dedup_tests
    TEST_PREFIX "url_dedup::"
)

# -------------------------
# PriorityQueue Tests
# -------------------------
add_executable(PriorityQueue_tests
    utils/PriorityQueue_test.cpp
)

target_link_libraries(PriorityQueue_tests
    PRIVATE
        utils
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(PriorityQueue_tests
    TEST_PREFIX "PriorityQueue::"
)

# -------------------------
# Index Feature Tests
# -------------------------

add_executable(url_features_tests
    index/url_features_test.cpp
)

target_link_libraries(url_features_tests
    PRIVATE
        index_lib
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(url_features_tests SYSTEM BEFORE PRIVATE
    ${FETCHED_GTEST_INCLUDE_DIRS}
)

gtest_discover_tests(url_features_tests
    TEST_PREFIX "url_features::"
)

add_executable(document_features_tests
    index/document_features_test.cpp
)

target_link_libraries(document_features_tests
    PRIVATE
        index_lib
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(document_features_tests SYSTEM BEFORE PRIVATE
    ${FETCHED_GTEST_INCLUDE_DIRS}
)

gtest_discover_tests(document_features_tests
    TEST_PREFIX "document_features::"
)

add_executable(query_engine_tests
    index/query_engine_test.cpp
)

target_link_libraries(query_engine_tests
    PRIVATE
        index_lib
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(query_engine_tests SYSTEM BEFORE PRIVATE
    ${FETCHED_GTEST_INCLUDE_DIRS}
)

gtest_discover_tests(query_engine_tests
    TEST_PREFIX "query_engine::"
)
