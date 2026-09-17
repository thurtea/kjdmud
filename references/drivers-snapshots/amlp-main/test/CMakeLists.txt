add_executable(amlp_tests
    test_lexer.cpp
)
target_include_directories(amlp_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
# Absolute repo root, baked in at configure time, so tests that read real
# on-disk files (e.g. mudlib/clone/wand_of_creation.c, see test_lexer.cpp's
# own readMudlibFile()) resolve correctly no matter what the process's own
# working directory happens to be at run time -- a direct run from build/,
# a ctest run (working directory build/test/), or anything else. Fixes a
# real ctest-vs-direct-run discrepancy: readMudlibFile() used to try a
# fixed list of CWD-relative paths ("../mudlib", "mudlib", "./mudlib"),
# none of which resolve from build/test/, ctest's own working directory
# for this target -- so "all tests pass" meant different things depending
# on which of the two ways the suite got invoked.
target_compile_definitions(amlp_tests PRIVATE AMLP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(amlp_tests
    PRIVATE
        core config compiler vm object efun apply dialect net scheduler persist
)
add_test(NAME amlp_tests COMMAND amlp_tests)
