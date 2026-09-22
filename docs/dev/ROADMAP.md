# kjdmud roadmap

kjdmud is a from-scratch LPC MUD driver written in C++20: lexer, parser,
bytecode compiler, VM, object system, call_out/heart_beat, and net. LPC
is mudlib-only. The host runtime stays C++20. Do not rewrite the driver
in LPC. The product is the driver. `mudlib/` is a minimum Library that
ships with the driver so it can boot and be tested. Track G world kit
proves that Library; do not grow a Palladium rules dump.

Primary dialect target is FluffOS. The `dialect` config also selects
LDMud. DGD is comparison-only.

## Done that matters

The language core works. Driver-side capabilities already landed:

- Multi-port `listen:` (telnet plus extra ports)
- TLS on marked listen ports
- WebSocket and WSS
- Connection encoding (default utf-8), GMCP, MSSP, MSDP, MXP, MSP, and ZMP
- MTTS: already driver-complete (`request_term_type`/
  `start_request_term_type`/`terminal_type`/`query_terminal_type`); the
  bitmask-parsing convention is genuinely mudlib-side in real FluffOS,
  not a driver gap
- `save_object` FluffOS `.o` write-side
- Net efun sidecar (`src/efun/NetEfuns.cpp`)
- `json_parse`/`json_serialize` (real LDMud efuns, dialect-gated to
  `"ldmud"` the same way `db_*` already is; real FluffOS has no JSON
  support to diverge from)

Build: CMake 3.20+, C++20, PCRE2, libcrypt, SQLite, OpenSSL. See
`INSTALL.md`. Run: `./build/kjdmud etc/driver.cfg` (or `make build` /
`test` / `run`).

## Parked on purpose

Dead Souls leftover `status` keyword, DGD parity, LLVM JIT, swapout,
hotboot, LSP, efun-count chase, host rewrite, and full generational GC
unless a leak is measured.

## Next

Next work is driver work in `src/` (compiler, VM, object, efun, net, gc,
scheduler, security, persist). Mudlib changes only to prove driver
behavior or keep the minimum boot path alive. External mudlibs under
`temp/` (TMI-2, RiftsMUD, Nightmare, and so on) are optional
compatibility probes to surface driver bugs. They are not a product
goal. Do not grow or "finish" those libraries. Prefer the next clear
driver gap in `src/` over starting another mudlib boot chase.

Landed after the kjdmud foundation baseline: `sscanf` `%i` / `%X` / `%o`.
TMI-2 under `temp/` boots Ready and completes live login: create, room
`look`, `say`, `inventory`, and exit movement (clone_object create args,
MUD sockets, soft connect errno for denied outbound). That probe is
enough for now. Resume FluffOS driver work from known gaps (for
example MUD socket read framing (landed), BINARY socket modes if a real
call site needs them, or the next row in module `instruct.md` files).
`sprintf` multi-column `%=` stays scoped out until a real call site
needs it.

G1-G5 kit is already in `mudlib/` (item/npc/room inheritables, domain
folders, wand verbs, domain graph save).

- [x] G6 first rooms: two linked Chi-Town 'Burbs rooms under
  `mudlib/domains/rifts/` with one scenery item, one takeable item, and
  one NPC.
- [x] G6 three-room loop: `watch_post` linked from `lower_gate`,
  reachable both ways.

`save_object` writes width>1 mappings as `key:v0;v1` and writes nothing
for object, closure, and buffer (restore is 0). `dump_state` now dumps
those extra columns too (`M<count>w<width>:`; width-1 dumps stay
`M<count>:`). Persist dump magic stays `AMLPSTATE1`.

Public site and browser WebSocket client live under `website/` (GitHub
Pages). Old local driver snapshot trees are not kept in this repository;
FluffOS citation sources for development stay under gitignored `temp/`.
`query_ip_port` returns the connection accept port under multi-port
`listen:`.

## Verification note (2026-09-22, updated same day)

Earlier the same day: this machine's own vendored `temp/` was absent,
but network access to clone a fresh copy of real upstream FluffOS for a
one-off verification pass was available and was used for the MSP row
and, independently re-verified rather than trusted from that prior
summary, the ZMP row (`docs/dev/STATUS.md` 2026-09-22 entries for both)
- deleted again after each use, never vendored into this repo at that
point. That same pass corrected three previous entries (GMCP/MSDP/MSSP)
that had shipped on a "public protocol, no driver to cite" assumption
made only because no source was checked at the time, not because none
exists, and `docs/COMPARISON.md` row 2.32's own "not in `src/proto/
instruct.md`'s own protocol set" guess about MSP/ZMP turned out to mean
the same thing - absent from one repo's own planning document, not from
the real driver. `src/proto/instruct.md`'s own five-protocol table is
now fully closed (GMCP/MSDP/MSSP/MXP landed, MTTS confirmed already
driver-complete), and MSP/ZMP have landed beyond that file's original
scope too; the next protocol-shaped gap, if any, would be outside that
file's own original scope the same way MSP/ZMP were.

**Update, same day:** "this machine's own vendored temp/ is absent" is
no longer true. The user's own local machine holds
`/home/thurtea/Documents/backups/amlp/temp/` - the "amlp" predecessor
project's full backup, including the exact `reference/
fluffos-2.9-ds2.08/` tree this repo's citations have always meant and a
second real corpus, `ds3.8.2_extracted/ds3.8.2/fluffos-2.23-ds03/`.
Both copied into this repo's own gitignored `temp/` this session (see
`docs/dev/STATUS.md`'s own dated entry). Prefer citing that restored
`temp/reference/fluffos-2.9-ds2.08/` directly for any real-FluffOS
verification going forward - it is the exact pinned source this
project's own historical citations were made against, not a
current-upstream tree that could in principle have drifted since. A
fresh clone (of FluffOS, LDMud, or anything else not already under
`temp/`) is still the right move for anything genuinely not vendored
there, same as this session already did twice for real LDMud (no
`temp/ldmud/` exists even after this restoration).

A second, separate finding surfaced re-verifying ZMP rather than trusted
from the prior session's own summary - real `GMCP`/`MSDP` each also have
their own real `GMCP_ENABLE`/`MSDP_ENABLE` applies
(`src/vm/internal/applies`) - is now fixed too
(`Server::fireGmcpEnableIfNegotiated()`/`fireMsdpEnableIfNegotiated()`,
same shape as MSP's own `msp_enable()`; `docs/dev/STATUS.md`
2026-09-22). The `sendMssp()`/`sendMsdp()`/`sendGmcp()` IAC-escaping gap
flagged alongside it is fixed too (`docs/dev/STATUS.md` 2026-09-22,
earlier entry). Re-checking `core.spec` for that same row also turned
up a real naming defect in this driver's own already-shipped MSDP send
efun (`send_msdp` instead of the real `send_msdp_variable`), corrected
in the same pass.

## Status record

These two files plus `git log`, build, and test output are authoritative,
not chat. Standing rules: `CURSOR.md`. Efun comparison:
`docs/COMPARISON.md`.
