# AMLP vs. real FluffOS, LDMud, and DGD

An evidence-based comparison, not a marketing page. Every number here is
either read directly out of `ROADMAP.md`'s own accounting (which is
itself sourced from `git log`-recorded, per-row citations against the
vendored reference sources) or freshly re-checked against those same
vendored sources while writing this file (`temp/reference/fluffos-2.9-ds2.08/`,
`temp/ldmud/`, `temp/dgd/`: see `CLAUDE.md` for how these are tracked
and why they are gitignored). Nothing here is from memory or general
reputation. Where a number is an estimate rather than an exact count,
it is labeled as one.

**DGD is comparison-only context, not a target.** `ROADMAP.md`'s own
Phase 1 header, and the scope clarification dated 2026-08-18, are
explicit about this: the actual goal is a FluffOS/LDMud-level driver
done better than either, not three-way parity with DGD. DGD numbers
below exist so a reader can see how a third, architecturally different
driver solved the same problems, not because AMLP is trying to match it
feature-for-feature.

Last updated: 2026-09-01 (full re-derivation of every phase fraction,
both rollups, and the test total directly from `ROADMAP.md`'s current
checkboxes and `STATUS.md`'s 2026-08-30 entry; new Phase 2 row-by-row
table; see the "Re-swept 2026-09-01" note below the phase table for the
numbers and the discrepancy flag against the requesting brief's stale
figures. Prior sweep, 2026-08-21: a fresh full-project status sweep,
Phase 0 confirmed still 16/16 with no open sub-gap of its own, row 1.7's own
`privilege_violation()` authorization gate now real for four trigger
points, `bind_lambda()`'s cross-object form, `set_driver_hook()`,
`call_out_info()`, and `input_to()`, and every driver-hook/efun-surface
number below re-checked against `ROADMAP.md`'s own current row text
rather than assumed carried forward. Phase 1's real-blocker fraction
itself is unchanged at 10/11, row 1.7 was already checked off before
this pass; what changed is how much of its own remaining cell is now
closed. See `ROADMAP.md`'s own dated corrections on each row and this
file's own rewritten sections below for the full accounting.)

**Re-swept 2026-08-21 (later the same day, once more, after
`notes/ACCOUNT_LOGIN_PLAN.md` closed out its own originally scoped
build ordering):** every Phase/row number above re-checked directly
against `ROADMAP.md`'s own current checkboxes (`awk`-counted per phase
section, not eyeballed) rather than trusted stale -- all five numbers
in the table below are unchanged (Phase 0 16/16, Phase 1 10/11 real-
blockers-only and 10/16 including DGD, Phase 2 0/22, Phase 3 0/8), row
1.8 confirmed still `[ ]` with the same zero-real-evidence scope
already on record, and every Phase 2/3 `src/` directory confirmed still
`instruct.md`-only (the one exception, `src/scheduler/Scheduler.cpp`,
predates this sweep by many sessions and backs Phase 0/1's own already-
shipped `call_out()`/`heart_beat()`, not any Phase 2 concurrency item).
This session's own real work (`notes/ACCOUNT_LOGIN_PLAN.md`'s build
ordering items 4 and 5, character creation and character selection,
plus a bounded stopgap fix for a width > 1 mapping save/restore
silent-truncation bug found by the immediately prior session's own
fact-check) is real mudlib content and a driver-level correctness fix,
not a `ROADMAP.md` row of its own -- see `STATUS.md`'s own dated
entries for the full writeups.
`ROADMAP.md` and `STATUS.md` are the living
documents; if this file and either of those disagree on a specific row's
status, trust `ROADMAP.md`'s own checkbox and re-derive this file's own
summary from it rather than the reverse.

**Re-swept 2026-08-22 (a fresh full-project status sweep, row 1.7's own
remaining `call_out_info()`/`input_to()` privilege-gate follow-on now
real too): the Phase 2/3 "not started" framing two paragraphs below was
stale and is corrected here.** Re-counted every phase directly against
`ROADMAP.md`'s own current checkboxes (`awk`-counted per phase section,
not eyeballed, and cross-checked against a fresh local build/test run)
rather than trusted forward from the 2026-08-21 sweep above: Phase 0 is
still 16/16 (100%, unchanged); Phase 1 is still 10/11 real-blockers-only
(91%, unchanged -- row 1.7 was already checked off before this pass and
stays so, its own remaining cell items, `H_LOAD_UIDS`/`H_CLONE_UIDS`/
`H_INCLUDE_DIRS`, type-map validation, plain `lambda()`, and
`inaugurate_master()`'s own arg=1/2/3 reload cases, all confirmed still
either zero real corpus evidence or the same weak, single-file evidence
already on record, not newly re-derived); but **Phase 2 is 3/22 (2.9
apply cache, 2.12 full PCRE suite, and 2.15 SQLite, all built and landed
2026-08-21, one sweep before this one) and Phase 3 is 1/9 (3.9, a real
third-party mudlib boot-and-play confirmation, also landed 2026-08-21;
row 3.9 itself is new since the 2026-08-21 sweep above, so Phase 3's own
row count moved from 8 to 9, not just its done count)** -- both phases
had already moved off 0% by the time of the 2026-08-21 sweep two
paragraphs below, which undercounted them; this pass corrects it rather
than repeating the same stale number forward again. One real
documentation inconsistency found and worth naming rather than quietly
smoothing over: `ROADMAP.md` row 1.7's own "715 tests passing" note
undercounts the actual suite size at that point in real history, since
it measured only that row's own local delta (709 to 715) without
accounting for the ~28 additional tests rows 2.9/2.12/2.15 had already
added in an intervening session; the real, freshly-measured total as of
this sweep, rebuilt and re-run directly rather than trusted from any
prior note, is **743**, matching `STATUS.md`'s own current dated entry,
not 715. See that entry for the full sweep and the recommendation
written there for what to pick up next.

---

## How far along is AMLP, in plain language

