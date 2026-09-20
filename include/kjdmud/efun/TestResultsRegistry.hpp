#pragma once
#include <string>
#include <vector>

namespace kjdmud {

// Backs the LPC-native test runner efuns (ROADMAP.md row 2.22):
// assert_equal, assert_not_equal, assert_throws, test_pass, test_fail,
// run_tests. These are driver-added conveniences for writing tests
// directly in LPC test files, not ported from any real FluffOS/LDMud/DGD
// driver. There is no func_spec.c citation for any of them.
//
// One process-wide buffer, matching DbRegistry/SocketRegistry's own
// established precedent for efun-backing state (this driver is
// single-threaded, so no locking). run_tests() clears the buffer before
// running the target object's own test_* functions; test_pass()/
// test_fail() append to whatever the current buffer is, whether called
// from inside a run_tests() pass or directly from any other LPC code.
struct TestResultEntry {
    std::string name;
    bool passed = false;
    std::string message;
};

class TestResultsRegistry {
public:
    static void clear();
    static void record(const std::string& name, bool passed, const std::string& message);
    static const std::vector<TestResultEntry>& entries();

    // Minimal hand-rolled JSON array writer for exactly this one shape
    // ({"name":...,"passed":...,"message":...}), not a general encoder.
    // ROADMAP.md row 2.22's own "write structured results to a JSON file
    // for CI integration" line is the only JSON requirement run_tests()
    // has. The general json_encode()/json_decode() efun pair (row 2.17)
    // is separate, unimplemented work this does not depend on.
    static bool writeJsonFile(const std::string& path);
};

} // namespace kjdmud
