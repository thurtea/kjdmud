# STATUS

**2026-09-20: MSSP (src/proto/instruct.md, Phase 3 row).** MUD Server
Status Protocol, telnet option 70/0x46. `Server::onNewConnection()` now
sends "IAC WILL MSSP" to every telnet (non-WebSocket) connection
alongside the existing TTYPE/NAWS/GMCP offers. On the client's "IAC DO
MSSP" reply, `Connection::handleNegotiation()` sets a new one-shot
`takeMsspNegotiated()` flag (same shape as `takeWindowSizeUpdate()`/
`takeTerminalTypeUpdate()`), which `Server::handleConnection()` consumes
to send the one-time MSSP data block via new `Connection::sendMssp()`:
NAME, PLAYERS, UPTIME, CODEBASE. No LPC apply or efun involved, matching
this row's own spec (no MSSP-specific efun is listed anywhere in
`src/proto/instruct.md`'s "New efuns" section). Public client protocol,
not tied to any one real LP driver's own C source, so this is not a
verified port against vendored reference source the way most of this
project's other work is (none is present on disk for this row either
way).

Deliberately placed in `src/net/Connection.cpp`, not a new `src/proto`
`MsspHandler` class as `instruct.md`'s own original sketch described:
`src/proto` already depends on `src/net` (for `Connection` itself), and
this is a one-way, `Server`-triggered send with no receiving/parsing
side, so a `Server`-called `src/proto` class would have been a circular
library dependency. `GmcpHandler` avoids this the same way in practice:
its own real send logic already lives in `Connection::sendGmcp()`, not
in `src/proto` itself. 1 new regression test in `test/test_net.cpp`
covers the negotiation flag and the exact wire bytes. Suite green (2/2
tests).

**2026-09-20: max_connections config key (src/config/instruct.md Phase
0).** New `Config::maxConnections()` (default 256), parsed from a
`max_connections` config line the same way `max_eval_cost` etc. already
are. `Server::onNewConnection()` now rejects (closes the fd, never
reaches `master->connect()`) any new connection once
`connectionCount()` is already at this value. The check itself is
pulled out as a small static `Server::atMaxConnections(currentCount,
maxConnections)` predicate, same reasoning as `dispatchLine()`/
`fireNetDeadIfLinkDead()`/`pollSockets()`: directly testable without a
live listening socket, since nothing in this codebase currently
constructs a full `Server` in a test. No real driver source is present
on disk (`temp/reference/fluffos-2.9-ds2.08` etc. are not checked out
right now) to cite an exact MAX_USERS-style rejection behavior against,
so this is a plain accept-or-close gate, not a verified port of a
specific reference driver's own over-limit handling. `save_format` and
`tls_cert`/`tls_key`, the other three rows this same instruct.md Phase
0 table lists, turned out already obsolete or already done:
`save_object` (`EfunTable.cpp`) unconditionally writes the real FluffOS
text format now, no format-switching enum was ever added or needed, and
`tls_cert`/`tls_key` already exist in `Config`. 2 new regression tests
in `test/test_net.cpp`. Suite green (2/2 tests).

**2026-09-20: LPC-native test runner (ROADMAP row 2.22).** New efuns
`assert_equal`, `assert_not_equal`, `assert_throws`, `test_pass`,
`test_fail`, `run_tests` (`src/efun/EfunTable.cpp`), backed by a new
`TestResultsRegistry` (`src/efun/TestResultsRegistry.hpp`/`.cpp`).
`run_tests(object)` calls every `test_*` function declared on the target
object, including inherited ones, recording a pass or the caught
`LpcRuntimeError` message per call; `test_pass`/`test_fail` let a test
function log finer-grained named sub-results directly into the same
buffer. New `test_results_path` config key (`Config.hpp`/`.cpp`): when
set, `run_tests` writes the full results buffer to that path as a
hand-rolled JSON array, no dependency on the still-unimplemented
`json_encode`/`json_decode` (row 2.17). Driver-added convenience efuns,
no FluffOS/LDMud/DGD citation for any of the six. 4 new regression tests
in `test/test_lexer.cpp`. Suite green (2/2 tests).

**2026-09-19: call_stack mode 2 (function names).** `ObjectFrameGuard`
now pushes/pops a per-frame function name in lockstep with
`callStack_`. `call_stack(2)` returns those names, current first.
`call_stack` modes 0-3 are all live. Regression added. Suite green
(2/2 tests).

**2026-09-19: call_stack mode 3 (origin).** `call_stack(3)` returns
per-frame `origin_name` strings, current frame first. Reads
`originFrames()` and zip-aligns from the innermost call frame so an
extra `ObjectFrameGuard` from a core-efun closure (no OriginGuard) does
not steal another frame's origin. Mode 2 followed in the next entry.
Regression added. Suite green (2/2 tests).