AMLP is a working, from-scratch LPC driver (lexer/parser/compiler/VM/
object system/network layer, no code shared with any real driver) that
already runs a real bundled mudlib end to end, login, movement,
command dispatch, object creation, persistence, sockets, and has grown
a substantial fraction of real FluffOS's own efun surface plus the start
of genuine LDMud and DGD dialect support behind a config switch. It is
not yet a drop-in replacement for either real driver: Phase 2 and Phase 3
(the features meant to eventually *exceed* what either real driver
offers) are still mostly planning documents rather than implemented
code, though 35 Phase 2 rows are now real and landed. Six of those are
substantial runtime work (statedump v1, dual persistence coexistence, a
coroutine scheduler v1, apply cache, full PCRE suite, built-in SQLite);
the other 29 are a package-by-package efun-conformance sweep. One
Phase 3 row is done (a real third-party mudlib now run through
substantial gameplay, not just booted). Phase 1's own real,
corpus-driven dialect-compatibility work is now substantially
exhausted: see immediately below.

**Phase 0 (stabilize the current base): complete.**
16 of 16 rows checked off, including `parse_*` (0.13a), the large
natural-language parser package that was the one still-open row as of
an earlier revision of this file: all 8 named efuns are implemented,
including real two-object matching and the `nicks` nickname-mapping
argument (`parse_sentence()`'s own last remaining sub-gap, closed
2026-08-20), no open sub-gap remains anywhere in this row.

**Phase 1 (dialect universality): the real, corpus-driven work is now
substantially exhausted, not "a bit under half done."** An earlier
revision of this file undercounted this badly: five rows (1.2, 1.3,
1.4, 1.9, 1.16) had real, already-landed work sitting in their own
`ROADMAP.md` cells across several earlier sessions that was never
reflected in their checkboxes, corrected 2026-08-20. Counting only the
rows that actually gate Phase 1 completion (DGD-only rows are comparison
context, not blockers, per the scope clarification above): **10 of 11
real blocking rows are done.** The one still open, row 1.8
(`#'lfun::`/`#'sefun::`/`#'var::` closure-literal prefixes), was
investigated for the first time this same session and found to have
**zero real corpus usage** for its own remaining scope across every
vendored mudlib corpus in `temp/`: deferred on the same
zero-evidence-discipline basis this project has applied consistently
elsewhere (`bind_lambda()`'s cross-object form, `lambda()` itself, DGD's
own five still-open rows), not forgotten or blocked. Counting only rows
with real, non-DGD, non-zero-evidence scope still open, as opposed to
counting every unchecked box regardless of what is actually left in it:
**zero real Phase 1 blockers remain.** See `ROADMAP.md`'s own row-by-row
citations for the underlying evidence on every count below, not repeated
here.

**Phase 2 and Phase 3: mostly still planning, but no longer zero code.**
The directories the largest Phase 2/3 work would live in (`src/jit`,
`src/gc`, `src/lsp`) still contain nothing but their own planning
`instruct.md`, and the biggest novel-architecture items (JIT
compilation, hotboot, object swapout, TLS, WebSocket, an LSP server,
generational GC) are all still real, considered plans, not real code.
But 35 Phase 2 rows and one Phase 3 row are now real, landed code:
statedump v1 (2.1), dual persistence coexistence (2.4), a coroutine
scheduler v1 (2.5), the apply cache (2.9), the full PCRE
`pcre_match`/`pcre_assoc` suite (2.12), built-in SQLite `db_*` efuns
(2.15, dialect-gated to `ldmud` per row 2.40), and 29 smaller
single-efun or single-argument spec-conformance slices from a
package-by-package `src/packages/*/*.spec` sweep (2.16, 2.23 through
2.30, 2.33a, 2.37, 2.46 through 2.62), plus a real third-party mudlib
boot-and-substantial-gameplay confirmation (3.9). See the table
immediately below for the current fraction of each phase, and the
2026-09-01 sweep note above for the full re-derivation.

**Re-swept 2026-08-27:** row 2.16 (`hash()`) landed this session,
picked specifically because it is real *current* FluffOS surface (this
row's own original title's `bcrypt` name was wrong, corrected in
`ROADMAP.md`'s own row -- real `hash()` never had a `bcrypt` algorithm
at all) that the vendored 2.9 ds2.08 reference this file otherwise cites
never had, confirmed absent from it entirely rather than assumed. See
`ROADMAP.md` row 2.16 for the full citation trail, the OpenSSL EVP-based
implementation, and its 3 new regression tests (764 total, up from 761).

**Re-swept 2026-08-27 (a further session, same day):** two new rows,
2.23 (`time_ns()`/`perf_counter_ns()`) and 2.24 (`secure_random()`),
picked up directly from that same session's own ranked modernization
research (its top two candidates), both real, genuinely new-since-2.9
current FluffOS efuns, confirmed absent from the vendored 2.9 ds2.08
reference entirely and confirmed against real current source (not
guessed) before writing any code. `secure_random()` is a real, distinct
security gap this driver's own pre-existing `random()` never filled
(a seeded, non-cryptographic PRNG, fine for gameplay, not for anything
security-sensitive) -- deliberately ported to match real FluffOS's own
actual `std::random_device("/dev/urandom")` mechanism exactly rather
than reusing this driver's own already-linked OpenSSL dependency via
`RAND_bytes()`, since real FluffOS itself never uses OpenSSL for this.
See `ROADMAP.md` rows 2.23/2.24 for the full citation trail, and
`STATUS.md`'s own dated entry for the live-verification account. 6 new
regression tests (773 total, up from 767).

