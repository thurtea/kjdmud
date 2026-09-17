#include "amlp/net/Server.hpp"
#include "amlp/config/Config.hpp"
#include "amlp/vm/VM.hpp"
#include "amlp/object/ObjectManager.hpp"
#include "amlp/object/LpcObject.hpp"
#include "amlp/net/OutputContext.hpp"
#include "amlp/net/SocketRegistry.hpp"
#include "amlp/net/SnoopRelay.hpp"
#include "amlp/core/Errors.hpp"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <algorithm>

namespace amlp {

namespace {
bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

// string|function dispatch for socket callbacks. A destructed owner
// silently drops the callback (PendingInputTo O_DESTRUCTED handling).
void fireSocketCallback(VM& vm, const Value& callback, const std::weak_ptr<LpcObject>& owner,
                         std::vector<Value> args) {
    if (std::holds_alternative<std::monostate>(callback.data)) return;
    if (auto* closure = std::get_if<std::shared_ptr<Closure>>(&callback.data)) {
        if (!*closure) return;
        try {
            vm.callClosure(*closure, std::move(args));
        } catch (const std::exception& e) {
            std::cerr << "[net] socket closure callback failed: " << e.what() << "\n";
        }
        return;
    }
    if (auto* name = std::get_if<std::string>(&callback.data)) {
        auto ob = owner.lock();
        if (!ob) return;
        try {
            // socket_efuns.c: safe_apply(..., ORIGIN_INTERNAL).
            vm.callFunction(ob, *name, std::move(args), Origin::Internal);
        } catch (const std::exception& e) {
            std::cerr << "[net] socket callback " << *name << "() failed: " << e.what() << "\n";
        }
    }
}

// Driver-detected close (peer EOF / I/O error): fire close_callback.
// LPC-initiated SocketRegistry::close() does not. `sock` is a local
// copy, valid after forceRemove() erases the registry entry.
void closeSocketAndFireCallback(VM& vm, const std::shared_ptr<LpcSocket>& sock) {
    Value cb = sock->closeCallback;
    std::weak_ptr<LpcObject> owner = sock->owner;
    int handle = sock->handle;
    SocketRegistry::forceRemove(handle);
    fireSocketCallback(vm, cb, owner, {Value(static_cast<int64_t>(handle))});
}
}

Server::Server(Config& config, VM& vm, ObjectManager& objects, Scheduler& scheduler)
    : config_(config), vm_(vm), objects_(objects), scheduler_(scheduler) {}

Server::~Server() {
    if (listenFd_ >= 0) {
        ::close(listenFd_);
    }
}

bool Server::listen() {
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        std::cerr << "[net] socket() failed: " << std::strerror(errno) << "\n";
        return false;
    }

    int opt = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(config_.port()));

    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[net] bind() failed on port " << config_.port()
                   << ": " << std::strerror(errno) << "\n";
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    if (::listen(listenFd_, 16) < 0) {
        std::cerr << "[net] listen() failed: " << std::strerror(errno) << "\n";
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    if (!setNonBlocking(listenFd_)) {
        std::cerr << "[net] warning: failed to set listen socket non-blocking\n";
    }

    std::cout << "[net] listening on port " << config_.port() << "\n";
    return true;
}

