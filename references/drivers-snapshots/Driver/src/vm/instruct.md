# src/vm/ - VM Interpreter, Value Type, Bytecode

## What lives here

| File | Role |
|------|------|
| `VM.cpp` + `include/.../VM.hpp` | Stack-based bytecode interpreter. `VM::run()` is the main eval loop. |
| `Value.cpp` + `include/.../Value.hpp` | `Value` variant type: int/float/string/array/mapping/object/closure. Also `LpcThrownValue`. |
| `include/.../Bytecode.hpp` | `OpCode` enum + `FunctionEntry` + `CompiledProgram`. |

## Files to read before touching this directory

- `include/amlp/vm/Value.hpp` - the full `ValueVariant` definition
- `include/amlp/vm/Bytecode.hpp` - every OpCode and CompiledProgram
- `include/amlp/vm/VM.hpp` - public VM API
- `ROADMAP.md` Phase 0 and Phase 1 rows
- Reference: `fluffos-2.9-ds2.08/interpret.c` - the reference VM (the inline
  comments throughout `VM.cpp` cite specific line/function names from it)

## Phase 0 tasks

### 0.4 - Real `set_eval_limit` accumulated-cost model

Currently `maxEvalCost_` is checked as a flat per-`run()` ceiling and
`set_eval_limit` is a no-op.

**What to build:**
- Add a `int64_t evalCost_` field to VM that accumulates across all nested
  `run()` / `callFunction()` / `callClosure()` calls within a single player
  command dispatch.
- Reset it to 0 at the top of `Server::dispatchLine()` before the call into
  the VM (the real `process_user_command()` reset point).
- `set_eval_limit(n)` sets `config_.maxEvalCost_` for the rest of the current
  command execution, matching real FluffOS's own `set_eval_limit()` semantics.
- Throw a descriptive `LpcRuntimeError` when the limit is hit, matching the
  real "Too long eval" error string.

**Reference:** `fluffos-2.9-ds2.08/interpret.c` - `eval_cost` global and the
`MAX_COST` check in `eval_instruction()`.

## Phase 1 tasks

### 1.7 - LDMud `lambda()` / `unbound_lambda()` / `bind_lambda()` closure kinds

