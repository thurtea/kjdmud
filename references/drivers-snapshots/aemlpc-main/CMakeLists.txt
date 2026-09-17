cmake_minimum_required(VERSION 3.20)
project(aemlpc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

option(AEMLPC_BUILD_TESTS "Build unit tests" ON)

# Warnings on by default for every target in the tree (set before the
# add_subdirectory calls below so it propagates to all of them). A clean
# build is warning-free as of the commit that added this. -Werror is
# deliberately not set: enforcing it needs a wider check across the
# compilers CI and contributors actually use, since GCC and Clang differ
# on which constructs they warn about.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

add_subdirectory(src/core)
add_subdirectory(src/config)
add_subdirectory(src/compiler)
add_subdirectory(src/vm)
add_subdirectory(src/security)
add_subdirectory(src/object)
add_subdirectory(src/net)
add_subdirectory(src/proto)
add_subdirectory(src/scheduler)
add_subdirectory(src/persist)
add_subdirectory(src/efun)
add_subdirectory(src/apply)
add_subdirectory(src/dialect)

add_executable(aemlpc src/main.cpp)
target_include_directories(aemlpc PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(aemlpc
    PRIVATE
        core config compiler vm object efun apply dialect net proto scheduler persist
)

if(AEMLPC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
