# kjdmud

kjdmud is a virtual machine, a network server, and a compiler for the LPC
language. It is a from-scratch game driver written in C++20: lexer, parser,
bytecode compiler, object system, and call_out / heart_beat scheduler.

Players connect over plain telnet, telnet over TLS, or WebSocket. The
primary dialect target is FluffOS-style LPC. The `dialect` config switch
can also select LDMud. A small bundled mudlib ships so the driver can boot
and be tested.

Homepage: https://thurtea.github.io/kjdmud/
Web client: https://thurtea.github.io/kjdmud/client.html
Source: https://github.com/thurtea/kjdmud

## Features

- C++20 host runtime; LPC is mudlib-only
- Multi-port listen table (telnet and WebSocket), optional TLS
- Connection encoding (default UTF-8) and GMCP
- FluffOS-style `save_object` write path
- SQLite database efuns
- Regression suite via CMake / CTest

## Layout

```
README.md     this file
INSTALL.md    build, test, run, connect
CREDITS.md    prior-art acknowledgements
CMakeLists.txt / Makefile
src/          driver source
include/kjdmud/  public headers
mudlib/       minimum Library for boot and tests
etc/          driver configs and cert helper
test/         C++ regression tests
testlib/      LPC-side test support
website/      public site and WebSocket client
docs/         COMPARISON.md and docs/dev/
```

## Build

See `INSTALL.md` for dependencies. Short form:

```
cmake -B build -S .
cmake --build build -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
ctest --test-dir build --output-on-failure
./build/kjdmud etc/driver.cfg
```

`make build`, `make test`, and `make run` wrap the same commands.

## Docs

- `INSTALL.md`: build, test, run, connect
- `docs/COMPARISON.md`: efun and feature comparison
- `docs/dev/ROADMAP.md`: sequenced work and parked items
- `docs/dev/STATUS.md`: dated development log
- `docs/dev/PROTOCOLS.md`: mudlib-facing usage notes for out-of-band
  telnet protocol efuns/applies (currently ZMP)
- `docs/dev/TESTING_SEAMS.md`: why `Server` exposes small public static
  apply-dispatch methods, and the naming convention new ones should
  follow
- `CREDITS.md`: prior-art drivers
- `website/`: overview, download, documentation, web client

No license file yet.
