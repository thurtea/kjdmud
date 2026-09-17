#pragma once
#include <memory>
#include <string>
#include "amlp/vm/Value.hpp"

namespace amlp {

class LpcObject;

// Real socket_err.h/socket_errors.h (fluffos-2.9-ds2.08), confirmed
// directly rather than assumed: EESUCCESS is 1, not 0 -- every other
// code is a small negative int, and socket_error(int)'s own real
// formula ("error = -(error + 1); return error_strings[error];")
// only makes sense against these exact values. error_strings itself
// (socket_err.c) is mirrored in SocketRegistry::errorString().
namespace SocketErr {
constexpr int Success       = 1;
constexpr int ESocket       = -1;
constexpr int ESetSockOpt   = -2;
constexpr int ENonBlock     = -3;
constexpr int ENoSocks      = -4;
constexpr int EFdRange      = -5;
constexpr int EBadF         = -6;
constexpr int ESecurity     = -7;
constexpr int EIsBound      = -8;
constexpr int EAddrInUse    = -9;
constexpr int EBind         = -10;
constexpr int EGetSockName  = -11;
constexpr int EModeNotSupp  = -12;
constexpr int ENoAddr       = -13;
constexpr int EIsConn       = -14;
constexpr int EListen       = -15;
constexpr int ENotListn     = -16;
constexpr int EWouldBlock   = -17;
constexpr int EIntr         = -18;
constexpr int EAccept       = -19;
constexpr int EIsListen     = -20;
constexpr int EBadAddr      = -21;
constexpr int EAlready      = -22;
constexpr int EConnRefused  = -23;
constexpr int EConnect      = -24;
constexpr int ENotConn      = -25;
constexpr int ETypeNotSupp  = -26;
constexpr int ESendTo       = -27;
constexpr int ESend         = -28;
constexpr int ECallback     = -29;
constexpr int ESockRlsd     = -30;
constexpr int ESockNotRlsd  = -31;
// Real socket_write()'s own MUD-mode wire-framing error (too many
// nested levels in the LPC value being sent) -- unreachable here since
// MUD mode itself is not implemented (SocketMode's own comment above),
// kept only so socket_error()/errorString() below never falls off the
// real table's own bounds for a code this driver simply never returns.
constexpr int EBadData      = -32;
}  // namespace SocketErr

// Real enum socket_mode (socket_efuns.h): MUD=0, STREAM=1, DATAGRAM=2,
// STREAM_BINARY=3, DATAGRAM_BINARY=4 (order and values confirmed against
// lib/secure/include/network.h's own #define MUD 0 / STREAM 1 / DATAGRAM
// 2, the real LPC-visible constants this mudlib's own daemon/network.c
// and secure/std/client.c actually pass). Only Stream and Datagram are
// implemented here -- MUD mode needs real socket_write()'s own arbitrary-
// LPC-value wire framing (a 4-byte length header around save_svalue()/
// restore_svalue() output), which this driver has no equivalent for
// while save_object()/restore_object() (ROADMAP row 0.7) still writes
// its own custom format rather than the real one; the two BINARY variants
// need a buffer type this driver's own Value has never had (the same
// pre-existing gap already noted on to_int()'s own T_BUFFER case).
// socket_create() rejects all three with SocketErr::EModeNotSupp, exactly
// matching real socket_create()'s own "default: return EEMODENOTSUPP;"
// for a mode outside its switch.
enum class SocketMode { Stream, Datagram };

// Real enum socket_state (socket_efuns.h), minus StateFlushing: real
// FluffOS enters STATE_FLUSHING only when a socket_close() happens while
// a partial write is still draining (S_BLOCKED), deferring the actual
// fd close until the write finishes. Not implemented here -- this
// driver's own socket_close() (SocketRegistry::close()) closes the fd
// immediately regardless of any pending partial write, a real, documented
// simplification (see SocketRegistry::close()'s own comment) rather than
// an oversight.
enum class SocketState { Unbound, Bound, Listen, DataXfer, Closed };

// One LPC efun socket -- real lpc_socket_t (socket_efuns.h) trimmed to
// what this driver actually needs. Deliberately a plain data holder (no
// behavior of its own): the actual socket() family syscalls live in
// SocketRegistry (the synchronous, efun-triggered half, mirroring
// Connection's own self-contained I/O) and the deferred, asynchronous
// half (incoming data, a pending accept, a completed write/connect, a
// peer disconnect) lives in Server::pollSockets() reading these public
// fields directly and firing the matching LPC callback via its own VM&
// -- the identical Connection/Server split this driver already uses for
// window_size (see Connection::windowSizeUpdated_'s own comment): a
// net-layer object never touches the VM directly.
class LpcSocket {
public:
    LpcSocket(int handle, int fd, SocketMode mode, std::shared_ptr<LpcObject> owner)
        : handle(handle), fd(fd), mode(mode), owner(std::move(owner)) {}
    ~LpcSocket();

