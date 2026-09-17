#include "amlp/net/SocketRegistry.hpp"
#include "amlp/object/LpcObject.hpp"
#include "amlp/vm/Value.hpp"

#include <unordered_map>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

namespace amlp {

LpcSocket::~LpcSocket() {
    if (fd >= 0) ::close(fd);
}

namespace {
std::unordered_map<int, std::shared_ptr<LpcSocket>> g_sockets;
int g_nextHandle = 0;

bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

// Real socket_name_to_sin() (socket_efuns.c): splits on the first space,
// the tail is a plain decimal port, the head is a numeric dotted-quad
// resolved via inet_addr() only -- no DNS/getaddrinfo() lookup at all.
// Matched here exactly, including the same "no space found -> reject"
// behavior (real function returns 0/false in that case, which every
// caller here maps to SocketErr::EBadAddr).
bool parseHostPort(const std::string& name, std::string& host, int& port) {
    size_t sp = name.find(' ');
    if (sp == std::string::npos) return false;
    host = name.substr(0, sp);
    std::string portStr = name.substr(sp + 1);
    if (portStr.empty()) return false;
    port = std::atoi(portStr.c_str());
    return true;
}

bool buildSockaddr(const std::string& host, int port, sockaddr_in& sin) {
    std::memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(static_cast<uint16_t>(port));
    in_addr_t addr = ::inet_addr(host.c_str());
    if (addr == INADDR_NONE && host != "255.255.255.255") return false;
    sin.sin_addr.s_addr = addr;
    return true;
}

// Real error_strings[] (socket_err.c), same order, same text, indexed by
// real socket_error()'s own "-(error + 1)" formula (SocketErr::ESocket
// == -1 -> index 0, and so on down to SocketErr::EBadData == -32 ->
// index 31). The last entry is real (EEBADDATA, socket_write()'s own
// MUD-mode wire-framing nesting-depth error) but unreachable here, since
// MUD mode itself is not implemented -- see SocketErr::EBadData's own
// comment, LpcSocket.hpp.
const char* const kErrorStrings[] = {
    "Problem creating socket",           // ESocket      -1
    "Problem with setsockopt",           // ESetSockOpt  -2
    "Problem setting non-blocking mode", // ENonBlock    -3
    "No more available efun sockets",    // ENoSocks     -4
    "Descriptor out of range",           // EFdRange     -5
    "Socket is closed",                  // EBadF        -6
    "Security violation attempted",      // ESecurity    -7
    "Socket is already bound",           // EIsBound     -8
    "Address already in use",            // EAddrInUse   -9
    "Problem with bind",                 // EBind        -10
    "Problem with getsockname",          // EGetSockName -11
    "Socket mode not supported",         // EModeNotSupp -12
    "Socket not bound to an address",    // ENoAddr      -13
    "Socket is already connected",       // EIsConn      -14
    "Problem with listen",               // EListen      -15
    "Socket not listening",              // ENotListn    -16
    "Operation would block",             // EWouldBlock  -17
    "Interrupted system call",           // EIntr        -18
    "Problem with accept",               // EAccept      -19
    "Socket is listening",               // EIsListen    -20
    "Problem with address format",       // EBadAddr     -21
    "Operation already in progress",     // EAlready     -22
    "Connection refused",                // EConnRefused -23
    "Problem with connect",              // EConnect     -24
    "Socket not connected",              // ENotConn     -25
    "Object type not supported",         // ETypeNotSupp -26
    "Problem with sendto",               // ESendTo      -27
    "Problem with send",                 // ESend        -28
    "Wait for callback",                 // ECallback    -29
    "Socket already released",           // ESockRlsd    -30
    "Socket not released",               // ESockNotRlsd -31
    "Data nested too deeply",            // EBadData     -32 (unreachable, see above)
};
constexpr int kErrorStringsCount = sizeof(kErrorStrings) / sizeof(kErrorStrings[0]);

const char* modeName(SocketMode m) {
    return m == SocketMode::Stream ? "STREAM" : "DATAGRAM";
}

const char* stateName(SocketState s) {
    switch (s) {
        case SocketState::Unbound:  return "UNBOUND";
        case SocketState::Bound:    return "BOUND";
        case SocketState::Listen:   return "LISTEN";
        case SocketState::DataXfer: return "DATA_XFER";
        case SocketState::Closed:   return "CLOSED";
    }
    return "CLOSED";
}
}  // namespace

int SocketRegistry::create(int mode, Value readCallback, Value closeCallback,
                            const std::shared_ptr<LpcObject>& owner) {
    SocketMode sm;
    switch (mode) {
        case 1: sm = SocketMode::Stream; break;
        case 2: sm = SocketMode::Datagram; break;
        // 0 (MUD), 3 (STREAM_BINARY), 4 (DATAGRAM_BINARY): real, but
        // unimplemented -- see LpcSocket.hpp's own enum comment.
        default: return SocketErr::EModeNotSupp;
    }

    int type = (sm == SocketMode::Stream) ? SOCK_STREAM : SOCK_DGRAM;
    int fd = ::socket(AF_INET, type, 0);
    if (fd < 0) return SocketErr::ESocket;

    int optval = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        ::close(fd);
        return SocketErr::ESetSockOpt;
    }
    if (!setNonBlocking(fd)) {
        ::close(fd);
        return SocketErr::ENonBlock;
    }

