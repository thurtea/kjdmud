# kjdmud

kjdmud is a from-scratch LPC game driver written in C++20, plus a small
bundled mudlib. It has its own lexer, parser, bytecode compiler, VM,
object system, and call_out/heart_beat scheduler. Players connect over
plain telnet, telnet over TLS, or WebSocket.

It targets the FluffOS dialect of LPC. The `dialect` config switch also
selects LDMud. DGD is comparison only.

The product is the driver. `mudlib/` is a minimum Library that ships so
the driver can boot and be tested. Historical snapshots of earlier names
(AMLP, crysis, aemlpc) live under `references/drivers-snapshots/`.

## Status

Early. No live deployments yet. Progress is tracked in
`docs/dev/ROADMAP.md` and `docs/dev/STATUS.md`.

## Build

See `INSTALL.md` for dependencies and exact commands. In short:

```
cmake -B build -S .
cmake --build build -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
ctest --test-dir build --output-on-failure
./build/kjdmud etc/driver.cfg
```

`make build`, `make test`, and `make run` wrap the same commands.

## Docs

- `INSTALL.md`: build, test, run, connect.
- `docs/dev/ROADMAP.md`: sequenced work and parked items.
- `docs/dev/STATUS.md`: dated development log.
- `docs/COMPARISON.md`: efun and feature comparison.
- `CREDITS.md`: prior-art drivers whose documented behavior shaped this one.
- `CURSOR.md`: standing rules for agents working in this repository.

No license file yet.
# kjdmud
