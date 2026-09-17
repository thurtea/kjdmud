# src/dialect/ - Dialect Enum, Config, and Dispatch (Phase 1a)

## Purpose

This directory owns the single source of truth for **which LPC dialect the
driver is running** and provides the concrete `BootApi` subclasses that
`src/apply` uses to route master/simul_efun/driver-object applies.

This is a **Phase 1 directory** - it does not exist yet as compiled code.
Create it only after Phase 0 is complete and the test suite is green.

## Files to create

### `include/amlp/dialect/LpcDialect.hpp`

```cpp
#pragma once
#include <string>
namespace amlp {

enum class LpcDialect {
    FluffOS,   // MudOS/FluffOS (: :) LPC - current default
    LdMud,     // Amylaar/LDMud #' lambda() LPC
    DGD,       // Dworkin's Generic Driver nil/atomic/rlimits LPC
};

const char* dialectName(LpcDialect d);  // "fluffos" / "ldmud" / "dgd"
LpcDialect dialectFromString(const std::string& s); // throws on unknown

} // namespace amlp
```

### `include/amlp/dialect/BootApi.hpp`

Abstract interface - see `src/apply/instruct.md` Phase 1.4 for the full
interface definition. Place the abstract base here; concrete implementations
in the `.cpp` files below.

### `src/dialect/FluffOsBootApi.cpp` + header

FluffOS/MudOS apply names:
- `connectApply()` → `"connect"`
- `logonApply()` → `"logon"`
- `compileObjectApply()` → `"compile_object"`
- `privsFileApply()` → `"privs_file"`
- `netDeadApply()` → `"net_dead"`
- `heartBeatErrorApply()` → `"heart_beat_error"`
- `hasAutoObject()` → false
- `simulEfunFile()` → `Config::simulEfunFile()` (may be empty)

### `src/dialect/LdmudBootApi.cpp` + header

**Corrected 2026-08-22** against a real LDMud 3.6.8 clone (`temp/ldmud`,
report-only comparison session, no code changes) - several names below were
wrong in earlier drafts of this file; see `src/apply/instruct.md`'s own
§1.16 for the full citations.

LDMud apply names - all of the above, plus:
- `"get_master_uid"` (**not** `"get_root_uid"` - that name was superseded in
  LDMud 3.2.1@40, confirmed via `doc/master/get_master_uid`'s own HISTORY
  line: "Introduced in 3.2.1@40 replacing get_root_uid()." This clone is
  3.6.8, many releases past that rename), `"get_bb_uid"`, `"valid_read"`,
  `"valid_write"`, `"make_path_absolute"`, `"query_allow_shadow"` (master-
  side shadow gate - confirmed real and correctly named already,
  implemented 2026-08-17, row 1.5)
  **2026-08-18, all three of the remaining names investigated in full,
  none small enough to implement this pass: see ROADMAP.md row 1.16 for
  the complete citations:** `get_bb_uid` is genuinely dead in this exact
  vendored 3.6.8 driver - grepped `STR_GET_BB_UID` across every real `.c`/
  `.h` file, zero call sites; `doc/master/get_bb_uid`'s own claim that
  `process_string()` calls it does not match `f_process_string()`'s/
  `process_value()`'s actual C, read in full. `make_path_absolute` has
  exactly one real call site, `ed.c`'s own line-editor filename
  resolution - blocked on this driver having no `ed()` efun at all
  (already excluded, `src/efun/instruct.md`). `valid_read`/`valid_write`
  turned out not LDMud-specific at all - FluffOS has the identical real
  applies (`fluffos-2.9-ds2.08/applies.h`'s own `APPLY_VALID_READ`/
  `APPLY_VALID_WRITE`), and this driver gates none of its 11 existing
  file efuns with either dialect's version yet; a whole missing
  cross-cutting feature, sized like `parse_*`, not a signature divergence.
