# src/apply/ - ApplyTable: Master/SimulEfun Apply Dispatch

## What lives here

| File | Role |
|------|------|
| `ApplyTable.cpp` + `include/.../ApplyTable.hpp` | Named apply dispatch: calls a function by string name on the master or simul_efun object. Backs `VM::applyMaster()`. |

An "apply" is a driver-to-mudlib callback: the driver calls a specific function
on a specific object by name. Examples: `connect()`, `logon()`,
`compile_object()`, `valid_read()`, `heart_beat()`.

## Files to read before touching this directory

- `include/amlp/apply/ApplyTable.hpp`
- Reference: `fluffos-2.9-ds2.08/applies.h` - the real apply name constants
- Reference: `fluffos-2.9-ds2.08/simulate.c` `safe_apply()` and `apply()`

## Phase 0 tasks

No urgent stub gaps here. Verify coverage:
- All applies listed in `applies.h` that are called by the FluffOS runtime
  should be documented (even if not all are wired up) in a comment at the
  top of `ApplyTable.cpp`.

## Phase 1 tasks

### 1.4 - Pluggable boot API

Currently the apply system is hard-wired to the FluffOS master/simul_efun
model. Phase 1 requires that the same machinery support three different apply
name sets.

**What to build:**

1. **`BootApi` abstract base class** (new file
   `include/amlp/apply/BootApi.hpp`):
   ```cpp
   class BootApi {
   public:
       virtual ~BootApi() = default;
       virtual std::string masterFile() const = 0;
       virtual std::optional<std::string> simulEfunFile() const = 0;
       virtual std::string connectApply() const = 0;
       virtual std::string logonApply() const = 0;
       virtual std::string compileObjectApply() const = 0;
       virtual std::string privsFileApply() const = 0;
       virtual std::string netDeadApply() const = 0;
       virtual std::string heartBeatErrorApply() const = 0;
       virtual bool hasAutoObject() const = 0;
       virtual std::optional<std::string> autoObjectFile() const = 0;
   };
   ```

2. **`FluffOsBootApi`**: returns FluffOS/MudOS apply names
   (`"connect"`, `"logon"`, `"compile_object"`, `"privs_file"`, `"net_dead"`,
   `"heart_beat_error"`). `hasAutoObject()` → false.

3. **`LdmudBootApi`**: LDMud apply names (`"connect"`, `"logon"`,
   `"compile_object"`, `"valid_read"`, `"valid_write"`, `"get_master_uid"`,
   `"get_bb_uid"`). Shadow policy applies (see `src/object`).
   `hasAutoObject()` → false (LDMud uses simul_efun differently).
   **Corrected 2026-08-22** (report-only comparison session against a real
   LDMud 3.6.8 clone, no code changes yet - see §1.16 below for full
   citations): this used to say `"get_root_uid"`, which was renamed to
   `"get_master_uid"` in LDMud 3.2.1@40 and no longer exists under the old
   name.

4. **`DgdBootApi`**: DGD has no master/simul_efun in the FluffOS sense.
   Instead: **driver object** handles boot callbacks and **auto object**
   is implicitly inherited by every other object. `hasAutoObject()` →
   true; `autoObjectFile()` → `/kernel/auto` (configurable).
   **Corrected 2026-08-22** (report-only comparison session against a real
   DGD clone, no code changes yet - see §1.15 below for the full grep
   methodology and citations): this used to list the driver-object
   callback set as `"initialize"`, `"path_read"`, `"path_write"`,
   `"compile_object"`, `"connect"`, `"disconnect"` - three of those six are
   wrong. There is no `"connect"` (real DGD forks by port type:
   `"telnet_connect"` / `"binary_connect"` / `"datagram_connect"`), no
   `"compile_object"` (DGD has no virtual-object concept; the nearest real
   analog is `"call_object"`, fired from `call_other()`'s string-target
   resolution, not a load-time fallback), and no `"disconnect"` under any
   name (that name is LDMud's, not DGD's - DGD's real connection-loss
   callback is `close(int destructing)`, called on the persistent user
   object itself, not the driver object). See §1.15 for the full, real
   17-name callback list plus `atomic_error`.

5. **`ApplyTable`** takes a `const BootApi&` and routes all applies through
   it. `VM::applyMaster()` is unchanged - it just calls `ApplyTable`.

6. **`src/dialect/instruct.md`** owns the code that constructs the right
   `BootApi` subclass based on the configured `LpcDialect`.

