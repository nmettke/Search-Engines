cmake_minimum_required(VERSION 3.20)

# Project definition
project(SearchEngine
    VERSION 1.0
    LANGUAGES CXX
)

# Enforce modern C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable testing infrastructure
include(CTest)
enable_testing()

# -----------------------------
# Fetch GoogleTest automatically
# -----------------------------
include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

# Prevent overriding parent project settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Prefer the fetched GoogleTest headers over any Homebrew/system install so the
# test macros match the built gtest library ABI.
include_directories(BEFORE SYSTEM ${googletest_SOURCE_DIR}/googletest/include)

# -----------------------------
# Add project directories
# -----------------------------
add_subdirectory(src)
add_subdirectory(test)