- `"valid_snoop"`, `"valid_query_snoop"` - real LDMud master applies
  (`doc/master/valid_snoop`, `doc/master/valid_query_snoop`), not previously
  documented anywhere in this repo. Directly relevant: Phase 0's own snoop
  family work found real FluffOS 2.9-ds2.08 has **no** master-level snoop
  gate at all (no `APPLY_VALID_SNOOP` in `applies.h`) - LDMud genuinely
  differs here, confirming this is a real per-dialect divergence, not a
  FluffOS-build quirk.
  **`valid_snoop` implemented 2026-08-17 (ROADMAP row 1.16), alongside a
  fuller re-scope of `snoop()` than this note alone implied.** Reading
  `temp/ldmud/src/comm.c`'s own `set_snoop()`/`v_snoop()` directly (not
  just `doc/efun/snoop`'s prose, which turned out stale) surfaced two more
  real divergences: the apply is called for both the start *and* stop
  forms, and real `snoop()` returns a plain `int` (1/0/-1), never the
  object, despite the doc's own "object snoop(...)" SYNOPSIS text --
  `func_spec`'s `"int snoop(object, void|object);"` and `v_snoop()`'s own
  `put_number(sp, i)` are authoritative. `valid_query_snoop` intentionally
  left unimplemented: it exists only to gate `query_snoop()`, which is
  itself obsolete in this exact 3.6.8 clone
  (`temp/ldmud/doc/obsolete/query_snoop`) - real modern LDMud replaced it
  with `interactive_info(ob, II_SNOOP_*)`, a materially larger, different
  efun this driver has no equivalent of at all. See ROADMAP.md row 1.16
  for the full citation trail.
- `hasAutoObject()` → false

