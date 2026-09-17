# AMLP

AMLP is an LPC game driver written in C++20. It compiles and runs an LPC
mudlib. Players connect over TCP. It has its own lexer, parser, bytecode
compiler, VM, object system, and call_out/heart_beat scheduler.

It targets the FluffOS dialect of LPC. The `dialect` config switch also
selects LDMud or DGD. DGD is there for comparison only.

## Status

Still early. No live deployments yet. The bundled mudlib in `mudlib/`
boots and runs character creation and basic gameplay.

Progress is tracked per row in `docs/dev/ROADMAP.md`:

| Phase | Focus | Done |
|-------|-------|------|
| 0 | Compiler/VM/object correctness, efun audit | 16 / 16 |
| 1 | FluffOS/LDMud/DGD dialect handling | 10 / 16 |
| 2 | Persistence, coroutine scheduler, package efuns, JIT groundwork | 21 / 49 |
| 3 | GC, uid/security trust, protocol handlers, deployment | 1 / 9 |

802 test checks pass. The efun table has 293 entries. `docs/COMPARISON.md`
compares that against FluffOS 2.9's efun set and explains each gap.

## Build

You need a C++20 compiler, CMake 3.20+, and the libraries listed in
`INSTALL.md` (PCRE2, libcrypt, SQLite).

```
cmake -B build -S .
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Run it:

```
./build/amlp etc/driver.cfg
```

`make` and `make test` do the same thing. `INSTALL.md` has the full setup
and per-distro package names.

## Docs

- `docs/dev/ROADMAP.md`: phase and row tracker with per-row citations.
- `docs/dev/STATUS.md`: dated development log.
- `docs/COMPARISON.md`: feature and efun-count comparison against FluffOS,
  LDMud, and DGD.
- `CREDITS.md`: prior-art drivers whose documented behavior shaped this
  one. AMLP was split out of the AetherMUD project.
- `src/<module>/instruct.md`: per-subsystem notes and backlog.

No license file yet.