**Name correction (2026-08-17):** `bind()` does not exist as an LDMud efun --
the real name is `bind_lambda(closure cl [, object ob])`, confirmed against
`temp/ldmud/doc/efun/bind_lambda` and `temp/ldmud/src/closure.c`'s own
`v_bind_lambda()` (this project's vendored 3.6.8 clone). Investigated in real
depth this pass (see ROADMAP.md row 1.7 for the full writeup) while doing the
adjacent row 1.5 shadow work, **not implemented**, confirmed genuinely
bigger than a normal batch item. Two things the plan below does not yet
account for, found by reading `v_bind_lambda()` in full: (1) real
`CLOSURE_BOUND_LAMBDA` rebinding is reference-count-aware, a shared bound
lambda (`ref > 1`) gets copy-on-write cloned rather than rebound in place,
so `bind_lambda()` itself needs more than a single `Kind::UnboundLambda`
case; (2) a non-`this_object()` target `ob` goes through
`privilege_violation("bind_lambda", this_object(), ob)`, a master-apply
subsystem (`doc/concepts/privilege`) this driver has no equivalent of at
all, separate from the UID-based FluffOS applies already implemented and
from anything currently planned for closures.

**New `Closure::Kind` enum values** (add to `Value.hpp`):
- `Kind::MudosStyle` - current `(: :)` closures (rename from the implicit
  default)
- `Kind::LambdaArray` - LDMud `lambda()`: body is a `std::shared_ptr<Array>`
  that the VM executes as LPC bytecode (see below)
- `Kind::UnboundLambda` - like `LambdaArray` but has no bound `owner`; must be
  `bind_lambda()`-ed before calling
- `Kind::LdmudSymbol` - `#'name` reference: name is baked at construction; kind
  is one of `FP_EFUN`, `FP_LOCAL`, `FP_SIMUL` - resolve and cache on first call.
  **Naming note (2026-08-18, from the row 1.2/1.3 scoping pass, ROADMAP.md):**
  despite the name, this `Closure::Kind` has nothing to do with real
  LDMud's own `symbol` *value type*, a genuinely separate, previously
  undocumented construct confirmed this same pass: `'name` (a bare
  leading quote, `L_SYMBOL` in `temp/ldmud/src/lex.c`) produces an LPC
  `symbol` value, distinct from `#'name`'s `L_CLOSURE`, with real efuns
  `symbol_function()`/`symbol_variable()`/`symbolp()` and no equivalent
  anywhere in this driver's `Value` variant. Do not conflate the two when
  this row is actually picked up, `Kind::LdmudSymbol` here is about
  `#'name` closures only; a real `symbol` value would need its own new
  `ValueVariant` member entirely, not a `Closure::Kind`.

**`VM::callClosure()` extensions:**
- `LambdaArray`: extract the `Array` body, interpret each element as either a
  literal value or a sub-instruction array (this is LDMud's own "arrays-as-code"
  format; see LDMud source `closure.c`).
- `UnboundLambda`: throw "unbound lambda called without bind_lambda()" if the
  owner is unset.
- `LdmudSymbol`: resolve name on first call (same tiered lookup as `Call`
  opcode), cache the resolved `FP_*` kind + index in the `Closure` struct to
  avoid re-resolving on subsequent calls.

### 1.10 - DGD `nil` as a distinct type

Add `struct Nil {}` to the `ValueVariant` in `Value.hpp`:
```cpp
using ValueVariant = std::variant<
    std::monostate,   // void (uninitialized)
    Nil,              // DGD nil - absence of value, distinct from 0
    int64_t,
    double,
    std::string,
    ...
>;
```

**Semantic rules:**
- `nil == nil` → true
- `nil == 0` → false (unlike `monostate`, which coerces to 0 in FluffOS mode)
- `isTruthy(nil)` → false
- Arithmetic on `nil` → throw type error
- Only active when `LpcDialect::DGD` is set; in FluffOS/LDMud modes `nil`
  does not exist and the `Nil` variant is never constructed.

**Implemented 2026-08-18 (ROADMAP.md row 1.10, greenlit as row
1.2/1.3's next slice after `atomic`):** this plan turned out accurate on
every semantic rule above once checked against the real DGD source
(`temp/dgd/src/data.h`'s own `VAL_TRUE`/`VAL_NIL` macros,
`temp/dgd/src/comp/compile.cpp`'s `matchType()`): confirmed and
implemented exactly as written, plus one real nuance not previously on
record here: DGD's own `nil` is only genuinely distinct under **strict
typechecking** (`temp/dgd/src/data.cpp`'s own `"nil.type = (stricttc) ?
T_NIL : T_INT;"`, a global driver config level, not a per-file pragma);
this implementation targets that mode, since it is the only one where
`nil` means anything. `nil == 0` needed no special-case code at all --
`Value.cpp`'s own `valuesEqual()` already rejects mismatched
`ValueVariant` alternatives before any type-specific check runs, so
adding `Nil` as a new alternative made this true for free. Arithmetic/
comparison throwing a type error likewise needed zero new code in
`VM.cpp`: `asArithmeticOperand()` already only recognized `int64_t`/
`double`/`std::monostate`, so every `Add`/`Sub`/`Mul`/`Div`/`Mod`/`Lt`/
`Lte`/`Gt`/`Gte` opcode's own pre-existing generic fallback already
covers `Nil` correctly. See ROADMAP.md row 1.10 and `src/compiler/
instruct.md` for the full citation trail and the Lexer/Parser/CodeGen
half of this work.

### 1.11 - DGD `rlimits` statement

New opcodes in `Bytecode.hpp`:
- `PushRlimits` - operands: ticks (int32), stack_depth (int32)
- `PopRlimits` - restore the previous limits

In `VM::run()`:
- `PushRlimits`: push the current `(evalCost_, maxStackDepth_)` pair onto a
  per-VM `rlimitsStack_`; set the new limits.
- `PopRlimits`: pop the saved pair and restore.
- Throw "eval cost limit exceeded" / "stack too deep" when either limit is hit.

### 1.12 - DGD `atomic` function modifier: checkpoint/rollback

**What to build:**
1. Add `bool isAtomic` flag to `FunctionEntry` in `Bytecode.hpp`.
2. In `VM::callFunction()`, before running an `isAtomic` function, snapshot
   the current `variables()` of every object currently on the call stack
   (the "write set"). Use a lightweight copy: `vector<pair<LpcObject*,
   vector<Value>>>`.
3. Wrap the body in a try/catch. On any `LpcRuntimeError`:
   - Restore all snapshotted `variables()` from the checkpoint.
   - Flush any buffered output queued during the atomic call.
   - Re-throw the error to the nearest enclosing `catch()` frame, as DGD does.
4. On success: discard the snapshot, commit buffered output normally.

**Reference:** DGD source `src/call_out.c`, `src/interpret.c` atomic notes;
chattheatre.github.io/lpc-doc/dgd/unusual.html.

## Phase 2 tasks

### 2.5 + 2.6 - C++20 coroutine scheduler + LPC `async`/`await`

New `OpCode::Suspend` - when the VM hits this opcode it:
1. Saves the full stack frame (locals, program counter, call-stack) into a
   heap-allocated `TaskFrame`.
2. Returns a sentinel `Value::Suspended` to the caller.
3. The `Scheduler` receives the `TaskFrame` and re-enqueues it after the
   `await`-ed delay.
4. On resume, `VM::resumeTask(TaskFrame&)` restores the frame and continues.

Use C++20 stackless coroutines (`co_await`) inside the C++ scheduler to model
the suspend/resume cleanly without OS threads.

### 2.10 - Closure bake-at-construction

Instead of storing just `functionName` (a string to re-resolve at every call),
resolve to a `FP_*` kind + index at `MakeClosure` opcode time:
- `FP_LOCAL` - function found in the current object's own program
- `FP_INHERITED` - found in an inherited program (store program index)
- `FP_SIMUL` - found in the simul_efun object
- `FP_EFUN` - found in the efun table (store efun table index)

Cache result in `Closure::resolvedKind` + `Closure::resolvedIndex` (add these
fields). `callClosure()` fast-paths on a non-zero `resolvedKind` rather than
re-running the tiered name walk.

## Testing

The VM test suite is currently spread across `test/test_lexer.cpp` and inline
integration tests through `Server::dispatchLine`. Add a dedicated
`test/test_vm.cpp` that exercises:
- Every `OpCode` directly (compile a tiny program, run it, check result)
- `throw()` / `catch()` interaction
- `rlimits` limit enforcement
- `atomic` rollback on error

```bash
ctest --test-dir build -R "vm" --output-on-failure
```

## Key invariants

- The `Value` variant must never grow a new alternative without updating
  `isTruthy()`, `valuesEqual()`, and every `switch`/`visit` pattern in the VM.
- `Nil` variant is only constructed when `LpcDialect::DGD` is active.
- The four-tier call resolution in `findFunctionInChain()` must not be bypassed
  by any new opcode without a documented reason.
- `LpcThrownValue` is a subclass of `LpcRuntimeError` - every catch site that
  handles `LpcRuntimeError` must decide whether to re-throw or consume a
  `LpcThrownValue`. See the comment in `Value.hpp`.