**Open design note, added 2026-08-22, not resolved here - just flagged so
it can't be missed when 1.4/1.15/1.16 are actually implemented:** the
`BootApi` interface above returns a single `std::string` per concept
(`connectApply()`, `netDeadApply()`). That shape does not fit any of the
three dialects for connect/disconnect:
- FluffOS: `connect` is one master apply; `net_dead` is one apply, but
  called on the *player object*, not master.
- DGD: connect is a **three-way fork by port type** (`telnet_connect` /
  `binary_connect` / `datagram_connect`), each a driver-object callback;
  the disconnect-equivalent (`close`) is called on the *persistent user
  object*, not the driver.
- LDMud: connect is one master apply; disconnect is one master apply
  (`disconnect(object ob, string remaining)`), called on **master**, not
  on the player object.
"Connect" needs either a per-dialect dispatch strategy (not a plain
string) to cover DGD's three-way fork, and "disconnect/net_dead" needs
`BootApi` to also express *where* the apply is called (master vs
driver-object vs the affected object itself) - a signature like
`virtual std::string netDeadApply() const = 0;` has no way to say that.
Resolve this before writing `ApplyTable`'s dispatch code, not after.

### 1.15 - DGD driver+auto object boot path

When `DgdBootApi` is active:
- On boot, load the driver object (`/kernel/driver` or `Config::masterFile()`).
- Call `driver_object->initialize()`.
- For every subsequently loaded object, **inject an `inherit "/kernel/auto";`**
  at the top of its compiled program - this is the DGD auto-inherit mechanism.
  Implement this as an `ObjectManager::compile()` preprocessing step that
  prepends the inherit declaration when `DgdBootApi::hasAutoObject()` is true
  and the file is not the auto object itself.

**Real driver-object apply surface, confirmed 2026-08-22** (report-only
comparison session against a real DGD clone, `temp/dgd`; no code changes
made) by grepping every `DGD::callDriver(...)` call site under `src/`,
plus `callCritical(...)` for the one exception noted below:

```
binary_connect    call_object        compile_error     compile_rlimits
datagram_connect  include_file       inherit_program   initialize
interrupt         object_type        path_read         path_write
recompile         restored           runtime_rlimits   telnet_connect
touch             atomic_error (via callCritical, not callDriver)
```

Only `initialize`/`path_read`/`path_write` of the previously-documented
six-name list were correct. The other three were wrong:
- **No `"connect"`.** Real DGD forks by port type instead:
  `telnet_connect(object)`, `binary_connect(object)`,
  `datagram_connect(object)` (`src/comm.cpp`), each returning the
  persistent object for that connection.
- **No `"compile_object"`.** DGD has no virtual-object concept - every
  object is compiled from a real, on-disk `.c` file. The nearest real
  analog is `call_object(string path)`, fired specifically from
  `call_other()`'s string-target resolution (`src/kfun/builtin.cpp`), not
  a load-time missing-file fallback. Do not model this as
  `compileObjectApply()`.
- **No `"disconnect"`.** DGD's real connection-loss notification is
  `close(int destructing)` (`src/comm.cpp`'s `Comm::close()`), called on
  the **persistent user object itself**, architecturally like FluffOS's
  object-level `net_dead()` - just under a different name. `"disconnect"`
  is LDMud's real master-apply name (see §1.16), not DGD's; an earlier
  draft of this file conflated the two.

Two names in the real list above are the previously-undocumented hooks
behind ROADMAP row 1.11's `rlimits` statement: `compile_rlimits` fires at
compile time when the parser encounters an `rlimits` statement (validates/
transforms the limit expressions), `runtime_rlimits` fires when a compiled
`rlimits` scope is actually entered at runtime. Whoever implements 1.11
needs both, not just VM-level checkpoint/rollback.

See `src/dialect/instruct.md`'s own open design note (also referenced in
§1.4 above) for why `connectApply()`/`netDeadApply()`'s current
one-string-per-concept shape does not fit this three-way connect fork or
the object-level (not driver-level) disconnect callback.

### 1.16 - LDMud master apply name table

LDMud's master has a richer apply surface than FluffOS. The key additional
applies beyond the FluffOS set, **corrected 2026-08-22** against a real
LDMud 3.6.8 clone (`temp/ldmud`, report-only comparison session, no code
changes made):
- `"get_master_uid"` - returns the root UID string. **Not** `"get_root_uid"`
  - that name was superseded in LDMud 3.2.1@40 (`doc/master/get_master_uid`'s
    own HISTORY: "Introduced in 3.2.1@40 replacing get_root_uid()."); this
    clone is 3.6.8, many releases past that rename, and `get_root_uid` no
    longer exists.
