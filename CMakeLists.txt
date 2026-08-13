cmake_minimum_required(VERSION 3.20)
project(mini-compiler LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# clang-tidy on every build
find_program(CLANG_TIDY_EXE NAMES clang-tidy)
if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
endif()

# Google Test
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Z3 (optional, for the predicate tests).
# find_package(Z3) only succeeds where Z3 ships its CMake package files; on
# Debian/Ubuntu it does not, yet libz3.so is present. tests/z3 used to be added
# unconditionally and link the bare name `z3`, so a machine without Z3 failed at
# link time with nothing having checked. Probe for the library itself and let
# tests/CMakeLists.txt gate on the result.
find_package(Z3 QUIET)
find_library(Z3_LIB NAMES z3)
if(Z3_LIB)
    message(STATUS "Z3 found: ${Z3_LIB} — predicate tests enabled")
else()
    message(STATUS "Z3 not found — predicate tests skipped")
endif()

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