    LpcSocket(const LpcSocket&) = delete;
    LpcSocket& operator=(const LpcSocket&) = delete;

    int handle;
    int fd;
    SocketMode mode;
    SocketState state = SocketState::Unbound;

    // Real owner_ob (object_t*, current_object at socket_create()/
    // socket_accept() time, reassigned by a completed socket_acquire()
    // below): a weak_ptr, not shared, matching this driver's own
    // established PendingInputTo precedent -- a socket outliving the
    // object that owns it must not itself keep that object alive, and a
    // callback firing after the owner is destructed is silently dropped
    // rather than treated as an error, the same way
    // Server::dispatchLine()'s own PendingInputTo::object.lock() check
    // already works. Real close_referencing_sockets(ob) (force-closing
    // every socket a destructed object still owns) is ported --
    // ObjectManager::destructObject()/reloadObject()'s own onDestructed
    // callback, wired to SocketRegistry::closeAllOwnedBy() from
    // EfunTable.cpp's own destruct()/reload_object() registrations, see
    // SocketRegistry's own file-level comment for the full citation.
    std::weak_ptr<LpcObject> owner;

    // string|function forms, exactly like Connection::pendingNotifyFail_
    // already stores notify_fail()'s own string-or-Closure argument --
    // monostate means "no callback registered".
    Value readCallback;
    Value writeCallback;
    Value closeCallback;

    // Real S_RELEASE: true between a successful socket_release() call
    // and the matching socket_acquire() that completes the handoff (or
    // the release being cancelled because no acquire ever happened --
    // see SocketRegistry::beginRelease()/cancelRelease()'s own
    // comments). releaseTarget mirrors real release_ob, the one object
    // socket_acquire() will accept this fd from while released is true
    // -- a weak_ptr for the same "never keeps the target object alive"
    // reason owner above already is one.
    bool released = false;
    std::weak_ptr<LpcObject> releaseTarget;

    // Real S_BLOCKED: true while a partial socket_write() is still being
    // flushed (pendingWrite holds the undelivered remainder), or while a
    // non-blocking connect() is still in progress. In both cases
    // write_callback(fd) fires (Server::pollSockets(), one arg) the
    // moment the fd next becomes writable -- matching real
    // socket_write_select_handler() exactly, including firing on a
    // completed *connect* the same way it fires on a completed *write*
    // flush (real FluffOS reuses the identical mechanism for both; a
    // dedicated "connect succeeded" callback does not exist).
    bool blocked = false;
    std::string pendingWrite;

    // Real S_WACCEPT: set the instant a LISTEN socket's own
    // read_callback(fd) fires (one argument, "a connection is waiting"),
    // cleared only when socket_accept() is actually called for this fd --
    // prevents Server::pollSockets() from re-firing read_callback every
    // single poll tick for the same still-unaccepted connection.
    bool acceptPending = false;

    // DATAGRAM mode only: the "host port" string real code lower-level
    // recvfrom()/inet_ntoa()+ntohs() would build fresh per packet is
    // instead reconstructed by Server::pollSockets() directly at read
    // time (real code never stores the remote address to the field this
    // one loosely mirrors); remoteAddr/remotePort below are set by
    // SocketRegistry::connect() and socket_accept()'s peer for
    // STREAM/DATA_XFER sockets and read back by socket_status().
    std::string remoteAddr;
    int remotePort = 0;
    std::string localAddr;
    int localPort = 0;
};

}  // namespace amlp
