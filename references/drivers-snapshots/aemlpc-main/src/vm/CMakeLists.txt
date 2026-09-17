find_package(PkgConfig REQUIRED)
pkg_check_modules(PCRE2 REQUIRED libpcre2-8)

add_library(vm
    VM.cpp
    Value.cpp
)
target_include_directories(vm PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(vm PRIVATE ${PCRE2_INCLUDE_DIRS})
target_link_libraries(vm PUBLIC core PRIVATE ${PCRE2_LIBRARIES})
target_compile_definitions(vm PRIVATE PCRE2_CODE_UNIT_WIDTH=8)
