#pragma once
#include <ctime>
#include <memory>
#include <string>
#include <vector>
#include "kjdmud/config/Config.hpp"
#include "kjdmud/net/Connection.hpp"

typedef struct ssl_ctx_st SSL_CTX;

namespace kjdmud {

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
    // own boundObject() is still intact. close() itself hasn't run
    // yet). Pulled out as its own directly-testable static method for
    // the same reason dispatchLine() is: no live socket accept loop
    // needed, just a Connection built over one half of a socketpair.
    // A no-op if the connection isn't actually closed yet, or has no
    // bound object (covers the real dested=1 case too: the destruct()
    // efun already closes the connection itself. See its own comment
    // Which clears boundObject() before this could ever run, so a
    // destructed object's own connection teardown correctly never
    // reaches net_dead(), matching the "dested" skip exactly). Deliberately
    // scoped to only this EOF/read-error path, not this driver's own
    // separate mid-dispatch runtime-error connection close (see
    // handleConnection()'s own catch block). Flagged as a scope
    // simplification, not assumed equivalent.
    static void fireNetDeadIfLinkDead(VM& vm, Connection& conn);

    // Row 0.10's own async half: every LPC efun socket currently
    // registered in SocketRegistry gets a non-blocking readiness check
    // (select()) each call, and any event found fires the matching LPC
    // callback (read_callback/write_callback/close_callback) via this
    // VM&. The deferred counterpart to SocketRegistry's own
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

    // src/config/instruct.md Phase 0's own max_connections row. Pulled
    // out as a pure static predicate for the same reason dispatchLine()/
    // fireNetDeadIfLinkDead()/pollSockets() above are: the actual
    // accept-or-reject decision is directly testable this way, no live
    // listening socket required.
    static bool atMaxConnections(size_t currentCount, int maxConnections) {
        return currentCount >= static_cast<size_t>(maxConnections);
    }

private:
    struct Listener {
        int fd = -1;
        ListenPort spec;
    };

    void acceptOn(const Listener& listener);
    void handleConnection(Connection& conn);
    void onNewConnection(int clientFd, const ListenPort& spec);
    bool openListener(const ListenPort& spec);

    Config& config_;
    VM& vm_;
    ObjectManager& objects_;
    Scheduler& scheduler_;
    std::vector<Listener> listeners_;
    SSL_CTX* sslCtx_ = nullptr;
    std::vector<std::shared_ptr<Connection>> connections_;
    // MSSP's own "UPTIME" variable (MsspHandler). Server is constructed
    // once, early in main(), the same "close enough to real boot time"
    // granularity uptime()'s own bootTime capture in EfunTable.cpp uses.
    const std::time_t bootTime_ = std::time(nullptr);
};

} // namespace kjdmud
