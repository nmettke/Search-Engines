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
# Index Ranking Tests
# -------------------------

add_executable(content_features_tests
    index/content_features_test.cpp
)

target_link_libraries(content_features_tests
    PRIVATE
        index_lib
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(content_features_tests SYSTEM BEFORE PRIVATE
    ${FETCHED_GTEST_INCLUDE_DIRS}
)

gtest_discover_tests(content_features_tests
    TEST_PREFIX "content_features::"
)

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

add_executable(static_ranker_tests
    index/static_ranker_test.cpp
)

target_link_libraries(static_ranker_tests
    PRIVATE
        index_lib
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(static_ranker_tests SYSTEM BEFORE PRIVATE
    ${FETCHED_GTEST_INCLUDE_DIRS}
)

gtest_discover_tests(static_ranker_tests
    TEST_PREFIX "static_ranker::"
)