    int handle = g_nextHandle++;
    auto sock = std::make_shared<LpcSocket>(handle, fd, sm, owner);
    sock->readCallback = std::move(readCallback);
    // Real socket_create(): "if (type == SOCK_DGRAM) close_callback = 0;"
    // -- a datagram socket never gets a close callback, regardless of
    // what was passed.
    if (sm == SocketMode::Stream) sock->closeCallback = std::move(closeCallback);
    sock->state = SocketState::Unbound;
    g_sockets[handle] = sock;
    return handle;
}

int SocketRegistry::bind(int handle, int port, const std::string& addr, bool hasAddr,
                          const std::shared_ptr<LpcObject>& caller) {
    auto it = g_sockets.find(handle);
    if (it == g_sockets.end()) return SocketErr::EFdRange;
    auto& sock = it->second;
    if (sock->state == SocketState::Closed) return SocketErr::EBadF;
    if (sock->owner.lock() != caller) return SocketErr::ESecurity;
    if (sock->state != SocketState::Unbound) return SocketErr::EIsBound;

    sockaddr_in sin{};
    sin.sin_family = AF_INET;
    sin.sin_port = htons(static_cast<uint16_t>(port));
    if (!hasAddr) {
        sin.sin_addr.s_addr = INADDR_ANY;
    } else {
        in_addr_t a = ::inet_addr(addr.c_str());
        if (a == INADDR_NONE) return SocketErr::EBadAddr;
        sin.sin_addr.s_addr = a;
    }

    if (::bind(sock->fd, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) < 0) {
        return (errno == EADDRINUSE) ? SocketErr::EAddrInUse : SocketErr::EBind;
    }
    socklen_t len = sizeof(sin);
    if (::getsockname(sock->fd, reinterpret_cast<sockaddr*>(&sin), &len) == 0) {
        sock->localAddr = ::inet_ntoa(sin.sin_addr);
        sock->localPort = ntohs(sin.sin_port);
    }
    sock->state = SocketState::Bound;
    return SocketErr::Success;
}

int SocketRegistry::listen(int handle, Value callback, const std::shared_ptr<LpcObject>& caller) {
    auto it = g_sockets.find(handle);
    if (it == g_sockets.end()) return SocketErr::EFdRange;
    auto& sock = it->second;
    if (sock->state == SocketState::Closed) return SocketErr::EBadF;
    if (sock->owner.lock() != caller) return SocketErr::ESecurity;
    if (sock->mode == SocketMode::Datagram) return SocketErr::EModeNotSupp;
    if (sock->state == SocketState::Unbound) return SocketErr::ENoAddr;
    if (sock->state != SocketState::Bound) return SocketErr::EIsConn;

    if (::listen(sock->fd, 5) < 0) return SocketErr::EListen;
    sock->state = SocketState::Listen;
    // Real socket_listen(): "set_read_callback(fd, callback);" -- the
    // listen socket's own read_callback IS the connect-pending signal,
    // there is no separate slot.
    sock->readCallback = std::move(callback);
    return SocketErr::Success;
}

