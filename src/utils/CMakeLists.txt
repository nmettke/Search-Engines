cmake_minimum_required(VERSION 3.20)

project(HOMEWORK_2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warnings (optional but recommended)
add_compile_options(-Wall -Wextra -Wpedantic)

# string_test executable
add_executable(string_test
    string_test.cpp
)

target_include_directories(string_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# vector_test executable
add_executable(vector_test
    vector_test.cpp
)

target_include_directories(vector_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
