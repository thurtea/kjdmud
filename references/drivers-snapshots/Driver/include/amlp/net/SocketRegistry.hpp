#pragma once
#include <memory>
#include <string>
#include <vector>
#include "amlp/vm/Value.hpp"
#include "amlp/net/LpcSocket.hpp"

namespace amlp {

class LpcObject;

// Global table of every live LPC efun socket -- real lpc_socks[] (a
// fixed-size C array in socket_efuns.c) reduced to the same "global map,
// no Server/VM reference needed" shape InteractiveRegistry already uses
// for connections. Owns every LpcSocket's real lifetime (a shared_ptr
// map entry); Server::pollSockets() only ever borrows a raw/shared
// reference to poll and fire callbacks, never outlives a call to
// SocketRegistry::all().
//
// Handle allocation is a monotonic counter, never reused within a
// session -- net/instruct.md's own explicit invariant for this row
// ("Socket handles returned by socket_create are globally unique
// integers (never reused within a session). Use a monotonic counter in
// SocketRegistry.") kept exactly as specified, a deliberate divergence
// from real find_new_socket()'s own array-slot-reuse behavior: nothing
// in this driver's LPC-visible contract depends on handle reuse, and
// reuse would only reintroduce a stale-handle class of bug real FluffOS
// itself has to guard against with owner_ob/state checks on every call.
//
// closeAllOwnedBy() below is real close_referencing_sockets(ob), with
// two real call sites both ported: object.c's own reload_object() and
// simulate.c's own destruct_object() ("if (ob->flags & O_EFUN_SOCKET)
// close_referencing_sockets(ob);", right alongside real destruct's own
// shadow/living-name handling ObjectManager::destructObject() already
// ports). The destruct_object() side was found while implementing the
// reload_object() one and initially left as a documented gap for a
// later session -- now closed: ObjectManager::destructObject()/
// reloadObject() both take an optional onDestructed callback (their own
// header comments), fired once per object either call actually
// destructs (including every link the real shadow-chain cascade
// destructs along with the one explicitly named), which EfunTable.cpp's
// own destruct()/reload_object() registrations both wire to this exact
// method -- `object` itself still cannot depend on `net`, so the actual
// close always happens at that outer layer, never inside ObjectManager
// directly.
class SocketRegistry {
public:
    // int socket_create(int mode, string|function read_callback,
    //                    void|string|function close_callback)
    // Real signature/min-max args confirmed against efun_defs.c's own
    // F_SOCKET_CREATE entry (2-3 args, ret TYPE_NUMBER); mode values are
    // exactly real MUD=0/STREAM=1/DATAGRAM=2/STREAM_BINARY=3/
    // DATAGRAM_BINARY=4 (lib/secure/include/network.h). Returns a new
    // handle (>= 0) on success, or a negative SocketErr code.
    static int create(int mode, Value readCallback, Value closeCallback,
                       const std::shared_ptr<LpcObject>& owner);

    // int socket_bind(int fd, int port, void|string addr)
    // addr omitted (hasAddr false) binds INADDR_ANY, matching real
    // socket_bind()'s own "if (!addr) ... INADDR_ANY" branch.
    static int bind(int handle, int port, const std::string& addr, bool hasAddr,
                     const std::shared_ptr<LpcObject>& caller);

    // int socket_listen(int fd, string|function connect_callback)
    // Real socket_listen()'s own second argument becomes the socket's
    // read_callback (there is no separate "listen callback" slot) --
    // confirmed directly: "set_read_callback(fd, callback);".
    static int listen(int handle, Value callback, const std::shared_ptr<LpcObject>& caller);

    // int socket_accept(int fd, string|function read_callback,
    //                    string|function write_callback)
    // Returns a new handle (>= 0) for the accepted connection, or a
    // negative SocketErr code -- matching real socket_accept()'s own
    // "find_new_socket()"-then-populate shape, not an in-place mutation
    // of the listening socket's own entry.
    static int accept(int handle, Value readCallback, Value writeCallback,
                       const std::shared_ptr<LpcObject>& caller);

    // int socket_connect(int fd, string address, string|function read_callback,
    //                     string|function write_callback)
    // address is real socket_name_to_sin()'s own "host port" form
    // (space-separated, numeric dotted-quad host only -- real FluffOS
    // does no DNS resolution here either, matching this driver's own
    // pre-existing query_ip_name() precedent).
    static int connect(int handle, const std::string& address, Value readCallback,
                        Value writeCallback, const std::shared_ptr<LpcObject>& caller);

    // int socket_write(int fd, mixed message, void|string address)
    // message must be a string -- real STREAM/DATAGRAM T_BUFFER/T_ARRAY
    // forms are not implemented (no buffer type, see LpcSocket.hpp's own
    // comment); a non-string message returns SocketErr::ETypeNotSupp,
    // matching real socket_write()'s own "default: return
    // EETYPENOTSUPP;" rather than throwing.
    static int write(int handle, const Value& message, const std::string& address,
                      bool hasAddress, const std::shared_ptr<LpcObject>& caller);