int SocketRegistry::accept(int handle, Value readCallback, Value writeCallback,
                            const std::shared_ptr<LpcObject>& caller) {
    auto it = g_sockets.find(handle);
    if (it == g_sockets.end()) return SocketErr::EFdRange;
    auto& listenSock = it->second;
    if (listenSock->state == SocketState::Closed) return SocketErr::EBadF;
    if (listenSock->owner.lock() != caller) return SocketErr::ESecurity;
    if (listenSock->mode == SocketMode::Datagram) return SocketErr::EModeNotSupp;
    if (listenSock->state != SocketState::Listen) return SocketErr::ENotListn;

    listenSock->acceptPending = false;  // real: "flags &= ~S_WACCEPT;"

    sockaddr_in peer{};
    socklen_t len = sizeof(peer);
    int newFd = ::accept(listenSock->fd, reinterpret_cast<sockaddr*>(&peer), &len);
    if (newFd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return SocketErr::EWouldBlock;
        if (errno == EINTR) return SocketErr::EIntr;
        return SocketErr::EAccept;
    }
    setNonBlocking(newFd);

    int newHandle = g_nextHandle++;
    auto sock = std::make_shared<LpcSocket>(newHandle, newFd, listenSock->mode, caller);
    sock->state = SocketState::DataXfer;
    sock->readCallback = std::move(readCallback);
    sock->writeCallback = std::move(writeCallback);
    // Real "copy_close_callback(i, fd)": the accepted socket inherits the
    // listening socket's own close_callback.
    sock->closeCallback = listenSock->closeCallback;
    sock->remoteAddr = ::inet_ntoa(peer.sin_addr);
    sock->remotePort = ntohs(peer.sin_port);
    sock->localAddr = listenSock->localAddr;
    sock->localPort = listenSock->localPort;
    g_sockets[newHandle] = sock;
    return newHandle;
}

int SocketRegistry::connect(int handle, const std::string& address, Value readCallback,
                             Value writeCallback, const std::shared_ptr<LpcObject>& caller) {
    auto it = g_sockets.find(handle);
    if (it == g_sockets.end()) return SocketErr::EFdRange;
    auto& sock = it->second;
    if (sock->state == SocketState::Closed) return SocketErr::EBadF;
    if (sock->owner.lock() != caller) return SocketErr::ESecurity;
    if (sock->mode == SocketMode::Datagram) return SocketErr::EModeNotSupp;
    if (sock->state == SocketState::Listen) return SocketErr::EIsListen;
    if (sock->state == SocketState::DataXfer) return SocketErr::EIsConn;

    std::string host;
    int port = 0;
    if (!parseHostPort(address, host, port)) return SocketErr::EBadAddr;
    sockaddr_in sin{};
    if (!buildSockaddr(host, port, sin)) return SocketErr::EBadAddr;

    sock->readCallback = std::move(readCallback);
    sock->writeCallback = std::move(writeCallback);

    if (::connect(sock->fd, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) < 0) {
        switch (errno) {
            case EINTR:        return SocketErr::EIntr;
            case EADDRINUSE:   return SocketErr::EAddrInUse;
            case EALREADY:     return SocketErr::EAlready;
            case ECONNREFUSED: return SocketErr::EConnRefused;
            case EINPROGRESS:  break;  // real: falls through to success below
            default:           return SocketErr::EConnect;
        }
    }
    sock->state = SocketState::DataXfer;
    sock->remoteAddr = host;
    sock->remotePort = port;
    // Real socket_connect(): "lpc_socks[fd].flags |= S_BLOCKED;" -- set
    // unconditionally, even for a connect() that returned success
    // immediately (a local/loopback connect very often does). This is
    // what makes Server::pollSockets()'s writable-check fire
    // write_callback(fd) exactly once as the real "connect complete"
    // notification, matching real socket_write_select_handler() being
    // the only place that clears S_BLOCKED and calls the write callback.
    sock->blocked = true;
    return SocketErr::Success;
}

