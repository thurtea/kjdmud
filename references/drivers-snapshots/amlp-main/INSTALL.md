# INSTALL

Real, tested instructions for standing up a fresh copy of this driver.
Written and verified on Fedora Linux 44 (gcc 16.1.1, cmake 4.3.0), but
nothing here is Fedora-specific beyond the package manager invocation --
any Linux with a C++20 compiler, CMake >= 3.20, and the two libraries
below will build this the same way.

## 1. Prerequisites

Build dependencies, confirmed directly from the real `CMakeLists.txt`
files under `src/*/CMakeLists.txt` (not assumed):

- A C++20 compiler. `CMakeLists.txt` sets
  `CMAKE_CXX_STANDARD 20`/`CMAKE_CXX_STANDARD_REQUIRED ON` at the top
  level; any reasonably recent GCC or Clang works.
- CMake >= 3.20 (`cmake_minimum_required(VERSION 3.20)`). `ctest` is
  not a separate package: it ships inside the `cmake` package itself.
- `pkg-config` (or Fedora's `pkgconf-pkg-config`, which provides the
  same `pkg-config` command): `src/efun/CMakeLists.txt` calls
  `find_package(PkgConfig REQUIRED)`.
- `libpcre2-8` development headers: the same file uses
  `pkg_check_modules(PCRE2 REQUIRED libpcre2-8)`, and links
  `${PCRE2_LIBRARIES}` into the `efun` library (this driver's own
  `pcre_*`-family LPC efuns).
- `libcrypt` development files (the `crypt()` password-hashing
  function): `src/efun/CMakeLists.txt` links a plain `crypt` target
  (`-lcrypt`) into the `efun` library.
- `sqlite3` development headers (ROADMAP.md row 2.15's real db_* efun
  family, 2026-08-21): the same file also uses
  `pkg_check_modules(SQLITE3 REQUIRED sqlite3)`, and links
  `${SQLITE3_LIBRARIES}` into the `efun` library.

On Fedora:

```
sudo dnf install gcc-c++ cmake pkgconf-pkg-config pcre2-devel libxcrypt-devel sqlite-devel
```

(`libxcrypt-devel` is what actually provides `libcrypt.so`/`crypt.h` on
Fedora; `pcre2-devel` provides the `libpcre2-8` pkg-config file
`pkg_check_modules` looks for; `sqlite-devel` provides the `sqlite3`
pkg-config file.)

On a Debian/Ubuntu-family system the equivalent packages are:

```
sudo apt install g++ cmake pkg-config libpcre2-dev libcrypt-dev libsqlite3-dev
```

No other external libraries are linked anywhere in the tree (checked
every `src/*/CMakeLists.txt` and `test/CMakeLists.txt` directly --
`core`, `config`, `compiler`, `vm`, `object`, `apply`, `dialect`,
`net`, and `scheduler` all only depend on each other and the standard
library).

## 2. Build

Exactly `README.md`'s own three commands: this file does not invent
any new steps, and `Makefile`'s own `build`/`test` targets are themselves
just a thin wrapper around these same three commands (see `Makefile`'s
own header comment):

```
cmake -B build -S .
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

(`-j4` is README.md's own example; any `-jN`, or `-j$(nproc)`, works --
`Makefile`'s own `build` target defaults `JOBS` to `nproc`.)

`ctest` needs no working-directory care: `test/CMakeLists.txt` bakes
the absolute repo root in at configure time
(`AMLP_SOURCE_DIR="${CMAKE_SOURCE_DIR}"`, consumed by the test suite's
own `readMudlibFile()` helper for tests that read real files under
`mudlib/`), specifically so the same test binary gives the same result
whether it's run directly, via `ctest --test-dir build`, or from
`build/test/`: confirmed by that file's own comment, not assumed.

Equivalently, from the repo root:

```
make build
make test
```

A full clean rebuild (not just recompiling changed objects) is
`make clean && make build`, or plain `rm -rf build` followed by the
three commands above: `Makefile`'s own `clean` target removes the
whole `build/` directory, including the CMake cache, not just compiled
objects.

At the time of writing this builds and passes 670 tests
(`ctest`/`amlp_tests`), zero failures.

## 3. Running the driver

```
./build/amlp etc/driver.cfg
```

boots the driver's own bundled mudlib under `mudlib/` (`README.md`'s
own fourth command; `make run` does the same thing after a build).

### Is anything hardcoded, or does the config file already handle this?

**The config file already handles it. Nothing needs editing or
renaming before a build, on this machine or any other, as long as the
driver is invoked from the repo root** (or `etc/driver.cfg` is copied
somewhere else with its relative paths rewritten to absolute ones --
see below). Confirmed directly from `Config.hpp`/`Config.cpp`
(`src/config/`), not assumed:

- `Config::loadFromFile()` parses a plain `key: value` line format,
  `#`-comments allowed, into a small fixed set of named keys: no
  compiled-in path or port anywhere in the driver itself.