void Server::onNewConnection(int clientFd) {
    setNonBlocking(clientFd);
    int one = 1;
    ::setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    auto conn = std::make_shared<Connection>(clientFd);

    // comm.c new_user(): IAC DO TTYPE then IAC DO NAWS before connect().
    // MXP is unsent; this driver does not process WILL MXP yet.
    conn->send(std::string("\xff\xfd\x18", 3));  // IAC DO TTYPE (255 253 24)
    conn->send(std::string("\xff\xfd\x1f", 3));  // IAC DO NAWS (255 253 31)

    OutputContext::set(conn.get());

    // connect() throwing fails this connection, not the whole process.
    Value result;
    try {
        result = vm_.applyMaster("connect", {});
    } catch (const std::exception& e) {
        OutputContext::set(nullptr);
        std::cerr << "[net] master->connect() failed: " << e.what() << "\n";
        ::close(clientFd);
        return;
    }

    if (!std::holds_alternative<std::shared_ptr<LpcObject>>(result.data)) {
        OutputContext::set(nullptr);
        std::cerr << "[net] master->connect() did not return an object; "
                      "closing connection\n";
        ::close(clientFd);
        return;
    }

    auto loginObj = std::get<std::shared_ptr<LpcObject>>(result.data);
    if (!loginObj) {
        OutputContext::set(nullptr);
        std::cerr << "[net] master->connect() returned a null object\n";
        ::close(clientFd);
        return;
    }

    conn->attach(loginObj);
    std::cout << "[net] connection fd=" << clientFd
               << " bound to " << loginObj->filename() << "\n";

    // backend.c logon(): apply(APPLY_LOGON, ob, 0, ORIGIN_DRIVER) right
    // after bind. Missing logon() is not an error. A runtime error
    // inside logon() closes only this connection.
    try {
        vm_.callFunction(loginObj, "logon", {});
    } catch (const std::exception& e) {
        OutputContext::set(nullptr);
        std::cerr << "[net] connection fd=" << clientFd
                   << " logon() failed: " << e.what() << "\n";
        conn->close();
        return;
    }

    OutputContext::set(nullptr);
    connections_.push_back(std::move(conn));
}

void Server::acceptNewConnections() {
    for (;;) {
        sockaddr_in clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);
        int clientFd = ::accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            std::cerr << "[net] accept() failed: " << std::strerror(errno) << "\n";
            break;
        }
        onNewConnection(clientFd);
    }
}

// comm.c process_user_command(): pending input_to() consumes the line
// first; process_input() runs only when nothing is pending.
void Server::dispatchLine(VM& vm, Connection& conn, const std::string& line) {
    // Real get_user_command() (comm.c): "ip->last_time = current_time"
    // runs the moment a full command line is pulled off the buffer, for
    // every line -- including one a pending input_to() handler is about
    // to consume -- and before process_user_command() itself even runs.
    // query_idle() (EfunTable.cpp) reads this back.
    conn.touchActivity();

    // Real process_user_command() (comm.c): clear_notify(ip->ob) runs
    // unconditionally at the very top, before even checking for a
    // pending input_to() handler -- a notify_fail() message set during
    // an earlier, unrelated dispatch must never leak into this one.
    conn.clearPendingNotifyFail();

    // Real interpret.c/backend.c: eval_cost is reset to 0 once at the
    // start of each top-level dispatch (process_user_command /
    // call_heart_beat / call_call_out) and then accumulates across all
    // nested apply/call_other/callClosure calls. Matches the single
    // global reset in the reference driver, not per-run() reset.
    vm.resetEvalCost();

    if (conn.hasPendingInputTo()) {
        std::optional<PendingInputTo> pending = conn.takePendingInputTo();
        auto target = pending->object.lock();
        // Real FluffOS's own O_DESTRUCTED check in
        // call_function_interactive(): a handler whose object died
        // before its next line arrived is simply dropped, not an error.
        if (target) {
            std::vector<Value> callArgs;
            callArgs.reserve(1 + pending->extraArgs.size());
            callArgs.emplace_back(line);
            for (auto& extra : pending->extraArgs) callArgs.push_back(extra);
            // comm.c call_function_interactive(): string form is
            // apply(..., ORIGIN_INTERNAL); closure form is call_function_pointer().
            if (auto* closure = std::get_if<std::shared_ptr<Closure>>(&pending->function.data)) {
                if (*closure) vm.callClosure(*closure, std::move(callArgs));
            } else if (auto* name = std::get_if<std::string>(&pending->function.data)) {
                vm.callFunction(target, *name, std::move(callArgs), Origin::Internal);
            }
        }
        return;
    }

    // comm.c process_input(): object's process_input() return decides
    // the line. String = replacement; truthy non-string = consumed;
    // otherwise dispatch the original unchanged.
    auto obj = conn.boundObject();
    if (!obj) return;

    std::string toDispatch = line;
    Value processed = vm.callFunction(obj, "process_input", {Value(line)});
    if (std::holds_alternative<std::string>(processed.data)) {
        toDispatch = std::get<std::string>(processed.data);
    } else if (std::holds_alternative<int64_t>(processed.data)) {
        if (std::get<int64_t>(processed.data) != 0) return; // fully consumed
    }
    // monostate (process_input undefined) or any other falsy/non-string
    // return: dispatch the original line, untouched.

    // real parse_command(): matches against command_giver's own action
    // table (see VM::dispatchCommand()'s own comment for the exact
    // add_action/enable_commands semantics this backs).
    bool claimed = vm.dispatchCommand(obj, toDispatch);

    // add_action.c notify_no_command(): only when nothing claimed the
    // command. String is shown; a function is called with no args and
    // its string return (if any) is shown. No driver-invented "What?".
    if (!claimed) {
        if (auto pending = conn.takePendingNotifyFail()) {
            if (auto* msg = std::get_if<std::string>(&pending->data)) {
                deliverToConnection(vm, &conn, *msg);
            } else if (auto* closure = std::get_if<std::shared_ptr<Closure>>(&pending->data)) {
                if (*closure) {
                    Value result = vm.callClosure(*closure, {});
                    if (auto* resultMsg = std::get_if<std::string>(&result.data)) {
                        deliverToConnection(vm, &conn, *resultMsg);
                    }
                }
            }
        }
    }
}

