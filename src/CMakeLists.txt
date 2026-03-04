# -------------------------
# Utils Library
# -------------------------

add_library(utils
    utils/SSL/LinuxSSL_Crawler.cpp
)

target_include_directories(utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/utils
)

target_compile_features(utils PUBLIC cxx_std_17)


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
        ${CMAKE_CURRENT_SOURCE_DIR}/parser
)

target_link_libraries(parser
    PUBLIC
        utils
)

target_compile_features(parser PUBLIC cxx_std_17)


# -------------------------
# Crawler Library
# -------------------------

add_library(crawler
    crawler/basic.cpp
)

target_include_directories(crawler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/crawler
)

target_link_libraries(crawler
    PUBLIC
        parser
        utils
)

target_compile_features(crawler PUBLIC cxx_std_17)