**2026-09-19: query_ip_number/name honor object arg.** Both efuns resolve
an optional interactive object through `InteractiveRegistry` (same
pattern as `query_ip_port`) instead of only `OutputContext::current()`.
No-arg still prefers the current connection, then command_giver.
`query_ip_name` stays numeric-only (no blocking DNS). Website footers
dropped affiliation disclaimers; CREDITS trimmed to a short prior-art
list. Suite green (2/2 tests).

**2026-09-19: query_ip_port uses accept port.** Multi-port `listen:` meant
`query_ip_port` returning only `Config::port()` was wrong for websocket
and TLS listeners. `Connection` now stores the accept port
(`Server::onNewConnection`); the efun reads it via
`InteractiveRegistry`. Regression updated. Suite green (2/2 tests).

**2026-09-19: Public site and snapshot purge.** Added `website/` (overview,
download, documentation, WebSocket client) modeled as a simple project
site. GitHub Pages workflow deploys `website/` from `main`. README and
INSTALL now describe only the current driver; INSTALL points the browser
client at `website/client.html`. Removed `references/` (old local driver
snapshot trees) from the repository. Deleted `research.md` and
`docs/dev/TODAY.md`. FluffOS citation sources remain under gitignored
`temp/` for local development. Next: small FluffOS driver gap in `src/`,
not another mudlib boot chase.

**2026-09-17: MUD socket read framing.** `Server::pollSockets` assembles
the 4-byte length header and save_variable body, then
`parseRestoreVariableTopLevel` for the read callback. Save/restore
helpers moved to `vm/SaveVariable` so net can use them. Round-trip
regression green.

**2026-09-17: Driver-first scope.** Product work stays in the driver
(`src/`). External mudlibs are probe-only; do not chase RiftsMUD /
Nightmare / library completion as the next goal. Prefer the next clear
driver gap over another mudlib boot matrix.

**2026-09-17: TMI-2 live look/move.** `clone_object`/`new` now pass
trailing args to `create()` (SOCKET styles were ignored before). MUD
socket mode (0) creates TCP and frames `socket_write` with
`htonl(len)+save_variable`. Non-blocking connect treats
EPERM/ENETUNREACH/EHOSTUNREACH like EINPROGRESS so I3 create can finish
when outbound routes are denied. Live on 4210: create, ENTER, quad
room `look`, `say`, `inventory`, `north` to MudOS room. Suite green
for new clone/MUD regressions.

**2026-09-17: TMI-2 live login.** Port 4210 adapter. New-character
flow completes (name letters-only, confirm, password, gender, race,
email, real name). Wizard grant, `say`, and `inventory` work. `look`
fails (no room): `I3` create dies on socket send `Descriptor out of
range`, `channels` create fails, `user::setup` aborts before
`complete_setup`. Groups "User ... not found" is mudlib noise. Next:
fix I3/channels so setup places the player. Handoff:
`notes/tomorrow.md`.

**2026-09-17: TMI-2 boot control.** Fetched `tmi2_fluffos_v3` under
gitignored `temp/`. First blockers fixed so simul_efun loads and the
driver reaches Ready: (1) `maskHashQuote` no longer mangles `'#'`
character constants; (2) `#ifdef 0` / `#ifndef 0` rewritten for system
cpp; (3) bare Ident `array` is a type only when a declaration follows,
so `array = array[offset..<1]` assigns; (4) `..` range ops masked with
spaces before cpp so macros like `D_IN` still expand (string/comment
aware). Suite green (949 checks, 0 fail). Bounded boot: Simul_efun
loaded, Master loaded, Ready on port 4201.

**2026-09-17: sscanf `%i`/`%X`/`%o`.** Integer alias, uppercase hex, and
octal format specs match FluffOS `inter_sscanf` bases. Adjacent `%s`
lookahead treats the new specs the same way. Three regression tests.
Suite green (944 checks, 0 fail).

**2026-09-17: kjdmud foundation.** Promoted `aemlpc-main` to the repo
root as kjdmud. Identity rename (`aemlpc` to `kjdmud`) for CMake,
`include/kjdmud/`, binary, and docs. Persist magic `AMLPSTATE1` kept.
Older trees archived under `references/drivers-snapshots/` (no nested
`.git` or `build/`). Apple ld: `object_manager_tests` no longer passes
GNU `--start-group` on macOS. Clean build + CTest green: 2/2 tests,
941 checks in `kjdmud_tests`, 0 fail. Bundled mudlib boots to Ready
for connections (`etc/driver.cfg` 5 iterations).

