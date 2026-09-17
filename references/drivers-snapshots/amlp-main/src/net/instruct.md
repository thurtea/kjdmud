# src/net/ - TCP Server, Connection, Telnet, Sockets

## What lives here

| File | Role |
|------|------|
| `Server.cpp` + `include/.../Server.hpp` | Listening socket, accept loop, per-connection dispatch, `dispatchLine()`, `fireNetDeadIfLinkDead()`. |
| `Connection.cpp` + `include/.../Connection.hpp` | One player TCP connection: buffered reads, `input_to` state, `pendingNotifyFail_`, echo mode. |
| `InteractiveRegistry.cpp` + `include/.../InteractiveRegistry.hpp` | Global map of live connections (for `users()`, `find_player()`, etc.). |
| `OutputContext.cpp` + `include/.../OutputContext.hpp` | Thread-local current-output-connection pointer for `write()`/`printf()`. |
| `SnoopRelay.cpp` + `include/.../SnoopRelay.hpp` | `deliverToConnection(VM&, Connection*, string)`: the snoop-output-duplication chokepoint every text-outputting efun (`write`/`receive`/`printf`/`message`/`say`) and `Server::dispatchLine()`'s own `notify_fail()` dispatch route through instead of calling `Connection::send()` directly; fires `receive_snoop(string)` on `conn->boundObject()->snoopedBy()` if set. See `EfunTable.cpp`'s `snoop`/`query_snoop`/`query_snooping` registration for the full real-semantics writeup (Phase 0.13). |

## Files to read before touching this directory

- `include/amlp/net/Server.hpp`
- `include/amlp/net/Connection.hpp`
- Reference: `fluffos-2.9-ds2.08/comm.c` - the reference networking layer
- Reference: `fluffos-2.9-ds2.08/comm.h` - `interactive_t` struct fields

## Phase 0 tasks

### 0.8 - Full telnet IAC negotiation + echo suppression + NAWS

Currently the driver accepts plain TCP. Real MUD clients send telnet option
negotiations that appear as garbage if not handled.

**What to build:**

1. **IAC parser in `Connection::pollLines()`:**
   - Detect `\xFF` (IAC) bytes in the raw read buffer.
   - Handle the three-byte sequences: IAC WILL/WONT/DO/DONT \<option\>.
   - Respond appropriately for the options we care about (see below).
   - Strip IAC sequences from the data before splitting into lines.

2. **Echo suppression (IAC WILL ECHO / IAC WONT ECHO):**
   - When `input_to()` is called with the `I_NOECHO` flag (flag value 1),
     send `IAC WILL ECHO` to suppress client-side echo (for password input).
   - When the `input_to` completes, send `IAC WONT ECHO` to re-enable.
   - `Connection` already has `echoSuppressed_` state - wire it up.

3. **NAWS (Negotiate About Window Size - option 31):**
   - When client sends `IAC DO NAWS`, respond `IAC WILL NAWS`.
   - Parse the `IAC SB NAWS \<w1\>\<w2\>\<h1\>\<h2\> IAC SE` subnegotiation.
   - Store `terminalWidth_` and `terminalHeight_` on `Connection`.
   - Expose via a new `query_screen_width()`/`query_screen_height()` efun.

4. **`terminal_colour` mapping (basic):**
   - Parse `%^RED%^` / `%^RESET%^` colour codes that many FluffOS mudlibs use.
   - Replace with ANSI escape sequences or strip depending on client capability.

**Reference:** RFC 854 (Telnet), RFC 857 (Echo option), RFC 1073 (NAWS).
`fluffos-2.9-ds2.08/comm.c`'s `telnet_neg()` function.

### 0.10 - `socket_*` efun family (basic)

These live in `src/efun/EfunTable.cpp` but require new `Connection`-like
infrastructure for non-player sockets.

**What to build in `src/net/`:**
1. `LpcSocket` class: wraps a non-blocking file descriptor; tracks state
   (created/bound/listening/connected/closed); has owner `weak_ptr<LpcObject>`.
2. `SocketRegistry` singleton: maps int handle → `shared_ptr<LpcSocket>`.
3. `Server::pollOnce()` must also poll registered `LpcSocket` fds for
   read/write readiness and fire the appropriate LPC callback
   (`socket_read_callback`, `socket_write_callback`).

Efuns to register (in `src/efun`):
- `socket_create(int type, string callback)` → int handle
- `socket_bind(int handle, int port)` → int status
- `socket_connect(int handle, string addr, int port, string read_cb)` → int
- `socket_write(int handle, mixed data)` → int
- `socket_close(int handle)` → int
- `socket_error(int handle)` → string
- `socket_status()` → array of status info

