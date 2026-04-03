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