**2026-09-13: sprintf `%'X'`.** Quoted pad strings now fill field width
(FluffOS `add_pad`). Cited
`temp/external/lpuni_fluffos_v1/fluffos-2.9-ds2.07/sprintf.c` line 926;
canonical `temp/reference/fluffos-2.9-ds2.08` is absent. Suite green (941
checks, 0 fail).

**2026-09-13: sprintf `%@`.** Array-spread now applies the rest of the
specifier to each element. Cited
`temp/external/lpuni_fluffos_v1/fluffos-2.9-ds2.07/sprintf.c` `INFO_ARRAY`;
canonical `temp/reference/fluffos-2.9-ds2.08` is absent. Suite green (940
checks, 0 fail).

**2026-09-13: sprintf `%s` zero.** Integer 0 and a missing mapping key now
print `0` (FluffOS `NULL_MSG`). Cited
`temp/external/lpuni_fluffos_v1/fluffos-2.9-ds2.07/sprintf.c` and `config.h`;
canonical `temp/reference/fluffos-2.9-ds2.08` is absent. Suite green (939
checks, 0 fail).

**2026-09-13: sprintf `%X`.** Uppercase hex now matches C `%X`. Cited
`temp/external/lpuni_fluffos_v1/fluffos-2.9-ds2.07/sprintf.c` `INFO_T_C_HEX`;
canonical `temp/reference/fluffos-2.9-ds2.08` is absent. Suite green (938 checks, 0 fail).

**2026-09-13: sprintf `%0*`.** Zero-padded dynamic field width now pads
like `%0Nd`. Cited `temp/external/lpuni_fluffos_v1/fluffos-2.9-ds2.07`
`sprintf.c` (field-size `0` then `*`); canonical
`temp/reference/fluffos-2.9-ds2.08` is absent. Suite green (937 checks, 0 fail).

**2026-09-13: G6 three-room loop.** `watch_post` is linked north of
`lower_gate` with a floodlight scenery item. Market tin, hawker, and
slag bin unchanged. Suite green (937 checks, 0 fail).

**2026-09-13: G6 first rooms.** Two linked Chi-Town 'Burbs rooms under
`mudlib/domains/rifts/` (lower gate and market lane), one scenery poster,
one takeable ration tin, one hawker NPC. Non-portables persist via the
G5 graph. G1-G5 kit unchanged. Suite green (936 checks, 0 fail).

**2026-09-13: `sscanf` regexp format.** `%(regexp)` now matches and
assigns an anchored PCRE2 match, including `%s%(regexp)` lookahead.
Cited `temp/external/lpuni_fluffos_v1/fluffos-2.9-ds2.07/interpret.c`
`inter_sscanf`; canonical `temp/reference/fluffos-2.9-ds2.08` is absent.
Suite green (935 checks, 0 fail).

**2026-09-12: array `|` union.** BitOr on two arrays is now FluffOS
`union_array` (all of left, then each right element not already in
left). Cited `temp/external/lpuni_fluffos_v1/fluffos-2.9-ds2.07`
`eoperators.c` `f_or` and `array.c` `union_array`;
`temp/reference/fluffos-2.9-ds2.08` is not on disk. `^` stays ints
only. Suite green (933, 0 fail).

**2026-09-12: dump_state width>1.** Persist now writes extra mapping
columns as `M<count>w<width>:`. Width-1 dumps stay `M<count>:`. Magic
`AMLPSTATE1` unchanged. Suite green (932, 0 fail).

**2026-09-12: save_object write-side.** Width>1 mappings no longer throw;
extra columns write as `key:v0;v1` and restore. Object, closure, and
buffer write nothing (FluffOS `save_svalue` has no case; cited
`temp/external/lpuni_fluffos_v1/fluffos-2.9-ds2.07/object.c` because
`temp/reference/fluffos-2.9-ds2.08` is not on disk). Restore of those
slots, and of older files that wrote `0`, is integer 0. `dump_state`
still throws on width>1. Suite green (931, 0 fail).

**2026-09-12: identity rename.** Residual `amlp` driver-name strings are
now `kjdmud` (boot text, default `mud_name`, `KJDMUD_MAX_ITERATIONS`,
tmp/test prefixes, instruct include paths). Targets were already
`kjdmud` / `kjdmud_tests`. Persist dump magic `AMLPSTATE1` left as the
on-disk format token. Suite green (929, 0 fail).

**2026-09-12: docs reset.** `ROADMAP.md` and `STATUS.md` rewritten from
scratch as short driver-first records. The driver is the product.
`mudlib/` is the minimum ship Library only. No world-content or Rifts
work on the roadmap. LPC stays mudlib-only; the host stays C++20.

Do not treat chat as status. Verify against these two files, `git log`,
build, and test.

Future STATUS entries stay a few lines each: the decision and what
changed, not the derivation.