- The real keys, confirmed against `Config.cpp`'s own parser (every one
  of them, not a subset): `mud_name`, `mudlib_root`, `master_file`,
  `port`, `heartbeat_interval_ms`, `max_eval_cost`, `include_dir`,
  `simul_efun_file`, `global_include_file`, `dialect`.
- `etc/driver.cfg`, the one config file in this repo, sets:

  ```
  mud_name: Library
  mudlib_root: mudlib
  master_file: /single/master
  simul_efun_file: /single/simul_efun
  include_dir: include
  global_include_file: <config.h>
  port: 1122
  heartbeat_interval_ms: 2000
  max_eval_cost: 10000000
  ```

- `mudlib_root`/`include_dir` are relative paths here, and relative
  paths in a config file resolve against the **current working
  directory the driver process is launched from**, not against the
  config file's own location, and not against any path baked in at
  build time. Confirmed directly from `main.cpp`'s own comment on this
  exact point (`Config::loadFromFile()` is handed whatever path is
  passed on the command line, with no fallback default, specifically
  because defaulting to a fixed path "would still silently assume the
  process runs from the repo root, an assumption nothing else here
  makes"). `include_dir`'s own entries are additionally resolved
  against `mudlib_root` itself when they don't already start with `/`
  (`ObjectManager.cpp`'s `splitIncludeDirs()`), which is why
  `include_dir: include` here correctly finds
  `mudlib/include/config.h` (the file `global_include_file: <config.h>`
  auto-`#include`s into every compiled object), not this repo's own
  top-level `include/` (that one holds the C++ driver's own headers,
  `include/amlp/...`, unrelated to the mudlib).
- Net effect: `./build/amlp etc/driver.cfg` only works correctly when
  run **from the repo root** (so `mudlib` and `include` resolve where
  they're supposed to). This matches every invocation documented in
  `README.md` and every prior live-verification session recorded in
  `STATUS.md`: none of them ever `cd`s anywhere else first.

### Pointing a fresh checkout somewhere else

Two supported ways, neither of which touches any source file:

1. **Run from that checkout's own repo root.** Since every path in
   `etc/driver.cfg` is relative, a `git clone` to any new location just
   works unmodified as long as you `cd` into it first:

   ```
   cd /wherever/you/cloned/amlp
   ./build/amlp etc/driver.cfg
   ```

2. **Point at a mudlib living somewhere else entirely**, without
   touching the checkout at all: copy `etc/driver.cfg` to a new file
   and rewrite `mudlib_root`/`include_dir` as absolute paths (leaving
   `master_file`/`simul_efun_file` as mudlib-root-relative LPC object
   paths, which they always are, and always start with `/`):

   ```
   mud_name: Library
   mudlib_root: /srv/library-mudlib
   master_file: /single/master
   simul_efun_file: /single/simul_efun
   include_dir: /srv/library-mudlib/include
   global_include_file: <config.h>
   port: 4000
   heartbeat_interval_ms: 2000
   max_eval_cost: 10000000
   ```

   then boot with `./build/amlp /path/to/that.cfg` from anywhere --
   absolute paths in the config are used exactly as given, with no
   further resolution against the working directory.

Changing the port for a given deployment is the same one-line edit
either way: change the `port:` value and reboot. `heartbeat_interval_ms`
(the `call_out()`/`heart_beat()` scheduler tick) and `max_eval_cost`
(this driver's own per-call VM instruction ceiling) are independent
knobs, also plain integers, also safe to change without touching any
source.

### Command-line arguments

`amlp`'s own real usage, confirmed from `main.cpp`:

```
./build/amlp <config-path> [max-iterations]
```

`<config-path>` is required (no fallback default, `main.cpp` exits
with a usage message rather than guessing one). `[max-iterations]`, or
the `AMLP_MAX_ITERATIONS` environment variable if the argument is
omitted, is real "test mode": it bounds how many scheduler poll
iterations the process runs before exiting on its own
(`main.cpp`'s own "(test mode: will exit after N poll iterations)"
message, `scheduler.run(server, maxIterations)`), useful for
scripted/test invocations, not needed for an ordinary long-running
deployment (0, the default either way, means "run forever until
`SIGINT`/`SIGTERM`").
