# Testing seams for connection-handling logic

`Server::handleConnection(Connection&)` is private by design (see its
own declaration in `Server.hpp`): it is the one place per-poll-cycle
connection handling actually happens, and keeping it private stops
anything outside `Server` from calling it out of sequence or against a
`Connection` not actually owned by a live accept loop. That is the right
call for the class's own public API, but it creates a real problem for
regression tests: most of what `handleConnection()` does - parsing
subnegotiation data that already arrived, then firing the matching LPC
apply - has nothing to do with sockets, accept loops, or `Server`
instance state at all. It only needs a `Connection` (which a test can
build directly over one half of a `socketpair()`, no listening socket
required) and, where an LPC apply fires, a `VM&`.

## The pattern

Where a piece of `handleConnection()`'s own logic is self-contained like
that - no `Server` instance state, no dependency on the rest of the
per-cycle work - pull it out as its own `public static` method taking
exactly the inputs it needs (`VM&` and/or `Connection&`, nothing else),
and have `handleConnection()` call it like any other step. This is not
new with the telnet protocol applies: `dispatchLine()` and
`fireNetDeadIfLinkDead()` already did this before any of GMCP/MSDP/MSSP/
MXP/MSP/ZMP existed, and their own header comments already state the
reasoning (`Server.hpp`, right above each declaration) - the protocol
apply methods below just kept following it:

- `Server::dispatchLine(VM&, Connection&, const std::string&)` - one
  line of player input, dispatched exactly the way a real connection's
  worth of input would be.
- `Server::fireNetDeadIfLinkDead(VM&, Connection&)` - the `net_dead()`
  apply, fired when `Connection::pollLines()` already detected the
  socket died.
- `Server::fireMspEnableIfNegotiated(VM&, Connection&)` /
  `fireGmcpEnableIfNegotiated(VM&, Connection&)` /
  `fireMsdpEnableIfNegotiated(VM&, Connection&)` - each protocol's own
  one-shot "negotiation just completed" apply (`msp_enable()`/
  `gmcp_enable()`/`msdp_enable()`).
- `Server::dispatchIncomingZmp(VM&, Connection&)` - drains every queued
  incoming ZMP message and fires `zmp_command()` once per message.

Every one of these is a thin wrapper around the same three steps: check
whether there is anything to do (a one-shot flag, or a non-empty queue
drained via the matching `Connection::takeX()` method), fetch the bound
object, and fire the apply inside an `OutputContext::set(&conn)` /
`OutputContext::set(nullptr)` pair with the call itself wrapped in a
`try`/`catch` that logs and isolates a runtime error from the caller
(the same per-apply error isolation `handleConnection()`'s own line
dispatch already uses). None of that needs `Server`'s own instance state
(`connections_`, the listener table, `bootTime_`, ...) - only the two
parameters already say what is needed.

## Why not test `handleConnection()` itself instead

It would work, but at a real cost this repo has not been willing to pay:
a full `Server` needs a `Config`, `VM`, `ObjectManager`, and `Scheduler`
all wired together correctly, and testing one specific piece of
`handleConnection()`'s own behavior (say, whether `msp_enable()` fires
exactly once) would still be entangled with everything else that
function does on the same call - line dispatch, every other protocol's
own incoming loop, `net_dead()` firing, and so on. A static method taking
only what it actually touches tests that one thing in isolation, which
is what most of the regression tests in `test/test_net.cpp` actually
want. Nothing stops a future test from building a full `Server` when a
test genuinely needs the whole accept-loop behavior (`Server`'s own
constructor and `pollOnce()` are both public); it's a matter of using
the narrower seam when it already exists and is sufficient, not banning
the wider one.

## For the next protocol row

If a new protocol needs a one-shot "negotiation completed" apply, name
the method `fire<Protocol>EnableIfNegotiated(VM&, Connection&)`,
matching `fireMspEnableIfNegotiated()`/`fireGmcpEnableIfNegotiated()`/
`fireMsdpEnableIfNegotiated()` exactly. If it needs to drain a queue of
incoming messages and fire one apply per message, name it
`dispatchIncoming<Protocol>(VM&, Connection&)`, matching
`dispatchIncomingZmp()`. Check the real driver source for whether the
apply is genuinely one-shot-per-negotiation (like MSP/GMCP/MSDP's own
`*_ENABLE` applies) or fires on every message (like ZMP's own
`zmp_command`, or GMCP's/MSDP's own already-existing incoming-data
applies) before picking which shape to copy - do not assume one from the
other. Either way, keep the method itself free of `Server` instance
state, so a test can call it directly over a `socketpair()`-backed
`Connection` and a `VM`, no live accept loop required - see
`test/test_net.cpp`'s own `testMspEnableFiresMudlibApplyOnce()` and
`testZmpCommandApplyDispatchReceivesCommandAndArgsArray()` for the
concrete shape a matching regression test for a new one should follow.
