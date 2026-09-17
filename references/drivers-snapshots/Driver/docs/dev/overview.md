# driver/ - Top-Level Entry Point

## What this directory is

The entire C++20 LPC driver lives here. It compiles to the `amlp` binary
that boots the MUD, compiles and runs LPC source files, manages objects,
dispatches player input, and fires timers.

## How to build and test

```bash
# Configure (once)
cmake -B build -S .

# Build
cmake --build build

# Run all tests
ctest --test-dir build --output-on-failure

# Run the driver
build/amlp etc/driver.cfg
```

Current baseline: **440 tests passing**. Every change must keep this green.

## Directory map

| Path | Owns | instruct.md |
|------|------|-------------|
| `src/compiler/` | Lexer, Parser, CodeGen, AST | [link](src/compiler/instruct.md) |
| `src/vm/` | VM interpreter, Value type, Bytecode | [link](src/vm/instruct.md) |
| `src/object/` | LpcObject, ObjectManager, LivingNameRegistry | [link](src/object/instruct.md) |
| `src/efun/` | EfunTable, all ~153 (target 300+) registered efuns | [link](src/efun/instruct.md) |
| `src/net/` | TCP server, Connection, telnet, sockets | [link](src/net/instruct.md) |
| `src/scheduler/` | call_out, heart_beat, async tasks | [link](src/scheduler/instruct.md) |
| `src/apply/` | ApplyTable, master/simul_efun apply dispatch | [link](src/apply/instruct.md) |
| `src/config/` | Config, driver.cfg parsing | [link](src/config/instruct.md) |
| `src/core/` | Errors, LpcRuntimeError, LpcThrownValue | [link](src/core/instruct.md) |
| `test/` | hand-rolled assert()-based suite (single file, no gtest) for all subsystems | [link](test/instruct.md) |
| `src/dialect/` *(Phase 1)* | Dialect enum, dialect-aware dispatch | [link](src/dialect/instruct.md) |
| `src/persist/` *(Phase 2a)* | Statedumps, hotboot, object swapout | [link](src/persist/instruct.md) |
| `src/jit/` *(Phase 2c)* | LLVM JIT backend | [link](src/jit/instruct.md) |
| `src/lsp/` *(Phase 2e)* | Language Server Protocol server | [link](src/lsp/instruct.md) |
| `src/gc/` *(Phase 3)* | Generational garbage collector | [link](src/gc/instruct.md) |
| `src/security/` *(Phase 3)* | privs_file / uid/gid trust hierarchy | [link](src/security/instruct.md) |
| `src/proto/` *(Phase 3)* | GMCP, MSDP, MSSP, MTTS, MXP protocol handlers | [link](src/proto/instruct.md) |

## Master roadmap

See [`ROADMAP.md`](ROADMAP.md) for the full phased plan with
per-task status checkboxes.

## Key invariants

- Namespace: everything lives in `namespace amlp`.
- Headers live under `include/amlp/<subsystem>/FileName.hpp`.
- Sources live under `src/<subsystem>/FileName.cpp`.
- Each subsystem is its own CMake library target; `CMakeLists.txt` links them.
- Every new efun must have at least one regression test in `test/test_lexer.cpp`.
- Never break `O_DESTRUCTED` apply guards once implemented (Phase 0.5).
- The four-tier call resolution order (local, inherited, simul_efun, efun
  table) must be preserved in all VM changes.
