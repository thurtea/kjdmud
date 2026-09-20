# Linux Install

Build, test, run, and connect to the driver on Linux, from a fresh clone.
No other context needed. For other platforms, see INSTALL.md.

## 1. Dependencies

From the real `CMakeLists.txt` files under `src/*/`:

- A C++20 compiler (recent GCC or Clang).
- CMake 3.20 or newer. `ctest` ships inside the `cmake` package.
- `pkg-config`.
- `libpcre2-8` dev headers (the `pcre_*` LPC efuns).
- `sqlite3` dev headers (the `db_*` LPC efuns).
- `libssl` and `libcrypto` dev headers (TLS listen ports).
- `libcrypt` dev files (`crypt()` password hashing).

Fedora / RHEL family:

```
sudo dnf install gcc-c++ cmake pkgconf-pkg-config pcre2-devel libxcrypt-devel sqlite-devel openssl-devel
```

Debian / Ubuntu:

```
sudo apt install g++ cmake pkg-config libpcre2-dev libcrypt-dev libsqlite3-dev libssl-dev
```

Arch:

```
sudo pacman -S gcc cmake pkgconf pcre2 sqlite openssl libxcrypt
```

## 2. Build and test

From the repo root:

```
cmake -B build -S .
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

`make build`, `make test`, and `make run` are thin wrappers around the
same commands. A full clean rebuild is `make clean && make build`, or
`rm -rf build` then the three commands above.

Expected: the suite reports `all tests passed`, 2/2 ctest targets green.

## 3. Run the driver

```
./build/kjdmud etc/driver.cfg
```

Run it from the repo root. `etc/driver.cfg` uses paths relative to the
current working directory (`mudlib_root: mudlib`, `include_dir: include`,
the cert paths), so any other working directory boots the wrong tree.
`make run` builds first, then does the same thing.

On boot you should see:

```
[net] listening on port 1122 (telnet)
[net] listening on port 1124 (websocket)
[net] skipping port 1126 (tls requested, tls_cert/tls_key missing or unloadable)
[net] skipping port 1128 (tls requested, tls_cert/tls_key missing or unloadable)
```

The two skip lines are correct until you generate dev certs (step 5).

`kjdmud <config-path> [max-iterations]`: the config path is required.
`max-iterations` (or `KJDMUD_MAX_ITERATIONS`) bounds scheduler poll
iterations before the process exits on its own, for scripted runs; the
default, 0, means run until `SIGINT` / `SIGTERM`.

### Config keys

`etc/driver.cfg` is `key: value` lines, `#` comments allowed. Keys:
`mud_name`, `mudlib_root`, `master_file`, `simul_efun_file`,
`include_dir`, `global_include_file`, `heartbeat_interval_ms`,
`max_eval_cost`, `max_connections`, `test_results_path`, `dialect`,
`port`, and repeatable `listen` / `tls_cert` / `tls_key`. Nothing is
compiled in; changing a port or pointing at a mudlib elsewhere is a
config edit, no rebuild. Absolute paths in the config are used as-is, so
a config with absolute `mudlib_root` / `include_dir` can be booted from
any directory.

## 4. Connect

Listen ports (`etc/driver.cfg`, even numbers from 1122):

| Port | Kind | Client |
|------|------|--------|
| 1122 | telnet | Mudlet, tintin, `nc`, a real telnet client |
| 1124 | websocket | `website/client.html` (`ws://`) |
| 1126 | telnet + TLS | Mudlet with SSL, after step 5 |
| 1128 | websocket + TLS | browser `wss://`, after step 5 |

The driver does not serve HTTP. Port 1124 only accepts a WebSocket
upgrade.

### Telnet (1122)

Most Linux distributions no longer ship a `telnet` binary by default.
A real telnet client (`telnet`, or a package like `inetutils-telnet` /
`telnet-bsd`) correctly consumes the driver's IAC negotiation (TTYPE,
NAWS, GMCP, MSSP). Plain `nc 127.0.0.1 1122` also connects, but shows
that negotiation as a few stray bytes at the very start of the session;
harmless, but expect that noise before the login prompt if you use it.
A Mudlet profile works too: host `127.0.0.1`, port `1122`, telnet, SSL
off.

First connection: pick an account name (no `/`), a password of at least
5 characters, then a character name. You land in the gatehouse holding
a wand of creation. `help` lists the live commands (including
`inventory`/`inv`; any command that does not exist now gives a clear
"Unknown command" message instead of no response at all).

### Browser (1124)

Leave the driver running and open `website/client.html` in a browser.
It defaults to `ws://127.0.0.1:1124`. If `file://` is awkward, serve the
directory with any static server, for example:

```
python3 -m http.server 8080 --directory website
```

then open `http://127.0.0.1:8080/client.html`.

### TLS (1126, 1128)

```
./etc/make-dev-certs.sh
```

writes gitignored self-signed `etc/dev-cert.pem` / `etc/dev-key.pem`.
Restart the driver; 1126 and 1128 now bind. Mudlet: port `1126`, SSL on,
accept the self-signed warning. Browser: `wss://127.0.0.1:1128` (a
self-signed `wss` is fussy in browsers; 1124 is the easy local path).
These certs are for local testing only.

## 5. Try it in-game

Logged in and holding the wand:

```
look
examine wand
help
inventory
north
create a small gizmo
look gizmo
edit gizmo a small carved token, warm to the touch.
look gizmo
room hall
exit north /data/created/hall
north
```

`clone` / `purge` / `create` / `npc` / `edit` / `room` / `exit` /
`domain` / `save` need `wizardp`, which a fresh login gets from
`user.c` `setup()`. `eval return 1 + 1;` runs one LPC statement for a
quick driver probe; `eval` errors (a bad statement, or an efun that
throws) show as `Error while processing your command.` at the top
command-dispatch level, and `catch(...)`/`assert_throws((: ... :))`
inside the statement itself can surface the real message instead.

A wizard-set exit or a plain `create`-d item only survives a restart if
it lives inside the active domain's own saved room graph (`domain
<name>`, then `save`): `save_domain()` walks
`mudlib/domains/<name>/rooms/*.c` only, so an exit added on a room
outside that domain (the gatehouse, for example) is not captured by
`save`, and a plain created item is only included in the graph if it
also defines `query_prevent_get()` returning true (fixed scenery, not a
takeable item). Both are real, current limits of the demo mudlib's save
system, not bugs in the connection or build steps above.

## 6. If something fails

- `bind() failed`: another process owns that port.
- Only 1122 binds: you are not launching from the repo root, or not
  using `etc/driver.cfg`.
- Browser socket error on 1124: driver not running, or the client is
  pointed at 1122 (telnet, not WebSocket).
- `pkg_check_modules` cannot find `libssl` / `libpcre2-8` / `sqlite3`:
  the `-devel` / `-dev` package is missing.
- `git status` shows new files under `mudlib/accounts/` or
  `mudlib/characters/` after you play: expected, real per-account save
  data; both paths are gitignored.
