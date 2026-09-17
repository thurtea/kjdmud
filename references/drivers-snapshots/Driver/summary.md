# AMLP status for peers (2026-09-05)

AMLP is a C++20 LPC driver plus a small bundled mudlib ("Library").
LPC is the mudlib language. The host runtime stays C++20. We are not
rewriting the driver, not chasing Dead Souls boot, and not chasing
current-FluffOS efun count.

This note is what landed in the 2026-09-05 session and how to try it.
Authoritative trackers remain `docs/dev/ROADMAP.md` and
`docs/dev/STATUS.md`.

## Where the project is

The language core was already a real driver (lexer, parser, bytecode
VM, objects, telnet IAC, uid/euid, `parse_*`, classes, closures). The
gap was the wire and the product around it: one plain TCP port, no
TLS, no WebSocket, no connection encoding, GMCP refused, and a 31-file
mudlib whose builder (the reeve's rod) could only clone/purge/create.

That gap is closed enough to connect in 2026 and build rooms in-game.

| Track | What it is | Status |
|-------|------------|--------|
| A | Multi-port listen table. `save_object` writes FluffOS `.o` text | done |
| B | TLS on marked listen ports (`query_connection_tls`) | done |
| C | WebSocket on its own port (RFC 6455). `wss` when that port is TLS | done |
| D | Connection encoding (default `utf-8`). GMCP (`has_gmcp`, `send_gmcp`) | done |
| E | Mudlib builder kit: look/examine, rod edit/room/exit, `/inherit/object`, wizards | done |
| F | Sidecar: `src/efun/NetEfuns.cpp`, `test/test_net.cpp` | done |

Parked on purpose: Dead Souls leftover `status` keyword, DGD, JIT,
swapout, hotboot, LSP, FFI, Python efuns, WASM, host-language rewrite,
full generational GC.

Next product work is still the mudlib: grow the rod's world kit, then
Rifts content on that kit. Do not merge AetherMUD into `mudlib/`.

Full suite: `all tests passed` on 2026-09-05 (macOS clang, Homebrew
OpenSSL). Linux (Alma/Fedora) remains the closer match to `INSTALL.md`.

## What changed in the driver

- `etc/driver.cfg` can list several `listen:` lines. `port: 1122` is
  still the single-telnet default if no `listen:` lines exist.
- `save_object` writes `#filename` plus `name value` FluffOS text.
  Restore still reads older tab-delimited files.
- TLS: libssl. A port marked `tls` does `SSL_accept` before telnet or
  the WebSocket handshake. Missing certs skip those ports instead of
  refusing to boot.
- WebSocket is a **second port**, not an upgrade on telnet. After the
  HTTP 101, the same line + IAC path runs.
- Encoding efuns: `set_encoding` / `query_encoding`.
- GMCP (telnet option 201): `has_gmcp`, `send_gmcp`, object apply
  `gmcp(string)`. MSDP/MSSP/MXP are not in yet.
- Net efuns live in `src/efun/NetEfuns.cpp`. Wire tests live in
  `test/test_net.cpp`.

## What changed in the mudlib

- Restored `mudlib/LIBRARY_MUDLIB_PLAN.md` and
  `mudlib/WAND_OF_CREATION_SCOPING.md`.
- `/inherit/object.c`: `id` / `short` / `long` / `move`.
- `/command/look.c` and `/command/examine.c`.
- Reeve's rod (`/clone/wand_of_creation.c`): `clone`, `purge`,
  `create`, `edit`, `room`, `exit`. Wizard-gated (`wizardp`). New
  objects inherit `/inherit/object`.
- `user.c` `enable_wizard()` compiles in (`__NO_WIZARDS__` unset).
- Browser page: `mudlib/www/client.html`.

## Port map (even numbers from 1122)

| Port | Kind | Who uses it |
|------|------|-------------|
| 1122 | telnet | Mudlet, tin, `nc` |
| 1124 | websocket | `mudlib/www/client.html` (`ws://`) |
| 1126 | telnet + TLS | Mudlet SSL (after certs) |
| 1128 | websocket + TLS | browser `wss://` (after certs) |
| 1130+ | reserved | not bound yet |

`port: 1122` remains in config for anything that still reads
`Config::port()` alone (`sys_network_ports()` reports the full table).

---

## How I test this (operator notes)

Run every command below from the **repo root**. Relative paths in
`etc/driver.cfg` (`mudlib`, `etc/dev-cert.pem`) resolve against cwd.

### 1. Build and regression suite

```
cmake -B build -S .
cmake --build build -j4
./build/test/amlp_tests
```

Or `make test`. Expect `all tests passed`.

On macOS, Homebrew OpenSSL is keg-only. Configure with:

```
export PATH="/opt/homebrew/bin:$PATH"
export PKG_CONFIG_PATH="/opt/homebrew/opt/openssl@3/lib/pkgconfig:${PKG_CONFIG_PATH}"
```

Linux packages: see `INSTALL.md` (`openssl-devel` / `libssl-dev`).

### 2. Boot the bundled Library mudlib

```
./build/amlp etc/driver.cfg
```

or `make run`. You should see listen lines for 1122 and 1124. 1126 and
1128 print a skip message until certs exist. That is correct.

### 3. Mudlet on telnet 1122

1. New profile.
2. Host: `127.0.0.1` on this machine, or the VPS public IP remotely.
3. Port: `1122`.
4. Protocol: telnet. Do not tick SSL for this port.
5. Connect.

First visit: create an account (name with no `/`), password at least 5
characters, then a character name. You land in Stonewick's gatehouse
with the reeve's rod handed to you. `help` lists live commands.

GMCP: Mudlet will see option 201 if it asks. There is not yet a rich
GMCP UI pack on this lib.

Remote VPS: open TCP 1122 (and 1124/1126/1128 if you want those) on
the firewall. Same ports, public IP.

### 4. Browser client on websocket 1124

The driver does **not** serve HTML. It only accepts a WebSocket
upgrade on 1124.

Local:

1. Leave the driver running.
2. Open `mudlib/www/client.html` in a browser (double-click, or
   `open mudlib/www/client.html` on macOS).
3. Default URL is `ws://127.0.0.1:1124`. Click connect if it did not
   already.
4. Login the same way as Mudlet.

Optional static server (not the driver), if `file://` is awkward:

```
python3 -m http.server 1130 --directory mudlib/www
```

then visit `http://127.0.0.1:1130/client.html`. 1130 is reserved in
the even-port scheme and is not a driver listen port.

### 5. TLS ports 1126 and 1128

```
chmod +x etc/make-dev-certs.sh
./etc/make-dev-certs.sh
```

Restart the driver. 1126 and 1128 should bind.

- Mudlet: host `127.0.0.1`, port `1126`, enable SSL/TLS. Accept the
  self-signed warning.
- Browser: set the client URL to `wss://127.0.0.1:1128`. Browsers are
  picky about self-signed `wss`. 1124 is the easy local path.

These PEMs are gitignored. Do not use them as production certs.

### 6. What to try in-game

Once logged in and holding the rod (it is given at the gatehouse):

```
look
look rod
examine rod
help
north
clone /clone/wand_of_creation
create gizmo
look gizmo
edit gizmo a carved token
room hall
exit north /data/created/hall
north
```

`clone` / `purge` / `create` / `edit` / `room` / `exit` require
`wizardp`. Fresh logins get that from `user.c` `setup()`.

`eval return 1 + 1;` still works for driver probes.

### 7. Quick smoke if something fails

- Driver prints `bind() failed`: something else owns that port.
- Only 1122 binds: you still have a `port:`-only config, or you are
  not using this `etc/driver.cfg`.
- Browser "socket error" on 1124: driver not running, or you pointed
  at 1122 (telnet, not WebSocket).
- Mudlet garbage characters at connect: that is telnet IAC (TTYPE,
  NAWS, GMCP). A real telnet client (Mudlet) consumes them. `nc` will
  show them as binary.
