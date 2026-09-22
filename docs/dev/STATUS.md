# STATUS

**2026-09-22: ZMP (docs/COMPARISON.md 2.32d).** Zenith MUD Protocol,
telnet option 93/0x5D. Re-cloned upstream FluffOS fresh (this machine's
own `temp/` still absent; per `docs/dev/ROADMAP.md`'s own "Verification
note") and re-verified from scratch rather than trusting the prior
session's own summary, per this row's own explicit instruction - one
correction surfaced immediately: `src/vm/internal/applies` maps `ZMP`
to the real LPC name `zmp_command`, not a bare `zmp` as the earlier
summary assumed without checking that file directly.

Confirmed genuinely driver-native and a real gap (not mudlib-side, the
MTTS-style trap this row was explicitly asked to check for): real
`on_telnet_do()`'s own ZMP case (`src/net/telnet.cc`) sets `USING_ZMP`
and nothing else - no apply fires on negotiation, unlike MSP's
`MSP_ENABLE`, confirmed directly by grepping `src/vm/internal/applies`
for a `ZMP_ENABLE` entry and finding none. So `Connection::zmpEnabled()`
follows GMCP/MSDP's plain-flag-plus-queued-incoming shape, not MSP's
one-shot-apply shape: no `Server`-level enable-dispatch method needed
for ZMP the way `fireMspEnableIfNegotiated()` was for MSP. Real
`on_telnet_dont()` has no ZMP case either (falls to the generic
log-only default), so no DONT/WONT handling was added, matching real
behavior rather than inventing one.

Wire framing verified directly against `src/thirdparty/libtelnet/
libtelnet.c`'s `telnet_send_zmp()`/`telnet_zmp_arg()`/`_zmp_telnet()`:
`IAC SB ZMP <command>\0<arg1>\0<arg2>\0...\0 IAC SE`, every field
(command included) IAC-escaped (the same `appendIacEscaped()` helper
MSP's own escaping fix now shares, factored out this row rather than a
second copy-paste) then NUL-terminated; the real receiver rejects a
payload not ending in NUL as an incomplete frame and fires no event at
all - matched exactly in `handleSubnegotiation()`'s own new ZMP branch,
confirmed by a dedicated malformed-frame regression test. Real
`f_send_zmp()` (`telnet_ext.cc`) has no `USING_ZMP` guard on the
*outgoing* side at all (unlike MSP's own real `USING_MSP` guard) -
matched deliberately, not overlooked, with its own regression test
proving `send_zmp`/`sendZmp()` still produce real wire bytes before
negotiation.

New `src/proto/ZmpHandler` (thin wrapper, mirrors GmcpHandler/
MsdpHandler - ZMP has a genuine receiving side, unlike MSP/MSSP). New
efuns with verified real names/signatures: `has_zmp(void|object)`
(mirrors `has_gmcp`/`has_msdp`/`has_msp`'s optional-object shape) and
`send_zmp(string, string*)` - confirmed to read `command_giver`, not
`current_object` the way `telnet_msp_oob` does (a real, verified
difference between the two rows, not an inconsistency), and confirmed
to silently skip non-string array elements rather than erroring,
matched exactly. New `Server::dispatchIncomingZmp(VM&, Connection&)`,
pulled out static for the same reason `fireMspEnableIfNegotiated()` was
- this row's own apply-dispatch test needed a seam GMCP's/MSDP's own
still-inline incoming loops do not have yet (deliberately not touched
here, out of this row's scope).

8 new regression tests in `test/test_net.cpp`, covering negotiation
(both directions), wire framing and escaping, inbound parsing (a normal
multi-arg message, a no-args message, and a malformed non-NUL-terminated
frame), the efuns (real signatures, non-string filtering), and the
`zmp_command` apply dispatch (including a no-refire check on a second
drain). Two real bugs in the tests themselves caught and fixed before
landing, both from imprecise manual byte-counting of C string literals
with embedded NULs: a missing trailing `\n` on one inbound-parsing test
(`pollLines()` needs a line terminator to report a "line" at all, even
an empty one - copied from the negotiation-test pattern but missed on
this one), and an off-by-one length passed to `std::string(literal, N)`
on another (a string literal's own compiler-appended terminator is easy
to double-count against one's own embedded `\0`; every such length in
this row's tests was then re-verified with a small Python script rather
than by further hand-counting). Full clean rebuild from scratch (exit
0), `ctest` 2/2 green, and a live boot where a real telnet client saw
`IAC WILL ZMP` offered on connect, a real `IAC DO ZMP` reply produced no
crash and no client-visible response (matching verified real semantics:
no enable-apply exists for ZMP to fire), and a real well-formed inbound
ZMP message (`IAC SB 93 "zmp.ping\0arg1\0" IAC SE`) was parsed without
crash and produced no client-visible response either (the driver's own
bundled login object has no `zmp_command()` defined, matching
`VM::callFunction`'s already-established silent-no-op-on-missing-
function behavior, not new to this row).

**Two separate findings from this same verification pass, not fixed
here - flagged for a future row:**
- Real `GMCP`/`MSDP` also each have their own real `GMCP_ENABLE`/
  `MSDP_ENABLE` applies (`src/vm/internal/applies`), confirmed directly
  in the same pass that found `ZMP_ENABLE` does *not* exist. This
  repo's own already-shipped `gmcp()`/`msdp()` handling never fires
  either enable-apply - a real, verified gap in code that predates this
  session, parallel to (but distinct from) the `sendMssp()`/`sendMsdp()`/
  `sendGmcp()` IAC-escaping gap already flagged under MSP's own entry
  below.
- `docs/COMPARISON.md` row 2.32's own original text guessed MSP/ZMP
  were "not in `src/proto/instruct.md`'s own protocol set" and left it
  at that; both turned out to be real driver-native FluffOS mechanisms
  once actually checked, not merely absent from that one file's own
  scope decision. The lesson already written into
  `docs/dev/ROADMAP.md`'s "Verification note" this session covers this
  case too: an absence in one repo's own planning document is not
  evidence of absence in the real driver.

**2026-09-22: MSP (docs/COMPARISON.md 2.32c), and a verification-tooling
note.** `src/proto/instruct.md`'s own protocol table is now fully closed
(GMCP/MSDP/MSSP/MXP landed, MTTS confirmed already driver-complete), so
this row is not from that table: it is `docs/COMPARISON.md` row 2.32's
own leftover "MSP/ZMP ... not in src/proto/instruct.md's own protocol
set" note, picked up because it is the next protocol-shaped Open row in
this repo's actual backlog and, unlike that leftover note's original
guess, MSP genuinely is native, driver-side FluffOS work.

This machine's `temp/` is absent (as it has been all session), but this
row needed the real thing to check MTTS-style false assumptions before
writing driver code, not just citing the absence and moving on: this
session cloned current upstream FluffOS fresh from
github.com/fluffos/fluffos (network access confirmed available in this
environment; deleted again after use, not vendored into this repo) and
grepped it directly. That search corrects something STATUS.md has
asserted three times now (MSSP/MSDP/MXP's own entries below): GMCP,
MSDP, MSSP, MSP, and ZMP are all real, native, driver-side mechanisms in
current FluffOS (`src/net/telnet.cc`, `src/net/msp.cc`,
`src/packages/core/mssp.cc`), each with its own real telnet option code,
negotiation, and efuns - not the "public protocol, not tied to any one
real LP driver's own C source" this repo assumed by necessity when
`temp/` was unavailable and no other source was checked. MXP's own
"not tied to any one real LP driver" line is the one of the four that
holds up: current FluffOS has no native MXP support at all, confirmed
by the same search (only `packages/dwlib/dwlib.cc`'s unrelated
`replace_mxp()` escaping efun matches "mxp" anywhere in that source
tree). This does not change what already shipped for GMCP/MSDP/MSSP -
their wire formats are public-spec-correct regardless, and retrofitting
them against real FluffOS signatures (which may differ from this
repo's own already-shipped, already-tested efun names) is a separate,
deliberately unstarted row, not assumed done here.

MSP itself (telnet option 90/0x5A): `Connection::mspEnabled()` and
`sendMspOob()`, negotiated the same two-branch Will/Do shape MSDP/MXP
already use, plus a new one-shot `takeMspEnableNegotiated()` flag - the
first of the five protocol rows where real source shows a driver
actually needs one: `on_telnet_do_msp()` (`src/net/msp.cc`) fires
`safe_apply(APPLY_MSP_ENABLE, ip->ob, 0, ORIGIN_DRIVER)` every time
negotiation completes, mapped LPC-side to plain `msp_enable()`
(`src/vm/internal/applies`: "MSP_ENABLE" with no override). That apply
needs VM access `Connection` deliberately does not have, so it is fired
from `Server`, pulled out as a new public static
`Server::fireMspEnableIfNegotiated(VM&, Connection&)` rather than
inlined in the private `handleConnection()` - matching this file's own
existing `dispatchLine()`/`fireNetDeadIfLinkDead()`/`pollSockets()`
convention (`net/instruct.md`'s own stated "Key invariants") of pulling
out anything that needs direct unit-test coverage as its own static,
Server-instance-free seam. `sendMspOob(payload)` is a raw passthrough
of an already-composed MSP trigger string (real code imposes no
structure on it either; the actual `!!SOUND(...)`/`!!MUSIC(...)`
grammar is mudlib-side in real FluffOS too), silently doing nothing if
MSP was never negotiated, matching real `telnet_send_msp_oob()`'s own
`USING_MSP` guard exactly. New efuns use the *real* verified names
verbatim (`has_msp`, `telnet_msp_oob`, from `core.spec`) rather than
this repo's own `has_X`/`send_X` convention invented for GMCP/MSDP/MXP
when no real name was available - the one place this row's naming
differs from the last several.

3 new regression tests in `test/test_net.cpp`, including this repo's
first test to exercise `Server`'s own apply-dispatch logic directly
(every prior GMCP/MSDP/MSSP/MXP row only unit-tested `Connection`'s
wire-level state, leaving apply dispatch to live-boot checking, since
nothing before MSP fired an apply on simple negotiation). One test
regression caught and fixed in the same session: the first draft of the
new efun-wiring test called `conn.pollLines()` without
`setNonBlocking(fds[0])` first (missed copying that from the
negotiation-test pattern, only the efun-test pattern, which never calls
`pollLines()` itself) and hung the whole suite for 143s before being
killed manually; fixed once caught, verified with a hard-timeout rerun
after. Full clean rebuild from scratch (exit 0), `ctest` 2/2 green, and
a live boot where a real telnet client saw all five of GMCP/MSSP/MSDP/
MXP/MSP offered on connect and a real "IAC DO MSP" reply produced no
crash and no client-visible response (matching real semantics exactly:
the apply fires silently, and the driver's own bundled login object has
no `msp_enable()` defined, which is expected and not an error per
`VM::callFunction`'s already-existing missing-function-is-a-silent-noop
behavior, not new to this row).

**2026-09-22 review pass (same day, before commit):** re-reviewed this
row's own staged diff before it was ever committed. Found and fixed one
real defect: `sendMspOob()` appended the mudlib-supplied payload
verbatim, but real `telnet_send()` (`src/thirdparty/libtelnet/
libtelnet.c`, confirmed directly - the function real `telnet_
subnegotiation()` actually calls for the payload portion, re-checked
against a fresh upstream clone since this repo's own `temp/` is still
absent) doubles any literal IAC (0xFF) byte in the payload before
writing it to the wire; skipping that is a real, reachable framing bug
specifically for MSP (unlike `sendMssp()`/`sendMsdp()`/`sendGmcp()`
elsewhere in this file, whose payloads are driver-composed ASCII
unlikely to ever contain 0xFF in practice, `telnet_msp_oob(string)`
takes fully arbitrary mudlib content by design). Fixed to match real
`telnet_send()`'s own doubling exactly; the other three `sendX()`
methods keep the same gap and are not touched here - a real, separate,
pre-existing finding for a future row, not assumed fixed by osmosis.
New regression test asserts the doubled-byte wire output directly.
Also strengthened the existing apply-dispatch test: `enabled` was a
flag set unconditionally to 1 on every `msp_enable()` call, so a second
`fireMspEnableIfNegotiated()` call re-setting it to 1 was
indistinguishable from a real double-fire in that one test's own
assertions (the one-shot flag itself was still correctly proven
separately in `testMspDoReplySetsEnabledFlagsOnce`, so this was a test
robustness gap, not a demonstrated driver bug); changed to an
incrementing counter so the same test now directly proves no double-
fire through the real dispatch path. Also corrected this file's own
"4 new regression tests" line above to the real count, 3 - test
function count, not edit count, was miscounted when first written.
Re-verified after both fixes: clean rebuild from scratch (exit 0),
`ctest` 2/2 green (~50s this pass, up from the usual ~35s; traced to an
unrelated background process on this machine consuming ~650% CPU
during the run, confirmed via `ps`, not a regression in the suite
itself), and a repeat live boot confirming the fix did not change
observable wire behavior for the already-covered cases.

**2026-09-22: MXP (src/proto/instruct.md Phase 3 row, docs/COMPARISON.md
2.32b), and MTTS correction (2.32).** MUD eXtension Protocol, telnet
option 91/0x5B. Negotiation only (`Connection::mxpEnabled()`, same
two-branch Will/Do shape MSDP just added, both offered proactively from
`Server::onNewConnection()`/`handleConnection()`'s WS-parity path
alongside GMCP/MSSP/MSDP); no incoming subnegotiation parsing, since
real MXP's optional client VERSION/SUPPORT replies have no reader in
this driver to consume them. New `src/proto/MxpHandler` (unlike
GmcpHandler/MsdpHandler, this one holds the real logic, not a thin
wrapper: three static text-wrap methods, `bold()`/`color()`/`link()`,
each returning plain text unchanged when the target connection has no
MXP, or the real `<B>`/`<COLOR FORE=.. BACK=..>`/`<SEND "..">` element
otherwise). New efuns `has_mxp(void|object)` (mirrors `has_gmcp`/
`has_msdp`'s optional-object-or-current-giver shape) and
`mxp_bold`/`mxp_color`/`mxp_link(object, string, ...)` (a required
target object instead, since wrapping text for a specific player ahead
of a `write()`/`tell_object()` call is the real use case, not
necessarily the currently-executing object; matches
`src/proto/instruct.md`'s own `mxp_tag(object player, ...)` sketch
shape). Public client protocol (zuggsoft.com/zmud/mxp.htm), same
citation caveat as MSSP/MSDP: not tied to any one real LP driver's own
C source, `temp/` absent on this machine either way. 3 new regression
tests in `test/test_net.cpp` (efun wrap-vs-plain-text round trip,
Will-branch wire bytes, Do-branch flag). Suite green (2/2 tests). Live
boot (`./build/kjdmud etc/driver.cfg`) confirmed a real telnet client
sees all four of GMCP/MSSP/MSDP/MXP offered on connect.

That closes every protocol in `src/proto/instruct.md`'s own table
except MTTS, which is not actually open: a comment already present in
`include/kjdmud/net/Connection.hpp` and `src/efun/EfunTable.cpp` (both
present since the very first commit, predating this repo's own
2026-09-12 docs reset, so never carried into `STATUS.md` until now)
documents that real FluffOS's own driver-level MTTS mechanism is
exactly `request_term_type()`/`start_request_term_type()`/
`terminal_type()` (the apply)/`query_terminal_type()`, all four of
which already exist in this driver. The multi-round "ask again, compare
to the previous answer, stop once it repeats or a third round yields
'MTTS <bitmask>'" convention that `src/proto/instruct.md`'s own
`MttsHandler.hpp` sketch (bitmask table, `query_client_flags()` efun)
assumed was driver-side is genuinely mudlib-side in real FluffOS,
confirmed by reading `comm.c` directly: no round-counting state, no
"MTTS" string comparison, and no bitmask table anywhere in the real
driver. Building that sketch would have been unverified, wrongly-scoped
driver work. `docs/COMPARISON.md` row 2.32 corrected to reflect this
rather than left listing MTTS as open driver work.

**2026-09-22: MSDP (src/proto/instruct.md Phase 3 row, docs/COMPARISON.md
2.32a).** Mud Server Data Protocol, telnet option 69/0x45. Same shape as
GMCP (bidirectional, so it lives in `src/net/Connection.cpp` plus a thin
`src/proto/MsdpHandler.hpp`/`.cpp` wrapper, not MSSP's Server-only
one-shot-flag pattern): `Server::onNewConnection()` now sends "IAC WILL
MSDP" alongside the existing TTYPE/NAWS/GMCP/MSSP offers (both the
telnet-only path and `handleConnection()`'s WebSocket parity path, added
together this time rather than needing a second live-testing pass the
way MSSP's WS-parity gap did). `Connection::handleNegotiation()` accepts
either a client-initiated "IAC WILL MSDP" or a reply "IAC DO MSDP" to
this driver's own offer, both setting `msdpEnabled_`.
`Connection::sendMsdp(var, value)` writes "IAC SB MSDP MSDP_VAR var
MSDP_VAL value IAC SE"; `handleSubnegotiation()` parses the same shape
back into `incomingMsdp_` (a `vector<pair<string,string>>`), drained by
`Server::handleConnection()` into a per-pair `msdp(var, val)` mudlib
apply, the same one-apply-per-message shape `gmcp()` already uses. New
efuns `has_msdp()`/`send_msdp(string, string)` in `src/efun/NetEfuns.cpp`,
named after the established `has_gmcp`/`send_gmcp` precedent rather than
`src/proto/instruct.md`'s own original `query_msdp`/`msdp_send` sketch
(that file already isn't a live status signal per `CLAUDE.md`, and
`GmcpHandler.hpp`'s own comment already documents the same kind of
naming deviation for GMCP). v1 scope: single scalar MSDP_VAR/MSDP_VAL
pairs only, no MSDP_TABLE/MSDP_ARRAY nesting (real spec section 3) since
nothing in this driver has a structured value to report yet; a real
client's REPORT/UNREPORT/LIST/RESET requests already arrive in the
plain scalar shape this covers, so nothing about that side is blocked
on the nesting gap. Public client protocol (tintin.mudhalla.net/
protocols/msdp), not tied to any one real LP driver's own C source, so
this is not a verified port against vendored reference source, same
citation caveat MSSP's own entry below already notes (`temp/` is not
present on disk on this machine either way). 3 new regression tests in
`test/test_net.cpp` (efun round-trip + wire bytes, incoming subneg
parsing, and the Do-branch negotiation flag), plus a live boot
(`./build/kjdmud etc/driver.cfg`) confirmed a real telnet client sees
"IAC WILL MSDP" (`\xff\xfb\x45`) on connect. Suite green (2/2 tests).

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
