# src/config/ - Driver Configuration

## What lives here

| File | Role |
|------|------|
| `Config.cpp` + `include/.../Config.hpp` | Parses `driver.cfg` (key = value format). Exposes typed accessors. |

## Files to read before touching this directory

- `include/amlp/config/Config.hpp` - current key set
- `etc/driver.cfg` - the live config file

## Current config keys

| Key | Default | Purpose |
|-----|---------|---------|
| `mudlib` | `./mudlib_stub` | Root path for `.c` file loading |
| `master_file` | `/master` | Master object path |
| `port` | `1129` | Listening port |
| `heartbeat_interval` | `2000` ms | Heartbeat cycle time |
| `max_eval_cost` | `10000000` | Per-command eval ceiling |
| `include_dir` | `secure/include` | Default `#include` search path |
| `simul_efun_file` | *(empty)* | Simul_efun object path |
| `mud_name` | `AMLP` | Predefined `MUD_NAME` macro |

## Phase 0 tasks

Add the following config keys (add accessor methods to `Config.hpp` and
parse them in `Config.cpp`):

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `save_format` | string | `"custom"` | `"custom"` or `"fluffos"` - controls `save_object` serializer |
| `tls_cert` | string | *(empty)* | Path to TLS certificate PEM |
| `tls_key` | string | *(empty)* | Path to TLS private key PEM |
| `max_connections` | int | `256` | Maximum simultaneous player connections |

## Phase 1 tasks

### 1.1 - `dialect` config key

The single most important new key for Phase 1:

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `dialect` | string | `"fluffos"` | One of `"fluffos"`, `"ldmud"`, `"dgd"` |

Add `LpcDialect dialect()` accessor that maps the string to the `LpcDialect`
enum from `src/dialect/`. `loadFromFile()` sets this after parsing.

Also add:

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `auto_object` | string | `/kernel/auto` | DGD auto-object path (ignored unless `dialect = dgd`) |
| `driver_object` | string | `/kernel/driver` | DGD driver-object path (ignored unless `dialect = dgd`) |

## Phase 2 tasks

Add:

| Key | Type | Default | Purpose |
|-----|------|---------|---------|
| `statedump_file` | string | `./state.dump` | World-level statedump path |
| `statedump_interval` | int | `3600` s | How often to auto-dump state (0 = disabled) |
| `swap_dir` | string | `./swap` | Directory for object swapout pages |
| `lsp_port` | int | `1130` | LSP server port when `--lsp` flag is active |
| `jit_threshold` | int | `100` | Number of times a function must be called before JIT compilation |

## Key invariants

- `Config::loadFromFile()` must not throw on unknown keys - log a warning and
  continue. This allows new keys to be added to the binary before updating
  `driver.cfg` files everywhere.
- All accessors must return const references or scalar values - never expose
  the internal `raw_` map.
- Config is read-only after boot - no setter methods. All mutable runtime
  state lives in the appropriate subsystem (VM, Scheduler, etc.).