int SocketRegistry::write(int handle, const Value& message, const std::string& address,
                           bool hasAddress, const std::shared_ptr<LpcObject>& caller) {
    auto it = g_sockets.find(handle);
    if (it == g_sockets.end()) return SocketErr::EFdRange;
    auto& sock = it->second;
    if (sock->state == SocketState::Closed) return SocketErr::EBadF;
    if (sock->owner.lock() != caller) return SocketErr::ESecurity;

    if (!std::holds_alternative<std::string>(message.data)) return SocketErr::ETypeNotSupp;
    const std::string& text = std::get<std::string>(message.data);

    if (sock->mode == SocketMode::Datagram) {
        if (!hasAddress) return SocketErr::ENoAddr;
        std::string host;
        int port = 0;
        if (!parseHostPort(address, host, port)) return SocketErr::EBadAddr;
        sockaddr_in sin{};
        if (!buildSockaddr(host, port, sin)) return SocketErr::EBadAddr;
        // Real DATAGRAM T_STRING write sends strlen()+1 bytes (the
        // trailing '\0' included) -- confirmed directly, and a genuine,
        // deliberate asymmetry against the STREAM case just below (which
        // does not send the terminator). Matched here exactly rather
        // than "fixed" to be consistent with STREAM.
        ssize_t sent = ::sendto(sock->fd, text.c_str(), text.size() + 1, 0,
                                 reinterpret_cast<sockaddr*>(&sin), sizeof(sin));
        if (sent < 0) return SocketErr::ESendTo;
        return SocketErr::Success;
    }

    // STREAM
    if (sock->state != SocketState::DataXfer) return SocketErr::ENotConn;
    if (hasAddress) return SocketErr::EBadAddr;
    if (sock->blocked) return SocketErr::EAlready;

    if (text.empty()) return SocketErr::Success;
    ssize_t off = ::write(sock->fd, text.data(), text.size());
    if (off <= 0) {
        if (off < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return SocketErr::EWouldBlock;
        if (off < 0 && errno == EINTR) return SocketErr::EIntr;
        // Real: "flags |= S_LINKDEAD; socket_close(fd, SC_FORCE |
        // SC_DO_CALLBACK | SC_FINAL_CLOSE); return EESEND;" -- a hard
        // write failure force-closes immediately AND fires
        // close_callback, unlike a normal LPC-initiated socket_close().
        // Known, narrow gap here: this method has no VM& (it is called
        // straight from EfunTable.cpp's socket_write lambda, which does
        // have one, but threading it through would widen this class's
        // contract for one rare edge case -- a peer that vanished at the
        // exact moment of an LPC-initiated write). The socket is still
        // force-closed correctly; only the close_callback firing itself
        // is dropped for this one synchronous path. Every other close
        // path this driver implements (a normal LPC socket_close(), and
        // Server::pollSockets()'s own poll-detected EOF/error handling)
        // is unaffected -- pollSockets() does have VM& and fires
        // close_callback correctly for a failure it detects itself.
        forceRemove(handle);
        return SocketErr::ESend;
    }
    if (static_cast<size_t>(off) < text.size()) {
        sock->blocked = true;
        sock->pendingWrite = text.substr(static_cast<size_t>(off));
        return SocketErr::ECallback;
    }
    return SocketErr::Success;
}

int SocketRegistry::close(int handle, const std::shared_ptr<LpcObject>& caller) {
    auto it = g_sockets.find(handle);
    if (it == g_sockets.end()) return SocketErr::EFdRange;
    auto& sock = it->second;
    if (sock->state == SocketState::Closed) return SocketErr::EBadF;
    if (sock->owner.lock() != caller) return SocketErr::ESecurity;

    // Real plain LPC-initiated socket_close(fd) (flags == 0): no
    // close_callback fires -- see SocketRegistry.hpp's own comment.
    g_sockets.erase(it);
    return SocketErr::Success;
}

void SocketRegistry::closeAllOwnedBy(const std::shared_ptr<LpcObject>& owner) {
    if (!owner) return;
    for (auto it = g_sockets.begin(); it != g_sockets.end();) {
        if (it->second->owner.lock() == owner && it->second->state != SocketState::Closed) {
            // erase() drops the last shared_ptr reference, running
            // LpcSocket's own destructor (::close(fd)) -- the same real
            // fd-close effect close() above gets from this identical
            // erase(), no separate syscall needed here.
            it = g_sockets.erase(it);
        } else {
            ++it;
        }
    }
}

std::string SocketRegistry::errorString(int error) {
    int index = -(error + 1);
    if (index < 0 || index >= kErrorStringsCount) {
        return "socket_error: invalid error number";
    }
    return kErrorStrings[index];
}

Value SocketRegistry::statusOne(const LpcSocket& sock) {
    auto arr = std::make_shared<Array>();
    arr->items.push_back(Value(static_cast<int64_t>(sock.handle)));
    arr->items.push_back(Value(std::string(stateName(sock.state))));
    arr->items.push_back(Value(std::string(modeName(sock.mode))));
    arr->items.push_back(Value(sock.localAddr.empty()
        ? std::string("")
        : sock.localAddr + " " + std::to_string(sock.localPort)));
    arr->items.push_back(Value(sock.remoteAddr.empty()
        ? std::string("")
        : sock.remoteAddr + " " + std::to_string(sock.remotePort)));
    if (auto ownerObj = sock.owner.lock()) {
        arr->items.push_back(Value(ownerObj));
    } else {
        arr->items.push_back(Value());
    }
    return Value(arr);
}

std::shared_ptr<LpcSocket> SocketRegistry::find(int handle) {
    auto it = g_sockets.find(handle);
    return it == g_sockets.end() ? nullptr : it->second;
}

std::vector<std::shared_ptr<LpcSocket>> SocketRegistry::all() {
    std::vector<std::shared_ptr<LpcSocket>> result;
    result.reserve(g_sockets.size());
    for (auto& [handle, sock] : g_sockets) result.push_back(sock);
    return result;
}

void SocketRegistry::forceRemove(int handle) {
    g_sockets.erase(handle);
}

int SocketRegistry::beginRelease(int handle, const std::shared_ptr<LpcObject>& ob,
                                  const std::shared_ptr<LpcObject>& caller) {
    auto it = g_sockets.find(handle);
    if (it == g_sockets.end()) return SocketErr::EFdRange;
    auto& sock = it->second;
    if (sock->state == SocketState::Closed) return SocketErr::EBadF;
    if (sock->owner.lock() != caller) return SocketErr::ESecurity;
    if (sock->released) return SocketErr::ESockRlsd;

    sock->released = true;
    sock->releaseTarget = ob;
    return SocketErr::Success;
}

bool SocketRegistry::isReleased(int handle) {
    auto sock = find(handle);
    return sock && sock->released;
}

void SocketRegistry::cancelRelease(int handle) {
    auto sock = find(handle);
    if (!sock) return;
    sock->released = false;
    sock->releaseTarget.reset();
}

int SocketRegistry::acquire(int handle, Value readCallback, Value writeCallback,
                             Value closeCallback, const std::shared_ptr<LpcObject>& caller) {
    auto it = g_sockets.find(handle);
    if (it == g_sockets.end()) return SocketErr::EFdRange;
    auto& sock = it->second;
    if (sock->state == SocketState::Closed) return SocketErr::EBadF;
    if (!sock->released) return SocketErr::ESockNotRlsd;
    if (sock->releaseTarget.lock() != caller) return SocketErr::ESecurity;

    sock->released = false;
    sock->owner = caller;
    sock->releaseTarget.reset();
    sock->readCallback = std::move(readCallback);
    sock->writeCallback = std::move(writeCallback);
    sock->closeCallback = std::move(closeCallback);
    return SocketErr::Success;
}

}  // namespace amlp