**Re-swept 2026-08-27 (a further session, same day):** row 2.17
(`json_encode`/`json_decode`) re-examined on request, specifically to
check whether its own real formatting semantics (mapping key order,
float precision) were actually specified in real current source rather
than assumed unspecified -- the check went further than that question
and found the row's own premise was wrong: `json_encode`/`json_decode`
are not real current-FluffOS efuns at all (a genuinely empty
`src/svalue_json.cc` placeholder, zero declarations across all 21 real
package `.spec` files, zero `docs/efun/` pages), only an unrelated
save-file CLI conversion pair (`json2o`/`o2json`) with a fully
documented but structurally different JSON schema. Deferred on
stronger, more definitive grounds than before, not built. Row 2.25
(`log2()`/`round()`) landed instead, this session's own fallback
candidate, found the same way rows 2.16/2.23/2.24 were: real,
genuinely new-since-2.9 current FluffOS efuns confirmed absent from the
vendored 2.9 reference, verified via plain standard math identities
with zero live-current-FluffOS-instance dependency, the same bar
`hash()`/`time_ns()`/`secure_random()` were held to. See `ROADMAP.md`
rows 2.17/2.25 and `STATUS.md`'s own dated entry for the full trail. 3
new regression tests (776 total, up from 773).

**Re-swept 2026-08-27 (a further session, same day):** the row 2.25
method (cross-check an already-partially-implemented efun category's
real current `.spec` file against this driver's own registered efuns)
applied systematically to `core.spec` (strings/arrays/mappings/objects/
general, all bundled in one real package in current FluffOS) plus
`trim.spec`, `contrib.spec`, and `ops.spec` (the latter confirmed to be
bytecode operators, not efuns, out of scope). Found 41 real names
confirmed genuinely absent from the vendored 2.9 reference; five small,
independently-verifiable ones built as five new rows (2.26-2.30:
`trim`/`ltrim`/`rtrim`, `explode_reversible`, `call_out_walltime`,
`enable_wizard`/`disable_wizard`/`wizardp`, `sys_network_ports`), the
rest named and scoped as six new deferred rows (2.31-2.36) rather than
built speculatively or dropped silently -- real protocol-negotiation
work (GMCP/MSDP/MSP/ZMP/MXP), a new value type plus a charset-conversion
dependency (buffers/encoding), a runtime-mutable config registry
`get_config()`'s own comment already flagged as missing, a real VM
frame-lifecycle feature (`defer()`), destruct-path wiring
(`query_notify_destruct`), and implementation-specific driver-internals
diagnostics. See `ROADMAP.md` rows 2.26-2.36 and `STATUS.md`'s own
dated entry for the full trail. 12 new regression tests (789 total, up
from 776).

**Re-swept 2026-08-27 (a further session, same day):** the full real
`src/packages/` directory enumerated live (21 real package `.spec`
files, confirmed via the GitHub API tree, not guessed) and cross-checked
against what this project had already swept -- `math`/`core`/`trim`/
`contrib`/`ops` in the prior sweep, `sockets`/`pcre`/`db` when those
Phase 0/2 rows were originally built. Found `dwlib`/`uids`/
`mudlib_stats`/`compress`/`external`/`async`/`develop`/`ffi`/`matrix`/
`jsbridge` had never actually been checked against this driver's
registered efuns. `dwlib.spec` yielded three small, independently-
verifiable names, built as row 2.37 (`roll_MdN`/`vowel`/`add_a`). The
rest named and scoped as eight new deferred rows (2.38-2.45) rather
than built speculatively: a real UID/EUID trust hierarchy correctly
declined as a bare, ungated stand-in (the same "looks real, isn't"
risk that would matter more here than it did for the wizard flag,
since uid/euid is explicitly security-relevant); TLS-only socket
options, deferred alongside row 2.13; `db_commit`/`db_rollback`,
excluded outright once their own real docs turned out to say **"Not
yet implemented!"** in real current FluffOS itself; a real, separate
finding along the way worth naming precisely -- this driver's own
already-shipped `db_*` family (row 2.15) was built and cited against
real **LDMud's** own db package, not real current FluffOS's own
`db.spec` at all (confirmed directly from `DbRegistry.hpp`'s own header
comment, not newly asserted), so `db_status()` is entangled with a
real, larger "which `db_*` contract does this driver actually honor"
question rather than a quick addition; a new mudlib-statistics
subsystem; zlib compression pending the same missing buffer type row
2.33 already named; subprocess spawning with a real security surface;
real async I/O architecture; and a group of remaining names (FFI,
further debug internals, a niche 3D-math package, a WASM-only bridge)
that each resolve to an already-stated reason rather than a new one.
See `ROADMAP.md` rows 2.37-2.45 and `STATUS.md`'s own dated entry for
the full trail. 3 new regression tests (792 total, up from 789).

**Re-swept 2026-08-27 (a further session, same day):** the LDMud-vs-
FluffOS `db_*` naming collision the previous sweep found (row 2.40) is
now resolved. Both real sources read directly, not assumed from either
side's own summary: real LDMud's own `pkg-mysql.c` and real current
FluffOS's own `db.c`/`db.spec` (a real, locally-vendored current-FluffOS
clone, `temp/fluffos/`) diverge on every real `db_*` name's own argument
shape or return-value meaning. Corpus evidence checked fresh: zero real
`db_*` call sites anywhere outside `core-lib`, and `core-lib`'s own real
usage does not merely fit LDMud's shape, it actively depends on it (one
line, `dbHandle = efun::db_exec(dbHandle, sqlQuery);`, only makes sense
under LDMud's own "returns the handle on success" contract -- under real
FluffOS's own "returns rows-affected" contract that exact real line
would silently corrupt the handle instead). Resolution: dialect-gate
this driver's own single, already-correct, evidence-backed LDMud-shaped
`db_*` family to `dialect: ldmud` only, rather than building a second,
unverifiable FluffOS-shaped target -- converting what was previously a
silent wrong-shape footgun under this driver's own default
(`dialect: fluffos`) into an honest "not implemented for this dialect"
gap. See `ROADMAP.md` rows 2.15/2.40 and `STATUS.md`'s own dated entry
for the full trail. 2 new regression tests (794 total, up from 792).

**Re-swept 2026-09-01: full re-derivation from `ROADMAP.md`'s and
`STATUS.md`'s current state, not carried forward from the 2026-08-27
notes above.** Every phase fraction below was re-counted directly
against `ROADMAP.md`'s own current checkboxes, and the test total taken
from `STATUS.md`'s own most recent dated entry (2026-08-30), not from
any figure already in this file or in the brief that requested this
refresh.

