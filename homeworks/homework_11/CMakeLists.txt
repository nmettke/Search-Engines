cmake_minimum_required(VERSION 3.20)

project(HOMEWORK_11 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(OpenSSL REQUIRED)

enable_testing()

add_executable(bloomfilter_test bloomfilter_test.cpp)
target_include_directories(bloomfilter_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bloomfilter_test PRIVATE OpenSSL::SSL OpenSSL::Crypto)

add_test(NAME bloomfilter_test COMMAND bloomfilter_test)