#pragma once
#include <memory>
#include <string>
#include <vector>
#include "amlp/net/Connection.hpp"

namespace amlp {

class Config;
class VM;
class ObjectManager;
class Scheduler;

class Server {
public:
    Server(Config& config, VM& vm, ObjectManager& objects, Scheduler& scheduler);
    ~Server();

    bool listen();
    void pollOnce();

    size_t connectionCount() const { return connections_.size(); }

    // The per-line input_to()-or-process_input() dispatch decision (see
    // comm.c's process_user_command()/call_function_interactive()),
    // pulled out of handleConnection() as a free-standing, directly
    // testable step: it only touches the VM and the one connection
    // passed in, no Server instance state, so unit tests can drive it
    // without a real listening socket. handleConnection() is what wraps
    // this in the per-connection error-isolation try/catch; this method
    // itself just throws normally on a runtime error like any other VM
    // call.
    static void dispatchLine(VM& vm, Connection& conn, const std::string& line);

    // Fires real FluffOS's own link-death apply (comm.c's
    // remove_interactive(ob, dested): "if (!dested) safe_apply(
    // APPLY_NET_DEAD, ob, 0, ORIGIN_DRIVER);") on a connection whose
    // underlying socket has already gone away (Connection::pollLines()
    // detected EOF/a read error and set closed_, but the connection's
    // own boundObject() is still intact -- close() itself hasn't run
    // yet). Pulled out as its own directly-testable static method for
    // the same reason dispatchLine() is: no live socket accept loop
    // needed, just a Connection built over one half of a socketpair.
    // A no-op if the connection isn't actually closed yet, or has no
    // bound object (covers the real dested=1 case too: the destruct()
    // efun already closes the connection itself -- see its own comment
    // -- which clears boundObject() before this could ever run, so a
    // destructed object's own connection teardown correctly never
    // reaches net_dead(), matching the "dested" skip exactly). Deliberately
    // scoped to only this EOF/read-error path, not this driver's own
    // separate mid-dispatch runtime-error connection close (see
    // handleConnection()'s own catch block) -- flagged as a scope
    // simplification, not assumed equivalent.
    static void fireNetDeadIfLinkDead(VM& vm, Connection& conn);

    // Row 0.10's own async half: every LPC efun socket currently
    // registered in SocketRegistry gets a non-blocking readiness check
    // (select()) each call, and any event found fires the matching LPC
    // callback (read_callback/write_callback/close_callback) via this
    // VM& -- the deferred counterpart to SocketRegistry's own
    // synchronous, efun-triggered half (create/bind/listen/accept/
    // connect/write/close), mirroring the exact Connection/Server split
    // window_size() already established: SocketRegistry (like
    // Connection) never touches a VM directly, only Server does. Pulled
    // out static and public, taking only a VM& (SocketRegistry itself is
    // a global registry, no Server instance state needed either) for the
    // same reason dispatchLine()/fireNetDeadIfLinkDead() are: a real
    // regression test can drive this directly over a socketpair-backed
    // LpcSocket, no live accept loop required.
    static void pollSockets(VM& vm);

private:
    void acceptNewConnections();
    void handleConnection(Connection& conn);
    void onNewConnection(int clientFd);

    Config& config_;
    VM& vm_;
    ObjectManager& objects_;
    Scheduler& scheduler_;
    int listenFd_ = -1;
    std::vector<std::shared_ptr<Connection>> connections_;
};

} // namespace amlp