**Reference:** `fluffos-2.9-ds2.08/packages/socket/socket.c`.

## Phase 2 tasks

### 2.13 - TLS support

Add OpenSSL-backed TLS wrapping over the existing plain TCP:

1. Add `bool useTls_` and `SSL*` to `Connection`.
2. New config key `tls_cert` / `tls_key` pointing to PEM files.
3. `Server::onNewConnection()`: if TLS is configured, perform `SSL_accept()`
   on the fd before handing it to `Connection`.
4. `Connection::pollLines()`: use `SSL_read()` / `SSL_write()` when TLS is
   active; fall back to plain `read()`/`write()` otherwise.
5. Expose `query_connection_tls(ob)` efun returning 1 if the connection is TLS.

### 2.14 - WebSocket framing on top of TLS

1. Add `bool useWebSocket_` to `Connection`.
2. Detect the HTTP `GET ... Upgrade: websocket` handshake in the first bytes
   from a new connection; perform the SHA-1 / Base64 key exchange.
3. After the handshake, all reads/writes go through WebSocket frame
   encode/decode (RFC 6455).
4. The rest of the connection API is unchanged - line-based input/output works
   the same way, just over WebSocket frames instead of raw TCP.

## Phase 3 tasks

### 3.4 - GMCP, MSDP, MSSP, MTTS, MXP

These are telnet subnegotiation-based out-of-band protocols. They share the
IAC parser from Phase 0.8 but each has its own option codes and data format.
See `src/proto/instruct.md` for the detailed implementation plan - the
`src/net` work is only the transport layer (IAC parser, subnegotiation buffer).
`src/proto` owns the protocol-specific encode/decode and the efun interface.

## Testing

- Use `socketpair(AF_UNIX, SOCK_STREAM, 0)` in tests to create connected fd
  pairs without a real listening socket.
- `Server::dispatchLine()` and `Server::fireNetDeadIfLinkDead()` are already
  static and testable without a live socket - keep them that way.
- Add `test/test_net.cpp` covering:
  - IAC sequence stripping
  - Echo suppression flag round-trip
  - NAWS subneg parsing
  - WebSocket handshake (Phase 2)

## Key invariants

- `Server::dispatchLine()` and `fireNetDeadIfLinkDead()` must remain static
  and free of `Server` instance state - they are the main unit-test seam.
- IAC bytes must never appear in the string delivered to `dispatchLine()`.
- `OutputContext::current()` must always be set before any efun that writes
  output is called - it is the single source of truth for "which connection
  should receive write() output right now".
- Socket handles returned by `socket_create` are globally unique integers
  (never reused within a session). Use a monotonic counter in `SocketRegistry`.

## Known gap, fixed 2026-08-18 (continued)

The driver process used to have no `SIGPIPE` handling at all: confirmed
directly, zero hits for `SIGPIPE`/`MSG_NOSIGNAL`/`SO_NOSIGPIPE` anywhere in
`src/`/`include/`; `main.cpp` registered handlers only for `SIGINT`/
`SIGTERM`. A client connection closing at the wrong moment relative to the
driver's own next `write()`/`send()` to that same socket could raise
`SIGPIPE`, whose default disposition kills the *entire process*, not just
that one connection. Reproduced once, live, during a `parse_*` live-
verification session (STATUS.md's own dated entry has the full citation).
Fixed the same day: `main.cpp` now calls `std::signal(SIGPIPE, SIG_IGN)`
alongside the existing `SIGINT`/`SIGTERM` registration, matching this
codebase's own existing `std::signal()` convention rather than introducing
`sigaction()`. `Connection::send()` (`Connection.cpp`) already tolerated a
failed `write()` gracefully on its own (`if (n < 0) { if (errno == EINTR)
continue; break; }`): it was only ever the process-wide signal
disposition that was missing, not anything in the write path itself, so no
other code needed to change. Deterministically regression-tested (`test/
test_lexer.cpp`'s own `testSigpipeIsIgnoredSoAWriteAfterThePeerCloses...`:
closing one end of a connected socketpair and writing to the other is the
textbook, 100%-reproducible way to raise `SIGPIPE`, no live server or
timing race needed) and confirmed live: a real driver process survived 50
forced abrupt (`SO_LINGER`-RST) client disconnects across two separate
rounds while a second, independent client stayed connected and kept
receiving normal responses throughout, both via this project's own
harness-tracked backgrounding, not manual `nohup`/`pkill`.