    // int socket_close(int fd)
    // Real signature: the LPC-visible efun always calls the internal C
    // socket_close(fd, flags) with flags == 0 (no SC_DO_CALLBACK) -- a
    // deliberate LPC-initiated close never fires close_callback, only a
    // driver-detected asynchronous failure does (Server::pollSockets()'s
    // own poll-loop close path, mirroring real SC_DO_CALLBACK's only
    // internal call site being the write-error branch of
    // socket_write_select_handler()). Simplification versus real
    // STATE_FLUSHING: closes the fd immediately even if a partial write
    // is still pending, rather than deferring the close until it drains
    // -- real FluffOS defers close specifically so a blocked write can
    // still finish delivering; this driver accepts dropping any
    // undelivered remainder instead, judged an acceptable simplification
    // for "basics" scope (no live call site anywhere in this mudlib
    // relies on STATE_FLUSHING's drain-before-close behavior).
    static int close(int handle, const std::shared_ptr<LpcObject>& caller);

    // string socket_error(int error)
    // Mirrors real socket_err.c's own error_strings[] table exactly
    // (same order, same text) plus real socket_error()'s own
    // "error = -(error + 1)" index formula.
    static std::string errorString(int error);

    // mixed *socket_status(void|int fd)
    // Real return shape per lib/packages/sockets_spec.c's own doc
    // comment: [fd, state-string, mode-string, local-addr, remote-addr,
    // owner]. fd < 0 (hasFd false) real behavior returns an array of
    // every socket's own status array; this is exposed as a plain Value
    // by the EfunTable.cpp registration, not here.
    static Value statusOne(const LpcSocket& sock);

    static std::shared_ptr<LpcSocket> find(int handle);
    static std::vector<std::shared_ptr<LpcSocket>> all();

    // real close_referencing_sockets(ob) (socket_efuns.c): "for (i...)
    // if (lpc_socks[i].owner_ob == ob && state != CLOSED && state !=
    // FLUSHING) socket_close(i, SC_FORCE);" -- SC_FORCE alone, without
    // SC_DO_CALLBACK, so no close_callback fires (the same real no-
    // callback behavior close() above already gives an ordinary LPC-
    // initiated close) and SC_FORCE itself bypasses the ownership check
    // close() above enforces (irrelevant here regardless, since every
    // socket actually matched already has this exact owner). No
    // STATE_FLUSHING equivalent to skip either -- see close()'s own
    // comment on why that state does not exist in this driver at all.
    // Backs reload_object() (EfunTable.cpp), the previously-missing
    // piece flagged in this class's own header comment above.
    static void closeAllOwnedBy(const std::shared_ptr<LpcObject>& owner);

    // Used only by Server::pollSockets()'s own poll-detected-failure
    // path (peer EOF, a read/write error, a partial write that can never
    // flush) -- closes the fd and erases the registry entry, but fires
    // no callback itself (no VM access here); the caller reads
    // closeCallback/owner *before* calling this and fires it afterward,
    // the same "read the one-shot state, then act on it" split
    // Connection::takeWindowSizeUpdate() already established.
    static void forceRemove(int handle);

    // int socket_release(int fd, object ob, string|function callback)
    // -- the validation/state half of real socket_release() (socket_efuns.c:
    // fd range, not closed/flushing, caller is the current owner, not
    // already released -- EFdRange/EBadF/ESecurity/ESockRlsd on a real
    // failure of any of those). On success, marks the socket released to
    // ob (LpcSocket::released/releaseTarget) and returns Success -- the
    // caller (EfunTable.cpp's own socket_release registration, the one
    // layer with VM& access) fires callback(fd, ob) itself immediately
    // after, real socket_efuns.c's own "safe_call_function_pointer(...)
    // : safe_apply(..., ob, 2, ORIGIN_INTERNAL)", then calls
    // isReleased()/cancelRelease() below to resolve the real "did the
    // callback complete a socket_acquire() before returning" outcome real
    // socket_release() determines by re-checking its own S_RELEASE flag
    // after the callback returns.
    static int beginRelease(int handle, const std::shared_ptr<LpcObject>& ob,
                             const std::shared_ptr<LpcObject>& caller);

    // True while handle sits between a successful beginRelease() and
    // either a completing socket_acquire() (acquire() below clears this)
    // or a cancelRelease() call. False for an unknown handle too --
    // never released is observably the same as no-longer-released here.
    static bool isReleased(int handle);

    // Reverts a beginRelease() that no socket_acquire() ever completed
    // for -- real socket_release()'s own "lpc_socks[fd].flags &=
    // ~S_RELEASE; lpc_socks[fd].release_ob = NULL;" fallback, run by the
    // EfunTable.cpp caller only when isReleased() is still true right
    // after firing the release callback.
    static void cancelRelease(int handle);

    // int socket_acquire(int fd, string|function read_callback,
    //                     string|function write_callback,
    //                     string|function close_callback)
    // -- real socket_acquire() (socket_efuns.c): only succeeds while
    // handle is released (isReleased() above) *to this exact caller*
    // (real "release_ob != current_object" check, ESecurity otherwise);
    // on success, reassigns ownership to caller, clears the released
    // state, and overwrites all three callbacks with the ones given
    // here -- real socket_acquire()'s own unconditional
    // set_read_callback()/set_write_callback()/set_close_callback()
    // trio, replacing whatever socket_create()/a prior socket_acquire()
    // had set, not merged with them.
    static int acquire(int handle, Value readCallback, Value writeCallback,
                        Value closeCallback, const std::shared_ptr<LpcObject>& caller);
};

}  // namespace amlp
