# src/object/ - LpcObject, ObjectManager, LivingNameRegistry

## What lives here

| File | Role |
|------|------|
| `LpcObject.cpp` + `include/.../LpcObject.hpp` | One LPC object instance: filename, program, variables, heartbeat interval, environment/inventory, living name, interactive state. |
| `ObjectManager.cpp` + `include/.../ObjectManager.hpp` | Object cache, compile, clone, destruct, simul_efun, master, virtual objects. |
| `LivingNameRegistry.cpp` + `include/.../LivingNameRegistry.hpp` | `set_living_name` → name lookup table for `find_living`/`find_player`. |

## Files to read before touching this directory

- `include/amlp/object/LpcObject.hpp` - full LpcObject API
- `include/amlp/object/ObjectManager.hpp`
- Reference: `fluffos-2.9-ds2.08/object.h` - `object_t` fields
- Reference: `fluffos-2.9-ds2.08/simulate.c` - `load_object`, `clone_object`,
  `destruct_object`, `load_virtual_object`
- Reference: `fluffos-2.9-ds2.08/add_action.c` - `hashed_living[]` table
  (already replicated as `LivingNameRegistry`)

## Phase 0 tasks

### 0.5 - Full `O_DESTRUCTED` apply guards

**Problem:** Currently `ObjectManager::destructObject()` removes the object
from the cache but does not prevent already-held `shared_ptr<LpcObject>`
references from invoking functions on the destroyed object. Real FluffOS sets
`O_DESTRUCTED` on the object and `VM::callFunction()` / `applyMaster()` /
`callClosure()` all check that flag before making any call.

**What to build:**
1. Add `bool destructed_ = false;` to `LpcObject`.
2. Add `bool isDestructed() const { return destructed_; }`.
3. `ObjectManager::destructObject()` sets `destructed_ = true` on the object
   in addition to removing it from the cache.
4. `VM::callFunction()` checks `obj->isDestructed()` at entry and throws
   `"call on destructed object"` if true, matching FluffOS's own message.
