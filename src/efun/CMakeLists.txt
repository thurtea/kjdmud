find_package(PkgConfig REQUIRED)
pkg_check_modules(PCRE2 REQUIRED libpcre2-8)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(LIBCRYPTO REQUIRED IMPORTED_TARGET libcrypto)

add_library(efun
    EfunTable.cpp
    NetEfuns.cpp
    ParserPackage.cpp
    DbRegistry.cpp
    TestResultsRegistry.cpp
)
target_include_directories(efun PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(efun PRIVATE ${PCRE2_INCLUDE_DIRS} ${SQLITE3_INCLUDE_DIRS} ${LIBCRYPTO_INCLUDE_DIRS})
target_link_libraries(efun PUBLIC core vm object net proto scheduler persist)
if(NOT APPLE)
    target_link_libraries(efun PUBLIC crypt)
endif()
target_link_libraries(efun PRIVATE ${PCRE2_LIBRARIES} ${SQLITE3_LIBRARIES} PkgConfig::LIBCRYPTO)
target_compile_definitions(efun PRIVATE PCRE2_CODE_UNIT_WIDTH=8)
