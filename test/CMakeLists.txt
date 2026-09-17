add_executable(kjdmud_tests
    test_lexer.cpp
    test_net.cpp
)
target_include_directories(kjdmud_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
# Absolute repo root, baked in at configure time, so tests that read real
# on-disk files (e.g. mudlib/clone/wand_of_creation.c, see test_lexer.cpp's
# own readMudlibFile()) resolve correctly no matter what the process's own
# working directory happens to be at run time. A direct run from build/,
# a ctest run (working directory build/test/), or anything else. Fixes a
# real ctest-vs-direct-run discrepancy: readMudlibFile() used to try a
# fixed list of CWD-relative paths ("../mudlib", "mudlib", "./mudlib"),
# none of which resolve from build/test/, ctest's own working directory
# for this target. So "all tests pass" meant different things depending
# on which of the two ways the suite got invoked.
target_compile_definitions(kjdmud_tests PRIVATE KJDMUD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(kjdmud_tests
    PRIVATE
        core config compiler vm object efun apply dialect net proto scheduler persist
)
add_test(NAME kjdmud_tests COMMAND kjdmud_tests)

add_executable(object_manager_tests
    test_object_manager.cpp
)
target_include_directories(object_manager_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
# GNU ld needs --start-group for this target's static-lib cycle. Apple ld
# rejects those flags, so use the same flat link line as kjdmud_tests.
set(_kjdmud_om_libs core config compiler vm object efun apply dialect net proto scheduler persist)
if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" OR APPLE)
    target_link_libraries(object_manager_tests PRIVATE ${_kjdmud_om_libs})
else()
    target_link_libraries(object_manager_tests
        PRIVATE
            "-Wl,--start-group"
            ${_kjdmud_om_libs}
            "-Wl,--end-group"
    )
endif()
add_test(NAME object_manager_tests COMMAND object_manager_tests)
