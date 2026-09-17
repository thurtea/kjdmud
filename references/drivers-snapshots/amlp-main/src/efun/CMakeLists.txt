find_package(PkgConfig REQUIRED)
pkg_check_modules(PCRE2 REQUIRED libpcre2-8)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(LIBCRYPTO REQUIRED libcrypto)

add_library(efun
    EfunTable.cpp
    ParserPackage.cpp
    DbRegistry.cpp
)
target_include_directories(efun PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(efun PRIVATE ${PCRE2_INCLUDE_DIRS} ${SQLITE3_INCLUDE_DIRS} ${LIBCRYPTO_INCLUDE_DIRS})
target_link_libraries(efun PUBLIC core vm object net scheduler persist crypt)
target_link_libraries(efun PRIVATE ${PCRE2_LIBRARIES} ${SQLITE3_LIBRARIES} ${LIBCRYPTO_LIBRARIES})
target_compile_definitions(efun PRIVATE PCRE2_CODE_UNIT_WIDTH=8)