void Server::fireNetDeadIfLinkDead(VM& vm, Connection& conn) {
    if (!conn.closed()) return;
    auto obj = conn.boundObject();
    if (!obj) return;

    OutputContext::set(&conn);
    try {
        vm.callFunction(obj, "net_dead", {});
    } catch (const std::exception& e) {
        std::cerr << "[net] connection fd=" << conn.fd()
                   << " net_dead() failed: " << e.what() << "\n";
    }
    OutputContext::set(nullptr);
}

void Server::pollSockets(VM& vm) {
    for (auto& sock : SocketRegistry::all()) {
        if (sock->fd < 0) continue;

        if (sock->mode == SocketMode::Datagram) {
            // Datagram sockets are read-polled regardless of state.
            pollfd pfd{sock->fd, POLLIN, 0};
            if (::poll(&pfd, 1, 0) <= 0 || !(pfd.revents & POLLIN)) continue;
            char buf[2048];
            sockaddr_in peer{};
            socklen_t peerLen = sizeof(peer);
            ssize_t n = ::recvfrom(sock->fd, buf, sizeof(buf) - 1, 0,
                                    reinterpret_cast<sockaddr*>(&peer), &peerLen);
            if (n <= 0) continue;
            buf[n] = '\0';
            std::string addr = std::string(::inet_ntoa(peer.sin_addr)) + " " +
                                std::to_string(ntohs(peer.sin_port));
            // Real: push_number(fd); push string; push addr;
            // call_callback(fd, S_READ_FP, 3) -- three args.
            fireSocketCallback(vm, sock->readCallback, sock->owner,
                {Value(static_cast<int64_t>(sock->handle)), Value(std::string(buf)), Value(addr)});
            continue;
        }

        if (sock->state == SocketState::Listen) {
            // Real S_WACCEPT: only re-arm/re-fire once socket_accept()
            // actually consumes the pending connection.
            if (sock->acceptPending) continue;
            pollfd pfd{sock->fd, POLLIN, 0};
            if (::poll(&pfd, 1, 0) <= 0 || !(pfd.revents & POLLIN)) continue;
            sock->acceptPending = true;
            // Real: push_number(fd); call_callback(fd, S_READ_FP, 1) --
            // one arg, the listening socket's own handle, not a new one
            // (socket_accept() itself, called from LPC in response, is
            // what actually performs accept() and returns the new
            // handle -- see SocketRegistry::accept()).
            fireSocketCallback(vm, sock->readCallback, sock->owner,
                {Value(static_cast<int64_t>(sock->handle))});
            continue;
        }

        if (sock->state != SocketState::DataXfer) continue;

        short events = POLLIN;
        if (sock->blocked) events |= POLLOUT;
        pollfd pfd{sock->fd, events, 0};
        if (::poll(&pfd, 1, 0) <= 0) continue;

        if (pfd.revents & POLLIN) {
            char buf[4096];
            ssize_t n = ::read(sock->fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                // Real: push_number(fd); push string; call_callback(fd,
                // S_READ_FP, 2) -- two args for STREAM (MUD mode's own
                // three-arg svalue form is not implemented, see
                // LpcSocket.hpp's own SocketMode comment).
                fireSocketCallback(vm, sock->readCallback, sock->owner,
                    {Value(static_cast<int64_t>(sock->handle)), Value(std::string(buf))});
            } else if (n == 0) {
                // Peer EOF: close here rather than spinning on a
                // readable-with-nothing socket. Real socket_read_select_handler()
                // does nothing special on cc<=0; close happens in comm.c.
                closeSocketAndFireCallback(vm, sock);
                continue;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                closeSocketAndFireCallback(vm, sock);
                continue;
            }
        }

        if ((pfd.revents & POLLOUT) && sock->blocked) {
            if (!sock->pendingWrite.empty()) {
                ssize_t off = ::write(sock->fd, sock->pendingWrite.data(), sock->pendingWrite.size());
                if (off > 0) {
                    sock->pendingWrite.erase(0, static_cast<size_t>(off));
                } else if (off < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                    closeSocketAndFireCallback(vm, sock);
                    continue;
                }
            }
            if (sock->pendingWrite.empty()) {
                // socket_write_select_handler(): clear S_BLOCKED, fire
                // write callback (flushed write or completed connect).
                sock->blocked = false;
                fireSocketCallback(vm, sock->writeCallback, sock->owner,
                    {Value(static_cast<int64_t>(sock->handle))});
            }
        }
    }
}