Two more corrections, not apply names but signature/mechanism errors an
earlier draft of this file carried:
- **`shadow(ob, flag)` is FluffOS's real signature, not LDMud's.** Real
  LDMud (`doc/efun/shadow`): `int shadow(object ob)` - one argument,
  returns int 1/0, not an object. LDMud's shadow permission is two-layered:
  a victim-side opt-out (`query_prevent_shadow()`, defined *on the victim
  object itself*, not a master apply) plus the master-side
  `query_allow_shadow()` already listed above. `src/object/instruct.md`
  should be checked against this the next time LDMud shadow semantics are
  actually implemented (Phase 1, not now).
  **Resolved 2026-08-17:** implemented, and one part of this note itself
  turned out to need correcting once `temp/ldmud/src/simulate.c`'s
  `validate_shadowing()` was read directly rather than inferred from the
  doc's prose: the "victim-side opt-out" is not a second driver-enforced
  layer at all. The driver only ever calls one apply,
  `query_allow_shadow()`; whether *that* apply's own LPC body consults
  the victim (e.g. `victim->prevent_shadow(...)`) is entirely a mudlib
  convention, not something `validate_shadowing()` itself checks, and
  even LDMud's own two doc files disagree on the convention's name
  (`doc/efun/shadow` says `query_prevent_shadow()`,
  `doc/master/query_allow_shadow` says `prevent_shadow()`), further
  evidence it is prose, not grammar. Also found and ported: LDMud's
  `validate_shadowing()` has no "cannot shadow the master object" guard
  at all (FluffOS's does), and real LDMud has a second, FluffOS-absent
  efun, `void unshadow(void)`. See ROADMAP.md row 1.5 for the full
  citation trail; implemented in `src/efun/EfunTable.cpp` with 4 new
  regression tests.
- **`bind()` does not exist as an LDMud efun.** The real name is
  `bind_lambda(closure, object ob)` (`doc/efun/bind_lambda`) - rebinds an
  unbound lambda, or an efun/simul-efun/operator closure, to a different
  object. Row 1.7's "`bind()`" should read `bind_lambda()`.
  **Investigated 2026-08-17, not implemented:** read
  `temp/ldmud/src/closure.c`'s own `v_bind_lambda()` in full. Genuinely
  bigger than a normal batch item: it switches on a closure-kind
  distinction (`CLOSURE_LFUN`/`CLOSURE_BOUND_LAMBDA`/
  `CLOSURE_UNBOUND_LAMBDA`/efun-simul_efun-operator, including
  reference-count-aware copy-on-write for shared bound lambdas) this
  driver's single flat `Closure` struct has no equivalent of, and gates
  non-self targets through `privilege_violation()`, a master-apply
  subsystem that doesn't exist here at all. Row 1.7 itself
  (`lambda()`/`unbound_lambda()`/`#'symbol`) is still entirely
  unimplemented, so there is not yet a real bound/unbound lambda value
  for a faithful `bind_lambda()` to rebind. See ROADMAP.md row 1.7 and
  `src/vm/instruct.md`'s own matching note for the full scope and the
  options left for whoever picks this up.
- **The `replaces` directive in `inherit` does not exist anywhere in real
  LDMud.** Grepped the actual grammar (`src/prolang.y`'s own
  `inheritance_qualifier`/`inheritance_modifier` productions): the full set
  of inheritance modifiers is `static`/`private`/`public`/`protected`/
  `nosave`/`nomask`/`deprecated`/`virtual` - no `replaces` token anywhere in
  the source tree. The real, closest LDMud feature is `replace_program()`
  (`doc/efun/replace_program`) - a **runtime efun**, not a compile-time
  `inherit` modifier: it shrinks a live object down to one of its
  already-inherited programs (to save memory/cache pressure), deferred to
  the end of the current backend cycle, and stops any shadowing on the
  object when it takes effect. This is unrelated to `inherit`'s own
  grammar. **Row 1.6 may need rescoping, not just a rename** - "a directive
  in `inherit`" and "a runtime efun that swaps a live object's program"
  are different-shaped features to implement (compiler-side vs VM-side),
  and whoever picks up 1.6 should re-read `doc/efun/replace_program` in
  full before deciding what the row is actually asking for.
  **Resolved 2026-08-17:** rescoped and implemented. Reading
  `doc/efun/replace_program` + `src/object.c`'s `v_replace_program()` in
  full surfaced the real, genuine per-dialect divergence: LDMud's
  `replace_program()` accepts a **zero-argument** call and auto-selects
  the object's sole direct inherit, something real FluffOS's
  `f_replace_program()` (`replace_program.c`) never allows (mandatory
  string argument, unconditionally). `replace_program(string)` itself was
  already FluffOS-scoped and done under row 0.13; this row's real, narrow
  scope was just the LDMud-only no-arg form, implemented in
  `src/efun/EfunTable.cpp` gated on `Config::dialect() == "ldmud"`, with
  3 new regression tests. See ROADMAP.md row 1.6 for the full citation
  trail.
- **Link-death notification is `disconnect(object ob, string remaining)`,
  a master apply - not `net_dead`, and not object-level.** Confirmed via
  `src/comm.c`: `callback_master(STR_DISCONNECT, 2)`, and
  `doc/master/disconnect`'s own signature. This is architecturally
  different from FluffOS's `net_dead()`, which is applied directly on the
  disconnected player object, not on master. The name `net_dead` belongs
  to FluffOS only - see the open design note under DgdBootApi below, this
  affects `BootApi`'s whole connect/disconnect abstraction, not just a
  string swap.

### `src/dialect/DgdBootApi.cpp` + header

**Corrected 2026-08-22** against a real DGD clone (`temp/dgd`, same
comparison session) - the driver-object apply surface below was wrong on
three of six previously-documented names; see `src/apply/instruct.md`'s
own §1.4/§1.15 for the full citations and the grep methodology.

DGD driver+auto model:
- `masterFile()` → driver object path (e.g. `/kernel/driver`)
- `simulEfunFile()` → `std::nullopt`
- **There is no `"connect"` callback.** Real DGD has three separate,
  port-type-specific driver-object callbacks instead:
  `"telnet_connect"`, `"binary_connect"`, `"datagram_connect"`
  (`src/comm.cpp`), each returning the persistent object for that
  connection type. `connectApply()` returning one string cannot model this
  - see the open design note below.
- **There is no `"compile_object"` callback.** DGD has no virtual-object
  concept at all - objects are always compiled from real, on-disk `.c`
  files. The nearest real analog is `"call_object"` (`string path` arg),
  fired specifically from `call_other()`'s string-target resolution
  (`src/kfun/builtin.cpp`), not a load-time missing-file fallback.
  `compileObjectApply()` should not be assumed for DGD.
- `"initialize"` - driver object boot callback (confirmed correct)
- `"path_read"` / `"path_write"` - path permission callbacks (confirmed
  correct)
- **There is no `"disconnect"` callback of any name on the driver
  object.** (Earlier drafts of this file claimed `"disconnect"` was "DGD's
  name for net_dead" - wrong on both counts: `"disconnect"` is LDMud's real
  master-apply name, not DGD's, and DGD's actual connection-loss
  notification is `close(int destructing)`, called **on the persistent
  user object itself** (`src/comm.cpp`'s `Comm::close()`) - architecturally
  like FluffOS's object-level `net_dead()`, just a different name. See the
  open design note below.
- Full real driver-object apply surface, confirmed by grepping every
  `DGD::callDriver(...)`/`callCritical(...)` call site in `src/`:
  `binary_connect`, `call_object`, `compile_error`, `compile_rlimits`,
  `datagram_connect`, `include_file`, `inherit_program`, `initialize`,
  `interrupt`, `object_type`, `path_read`, `path_write`, `recompile`,
  `restored`, `runtime_rlimits`, `telnet_connect`, `touch`, `atomic_error`.
  Two of these are the real hooks behind ROADMAP row 1.11's `rlimits`
  statement and were not previously documented anywhere: `compile_rlimits`
  fires at compile time when the parser encounters an `rlimits` statement,
  `runtime_rlimits` fires when a compiled `rlimits` scope is actually
  entered at runtime.
- `hasAutoObject()` → true
- `autoObjectFile()` → `Config::autoObjectFile()` (e.g. `/kernel/auto` -
  confirmed real and configurable: `auto_object`/`driver_object` are both
  genuine, user-set DGD config keys, not driver-hardcoded paths; `/kernel/auto`
  is just kernel-mudlib convention for the value)

**Open design note (do not resolve now, just don't lose track of it):**
`BootApi`'s current shape assumes one apply name per concept
(`connectApply()` returns a single string, `netDeadApply()` returns a
single string). That pattern does not cleanly fit any of the three
dialects for connect/disconnect once 1.4/1.15/1.16 are actually
implemented:
- FluffOS: `connect` is one master apply; `net_dead` is one apply, but
  called on the *player object*, not master.
- DGD: connect is a **three-way fork by port type** (`telnet_connect` /
  `binary_connect` / `datagram_connect`), each a driver-object callback;
  disconnect (`close`) is called on the *persistent user object*, not the
  driver.
- LDMud: connect is one master apply; disconnect is one master apply
  (`disconnect(object ob, string remaining)`), called on **master**, not
  on the player object.

So "connect" needs either a per-dialect dispatch strategy (not a plain
string) for DGD's three-way fork, and "disconnect/net_dead" needs the
`BootApi` interface to also carry *where* the apply is called (master vs
driver-object vs the affected object itself), which the current
`virtual std::string netDeadApply() const = 0;`-style signature has no way
to express. Whoever implements 1.4/1.15/1.16 needs to resolve this design
question first - a single `std::string` return type per concept is not
enough.

### `src/dialect/DialectFactory.cpp` + header

```cpp
std::unique_ptr<BootApi> makeBootApi(LpcDialect d, const Config& config);
```

Called from `main.cpp` after `Config::loadFromFile()`.

## How dialect flows through the driver

```
Config::loadFromFile()
  → Config::dialect() returns LpcDialect enum
    → DialectFactory::makeBootApi(dialect, config)
      → unique_ptr<BootApi> passed to:
        - ApplyTable  (apply name routing)
        - ObjectManager (auto-inherit injection when DGD)
        - Lexer / Parser (dialect-specific token set)
        - VM (nil/atomic/rlimits behavior flags)
```

## CMakeLists.txt for this directory

```cmake
add_library(dialect STATIC
    FluffOsBootApi.cpp
    LdmudBootApi.cpp
    DgdBootApi.cpp
    DialectFactory.cpp
)
target_include_directories(dialect PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(dialect PUBLIC config)
```

Add `add_subdirectory(src/dialect)` and `dialect` to the link line in the
top-level `driver/CMakeLists.txt`.

## Testing

`test/test_dialect_fluffos.cpp`, `test_dialect_ldmud.cpp`,
`test_dialect_dgd.cpp` - see `test/instruct.md` Phase 1 task list.

Each test sets `Config::dialect_` and verifies correct apply names,
token recognition, and runtime semantic differences.
