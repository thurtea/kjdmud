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
- Connection encoding (default utf-8) and GMCP
- `save_object` FluffOS `.o` write-side
- Net efun sidecar (`src/efun/NetEfuns.cpp`)

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

Identity rename to `kjdmud` is closed (prior names: AMLP, crysis,
aemlpc). Snapshots of those trees live under
`references/drivers-snapshots/`. `save_object` writes width>1 mappings
as `key:v0;v1` and writes nothing for object, closure, and buffer
(restore is 0). `dump_state` now dumps those extra columns too
(`M<count>w<width>:`; width-1 dumps stay `M<count>:`). Persist dump
magic stays `AMLPSTATE1`.

## Status record

These two files plus `git log`, build, and test output are authoritative,
not chat. Standing rules: `CURSOR.md`. Efun comparison:
`docs/COMPARISON.md`.