void Server::handleConnection(Connection& conn) {
    auto lines = conn.pollLines();

    auto obj = conn.boundObject();

    // comm.c: APPLY_WINDOW_SIZE on NAWS, independent of any text line.
    if (conn.takeWindowSizeUpdate() && obj) {
        OutputContext::set(&conn);
        try {
            vm_.callFunction(obj, "window_size",
                {Value(static_cast<int64_t>(conn.terminalWidth())),
                 Value(static_cast<int64_t>(conn.terminalHeight()))});
        } catch (const std::exception& e) {
            std::cerr << "[net] connection fd=" << conn.fd()
                       << " window_size() failed: " << e.what() << "\n";
        }
        OutputContext::set(nullptr);
    }

    // comm.c: APPLY_TERMINAL_TYPE on SB TTYPE IS, same shape as NAWS.
    if (conn.takeTerminalTypeUpdate() && obj) {
        OutputContext::set(&conn);
        try {
            vm_.callFunction(obj, "terminal_type", {Value(conn.terminalType())});
        } catch (const std::exception& e) {
            std::cerr << "[net] connection fd=" << conn.fd()
                       << " terminal_type() failed: " << e.what() << "\n";
        }
        OutputContext::set(nullptr);
    }

    if (obj && !lines.empty()) {
        OutputContext::set(&conn);
        for (const auto& line : lines) {
            // Per-command recovery: an uncaught error aborts only this
            // line (add_action.cc safe_parse_command() / comm.cc
            // safe_apply). VM stacks are RAII, so C++ unwind restores
            // state; evalCost_ is reset by the next dispatchLine().
            try {
                dispatchLine(vm_, conn, line);
            } catch (const std::exception& e) {
                std::cerr << "[net] connection fd=" << conn.fd()
                           << " input handling failed (command isolated, "
                              "connection stays open): " << e.what() << "\n";
                // Generic player-facing message (DEFAULT_ERROR_MESSAGE).
                // Detail stays in the driver log. Nested try: delivery
                // itself may throw (snoop relay).
                try {
                    deliverToConnection(vm_, &conn,
                        "Error while processing your command.\n");
                } catch (const std::exception& e2) {
                    std::cerr << "[net] connection fd=" << conn.fd()
                               << " error-report delivery itself failed: "
                               << e2.what() << "\n";
                }
                // Connection and remaining buffered input continue.
                continue;
            }
        }
        OutputContext::set(nullptr);
    }

    // pollLines() saw EOF/read error; close() has not run yet.
    fireNetDeadIfLinkDead(vm_, conn);
}

void Server::pollOnce() {
    if (listenFd_ < 0) return;

    acceptNewConnections();

    for (auto& conn : connections_) {
        if (conn->isOpen()) {
            handleConnection(*conn);
        }
    }

    pollSockets(vm_);

    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
                        [](const std::shared_ptr<Connection>& c) { return c->closed(); }),
        connections_.end());
}

} // namespace amlp