- `"get_bb_uid"` - returns the backbone (fallback) UID.
  **Investigated 2026-08-18, not implementable against anything real in
  this exact vendored driver:** `doc/master/get_bb_uid` claims
  `process_string()` calls it when there is no current object, but
  `temp/ldmud/src/efuns.c`'s own `f_process_string()`/`process_value()`,
  read in full, make no such call at all. Grepped the generated constant
  `STR_GET_BB_UID` across every real `.c`/`.h` file in `temp/ldmud/src`:
  zero call sites anywhere in the driver: the name appears only in the
  string-table declaration and in `applied_spec` (a compile-time
  type-check spec for the compiler, not a call-site generator, confirmed
  by that file's own header comment). Dead scaffolding in this exact
  build, the doc is stale, the C is authoritative. See ROADMAP.md row
  1.16.
- `"valid_read"` / `"valid_write"` - path-level filesystem permission
  checks. **Investigated 2026-08-18:** not LDMud-specific at all, real
  FluffOS has the identical applies (`fluffos-2.9-ds2.08/applies.h`'s own
  `APPLY_VALID_READ`(33)/`APPLY_VALID_WRITE`(38)) and this driver
  currently gates none of its 11 existing file efuns with either
  dialect's version. `temp/ldmud/src/simulate.c`'s own
  `check_valid_path()`, read in full, confirmed one shared function
  behind both applies (`apply_master(STR_VALID_WRITE/READ, 4)`, args
  `path, uid-or-0, func, ob`, matching `doc/master/valid_read`/
  `valid_write`'s own SYNOPSIS exactly) called from every file-touching
  efun this driver already has. A whole missing cross-cutting security
  feature for both dialects at once, a new shared path-check helper
  plus wiring into 11+ call sites, not a single-efun signature
  divergence; recommended as its own future row rather than staying
  folded into 1.16. See ROADMAP.md row 1.16 for the full citation.
- `"make_path_absolute"` - resolve a relative path. **Investigated
  2026-08-18:** exactly one real call site in the whole driver,
  `temp/ldmud/src/ed.c:1128`, inside the built-in line editor's own
  relative-filename resolution: blocked on this driver having no
  `ed()` efun at all (already excluded elsewhere, see
  `src/efun/instruct.md`'s own "`get_char`/`ed`/`origin`/`resolve`" note).
  No other real caller to model against until `ed()` itself exists. See
  ROADMAP.md row 1.16.
- `"query_allow_shadow"` - master-side shadow permission check (see
  `src/object`), confirmed real and correctly named. Real LDMud shadow
  permission is two-layered, though: this master apply is only the second
  layer. The first is `query_prevent_shadow()`, defined **on the victim
  object itself** (not a master apply at all) - an object that defines it
  to return 1 cannot be shadowed regardless of what `query_allow_shadow()`
  says. Also note real LDMud `shadow()` itself is `int shadow(object ob)`
  - one argument, returns int 1/0 - not FluffOS's `shadow(ob, flag)`
    two-argument, object-returning form; an earlier draft of this project's
    own docs had attributed FluffOS's real signature to LDMud. `bind()`
    does not exist as an LDMud efun either - the real name is
    `bind_lambda(closure, object ob)` (`doc/efun/bind_lambda`), used both
    to bind an unbound lambda and to rebind an efun/simul-efun/operator
    closure to a different object. None of this is `src/apply`'s own
    concern to fix (it's `src/object`/`src/vm`/`src/compiler` territory
    for the actual Phase 1 rows), but it belongs here since this section
    is the LDMud apply-surface reference other rows will read.
    **Resolved 2026-08-17 for the `shadow()` half:** implemented in
    `src/efun/EfunTable.cpp` (ROADMAP row 1.5), gated on
    `Config::dialect()`. One correction to this note's own "two-layered"
    framing, found by reading `simulate.c`'s `validate_shadowing()`
    directly: it is not driver-enforced two-layer permission at all --
    the driver calls only `query_allow_shadow()`; a victim-side check is
    purely whatever that apply's own mudlib body chooses to do, and
    LDMud's own two doc files even disagree on what to call it
    (`query_prevent_shadow()` vs `prevent_shadow()`). Also added real
    LDMud's `unshadow()`, which has no FluffOS equivalent at all. The
    `bind_lambda()` half remains genuinely unimplemented: investigated
    in real depth this same pass, confirmed bigger than a normal batch
    item (closure-kind matrix plus a `privilege_violation()` subsystem
    this driver has no equivalent of); see ROADMAP.md row 1.7 and
    `src/vm/instruct.md` for the full scope and options.
- `"valid_snoop"` / `"valid_query_snoop"` - real LDMud master applies
  (`doc/master/valid_snoop`, `doc/master/valid_query_snoop`), not
  previously documented anywhere in this repo. Directly relevant: Phase
  0's own snoop family work confirmed real FluffOS 2.9-ds2.08 has **no**
  master-level snoop gate at all (no `APPLY_VALID_SNOOP` in `applies.h`) -
  LDMud genuinely differs here, a real per-dialect divergence worth
  carrying into whichever row eventually gives `snoop()` dialect-aware
  behavior.
  **`valid_snoop` implemented 2026-08-17.** `snoop()` itself needed a
  fuller re-scope than this note alone flagged: reading
  `temp/ldmud/src/comm.c`'s own `set_snoop()`/`v_snoop()` in full showed
  the apply gates both the start and stop forms (not just start), and
  that real `snoop()` returns a plain `int` (1/0/-1), never the object --
  `doc/efun/snoop`'s own "object snoop(...)" SYNOPSIS text is stale;
  `func_spec`'s `"int snoop(object, void|object);"` and `v_snoop()`'s own
  `put_number(sp, i)` are what the driver actually does. Implemented in
  `src/efun/EfunTable.cpp`'s `snoop` registration, gated on
  `Config::dialect()`, 5 new regression tests. `valid_query_snoop` left
  unimplemented on purpose: it only ever gated `query_snoop()`, which is
  itself obsolete in this exact 3.6.8 clone
  (`temp/ldmud/doc/obsolete/query_snoop`), replaced by the much larger
  `interactive_info(ob, II_SNOOP_*)` this driver has no equivalent of.
  See ROADMAP.md row 1.16 for the full citation trail.
- The `replaces` directive in `inherit` **does not exist anywhere in real
  LDMud** - grepped the actual grammar (`src/prolang.y`'s
  `inheritance_qualifier`/`inheritance_modifier` productions): the
  complete modifier set is `static`/`private`/`public`/`protected`/
  `nosave`/`nomask`/`deprecated`/`virtual`, no `replaces` token anywhere
  in the source tree. The real, closest LDMud feature is
  `replace_program()` (`doc/efun/replace_program`) - a **runtime efun**,
  not a compile-time `inherit` modifier: it shrinks a live object down to
  one of its already-inherited programs, deferred to the end of the
  current backend cycle, and stops shadowing on the object when it takes
  effect. **ROADMAP row 1.6 may need rescoping, not just a rename** - a
  compiler-side `inherit` directive and a VM-side runtime efun are
  different-shaped features; whoever picks up 1.6 should read
  `doc/efun/replace_program` in full before deciding what the row is
  actually asking for.
  **Resolved 2026-08-17:** rescoped and implemented - see
  `src/dialect/instruct.md`'s matching note and ROADMAP.md row 1.6. The
  real divergence was LDMud's `replace_program()` accepting a
  zero-argument call (auto-selecting the object's sole direct inherit),
  which real FluffOS never allows; implemented in
  `src/efun/EfunTable.cpp`, gated on `Config::dialect()`.
- Link-death notification is `disconnect(object ob, string remaining)` - a
  **master** apply (`callback_master(STR_DISCONNECT, 2)` in `src/comm.c`),
  not an object-level one, and the name `net_dead` belongs to FluffOS
  only. See the open design note under §1.4 above - this affects
  `BootApi`'s whole connect/disconnect abstraction, not just a string.

Add these to `LdmudBootApi` and wire them into `ApplyTable`'s apply-dispatch
methods.

## Phase 2 tasks

### 2.9 - Apply cache

The apply cache sits logically in `src/apply` because it caches the result of
"what function does this object have for this apply name?".

**What to build:**
1. `ApplyCache` singleton (or a member of `ApplyTable`):
   `unordered_map<pair<LpcObject*, string>, FunctionEntry*>`.
2. On a miss: run the existing `findFunctionInChain()` walk; store the result.
3. On hit: skip the walk, call directly.
4. Invalidation: when `ObjectManager::loadObject()` recompiles an object,
   erase all cache entries where the first element matches the recompiled
   object OR any object that inherits from it (the inheritance chain must be
   invalidated too - track the reverse inherit map).
5. On `destructObject()`: erase all entries for the destroyed object.

**Expected impact:** eliminates the dominant hot path in combat/heartbeat storms
where `call_heart_beat()` calls `apply("heart_beat", ob)` on every living
object every 2 seconds.

## Key invariants

- All apply dispatch (master, simul_efun, per-object) must check
  `obj->isDestructed()` before calling - see `src/object/instruct.md` Phase 0.5.
- Applies that return `void` in the spec should silently swallow the result
  rather than throwing on a non-void return.
- The `BootApi` abstraction must not be bypassed: never hardcode `"connect"` or
  any other apply name string outside of a `BootApi` subclass.
