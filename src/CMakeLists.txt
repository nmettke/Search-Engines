# -------------------------
# Utils Library
# -------------------------
add_library(utils
    utils/SSL/LinuxSSL_Crawler.cpp
    utils/Utf8.cpp
)

target_include_directories(utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 3. Tell the linker to include the OpenSSL binary code
target_link_libraries(utils PRIVATE OpenSSL::SSL OpenSSL::Crypto)

target_compile_features(utils PUBLIC cxx_std_17)

find_package(OpenSSL REQUIRED)
target_link_libraries(utils
    PUBLIC
        OpenSSL::SSL
        OpenSSL::Crypto
)

# -------------------------
# Parser Library
# -------------------------

add_library(parser
    parser/HtmlParser.cpp
    parser/HtmlTags.cpp
    parser/ParsePage.cpp
)

target_include_directories(parser
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(parser
    PUBLIC
        utils
)

target_compile_features(parser PUBLIC cxx_std_17)


# -------------------------
# Crawler Library
# -------------------------

add_library(crawler_lib
    crawler/FrontierItemHelper.cpp
    crawler/FrontierItem.cpp
    crawler/frontier.cpp
    crawler/url_dedup.cpp
    crawler/UrlFilter.cpp
    crawler/checkpoint.cpp
    crawler/RobotsTxt.cpp
    crawler/RobotsCache.cpp
    crawler/DelayedQueue.cpp
)

target_include_directories(crawler_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(crawler_lib
    PUBLIC
        parser
        utils
)

set(CRAWLER_MAIN_SOURCE crawler/Crawler.cpp)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/crawler/crawler.cpp)
    set(CRAWLER_MAIN_SOURCE crawler/crawler.cpp)
endif()

add_executable(crawler
    ${CRAWLER_MAIN_SOURCE}
)

target_link_libraries(crawler
    PRIVATE
        crawler_lib
        parser
        utils
)

add_executable(rank_seedlist EXCLUDE_FROM_ALL
    ../rank_seedlist.cpp
)

target_link_libraries(rank_seedlist
    PRIVATE
        crawler_lib
)

target_compile_features(crawler_lib PUBLIC cxx_std_17)
target_compile_features(crawler PUBLIC cxx_std_17)

# -------------------------
# Index library (for src/main.cpp)
# -------------------------

find_package(Threads REQUIRED)

add_library(index_lib
    index/src/lib/Common.cpp
    index/src/lib/vbyte.cpp
    index/src/lib/seek_table.cpp
    index/src/lib/document_features.cpp
    index/src/lib/in_memory_index.cpp
    index/src/lib/disk_chunk_writer.cpp
    index/src/lib/disk_chunk_reader.cpp
    index/src/lib/chunk_flusher.cpp
    index/src/lib/indexQueue.cpp
    index/src/lib/tokenizer.cpp
    index/src/lib/query_engine.cpp
    index/src/lib/query_tokenizer.cpp
    index/src/lib/query_compiler.cpp
    index/src/lib/isr.cpp
    index/src/lib/isr_and.cpp
    index/src/lib/isr_or.cpp
    index/src/lib/isr_phrase.cpp
    index/src/lib/isr_container.cpp
    index/src/lib/url_features.cpp
)

target_include_directories(index_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/index/src/lib
)

target_link_libraries(index_lib
    PUBLIC
        parser
        utils
)

target_compile_features(index_lib PUBLIC cxx_std_17)

add_executable(search_engine
    main.cpp
)

target_link_libraries(search_engine
    PRIVATE
        crawler_lib
        index_lib
        Threads::Threads
)

target_compile_features(search_engine PUBLIC cxx_std_17)

add_executable(search_engine_distributed
    main_distributed.cpp
)

target_link_libraries(search_engine_distributed
    PRIVATE
        crawler_lib
        index_lib
        Threads::Threads
)

target_compile_features(search_engine_distributed PUBLIC cxx_std_17)
