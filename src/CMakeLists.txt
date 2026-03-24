# -------------------------
# Utils Library
# -------------------------

add_library(utils
    utils/SSL/LinuxSSL_Crawler.cpp
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
    PRIVATE
        ${OPENSSL_INCLUDE_DIR}  # 2. Tell the compiler where ssl.h lives
)

# 3. Tell the linker to include the OpenSSL binary code
target_link_libraries(utils PRIVATE OpenSSL::SSL OpenSSL::Crypto)

target_compile_features(utils PUBLIC cxx_std_17)

#### HOT FIX END ####

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

add_library(crawler
    crawler/FrontierItemHelper.cpp
    crawler/FrontierItem.cpp
    crawler/frontier.cpp
)

target_include_directories(crawler
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(crawler
    PUBLIC
        parser
        utils
)

target_compile_features(crawler PUBLIC cxx_std_17)