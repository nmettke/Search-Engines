# -------------------------
# Utils Library
# -------------------------
add_library(utils
    utils/SSL/LinuxSSL_Crawler.cpp
    utils/Utf8.cpp
)

# target_include_directories(utils
#     PUBLIC
#         ${CMAKE_CURRENT_SOURCE_DIR}
# )

# target_compile_features(utils PUBLIC cxx_std_17)

##### HOT FIX: COULDN'T BUILD WITH OPENSSL #####

# 1. Locate OpenSSL on the system
find_package(OpenSSL REQUIRED)

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
    crawler/checkpoint.cpp
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

target_compile_features(crawler_lib PUBLIC cxx_std_17)
target_compile_features(crawler PUBLIC cxx_std_17)

# -------------------------
# IPC (Unix socket wire format for crawler -> indexer)
# -------------------------
add_library(ipc
    ipc/wire_document.cpp
    ipc/unix_socket.cpp
)
target_include_directories(ipc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(ipc PUBLIC cxx_std_17)

target_link_libraries(crawler PRIVATE ipc)

# -------------------------
# Search index (library + indexer daemon + demo)
# -------------------------
set(INDEX_LIB_SRCS
    index/src/lib/vbyte.cpp
    index/src/lib/tokenizer.cpp
    index/src/lib/seek_table.cpp
    index/src/lib/query_engine.cpp
    index/src/lib/isr.cpp
    index/src/lib/in_memory_index.cpp
    index/src/lib/disk_chunk_writer.cpp
    index/src/lib/disk_chunk_reader.cpp
    index/src/lib/chunk_flusher.cpp
    index/src/lib/Common.cpp
)

add_library(index_core STATIC ${INDEX_LIB_SRCS})
target_include_directories(index_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/index/src)
target_compile_features(index_core PUBLIC cxx_std_17)

add_executable(indexer index/src/indexer_main.cpp)
target_include_directories(indexer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/index/src
)
target_link_libraries(indexer PRIVATE index_core ipc)
target_compile_features(indexer PUBLIC cxx_std_17)

add_executable(index_master index/src/master_main.cpp)
target_include_directories(index_master PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/index/src
)
target_link_libraries(index_master PRIVATE index_core ipc)
target_compile_features(index_master PUBLIC cxx_std_17)

add_executable(index_demo index/src/main.cpp)
target_link_libraries(index_demo PRIVATE index_core)
target_compile_features(index_demo PUBLIC cxx_std_17)
