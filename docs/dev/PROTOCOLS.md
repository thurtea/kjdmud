# Mudlib-facing protocol usage notes

Out-of-band telnet protocol efuns and applies live in `src/net/Connection`
(wire framing/negotiation), `src/proto/*Handler` (thin send-side wrappers
where a protocol has a real receiving side), and `src/efun/NetEfuns.cpp`
(the mudlib-facing efun surface). This file covers the mudlib side: what
to actually write in LPC to use each protocol. Driver-side implementation
detail and FluffOS source citations live in `docs/dev/STATUS.md`'s own
dated entries; this file only restates what a mudlib author needs.

## ZMP

Zenith MUD Protocol, telnet option 93. See `docs/dev/STATUS.md`'s own
2026-09-22 ZMP entry for the full driver-side citation trail.

### Enabling ZMP in a client

Nothing mudlib-side is required to offer ZMP: the driver proactively
sends `IAC WILL ZMP` to every telnet connection on accept
(`Server::onNewConnection()`), the same way it already does for
GMCP/MSDP/MSSP/MXP/MSP. A ZMP-aware client (zMUD, CMUD, and others that
implement the protocol) replies `IAC DO ZMP` on its own; nothing in the
mudlib has to request it. `has_zmp(void|object)` reports whether a given
connection (or the current one) completed that negotiation.

### Receiving a ZMP command: `zmp_command()`

**Real signature** (`src/vm/internal/applies`: `ZMP:zmp_command`,
confirmed against current upstream FluffOS source, `src/net/telnet.cc`'s
own `on_telnet_do_zmp()`):

```lpc
void zmp_command(string cmd, mixed *args)
```

Define this function on any object that is currently the bound object of
an interactive connection (typically the player's own body/user object).
The driver calls it once per incoming ZMP message: `cmd` is the ZMP
command name (e.g. `"zmp.ping"`), `args` is an array of the message's
remaining fields, always strings in real FluffOS. There is no
`command_giver` argument passed in - the function runs *on* the
interactive object itself, the same way `terminal_type()`/`window_size()`
already do; read `this_object()` or `this_player()` for the connection's
own identity if needed, do not expect a first positional argument for it.

A minimal example handler:

```lpc
// A trivial ZMP command dispatcher. Extend the switch as real ZMP
// commands are needed; unrecognized commands are silently ignored,
// matching how most real MUD clients handle an unrecognized command
// list too.
void zmp_command(string cmd, mixed *args) {
    switch (cmd) {
        case "zmp.ping":
            // Echo back a pong with the same arguments, demonstrating
            // the outbound side below.
            send_zmp("zmp.pong", args);
            break;
        case "zmp.identify":
            write("Client identified itself via ZMP: " +
                  implode(args, " ") + "\n");
            break;
        default:
            // Unknown command: do nothing. A real handler might log
            // this at debug level.
            break;
    }
}
```

### Sending a ZMP command: `send_zmp()`

**Real signature** (`core.spec`): `void send_zmp(string, string *)`.
Reads `command_giver`, not `current_object` (unlike `telnet_msp_oob()`,
which reads `current_object` - the two protocols' real efuns differ on
this, confirmed directly rather than assumed from one implying the
other). Call it from code running in response to that player's own
input (a command handler, for example), not from an arbitrary object
acting on a different player's connection:

```lpc
send_zmp("zmp.pong", ({ "arg1", "arg2" }));
```

Any non-string element in the array argument is silently dropped before
it reaches the wire (matches real `f_send_zmp()`'s own
`item.type == T_STRING` filter) - it is not an error to pass one, it is
just not sent.

### Framing notes relevant to mudlib code

None of this needs to be handled by mudlib code - it is entirely the
driver's job - but it explains what shows up if something goes wrong:

- Every field (the command name and each argument) is NUL-terminated on
  the wire, and any literal `0xFF` (IAC) byte inside a field is doubled
  before being sent, so a string containing either a NUL byte or `0xFF`
  as ordinary data cannot currently round-trip through `send_zmp()` -
  a NUL byte in an LPC string is either impossible depending on how it
  was constructed, or would itself be read by a real client as an early
  field terminator; this is a real-protocol limitation, not a driver bug
  to work around.
- If a client ever sends a malformed message (a subnegotiation that does
  not end in a NUL byte), the driver silently discards it: `zmp_command()`
  is not called at all for that message, and nothing crashes. There is
  no way for mudlib code to observe a malformed message was even sent.
