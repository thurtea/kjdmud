# test/ - gtest Regression Suite

## What lives here

| File | Role |
|------|------|
| `CMakeLists.txt` | Builds the test binary; links against all driver libs + gtest. |
| `test_lexer.cpp` | Current primary (and only) test file, 440 tests as of 2026-08-16. |

## How to run

```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Run a specific filter
ctest --test-dir build -R "lexer" --output-on-failure

# Run directly (more verbose)
./build/test/amlp_tests --gtest_filter="*sscanf*"
```

## Phase 0 requirement

**Before Phase 1 begins, every efun must have at least one regression test.**

The Phase 0 test-coverage audit:
1. List every registered efun name from `EfunTable.cpp`.
2. For each one, search `test_lexer.cpp` for a test that calls it via LPC code.
3. Any efun with no test gets one added - minimum: happy path + one error case.

New test files to create (one per subsystem):

| File | Covers |
|------|--------|
| `test_vm.cpp` | Every OpCode, throw/catch, eval-cost limit, atomic rollback |
| `test_efun_string.cpp` | All string efuns: `sprintf`, `sscanf`, `strlen`, `explode`, etc. |
| `test_efun_array.cpp` | All array efuns: `map`, `filter`, `sort_array`, `member_array`, etc. |
| `test_efun_mapping.cpp` | All mapping efuns: `keys`, `values`, `m_delete`, etc. |
| `test_efun_io.cpp` | `write_file`, `read_file`, `file_size`, `get_dir`, etc. |
| `test_efun_math.cpp` | `abs`, `min`, `max`, `pow`, `sqrt`, `random`, etc. |
| `test_efun_object.cpp` | `clone_object`, `destruct`, `find_object`, `users`, etc. |
| `test_object.cpp` | LpcObject lifecycle, destruct guards, shadow chain |
| `test_scheduler.cpp` | call_out timing, heartbeat, removal, error isolation |
| `test_net.cpp` | IAC parsing, echo suppression, NAWS, WebSocket handshake |
| `test_save.cpp` | `save_object`/`restore_object` in both custom and FluffOS formats |

## Testing conventions

### Test file structure

Each test file follows this pattern:
```cpp
#include <gtest/gtest.h>
#include "amlp/config/Config.hpp"
#include "amlp/object/ObjectManager.hpp"
#include "amlp/vm/VM.hpp"
#include "amlp/scheduler/Scheduler.hpp"
#include "amlp/net/Server.hpp"
#include "amlp/efun/EfunTable.hpp"

class FeatureTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.loadFromFile("etc/test.cfg");
        // Or set fields directly:
        // config_.mudlibRoot_ = "test/mudlib_stub";
        registerCoreEfuns();
        objects_ = std::make_unique<ObjectManager>(config_);
        vm_ = std::make_unique<VM>(*objects_, config_);
        objects_->setVM(vm_.get());
        objects_->loadMasterObject();
    }
    Config config_;
    std::unique_ptr<ObjectManager> objects_;
    std::unique_ptr<VM> vm_;
};
```

### How to drive LPC code in a test

Use `Server::dispatchLine()` to drive a full input line through an object:
```cpp
// Create a socket pair
int fds[2];
socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
auto conn = std::make_shared<Connection>(fds[0]);
Server::dispatchLine(*vm_, *conn, "go north");
```

For pure VM unit tests (no network), compile a snippet directly:
```cpp
auto prog = compiler_.compile("/some/object.c");
auto obj = std::make_shared<LpcObject>("/some/object", prog);
Value result = vm_->callFunction(obj, "my_func", {Value(42LL)});
EXPECT_EQ(std::get<int64_t>(result.data), 84LL);
```

## Phase 1 tasks

Add dialect-specific test files:

| File | Covers |
|------|--------|
| `test_dialect_fluffos.cpp` | FluffOS-specific: `(: :)` closures, `(*fp)()`, simul_efun |
| `test_dialect_ldmud.cpp` | LDMud-specific: `#'name`, `lambda()`, mapping width, shadows |
| `test_dialect_dgd.cpp` | DGD-specific: `nil`, `atomic` rollback, `rlimits`, LWOs |

Each dialect test must use `Config::setDialect(LpcDialect::X)` before creating
the VM to ensure the correct parser/runtime path is active.

## Phase 2 tasks

| File | Covers |
|------|--------|
| `test_async.cpp` | `async`/`await`, call_out_future, coroutine resume |
| `test_persist.cpp` | Statedump round-trip, object swapout/swapin |
| `test_jit.cpp` | JIT compilation of hot functions; output identical to interpreter |
| `test_lsp.cpp` | LSP protocol messages: textDocument/hover, definition, diagnostics |

## Key invariants

- Every test file must compile and pass in isolation - no test-order dependencies.
- Tests must not write to the real mudlib directory. Use `etc/test.cfg`
  pointing to `test/mudlib_stub`.
- Tests that create files (save_object, write_file) must clean up in `TearDown()`.
- Never remove or disable an existing test. A failing test is a blocking issue.
- The 440-test baseline must remain green on every commit.
