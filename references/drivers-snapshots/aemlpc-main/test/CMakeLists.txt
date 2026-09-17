add_executable(aemlpc_tests
    test_lexer.cpp
    test_net.cpp
)
target_include_directories(aemlpc_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
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
target_compile_definitions(aemlpc_tests PRIVATE AEMLPC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(aemlpc_tests
    PRIVATE
        core config compiler vm object efun apply dialect net proto scheduler persist
)
add_test(NAME aemlpc_tests COMMAND aemlpc_tests)

add_executable(object_manager_tests
    test_object_manager.cpp
)
target_include_directories(object_manager_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(object_manager_tests
    PRIVATE
        "-Wl,--start-group"
        core config compiler vm object efun apply dialect net proto scheduler persist
        "-Wl,--end-group"
)
add_test(NAME object_manager_tests COMMAND object_manager_tests)
