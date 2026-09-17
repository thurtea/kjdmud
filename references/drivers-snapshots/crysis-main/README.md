# crysis

crysis is a from-scratch LPC game driver written in C++20, plus a small
bundled mudlib ("Library"). It has its own lexer, parser, bytecode
compiler, VM, object system, and call_out/heart_beat scheduler. Players
connect over plain telnet, telnet over TLS, or WebSocket.

It targets the FluffOS dialect of LPC. The `dialect` config switch also
selects LDMud or DGD. DGD is there for comparison only.

The project was formerly named AMLP and was split out of the AetherMUD
project. The move to the `crysis` name is a direction shift, not just a
rename. The direction on record (`docs/dev/ROADMAP.md`, the 2026-09-05
"Direction" note and "Sequenced tracks" section, and `docs/dev/STATUS.md`):
a modern LPC driver plus a small in-game library, centered on the wand
of creation, that can build any world, with a Rifts-era recreation
(Nightmare / AetherMUD lineage, 1996-2001 riftsmud.com) as the first
real target. A fuller statement of the project's identity under the new
name has not been written yet.

## Status

Early. No live deployments yet. The bundled mudlib in `mudlib/` boots and
runs account creation, character creation, four Stonewick rooms, and the
wand of creation (an in-game builder).

The language core is a working driver: lexer, parser, bytecode VM,
objects, telnet IAC, uid/euid, classes, closures, `parse_*`. Tracks A-F
of the 2026-09-05 direction landed on 2026-09-05
(`docs/dev/ROADMAP.md`, "Sequenced tracks"):

| Track | What it is | Status |
|-------|------------|--------|
| A | Multi-port `listen:` table; `save_object` writes FluffOS `.o` text | done |
| B | TLS on marked listen ports (`query_connection_tls`) | done |
| C | WebSocket on its own port (RFC 6455); `wss` on a TLS websocket port | done |
| D | Connection encoding (default `utf-8`); GMCP (`has_gmcp`, `send_gmcp`) | done |
| E | Mudlib builder kit: look/examine, wand `edit`/`room`/`exit`, `/inherit/object`, wizard gating | done |
| F | Net efun sidecar: `src/efun/NetEfuns.cpp`, `test/test_net.cpp` | done |

Next is Track G, the Library world kit (item / room / NPC inheritables,
domain folders, `save_object` persistence of wand-created object graphs),
then Rifts-era content on that kit. Track G is scoped in
`docs/dev/ROADMAP.md`; sub-item G1 (the item inheritable) has landed, the
rest has not.

Parked on purpose: Dead Souls 3.8.2 boot (the leftover `status`
keyword), DGD parity, LLVM JIT, swapout, hotboot, LSP, current-FluffOS
efun-count parity, a host-language rewrite, and a full generational GC.

922 tests pass (verified 2026-09-06). `docs/COMPARISON.md` compares the
efun set against FluffOS 2.9, LDMud, and DGD.

Progress is tracked per row in `docs/dev/ROADMAP.md`; dated development
notes are in `docs/dev/STATUS.md`. Those two files are the authoritative
status record, not chat history or session summaries.

## Build and run

See `INSTALL.md` for dependencies, the exact build/test commands, and how
to run and connect to the driver. In short, from the repo root:

```
cmake -B build -S .
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
./build/amlp etc/driver.cfg
```

`make build`, `make test`, and `make run` wrap the same commands.

## Docs

- `INSTALL.md`: build, test, run, connect.
- `docs/dev/ROADMAP.md`: sequenced tracks, phase and row tracker with
  per-row citations, and the Library mudlib constraints.
- `docs/dev/STATUS.md`: dated development log.
- `docs/COMPARISON.md`: feature and efun-count comparison against FluffOS,
  LDMud, and DGD.
- `CREDITS.md`: prior-art drivers whose documented behavior shaped this
  one. crysis (as AMLP) was split out of the AetherMUD project.
- `src/<module>/instruct.md`: per-subsystem notes and backlog. These
  frame tasks as open regardless of completion, so they are not a live
  status signal.

No license file yet.
