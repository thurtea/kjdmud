# src/core/ - Errors and Core Types

## What lives here

| File | Role |
|------|------|
| `Errors.cpp` + `include/.../Errors.hpp` | `LpcRuntimeError` (the base exception type for all LPC errors) and `LpcThrownValue` (in `Value.hpp`). |

## Files to read before touching this directory

- `include/amlp/core/Errors.hpp`
- `include/amlp/vm/Value.hpp` - `LpcThrownValue` definition

## Current state

`LpcRuntimeError` is a simple `std::runtime_error` subclass:
- `.what()` returns the error message string
- Used everywhere a LPC runtime error should abort the current eval

`LpcThrownValue` (in `Value.hpp`) is a subclass of `LpcRuntimeError` that
carries an arbitrary `Value` - the payload of `throw(value)` - so it can
be caught by `catch()` and the value returned to LPC code.

## Phase 0 tasks

No stub gaps here. However, before Phase 1 begins, improve error quality
across the driver:

- Every `throw LpcRuntimeError(...)` in the VM, compiler, and efun table
  should include: object filename, function name, and line number where the
  error occurred.
- The message format should match FluffOS's own error format:
  `"*Error: <message>\nObject: <path>\nFunction: <name>\nLine: <N>"`
- Add `struct ErrorContext { string filename; string function; int line; }`
  as a thread-local (or VM-member) value that the compiler/VM sets before
  each operation and the error catches.

## Phase 2 tasks

### 2.20 - Structured error objects: JSON-serializable diagnostics

Instead of flat `what()` strings, build a `StructuredError` type that carries:
- `string message` - the human-readable error text
- `string objectPath` - the LPC file where the error originated
- `string functionName`
- `int line`, `int column`
- `vector<StackFrame> callStack` - full LPC call stack at error time
- `string dialectHint` - which dialect rule triggered this (if applicable)

Serialization:
```cpp
std::string toJson() const;
static StructuredError fromJson(const std::string&);
```

`LpcRuntimeError::what()` continues to return the flat string for backward
compatibility. `StructuredError` is a new parallel path used by:
- The LSP server (Phase 2.19) for inline diagnostics
- The test runner efuns (Phase 2.22) for structured test output
- A new `last_error()` efun that mudlib error handlers can call to get the
  full structured error for the most recent error

## Key invariants

- `LpcRuntimeError` must remain the single base type for all LPC errors -
  never use `std::exception` directly in LPC-layer catch sites.
- `LpcThrownValue` must remain a subclass of `LpcRuntimeError`, not a sibling,
  so that generic `catch (LpcRuntimeError&)` sites can be upgraded to
  distinguish thrown values without breaking.
- `StructuredError` (Phase 2.20) must not replace `LpcRuntimeError` - it is
  additive. Existing error handling paths must continue to work unchanged.