5. `VM::callClosure()` does the same via `Closure::owner.lock()` (already a
   `weak_ptr`; a null lock means destructed - this is already partially
   handled, but the error message should match FluffOS's exact string).
6. Every `apply*` call in `ApplyTable` must also check `isDestructed()` before
   the call.

**Reference:** `fluffos-2.9-ds2.08/object.h` `O_DESTRUCTED` flag;
`interpret.c`'s `apply_low()` check.

### 0.6 - Shadow support

Shadows allow one object to intercept `call_other()` calls to another.

**What to build:**
1. Add `std::shared_ptr<LpcObject> shadow_` and
   `std::weak_ptr<LpcObject> shadowedBy_` to `LpcObject`.
2. New `shadow(object ob, int flag)` efun (in `src/efun`): attaches the calling
   object as a shadow of `ob`; checks `query_allow_shadow()` on `ob`.
3. In `VM::callFunction()`, before the tiered lookup: if `obj->shadowedBy_` is
   non-null, first attempt the call on the shadow object. If the shadow defines
   the function and returns a truthy value, use that result. Otherwise fall
   through to `obj` itself. This is the real FluffOS shadow-chain traversal
   (see `simulate.c`'s `apply_low()` shadow walk).
4. `query_shadowing(ob)` efun: returns the shadow object or 0.

**Reference:** `fluffos-2.9-ds2.08/simulate.c` `apply_low()` shadow section.

## Phase 1 tasks

### 1.6 - LDMud `replaces` in inherit (resolved: no such directive exists)

This section (formerly mislabeled "1.5", ROADMAP's row is actually 1.6)
described `inherit "path" replaces "other_path";` as a compile-time
directive `ObjectManager` would need to resolve. That premise does not
match real LDMud: re-grepped `temp/ldmud/src/prolang.y`'s own
`inheritance_qualifier`/`inheritance_modifier` productions and there is no
`replaces` token anywhere in the inherit grammar (the complete modifier
set is `static`/`private`/`public`/`protected`/`nosave`/`nomask`/
`deprecated`/`virtual`/`visible`). Nothing here for `src/object` or
`ObjectManager` to build: the real per-dialect divergence turned out to
live entirely in the `replace_program()` **efun**'s own argument handling
(a runtime call, not an `inherit` directive), implemented 2026-08-17 in
`src/efun/EfunTable.cpp`. See ROADMAP.md row 1.6 for the full citation
trail and `src/efun/instruct.md` for the efun-side detail.

### 1.5 - LDMud shadows (resolved: efun-level signature work, not `src/object`)

Section 0.6 above names the FluffOS master apply as `query_allow_shadow()`
-- that was wrong even for FluffOS at the time it was written (the real
FluffOS apply is `valid_shadow()`, `applies_table.c`'s own
`APPLY_VALID_SHADOW`, and that is what the actual implementation in
`src/efun/EfunTable.cpp` correctly uses). `query_allow_shadow()` is real,
but it is LDMud's name, not FluffOS's, confirmed against
`temp/ldmud/doc/master/query_allow_shadow`. Row 1.5 (LDMud shadow work)
turned out to need no changes here in `src/object` at all: the shadow-chain
`call_other` redirection built under 0.6 (`VM::callFunction()`'s shadow
walk) is already dialect-agnostic and matches LDMud's own documented
model too (`temp/ldmud/doc/efun/shadow`'s "not even object B can
call_other() itself", already covered by the existing `!= current_object`
re-entry guard). The real, narrow divergence, `shadow()`'s own argument
count/return type/master-apply-name, plus the LDMud-only `unshadow()`
efun: lives entirely in `src/efun/EfunTable.cpp`, gated on
`Config::dialect()`, implemented 2026-08-17. See ROADMAP.md row 1.5 for
the full citation trail.

### 1.14 - DGD Lightweight Objects (LWOs)

LWOs are value-semantics objects: they are copied when passed across
timeslice/task boundaries rather than shared by reference.

**What to build:**
1. Add `bool isLightweight_ = false;` to `LpcObject`.
2. New `new_object(path)` efun (DGD dialect only): loads the file but creates
   an LWO rather than a persistent object - the result is not inserted into
   the ObjectManager cache.
3. `VM::callFunction()` on an LWO does NOT look it up by name - it operates
   directly on the passed `shared_ptr`.
4. When a coroutine task boundary is crossed (Phase 2 - `Suspend` opcode),
   any LWO values on the stack are deep-copied.

### 2.21 - Hot-reload: recompile + migrate live object instances

When a `.c` file is modified while the server is running:
1. Recompile it to a new `CompiledProgram`.
2. For every live `LpcObject` in the cache whose `filename()` matches:
   a. Save each object's current `variables()` by name (not by index, since
      the variable layout may have changed).
   b. Replace `program_` with the new `CompiledProgram`.
   c. Resize `variables_` to the new variable count.
   d. Restore saved variables by name where names still match.
3. If the object has a `create()` function and the new program version defines
   it, optionally re-run `create()` (configurable - some mudlibs rely on
   create() idempotency for hot-reload).

**Trigger:** a new `reload_object(path)` efun (Phase 2e scope).

## Testing

Add `test/test_object.cpp`:
- destruct + call → error
- shadow chain traversal
- living name registry round-trip
- hot-reload variable migration

## Key invariants

- `LpcObject` is always held as `shared_ptr<LpcObject>` - never a raw pointer.
- Every `shared_ptr<LpcObject>` that crosses a call boundary must check
  `isDestructed()` before use (Phase 0.5).
- The object cache key is the normalized filename (no trailing `.c`).
- `LivingNameRegistry` stores `weak_ptr<LpcObject>` - a stale entry is
  silently skipped on lookup, matching real FluffOS behavior.