- **Phase 0: 16/16, 100%** (rows 0.1 through 0.15 plus 0.13a, every box
  `[x]`).
- **Phase 1, real blockers only: 10/11, 91%** (rows 1.1 through 1.7,
  1.9, 1.10, 1.16 done; row 1.8, the `#'lfun::`/`#'sefun::`/`#'var::`
  closure-literal prefixes, still `[ ]`, deferred on zero real corpus
  evidence, not blocked). DGD-only rows 1.11 through 1.15 are excluded
  per the Phase 1 header's own scope.
- **Phase 1 including the 5 DGD-only comparison rows: 10/16, 63%.**
- **Phase 2: 35/63, 56%** (up from this file's own prior 14/45). Done:
  2.1, 2.4, 2.5, 2.9, 2.12, 2.15, 2.16, 2.23 through 2.30, 2.33a, 2.37,
  2.40, 2.46 through 2.62. Open (28): 2.2, 2.3, 2.6 through 2.8, 2.10,
  2.11, 2.13, 2.14, 2.17 through 2.22, 2.31 through 2.36, 2.38, 2.39,
  2.41 through 2.45. Full row-by-row table immediately below the phase
  table. (This sweep found 33/61; rows 2.61 and 2.62, `implode()`'s
  function/fold form and `sprintf` `%f`, landed later the same session,
  bringing it to 35/63.)
- **Phase 3: 1/9, 11%** (only row 3.9 done).
- **Rollup, excluding the 5 DGD-only Phase 1 rows: 62/99, about 63%**
  (16 + 11 + 63 + 9 rows; 16 + 10 + 35 + 1 done).
- **Rollup, including all Phase 1 rows: 62/104, about 60%.**
- **Test suite: 820 passing** (`STATUS.md`, row 2.62's own entry: "820
  tests passing, up from 819"; the prior 2026-08-30 entry was 818).

**Discrepancy flagged, per this file's own "trust `ROADMAP.md`'s
checkbox and re-derive" rule.** The brief that requested this refresh
carried a Phase 2 model of 22 rows and 6 done (27%), a 761 test total,
and rollups of 33/58 (about 57%) and 33/63 (about 52%). Those figures
predate roughly 18 `ROADMAP.md` rows (2.33a and 2.46 through 2.62, a
package-by-package `src/packages/*/*.spec` sweep plus several
already-registered-efun argument-shape fixes) and about 59 regression
tests that have landed since, through 2026-09-01. The live checkboxes
and the live test count were used instead. The brief's narrower "6 real
Phase 2 rows" reading is still a fair description of the
architecturally substantial runtime work that has landed: statedump v1
(2.1), dual persistence coexistence (2.4), a coroutine scheduler v1
(2.5), the apply cache (2.9), the full PCRE suite (2.12), and built-in
SQLite (2.15). The other 29 done Phase 2 rows are small, single-efun or
single-argument spec-conformance slices. Counting them as equal rows
makes "56% of Phase 2" and the roughly 63% rollup overstate how much of
Phase 2's novel-architecture surface actually exists. Object swapout
(2.2), hotboot (2.3), `async`/`await` with the awaitable call_out and
Open Hydra (2.6 through 2.8), closure bake-at-construction (2.10), the
LLVM JIT (2.11), TLS (2.13), WebSocket (2.14),
`json_encode`/`json_decode` (2.17), async HTTP (2.18), the LSP server
(2.19), structured error objects (2.20), live hot-reload (2.21), and
the LPC-native test runner (2.22) are all still `[ ]`.

**Row 3.9 (third-party mudlib) strengthened since this file last called
it a "boot-and-play confirmation".** AetherMUD (a real
Nightmare-3.2-lineage Rifts mudlib) is now exercised through login,
character creation, movement, inventory/`help`/`quit`, NPC dialogue,
and real non-safe-zone combat reached by genuine player progression,
with hit/miss/parry/dodge/armor-destruction resolution confirmed
firing, not just a boot test. Two real driver-side gaps were found and
fixed during that pass, both cited in `ROADMAP.md` row 3.9:
`sprintf`'s own `%d`/`%o`/`%x`/`%c` rejecting a missing-mapping-key
value that real FluffOS unifies as `T_NUMBER` 0 (fixed 2026-08-25),
and array-form `call_other()`/`->` on an `object *` target, a real
first-class `func_spec.c` form this driver threw on (fixed
2026-08-25). A separate per-command error-isolation gap (an uncaught
command error closing the whole connection instead of just that
command, unlike real FluffOS's per-task recovery) was also fixed,
2026-08-27.

| Phase | Rows | Done | Open | % done |
|---|---|---|---|---|
| 0, Stabilize | 16 | 16 | 0 | 100% |
| 1, Dialect universality (real blockers only, DGD-only rows excluded) | 11 | 10 | 1 | 91% |
| 1, Dialect universality (including 5 DGD-only comparison rows) | 16 | 10 | 6 | 63% |
| 2, Beyond both (novel features) | 63 | 35 | 28 | 56% |
| 3: Production hardening + docs | 9 | 1 | 8 | 11% |
| Rollup, excluding the 5 DGD-only Phase 1 rows | 99 | 62 | 37 | ~63% |
| Rollup, including all Phase 1 rows | 104 | 62 | 42 | ~60% |

### Phase 2, row by row (from `ROADMAP.md`'s own current checkboxes)

| Row | Status | Item |
|---|---|---|
| 2.1 | Done | World-level statedump: full-heap binary snapshot (v1 slice) |
| 2.2 | Open | Object swapout: page inactive objects to disk, demand-page on access |
| 2.3 | Open | Hotboot: fd-passing exec into a new binary without dropping connections |
| 2.4 | Done | Dual persistence: per-object `save_object` and world snapshot coexist |
| 2.5 | Done | C++20 coroutine scheduler: cooperative suspend/resume (v1 slice) |
| 2.6 | Open | LPC `async`/`await` keyword pair backed by the coroutine scheduler |
| 2.7 | Open | `call_out_future(delay)`: awaitable call_out |
| 2.8 | Open | Open Hydra: speculative parallel tasks on disjoint object graphs |
| 2.9 | Done | Apply cache: (object x function-name) to FunctionEntry, invalidate on recompile |
| 2.10 | Open | Closure bake-at-construction: resolve `FP_*` kind and index at bind time |
| 2.11 | Open | LLVM JIT backend: compile hot bytecode functions to native |
| 2.12 | Done | Full PCRE regexp suite (`pcre_match`/`pcre_assoc`) |
| 2.13 | Open | TLS support (OpenSSL/BoringSSL) for game plus MXP/WebSocket |
| 2.14 | Open | WebSocket framing on top of TLS |
| 2.15 | Done | SQLite built-in `db_*` efuns (LDMud shape, dialect-gated to `ldmud`) |
| 2.16 | Done | `hash()` digest efun (SHA-256/512, MD5, RIPEMD, and more) |
| 2.17 | Open | `json_encode`/`json_decode` efun pair |
| 2.18 | Open | `http_get`/`http_post` async efuns (non-blocking, via async scheduler) |
| 2.19 | Open | LSP server for LPC (`--lsp`): hover, go-to-def, diagnostics |
| 2.20 | Open | Structured error objects: JSON-serializable source/line/column/message |
| 2.21 | Open | Hot-reload: recompile and migrate one `.c` file while the server is live |
| 2.22 | Open | LPC-native test runner: `assert_equal`/`assert_throws` efun suite |
| 2.23 | Done | `time_ns()`/`perf_counter_ns()`: nanosecond-precision time efuns |
| 2.24 | Done | `secure_random(int n)`: cryptographically secure random efun |
| 2.25 | Done | `log2()`/`round()`: base-2 logarithm and rounding efuns |
| 2.26 | Done | `trim()`/`ltrim()`/`rtrim()`: string trimming efuns |
| 2.27 | Done | `explode_reversible(string, string)`: lossless string split |
| 2.28 | Done | `call_out_walltime(...)`: real-seconds call_out variant |
| 2.29 | Done | `enable_wizard()`/`disable_wizard()`/`wizardp()`: wizard-flag efuns |
| 2.30 | Done | `sys_network_ports()`: list active listening ports |
| 2.31 | Open | `query_notify_destruct()`/`set_notify_destruct()`: destruct-notification efuns |
| 2.32 | Open | GMCP/MSDP/MSP/ZMP/MXP telnet protocol extension efuns |
| 2.33 | Open | UTF-8/charset conversion plus remaining buffer-type efuns |
| 2.33a | Done | Buffer value type plus the dependency-free buffer efuns |
| 2.34 | Open | `set_config(int, mixed)`: runtime-mutable driver config |
| 2.35 | Open | `defer(function)`: run-on-current-function-end (success or error) |
| 2.36 | Open | Driver-internals diagnostic efuns (`cache_stats`/`malloc_status`/etc.) |
| 2.37 | Done | `roll_MdN()`/`vowel()`/`add_a()`: dice-roll and article-selection efuns |
| 2.38 | Open | `seteuid()`/`geteuid()`/`getuid()`/`export_uid()`: UID/EUID trust hierarchy |
| 2.39 | Open | `socket_get_option()`/`socket_set_option()`: per-socket TLS config |
| 2.40 | Done | Resolve the LDMud-vs-FluffOS `db_*` naming collision (dialect gate) |
| 2.41 | Open | `domain_stats()`/`author_stats()`: per-domain/per-author mudlib statistics |
| 2.42 | Open | `compress()`/`uncompress()`/`compress_file()`/`uncompress_file()`: zlib efuns |
| 2.43 | Open | `external_start(...)`: spawn an external shell command |
| 2.44 | Open | `async_read`/`async_write`/`async_getdir`/`async_db_exec`: non-blocking I/O efuns |
| 2.45 | Open | `ffi_*` (18 names), remaining `develop.spec` debug internals, misc packages |
| 2.46 | Done | `sha1(string)`: SHA-1 digest efun |
| 2.47 | Done | `matrix.spec` slice 1: `id_matrix()`/`translate()`/`scale()` |
| 2.48 | Done | `matrix.spec` slice 2: `rotate_x()`/`rotate_y()`/`rotate_z()` |
| 2.49 | Done | `matrix.spec` final slice: `lookat_rotate()`/`lookat_rotate2()` |
| 2.50 | Done | `contrib.spec` timezone efuns: `zonetime()`/`is_daylight_savings_time()` |
| 2.51 | Done | `math.spec` vector efuns: `norm()`/`dotprod()`/`distance()`/`angle()` |
| 2.52 | Done | `string_difference(string, string)`: Levenshtein edit distance |
| 2.53 | Done | `pcre.spec` read side: `pcre_version()`/`pcre_extract()`/`pcre_match_all()` |
| 2.54 | Done | `pcre_replace(string, string, string *, void\|int)` |
| 2.55 | Done | `dwlib.spec` markup escaping: `replace_html()`/`replace_mxp()` |
| 2.56 | Done | `str_to_arr()`/`arr_to_str()`: `USE_ICONV` UTF-8 conversion pair |
| 2.57 | Done | `replace_string()` occurrence-range 4th/5th arguments |
| 2.58 | Done | `strsrch()`/`strstr()`: int-char needle and direction-flag 3rd argument |
| 2.59 | Done | `get_dir(string, int flags)`: the optional stat-flag argument |
| 2.60 | Done | `sprintf` `%=` column / word-wrap mode (single-column form) |
| 2.61 | Done | `implode()` function/fold form, and non-string skipping in the join form |
| 2.62 | Done | `sprintf` `%f` float specifier, `%i` alias, `+`/space pad-prefix flags |

**What is left open in Phase 1, and why each item stays open** (each
with its own detailed, source-cited scoping note in `ROADMAP.md`, not
guessed at here):

- **Row 1.8 (LDMud `#'lfun::`/`#'sefun::`/`#'var::` closure-literal
  prefixes), the one row with real remaining scope, deferred on
  evidence**: the bare `#'name` form and the `#'efun::name` forced-tier
  prefix (rows 1.2/1.3) are real and tested; `#'lfun::`/`#'sefun::`
  (more forced-tier prefixes) and `#'var::` (`CLOSURE_IDENTIFIER`, a
  reference-to-a-global-variable closure kind this driver has no model
  of at all, structurally distinct from a callable closure) have zero
  confirmed real mudlib call sites anywhere in `temp/`: the only hit
  for any of the three is the LDMud driver's own changelog prose noting
  when it added them, not a real mudlib using them.
- **Row 1.7's own remaining sub-items (the row itself is closed, these
  are real, named exceptions inside it, not a separate open row)**:
  `privilege_violation()`, once flagged as a defensive-completeness
  question with no corpus signal of its own by nature rather than a
  compatibility gap, was picked up on exactly that basis and is now
  real for four trigger points: `bind_lambda()`'s cross-object form,
  `set_driver_hook()`, `call_out_info()` (dialect-gated, real FluffOS
  never had this mechanism at all), and `input_to()` (gated on the real
  `INPUT_IGNORE_BANG` flag bit specifically). `call_out_info()`/
  `input_to()` were this row's own last precisely-scoped, real-evidence
  items; both are now closed. `H_LOAD_UIDS`/`H_CLONE_UIDS`/
  `H_INCLUDE_DIRS` driver-hook trigger points have real but minimal
  evidence, 3 real call sites, all in one file (`secure/master/
  hooks.c`), versus 324 files defining `reset()` and 43 defining
  `clean_up()`, which is why `H_RESET`/`H_CLEAN_UP` (now real,
  dialect-gated where the two real drivers genuinely disagree) was
  picked first. Plain dialect-agnostic `lambda()`, per-hook type-map
  validation, `inaugurate_master()`'s own arg=1/2/3 master-reload/
  reactivation cases (only arg=0, first boot, is wired), and the
  remaining plain-string `hooks.c` hooks all have zero real corpus
  pressure, deferred on the same evidence discipline as everywhere
  else in this row, not forgotten.
- **Row 1.9's own remaining sub-items (the row itself is closed, same
  pattern as row 1.7 above)**: `m_allocate`/`m_entry`/`m_reallocate`/
  `m_add`/`m_contains` (the real N-columns-wide efun family) and the
  `([:width])` empty-mapping literal all have zero real call sites
  across every corpus in `temp/`: `m_indices()`/`m_values()` (the two
  real names with real usage), the width-2 `([ k: v1; v2 ])` literal, and
  `map[key, n]` indexing/assignment (including a real IncDec-on-column
  bug this project's own live-bug-first discipline caught and fixed) are
  the parts of this row real corpus evidence actually called for, and
  are done.
- **DGD's own five still-open rows (1.11-1.15)**: real, considered
  scope with real citations against `temp/dgd/`'s own source, but
  explicitly comparison context rather than a Phase 1 blocker per this
  project's own stated goal: a FluffOS/LDMud-level driver done better
  than either, not three-way parity with DGD.

## Row 0.13a (`parse_*`), now checked (partial), in detail

FluffOS's real natural-language sentence/grammar-rule parser package
(`packages/parser.c`, 3,419 lines): confirmed, not assumed, to matter:
Dead Souls' own core command dispatch calls `parse_sentence()` directly.
All 8 real efun names are now implemented (`parse_init`, `parse_add_rule`,
`parse_add_synonym`, `parse_remove`, `parse_dump`, `parse_refresh`,
`parse_sentence`, `parse_my_rules`), including full `OBJ`/`LIV`/`OBS`/`LVS`
noun-phrase-to-object matching, both single-object rules (candidate
resolution, adjective/ordinal narrowing, `LIV_MODIFIER`, "all of"/plural
`OBS` matching, ambiguity/error reporting) and real two-object rules
("give OBJ to LIV", both singular and plural shapes, e.g. "give OBS to
LIV") via `dependent_check_functions()`/`check_one_relation()`/
`check_object_relations()`, all live-verified against a real running
driver. Real corpus evidence found 77 real two-object rules in Dead
Souls' own `lib/verbs/` alone, so this was a real, sized piece of work,
not a theoretical corner case. `parse_sentence()`'s own 4th `nicks`
argument (a caller-supplied nickname mapping, real `add_nicknames()`/
`expand_node()`) is now real too, 2026-08-20, this row's own last
remaining sub-gap is closed. See `ROADMAP.md` row 0.13a for the full
component breakdown, citations, and live-verification history.

---

## Codebase scale

Raw line counts, `.c`/`.cpp`/`.h`/`.hpp` only, each driver's own real
source tree as vendored in `temp/`. A scale comparison, not a quality
one: AMLP is deliberately smaller because it targets specific,
confirmed-real-usage compatibility rather than reimplementing every
package (own database/crypto/ed-editor/full-MXP suites) either real
driver ships.

| Driver | Lines (`.c`/`.cpp`/`.h`) | Note |
|---|---|---|
| LDMud | ~211,600 | `temp/ldmud/src` |
| FluffOS 2.9 (ds2.08) | ~92,100 | `temp/reference/fluffos-2.9-ds2.08` |
| DGD (this vendored C++ port) | ~70,500 | `temp/dgd/src` |
| **AMLP** | **~22,500** | `src/` + `include/`, this repo |

## Efun / kfun surface

| Driver | Real count | Method |
|---|---|---|
| FluffOS 2.9 (ds2.08) | 270 | `ROADMAP.md` row 0.13's own `efun_defs.c` accounting (excludes ifdef'd-out/non-runtime entries; a raw `grep -c '^{"'` over the same file gives 276, the 6-name difference being exactly those exclusions) |
| LDMud | ~305 | Rough estimate: `temp/ldmud/doc/efun/` file count (one doc page per real efun is LDMud's own documentation convention; not independently cross-checked against a table the way the FluffOS/DGD/AMLP numbers were, so treat as approximate) |
| DGD (this vendored C++ port) | 243 | Real count: `grep -c '^FUNCDEF('` across `temp/dgd/src/kfun/{builtin,std,file,math,extra}.cpp` |
| **AMLP** | **248 of 270 real FluffOS names** (240 non-`parse_*` + all 8 `parse_*`) | `ROADMAP.md` row 0.13/0.13a's own accounting. The 22-name real gap: 40 non-`parse_*` names are documented, individually-verified exclusions (architecture mismatch, e.g. no `TYPE_CLASS`/buffer-type/ed()-editor equivalent, or zero real call sites across all six vendored mudlib corpora) minus the ones no longer counted against the gap. `parse_*` itself is no longer part of the gap at all (all 8 names implemented, every argument of every one of them real as of 2026-08-20's `nicks` slice, see row 0.13a's own entry). AMLP's own efun table primarily targets FluffOS's surface, with LDMud/DGD-specific additions layered on where a dialect diverges (`m_indices`/`m_values`, `#'name`, `nil`, `atomic`): it does not separately track coverage against LDMud's or DGD's own full efun/kfun lists the way it does for FluffOS. |

## Master/boot apply coverage

All three real drivers gate a running game through a "master object"
(FluffOS/LDMud) or "driver object" (DGD) that the driver core calls back
into for privilege checks, boot sequencing, and connection lifecycle
events, dozens of real named applies each. AMLP's own `BootApi`
abstraction (`include/amlp/dialect/BootApi.hpp`) currently recognizes
exactly **one** real per-dialect apply, `masterUidApply()`
(`get_root_uid` for FluffOS, `get_master_uid` for LDMud), deliberately
narrow, not an oversight: `connectApply()`/`netDeadApply()` are
explicitly omitted pending the three-way connect/disconnect design
question above (row 1.4/1.16). Real per-file `valid_read`/`valid_write`
privilege checks are implemented directly in `EfunTable.cpp` (dialect-
gated per real FluffOS 3-arg vs. LDMud 4-arg call convention) without
going through the `BootApi` abstraction at all. This is a real, current
gap against both real drivers' own much larger master-apply surface,
not yet closed.

---

## Feature-by-feature

Checkmarks mean "implemented and verified live against the real running
driver," not "attempted." A dash means the real driver in that column
does not have the feature at all (not a gap for it, just not
applicable).

| Feature | AMLP | FluffOS 2.9 | LDMud | DGD |
|---|---|---|---|---|
| Dialect selectable via config, one driver | Yes (`fluffos`/`ldmud`/`dgd`) |, (is FluffOS) |, (is LDMud) |, (is DGD) |
| Closures: `(: name :)` / `#'name` (FluffOS-style) | Yes | Yes | Yes (also has its own richer kinds) |, |
| Driver hooks (`set_driver_hook()`, `inaugurate_master()` boot wiring) | Partial (full 32-slot storage/dispatch real; 5 of several real trigger points wired, `H_MOVE_OBJECT0/1`, `H_MODIFY_COMMAND`, `H_RESET`, `H_CLEAN_UP`; only `inaugurate_master()`'s arg=0 first-boot case is wired, arg=1/2/3 reload/reactivation cases are not) |, | Yes |, |
| `privilege_violation()` authorization gate | Partial (4 real trigger points wired: `bind_lambda()` cross-object, `set_driver_hook()`, `call_out_info()` dialect-gated, `input_to()`'s `INPUT_IGNORE_BANG` flag; ~20 of 26 real doc-cataloged operations remain ungated, zero real corpus evidence or no implemented mechanism to gate for each) |, | Yes |, |
| Closures: real `lambda()`/`unbound_lambda()`/`bind_lambda()` kind distinction | Partial (`unbound_lambda()`/`bind_lambda()` real for the one confirmed corpus quoted-code shape; plain `lambda()` and the full closure-kind matrix not started, row 1.7/1.8 open) |, | Yes |, |
| Mapping width > 1 (`m_allocate`, N-column values) | Partial (`m_indices`/`m_values` real names ported, single-column only; row 1.9 open) |, | Yes |, |
| Shadows (`shadow()`, LDMud `unshadow()`/`query_allow_shadow`) | Yes | Yes (FluffOS shape) | Yes (LDMud shape, done) |, |
| `replace_program()`, LDMud no-arg sole-inherit form | Yes | Partial (has `replace_program`, not the LDMud no-arg form) | Yes |, |
| `nil` as a distinct value | Yes (dialect-gated) |, |, | Yes |
| `atomic` function modifier (checkpoint/rollback) | Lexed only, no VM semantics (row 1.12, not started) |, |, | Yes |
| `rlimits` (per-task tick/stack limits) | No (row 1.11, not started) |, |, | Yes |
| `parse_string` (grammar-driven string parsing kfun) | No (row 1.13, not started, confirmed comparable in size to `parse_*` itself, a dedicated DFA+LALR subsystem) |, |, | Yes |
| Lightweight objects (value-semantics objects) | No (row 1.14, not started) |, |, | Yes |
| `parse_*` natural-language sentence parser | All 8 efuns real, including single- and two-object `OBJ`/`LIV`/`OBS`/`LVS` matching and the `nicks` nickname-mapping argument | Yes (real source this work is ported from) |, |, |
| `save_object`/`restore_object`, real `.o` text format | Partial (restore-side only; save still uses this driver's own format) | Yes | Yes (own format) | Statedump-based, different model entirely |
| PCRE `regexp`/`regexplode`/`reg_assoc` | Yes | Yes | Yes (own regexp efuns) |, |
| Full telnet IAC negotiation, echo suppression, NAWS | Yes | Yes | Yes | Yes |
| `socket_*` efun family | Partial (STREAM/DATAGRAM only, no MUD mode, no binary modes) | Yes (full) | Yes (full) |, |
| Coroutine scheduler / `async`/`await` | Partial (row 2.5 coroutine scheduler v1 slice landed; `async`/`await` keywords row 2.6, not started) |, |, |, |
| LLVM JIT backend | No (Phase 2, not started) |, |, |, |
| Hotboot (fd-passing exec, connections survive) | No (Phase 2 row 2.3, not started) | Yes | Yes | Yes (via statedump/restart, different mechanism) |
| World-level statedump / object swapout | Partial (row 2.1 statedump v1 slice landed, plus row 2.4 dual persistence coexistence; object swapout row 2.2, not started) |, |, | Yes (DGD's own signature architecture) |
| TLS / WebSocket | No (Phase 2, not started) | Not in this vendored ds2.08 snapshot | Not checked | Not checked |
| Built-in SQLite / hash / JSON efuns | Partial (SQLite `db_connect`/`db_exec`/`db_fetch`/`db_close`, row 2.15, real LDMud shape specifically, dialect-gated to `dialect: ldmud` per row 2.40 -- real current FluffOS's own `db.spec` has a genuinely different `db_*` contract this driver does not implement; `hash()`, row 2.16, built and landed; JSON efuns, row 2.17, not started) | Some (own DB package options) | Some | Some |
| LSP server (`--lsp`) | No (Phase 2, not started) |, |, |: |
| Generational GC (replacing `shared_ptr`) | No (Phase 3, not started) | Real GC | Real GC | Real GC |
| Full privilege/uid trust hierarchy | Partial (`privs()`, no full uid/euid/domain hierarchy) | Yes | Yes | Yes (own model) |

---

## What AMLP does not have, stated plainly

- **No `ed()` line editor, no database package, no crypto package
  beyond `crypt`/`oldcrypt`**: real FluffOS/LDMud both ship these;
  AMLP's own row 0.13 accounting lists them as confirmed architecture-
  mismatch exclusions, not silent gaps, but they are real absent
  features regardless of the reason.
- **No real garbage collector.** Object lifetime is plain
  `std::shared_ptr` reference counting throughout (`LpcObject`, closures,
  arrays, mappings). This means no cycle collection at all: a real,
  documented category of memory a genuine GC would reclaim that this
  driver currently never does. Phase 3's own `src/gc` item is exactly
  this, and is entirely unstarted.
- **The headline "exceed FluffOS/LDMud" features do not exist yet.**
  No TLS (row 2.13), no WebSocket (row 2.14), no hotboot (row 2.3), no
  LLVM JIT (row 2.11), no LSP server (row 2.19), no object swapout
  (row 2.2), no `async`/`await` (rows 2.6 through 2.8), no live
  hot-reload (row 2.21), no generational GC (Phase 3 row 3.3, `src/gc`
  is `instruct.md`-only). Production-hardening and documentation are
  almost entirely open: rows 3.1 through 3.8 (full uid/gid trust
  hierarchy, per-domain filesystem jail, generational GC, a
  conformance test suite, an LPC language spec, a per-dialect driver
  API reference and porting guide, a second real third-party mudlib)
  are all `[ ]`; only row 3.9 is done.
- **Most Phase 2/3 differentiators are still a plan, not code.**
  JIT, hotboot, object swapout, TLS, WebSocket, the LSP server,
  hot-reload, `async`/`await`, a conformance suite, and generational
  GC all still have only a real `instruct.md` and zero implementation,
  and none should be described as "in progress." Thirty-five Phase 2
  rows are the exception, real and landed rather than planned. Six are
  architecturally substantial runtime work: statedump v1 (2.1), dual
  persistence coexistence (2.4), the coroutine scheduler v1 (2.5), the
  apply cache (2.9), the full PCRE suite (2.12), and built-in SQLite
  (2.15, dialect-gated to `ldmud` per row 2.40). The other 29 are
  small single-efun or single-argument spec-conformance slices from a
  package-by-package `src/packages/*/*.spec` sweep (2.16, 2.23 through
  2.30, 2.33a, 2.37, 2.46 through 2.62). Phase 3's own row 3.9 (the
  AetherMUD mudlib, now run through login, character creation,
  movement, dialogue, and real combat, not just a boot) is the one
  landed Phase 3 row.
- **Master/boot apply coverage is currently one name deep**
  (`masterUidApply()` only) against each real driver's own much larger
  master-object callback surface: see the section above. LDMud's
  separate driver-hook mechanism (`set_driver_hook()`,
  `inaugurate_master()`) is real and automatically wired at boot, but
  only 5 of its many real trigger points actually dispatch anything
  yet, and `privilege_violation()`, the real authorization gate several
  of those hooks and efuns sit behind, covers only 4 of 26 real
  doc-cataloged operations.
- **Dialect coverage is asymmetric.** FluffOS is the primary, most
  complete target (this is where the bundled mudlib and most of the
  regression corpus point); LDMud has real, working, dialect-gated
  pieces (closures, `m_indices`/`m_values`, shadows, `replace_program`,
  driver hooks) but real gaps (full closure kinds, mapping width, most
  hook trigger points); DGD support is the
  thinnest of the three by design (comparison-only, not a completion
  target): `nil` and `atomic`-the-keyword are the only DGD-dialect
  pieces implemented, with the rest (`rlimits`, `atomic`-the-semantics,
  `parse_string`, LWOs, the driver+auto boot path) confirmed real and
  scoped but not started.
- **This driver's own memory model can reach states real FluffOS
  cannot.** Documented directly in `STATUS.md`/`ROADMAP.md` where
  found (e.g. `parse_dump()`'s own `"(destructed)"` fallback for a
  weak_ptr that expired without ever going through `destruct()`) --
  a consequence of `shared_ptr`-based lifetime instead of real FluffOS's
  synchronous refcounted free, not a bug, but a real behavioral
  difference worth knowing about if porting mudlib code that depends on
  exact destruction timing.

## Where AMLP is already a real, working, from-scratch driver

Worth stating alongside the gaps above, since a gap list alone
undersells what already works: AMLP is not a wrapper or a fork of any
of the three real drivers: its own lexer, parser, code generator,
bytecode VM, object system, and network layer are original
implementations, verified continuously against real vendored source and
a real bundled mudlib rather than against assumption. 820 regression
tests pass as of this writing (`STATUS.md` 2026-09-01; the count
changes every session), and the discipline behind every checked
row above is the same: read the real source, port the real behavior
(including confirmed real quirks and off-by-ones where they exist, not
just the "sensible" version), and verify live against a real running
instance before calling anything done.
