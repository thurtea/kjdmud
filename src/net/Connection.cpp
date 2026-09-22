#include "kjdmud/net/Connection.hpp"
#include "kjdmud/net/InteractiveRegistry.hpp"
#include "kjdmud/object/LpcObject.hpp"
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <poll.h>
#include <cstdint>
#include <cctype>

namespace kjdmud {

namespace {
// Real telnet.h constants (fluffos-2.9-ds2.08), confirmed directly, not
// assumed from general telnet/RFC knowledge alone.
constexpr unsigned char kIac = 255;
constexpr unsigned char kDont = 254;
constexpr unsigned char kDo = 253;
constexpr unsigned char kWont = 252;
constexpr unsigned char kWill = 251;
constexpr unsigned char kSb = 250;
constexpr unsigned char kSe = 240;
constexpr unsigned char kTelOptEcho = 1;
constexpr unsigned char kTelOptTtype = 24;
constexpr unsigned char kTelOptNaws = 31;
constexpr unsigned char kTelOptGmcp = 201;
// Real client-facing MSSP telnet option code (public MSSP spec, not
// FluffOS/LDMud/DGD-specific; see MsspHandler.hpp's own comment).
constexpr unsigned char kTelOptMssp = 70;
// Real client-facing MSDP telnet option code (public MSDP spec, same
// provenance note as kTelOptMssp above: not FluffOS/LDMud/DGD-specific).
constexpr unsigned char kTelOptMsdp = 69;
// Real client-facing MXP telnet option code (public MXP spec, same
// provenance note as kTelOptMssp/kTelOptMsdp above).
constexpr unsigned char kTelOptMxp = 91;
// Real telnet option code for MSP, confirmed directly against current
// FluffOS source: src/net/telnet.h "#define TELNET_TELOPT_MSP 90".
constexpr unsigned char kTelOptMsp = 90;
// Real telnet option code for ZMP, confirmed directly against current
// FluffOS source: src/thirdparty/libtelnet/libtelnet.h
// "#define TELNET_TELOPT_ZMP 93".
constexpr unsigned char kTelOptZmp = 93;
constexpr unsigned char kTelQualIs = 0;
constexpr unsigned char kTelQualSend = 1;

const char* kWsMagic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

std::string sha1Base64(const std::string& input) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return {};
    if (EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, input.data(), input.size()) != 1 ||
        EVP_DigestFinal_ex(ctx, hash, &hashLen) != 1) {
        EVP_MD_CTX_free(ctx);
        return {};
    }
    EVP_MD_CTX_free(ctx);

    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* mem = BIO_new(BIO_s_mem());
    if (!b64 || !mem) {
        BIO_free_all(b64);
        BIO_free(mem);
        return {};
    }
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_push(b64, mem);
    BIO_write(b64, hash, static_cast<int>(hashLen));
    BIO_flush(b64);
    BUF_MEM* buf = nullptr;
    BIO_get_mem_ptr(b64, &buf);
    std::string out = (buf && buf->data) ? std::string(buf->data, buf->length) : std::string();
    BIO_free_all(b64);
    return out;
}

std::string headerValue(const std::string& headers, const std::string& name) {
    std::string lowerHeaders = headers;
    std::string lowerName = name;
    for (char& c : lowerHeaders) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char& c : lowerName) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    size_t pos = lowerHeaders.find(lowerName + ":");
    if (pos == std::string::npos) return {};
    pos = headers.find(':', pos);
    if (pos == std::string::npos) return {};
    ++pos;
    while (pos < headers.size() && (headers[pos] == ' ' || headers[pos] == '\t')) ++pos;
    size_t end = headers.find('\r', pos);
    if (end == std::string::npos) end = headers.find('\n', pos);
    if (end == std::string::npos) end = headers.size();
    return headers.substr(pos, end - pos);
}
}  // namespace

// Real new_user() (comm.c): the freshly allocated interactive_t's own
// last_time is set to current_time right at setup, before any object is
// even bound to it. Matched here at Connection construction, the
// closest equivalent this driver has.
Connection::Connection(int fd) : fd_(fd), lastActivityTime_(std::time(nullptr)) {}

Connection::~Connection() {
    close();
}

void Connection::attach(std::shared_ptr<LpcObject> obj) {
    if (boundObject_) InteractiveRegistry::remove(boundObject_);
    boundObject_ = std::move(obj);
    if (boundObject_) {
        InteractiveRegistry::add(boundObject_, this);
        // Real O_ONCE_INTERACTIVE (object.h): set the first time an
        // object is ever bound to a connection, never cleared again.
        // See LpcObject::wasEverInteractive()'s own comment for why this
        // is a separate, sticky flag from InteractiveRegistry membership.
        boundObject_->setWasEverInteractive(true);
    }
}

void Connection::close() {
    if (boundObject_) {
        // Real remove_interactive() (comm.c): "if (ip->snooped_by) {
        // ip->snooped_by->flags &= ~O_SNOOP; ip->snooped_by = 0; }".
        // Runs unconditionally on every connection close (net death or a
        // destruct()-driven close alike, both routes end up here), not
        // just an explicit snoop(0) call. Once the victim is gone there is
        // nothing left to duplicate output from, so any snooper watching
        // it is unlinked. Deliberately one-directional: the closing
        // object's own outgoing snoop (snooping_, if it was itself acting
        // as a snooper) is untouched here, matching real remove_interactive()
        // exactly. That side is only ever cleared by an actual destruct
        // (see ObjectManager::destructObject()'s own comment).
        if (auto snooper = boundObject_->snoopedBy().lock()) {
            snooper->setSnooping(std::weak_ptr<LpcObject>());
        }
        boundObject_->setSnoopedBy(std::weak_ptr<LpcObject>());

        InteractiveRegistry::remove(boundObject_);
        boundObject_.reset();
    }
    if (ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    closed_ = true;
}

ssize_t Connection::rawRead(char* buf, size_t n) {
    if (ssl_) {
        int rc = SSL_read(ssl_, buf, static_cast<int>(n));
        if (rc > 0) return rc;
        int err = SSL_get_error(ssl_, rc);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            errno = EAGAIN;
            return -1;
        }
        if (err == SSL_ERROR_ZERO_RETURN) return 0;
        errno = EIO;
        return -1;
    }
    return ::read(fd_, buf, n);
}

ssize_t Connection::rawWrite(const char* buf, size_t n) {
    if (ssl_) {
        int rc = SSL_write(ssl_, buf, static_cast<int>(n));
        if (rc > 0) return rc;
        int err = SSL_get_error(ssl_, rc);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            errno = EAGAIN;
            return -1;
        }
        errno = EIO;
        return -1;
    }
    return ::write(fd_, buf, n);
}

bool Connection::acceptTls(SSL_CTX* ctx) {
    if (!ctx || fd_ < 0) return false;
    ssl_ = SSL_new(ctx);
    if (!ssl_) return false;
    SSL_set_fd(ssl_, fd_);
    SSL_set_accept_state(ssl_);
    for (int attempt = 0; attempt < 200; ++attempt) {
        int rc = SSL_accept(ssl_);
        if (rc == 1) return true;
        int err = SSL_get_error(ssl_, rc);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            SSL_free(ssl_);
            ssl_ = nullptr;
            return false;
        }
        pollfd pfd{fd_, static_cast<short>(err == SSL_ERROR_WANT_WRITE ? POLLOUT : POLLIN), 0};
        if (::poll(&pfd, 1, 50) < 0 && errno != EINTR) {
            SSL_free(ssl_);
            ssl_ = nullptr;
            return false;
        }
    }
    SSL_free(ssl_);
    ssl_ = nullptr;
    return false;
}

void Connection::send(const std::string& data) {
    if (fd_ < 0) return;
    // Hold writes until the 101 is on the wire. A browser rejects the
    // upgrade if the MOTD/banner arrives as raw text first.
    if (useWebSocket_ && !wsHandshakeDone_) {
        wsPendingSend_ += data;
        return;
    }
    std::string wire = data;
    if (useWebSocket_ && wsHandshakeDone_) {
        wire = encodeWsFrame(data);
    }
    size_t total = 0;
    while (total < wire.size()) {
        ssize_t n = rawWrite(wire.data() + total, wire.size() - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        total += static_cast<size_t>(n);
    }
}

void Connection::sendGmcp(const std::string& package) {
    unsigned char prefix[] = {kIac, kSb, kTelOptGmcp};
    unsigned char suffix[] = {kIac, kSe};
    send(std::string(reinterpret_cast<char*>(prefix), sizeof(prefix)) +
         package +
         std::string(reinterpret_cast<char*>(suffix), sizeof(suffix)));
}

namespace {
// Real MSSP wire constants (public spec): MSSP_VAR/MSSP_VAL are raw
// bytes 1 and 2, not the ASCII characters '1'/'2'.
constexpr unsigned char kMsspVar = 1;
constexpr unsigned char kMsspVal = 2;

void appendMsspVar(std::string& out, const std::string& name, const std::string& value) {
    out += static_cast<char>(kMsspVar);
    out += name;
    out += static_cast<char>(kMsspVal);
    out += value;
}
} // namespace

void Connection::sendMssp(const std::string& mudName, int playerCount, int uptimeSeconds) {
    std::string out;
    out += static_cast<char>(kIac);
    out += static_cast<char>(kSb);
    out += static_cast<char>(kTelOptMssp);
    appendMsspVar(out, "NAME", mudName);
    appendMsspVar(out, "PLAYERS", std::to_string(playerCount));
    appendMsspVar(out, "UPTIME", std::to_string(uptimeSeconds));
    appendMsspVar(out, "CODEBASE", "kjdmud");
    out += static_cast<char>(kIac);
    out += static_cast<char>(kSe);
    send(out);
}

namespace {
// Real MSDP wire constants (public spec, tintin.mudhalla.net/protocols/
// msdp): MSDP_VAR/MSDP_VAL are raw bytes 1 and 2, same numeric values as
// MSSP_VAR/MSSP_VAL above by coincidence of the two independent specs,
// not a shared constant - each is scoped inside its own telnet option's
// subnegotiation (69 here, 70 above), so there is no wire collision.
// MSDP_TABLE_OPEN/CLOSE (3/4) and MSDP_ARRAY_OPEN/CLOSE (5/6) are real
// spec bytes for structured values; out of scope for this driver's v1
// (see Connection.hpp's own sendMsdp() comment).
constexpr unsigned char kMsdpVar = 1;
constexpr unsigned char kMsdpVal = 2;
} // namespace

void Connection::sendMsdp(const std::string& varName, const std::string& value) {
    std::string out;
    out += static_cast<char>(kIac);
    out += static_cast<char>(kSb);
    out += static_cast<char>(kTelOptMsdp);
    out += static_cast<char>(kMsdpVar);
    out += varName;
    out += static_cast<char>(kMsdpVal);
    out += value;
    out += static_cast<char>(kIac);
    out += static_cast<char>(kSe);
    send(out);
}

namespace {
// Real telnet_send() (src/thirdparty/libtelnet/libtelnet.c), used by
// telnet_subnegotiation() for the payload portion of every real
// subnegotiation send: doubles any literal IAC (0xFF) byte found before
// it hits the wire, or a client parsing the subnegotiation would read
// that lone 0xFF as the start of a new telnet command and truncate the
// payload right there. Shared here for both sendMspOob() and sendZmp()
// below, the two sendX() methods in this file whose payload is fully
// arbitrary mudlib-authored content (unlike sendMssp()'s/sendMsdp()'s
// own driver-composed ASCII payloads elsewhere in this file, which
// cannot contain 0xFF in practice - see this row's own STATUS.md entry
// for why those two, plus sendGmcp(), are flagged as a known gap rather
// than fixed here too).
void appendIacEscaped(std::string& out, const std::string& data) {
    for (unsigned char c : data) {
        out += static_cast<char>(c);
        if (c == kIac) out += static_cast<char>(kIac);
    }
}
} // namespace

void Connection::sendMspOob(const std::string& payload) {
    // Real telnet_send_msp_oob() (src/net/msp.cc): "if (ip->iflags &
    // USING_MSP) { telnet_subnegotiation(...); }", silently doing
    // nothing otherwise. No return value either way in the real efun
    // wrapping this (f_telnet_msp_oob), so a silent no-op matches.
    if (!mspEnabled_) return;
    std::string out;
    out += static_cast<char>(kIac);
    out += static_cast<char>(kSb);
    out += static_cast<char>(kTelOptMsp);
    appendIacEscaped(out, payload);
    out += static_cast<char>(kIac);
    out += static_cast<char>(kSe);
    send(out);
}

void Connection::sendZmp(const std::string& command, const std::vector<std::string>& args) {
    // Real f_send_zmp() (src/packages/core/telnet_ext.cc) only checks
    // "ip && ip->telnet" (i.e. the connection is interactive at all),
    // not USING_ZMP - real ZMP has no enabled-flag guard on the send
    // side the way MSP's telnet_send_msp_oob() has (see that method's
    // own comment). Matched here deliberately: unlike sendMspOob()'s
    // real guard, there is no verified "if (ip->iflags & USING_ZMP)"
    // check to port for the outgoing side.
    std::string out;
    out += static_cast<char>(kIac);
    out += static_cast<char>(kSb);
    out += static_cast<char>(kTelOptZmp);
    // Real telnet_begin_zmp()+telnet_zmp_arg() (libtelnet.c): every
    // field, the command included, is escaped via telnet_send() (see
    // appendIacEscaped()'s own comment) then NUL-terminated on the wire
    // ("strlen(arg) + 1" bytes sent per field). Real f_send_zmp() skips
    // any non-string array element outright rather than erroring; the
    // efun wrapping this already filters those before calling here (see
    // NetEfuns.cpp's own send_zmp registration), so this method itself
    // trusts args to already be all-string.
    appendIacEscaped(out, command);
    out += '\0';
    for (const auto& arg : args) {
        appendIacEscaped(out, arg);
        out += '\0';
    }
    out += static_cast<char>(kIac);
    out += static_cast<char>(kSe);
    send(out);
}

std::string Connection::encodeWsFrame(const std::string& payload) const {
    std::string frame;
    frame.push_back(static_cast<char>(0x81));
    size_t len = payload.size();
    if (len <= 125) {
        frame.push_back(static_cast<char>(len));
    } else if (len <= 0xffff) {
        frame.push_back(126);
        frame.push_back(static_cast<char>((len >> 8) & 0xff));
        frame.push_back(static_cast<char>(len & 0xff));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<char>((len >> (i * 8)) & 0xff));
        }
    }
    frame += payload;
    return frame;
}

std::string Connection::decodeWsFrames(std::string incoming) {
    wsFrameBuffer_ += incoming;
    std::string payload;
    while (wsFrameBuffer_.size() >= 2) {
        unsigned char b0 = static_cast<unsigned char>(wsFrameBuffer_[0]);
        unsigned char b1 = static_cast<unsigned char>(wsFrameBuffer_[1]);
        unsigned char opcode = b0 & 0x0f;
        bool masked = (b1 & 0x80) != 0;
        uint64_t len = b1 & 0x7f;
        size_t header = 2;
        if (len == 126) {
            if (wsFrameBuffer_.size() < 4) break;
            len = (static_cast<unsigned char>(wsFrameBuffer_[2]) << 8) |
                  static_cast<unsigned char>(wsFrameBuffer_[3]);
            header = 4;
        } else if (len == 127) {
            if (wsFrameBuffer_.size() < 10) break;
            len = 0;
            for (int i = 0; i < 8; ++i) {
                len = (len << 8) | static_cast<unsigned char>(wsFrameBuffer_[2 + i]);
            }
            header = 10;
        }
        size_t maskOff = header;
        if (masked) header += 4;
        if (wsFrameBuffer_.size() < header + len) break;
        std::string data = wsFrameBuffer_.substr(header, static_cast<size_t>(len));
        if (masked) {
            unsigned char mask[4];
            for (int i = 0; i < 4; ++i) {
                mask[i] = static_cast<unsigned char>(wsFrameBuffer_[maskOff + i]);
            }
            for (size_t i = 0; i < data.size(); ++i) {
                data[i] = static_cast<char>(static_cast<unsigned char>(data[i]) ^ mask[i % 4]);
            }
        }
        wsFrameBuffer_.erase(0, header + static_cast<size_t>(len));
        if (opcode == 0x8) {
            closed_ = true;
            break;
        }
        if (opcode == 0x9) {
            std::string pong;
            pong.push_back(static_cast<char>(0x8a));
            pong.push_back(static_cast<char>(data.size() <= 125 ? data.size() : 0));
            if (data.size() <= 125) pong += data;
            rawWrite(pong.data(), pong.size());
            continue;
        }
        if (opcode == 0x1 || opcode == 0x2 || opcode == 0x0) payload += data;
    }
    return payload;
}

bool Connection::tryCompleteWebSocketHandshake() {
    size_t end = wsHttpBuffer_.find("\r\n\r\n");
    if (end == std::string::npos) return false;
    std::string headers = wsHttpBuffer_.substr(0, end);
    std::string leftover = wsHttpBuffer_.substr(end + 4);
    wsHttpBuffer_.clear();
    std::string key = headerValue(headers, "Sec-WebSocket-Key");
    if (key.empty()) {
        closed_ = true;
        return false;
    }
    std::string accept = sha1Base64(key + kWsMagic);
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
    size_t total = 0;
    while (total < response.size()) {
        ssize_t n = rawWrite(response.data() + total, response.size() - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            closed_ = true;
            return false;
        }
        total += static_cast<size_t>(n);
    }
    wsHandshakeDone_ = true;
    if (!wsPendingSend_.empty()) {
        std::string pending = std::move(wsPendingSend_);
        wsPendingSend_.clear();
        send(pending);
    }
    if (!leftover.empty()) {
        inputBuffer_ += decodeWsFrames(leftover);
    }
    return true;
}

std::vector<std::string> Connection::pollLines() {
    std::vector<std::string> lines;
    if (fd_ < 0) return lines;

    char buf[4096];
    std::string rawChunk;
    for (;;) {
        ssize_t n = rawRead(buf, sizeof(buf));
        if (n > 0) {
            rawChunk.append(buf, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            closed_ = true;
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        closed_ = true;
        break;
    }

    if (useWebSocket_ && !wsHandshakeDone_) {
        wsHttpBuffer_ += rawChunk;
        if (!tryCompleteWebSocketHandshake()) return lines;
        rawChunk.clear();
    } else if (useWebSocket_ && wsHandshakeDone_ && !rawChunk.empty()) {
        rawChunk = decodeWsFrames(rawChunk);
    }

    // Telnet IAC processing (Phase 0.8): strips and acts on every IAC
    // sequence, leaving only plain data bytes. Key invariant this
    // driver already documents (net/instruct.md): IAC bytes must never
    // reach dispatchLine(). Done here, before line-splitting, not after,
    // so a "\xff\xf9" or similar sequence can never be mistaken for
    // literal text containing a newline.
    processTelnetBytes(rawChunk, inputBuffer_);

    size_t pos;
    while ((pos = inputBuffer_.find('\n')) != std::string::npos) {
        std::string line = inputBuffer_.substr(0, pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        inputBuffer_.erase(0, pos + 1);

        // Real get_user_command() (comm.c): "must not enable echo before
        // the user input is received". IAC WONT ECHO fires the moment
        // a full line is actually pulled off the buffer, not when the
        // registered input_to() callback later finishes running.
        if (echoSuppressed_) {
            echoSuppressed_ = false;
            unsigned char resp[] = {kIac, kWont, kTelOptEcho};
            send(std::string(reinterpret_cast<char*>(resp), sizeof(resp)));
        }
    }

    return lines;
}

void Connection::suppressEcho() {
    if (echoSuppressed_) return;
    echoSuppressed_ = true;
    unsigned char resp[] = {kIac, kWill, kTelOptEcho};
    send(std::string(reinterpret_cast<char*>(resp), sizeof(resp)));
}

void Connection::requestWindowSize() {
    unsigned char req[] = {kIac, kDo, kTelOptNaws};
    send(std::string(reinterpret_cast<char*>(req), sizeof(req)));
}

void Connection::requestTerminalType() {
    // Real telnet_term_query[] (comm.c): IAC SB TTYPE SEND IAC SE.
    unsigned char req[] = {kIac, kSb, kTelOptTtype, kTelQualSend, kIac, kSe};
    send(std::string(reinterpret_cast<char*>(req), sizeof(req)));
}

void Connection::startRequestTerminalType() {
    // Real telnet_do_ttype[] (comm.c): IAC DO TTYPE.
    unsigned char req[] = {kIac, kDo, kTelOptTtype};
    send(std::string(reinterpret_cast<char*>(req), sizeof(req)));
}

void Connection::processTelnetBytes(const std::string& raw, std::string& plainOut) {
    for (unsigned char b : raw) {
        switch (telnetState_) {
            case TelnetState::Data:
                if (b == kIac) {
                    telnetState_ = TelnetState::Iac;
                } else {
                    plainOut.push_back(static_cast<char>(b));
                }
                break;

            case TelnetState::Iac:
                switch (b) {
                    case kIac:
                        // Real TS_IAC's own "case IAC: ip->state =
                        // TS_DATA; ip->text[...] = from[i];". a
                        // doubled IAC is an escaped literal 0xFF data
                        // byte, not the start of a new command.
                        plainOut.push_back(static_cast<char>(kIac));
                        telnetState_ = TelnetState::Data;
                        break;
                    case kWill: telnetState_ = TelnetState::Will; break;
                    case kWont: telnetState_ = TelnetState::Wont; break;
                    case kDo: telnetState_ = TelnetState::Do; break;
                    case kDont: telnetState_ = TelnetState::Dont; break;
                    case kSb:
                        telnetState_ = TelnetState::Sb;
                        sbBuffer_.clear();
                        break;
                    default:
                        // Real TS_IAC's own default branch: other
                        // two-byte commands (GA, NOP, AYT, etc) outside
                        // this task's scope just return to TS_DATA.
                        telnetState_ = TelnetState::Data;
                        break;
                }
                break;

            case TelnetState::Will:
                handleNegotiation(TelnetState::Will, b);
                telnetState_ = TelnetState::Data;
                break;

            case TelnetState::Wont:
                handleNegotiation(TelnetState::Wont, b);
                telnetState_ = TelnetState::Data;
                break;

            case TelnetState::Do:
                handleNegotiation(TelnetState::Do, b);
                telnetState_ = TelnetState::Data;
                break;

            case TelnetState::Dont:
                handleNegotiation(TelnetState::Dont, b);
                telnetState_ = TelnetState::Data;
                break;

            case TelnetState::Sb:
                if (b == kIac) {
                    telnetState_ = TelnetState::SbIac;
                } else {
                    sbBuffer_.push_back(static_cast<char>(b));
                }
                break;

            case TelnetState::SbIac:
                if (b == kSe) {
                    handleSubnegotiation();
                    telnetState_ = TelnetState::Data;
                } else if (b == kIac) {
                    // Escaped literal 0xFF inside subnegotiation data.
                    sbBuffer_.push_back(static_cast<char>(kIac));
                    telnetState_ = TelnetState::Sb;
                } else {
                    // Real comm.c's own comment on this exact case (the
                    // RFCs are not clear on what to do here either):
                    // safest is to abandon the subnegotiation.
                    telnetState_ = TelnetState::Data;
                }
                break;
        }
    }
}

void Connection::handleNegotiation(TelnetState kind, unsigned char option) {
    // Real TS_WILL/TS_DO (comm.c): ECHO and NAWS are both silently
    // accepted with no reply ("do nothing, but don't send a dont/wont
    // response"); anything else this driver does not support is
    // actively refused, matching the real default branches exactly
    // rather than staying silent (a silent non-response can leave a
    // strict telnet client's own negotiation state machine hanging).
    // TTYPE is a real, separate real TS_WILL case, not a silent accept:
    // "case TELOPT_TTYPE: add_binary_message(ip->ob, telnet_term_query,
    // ...); break;" (comm.c:811-813). A client volunteering WILL TTYPE
    // unprompted (this driver does not always send DO TTYPE first, e.g.
    // a raw socketpair test harness with no Server::onNewConnection() run)
    // is answered with the same SB TTYPE SEND probe immediately, not the
    // bare silent accept ECHO/NAWS get. Before this, WILL TTYPE fell
    // through to the default branch below and was wrongly refused with
    // IAC DONT TTYPE, a real behavioral bug relative to comm.c.
    if (kind == TelnetState::Will) {
        if (option == kTelOptEcho || option == kTelOptNaws) return;
        if (option == kTelOptTtype) {
            requestTerminalType();
            return;
        }
        if (option == kTelOptGmcp) {
            unsigned char resp[] = {kIac, kDo, kTelOptGmcp};
            send(std::string(reinterpret_cast<char*>(resp), sizeof(resp)));
            gmcpEnabled_ = true;
            return;
        }
        // A client volunteering "IAC WILL MSDP" unprompted (this driver
        // does not always send its own proactive WILL MSDP first, e.g.
        // a raw socketpair test harness). Same accept-and-enable shape
        // as GMCP just above.
        if (option == kTelOptMsdp) {
            unsigned char resp[] = {kIac, kDo, kTelOptMsdp};
            send(std::string(reinterpret_cast<char*>(resp), sizeof(resp)));
            msdpEnabled_ = true;
            return;
        }
        // A client volunteering "IAC WILL MXP" unprompted, same shape
        // as the MSDP branch just above.
        if (option == kTelOptMxp) {
            unsigned char resp[] = {kIac, kDo, kTelOptMxp};
            send(std::string(reinterpret_cast<char*>(resp), sizeof(resp)));
            mxpEnabled_ = true;
            return;
        }
        // A client volunteering "IAC WILL MSP" unprompted. Real
        // on_telnet_do_msp() (src/net/telnet.cc's on_telnet_do()
        // dispatcher) runs identically whichever side's offer this
        // negotiation actually completes, so this branch and the Do
        // branch below both set the same two flags.
        if (option == kTelOptMsp) {
            unsigned char resp[] = {kIac, kDo, kTelOptMsp};
            send(std::string(reinterpret_cast<char*>(resp), sizeof(resp)));
            mspEnabled_ = true;
            mspEnableNegotiated_ = true;
            return;
        }
        // A client volunteering "IAC WILL ZMP" unprompted, same shape
        // as GMCP's own branch above (a plain enabled flag, no apply
        // fired here - see Connection.hpp's own zmpEnabled() comment
        // for why ZMP differs from MSP on that point).
        if (option == kTelOptZmp) {
            unsigned char resp[] = {kIac, kDo, kTelOptZmp};
            send(std::string(reinterpret_cast<char*>(resp), sizeof(resp)));
            zmpEnabled_ = true;
            return;
        }
        unsigned char resp[] = {kIac, kDont, option};
        send(std::string(reinterpret_cast<char*>(resp), sizeof(resp)));
    } else if (kind == TelnetState::Do) {
        if (option == kTelOptEcho) return;
        if (option == kTelOptGmcp) {
            gmcpEnabled_ = true;
            return;
        }
        if (option == kTelOptMssp) {
            msspNegotiated_ = true;
            return;
        }
        // The client's "IAC DO MSDP" reply to this driver's own
        // proactive "IAC WILL MSDP" (Server::onNewConnection(),
        // handleConnection()'s WebSocket parity path). Unlike MSSP's
        // one-shot flag, MSDP has no single fixed data block to send
        // back here - it just enables sendMsdp()/incoming var parsing
        // from here on, same as the GMCP Do-branch just above.
        if (option == kTelOptMsdp) {
            msdpEnabled_ = true;
            return;
        }
        // The client's "IAC DO MXP" reply to this driver's own
        // proactive "IAC WILL MXP" offer, same shape as the MSDP branch
        // just above.
        if (option == kTelOptMxp) {
            mxpEnabled_ = true;
            return;
        }
        // The client's "IAC DO MSP" reply to this driver's own
        // proactive "IAC WILL MSP" offer, same shape as the MXP branch
        // just above.
        if (option == kTelOptMsp) {
            mspEnabled_ = true;
            mspEnableNegotiated_ = true;
            return;
        }
        // The client's "IAC DO ZMP" reply to this driver's own
        // proactive "IAC WILL ZMP" offer, same shape as the MSP branch
        // just above.
        if (option == kTelOptZmp) {
            zmpEnabled_ = true;
            return;
        }
        unsigned char resp[] = {kIac, kWont, option};
        send(std::string(reinterpret_cast<char*>(resp), sizeof(resp)));
    }
    // TS_WONT / TS_DONT: real comm.c sends no reply for any option this
    // task's scope covers (ECHO, NAWS); ZMP's own real on_telnet_dont()
    // has no case for it either (falls to the generic "log only, no
    // action" default there), so no special handling is added here.
}

void Connection::handleSubnegotiation() {
    // Real comm.c: sb_buf[0] is the option byte, payload follows.
    // Confirmed against the real NAWS handler directly ("case
    // TELOPT_NAWS: if (ip->sb_pos >= 5) { push_number(sb_buf[1]<<8 |
    // sb_buf[2]); push_number(sb_buf[3]<<8 | sb_buf[4]); ... }").
    // RFC 1073's own big-endian 16-bit width then height, exactly.
    if (sbBuffer_.empty()) return;
    unsigned char sbOption = static_cast<unsigned char>(sbBuffer_[0]);

    if (sbOption == kTelOptTtype) {
        // Real "case TELOPT_TTYPE: if (!ip->sb_buf[1]) { copy_and_push_
        // string(ip->sb_buf + 2); apply(APPLY_TERMINAL_TYPE, ...); }"
        // (comm.c:1078-1083). sb_buf[1] is the TELQUAL byte, only a
        // TELQUAL_IS(0) response (never a client echoing TELQUAL_SEND(1)
        // back) carries an actual terminal-type string starting at index 2.
        if (sbBuffer_.size() >= 2 && static_cast<unsigned char>(sbBuffer_[1]) == kTelQualIs) {
            terminalType_ = sbBuffer_.substr(2);
            // Real comm.c fires APPLY_TERMINAL_TYPE every time this
            // branch runs, not just on a changed value. Matched here
            // via the same one-shot-flag shape windowSizeUpdated_ below
            // already uses for its own real "fires every time" apply.
            terminalTypeUpdated_ = true;
        }
        return;
    }

    if (sbOption == kTelOptGmcp) {
        if (sbBuffer_.size() > 1) incomingGmcp_.push_back(sbBuffer_.substr(1));
        gmcpEnabled_ = true;
        return;
    }

    if (sbOption == kTelOptMsdp) {
        // v1 scope (see Connection.hpp's own sendMsdp() comment): a
        // single "MSDP_VAR name MSDP_VAL value" pair per subnegotiation,
        // no MSDP_TABLE/MSDP_ARRAY nesting. A real client's REPORT/LIST/
        // RESET requests arrive in exactly this shape (MSDP_VAR "REPORT"
        // MSDP_VAL "<var name>"), so this already covers them; the
        // mudlib apply this feeds (Server::handleConnection()) decides
        // what, if anything, to do with a given var/val pair.
        if (sbBuffer_.size() > 1 && static_cast<unsigned char>(sbBuffer_[1]) == kMsdpVar) {
            size_t valPos = sbBuffer_.find(static_cast<char>(kMsdpVal), 2);
            std::string name = (valPos == std::string::npos)
                ? sbBuffer_.substr(2)
                : sbBuffer_.substr(2, valPos - 2);
            std::string value = (valPos == std::string::npos)
                ? std::string()
                : sbBuffer_.substr(valPos + 1);
            incomingMsdp_.emplace_back(std::move(name), std::move(value));
        }
        msdpEnabled_ = true;
        return;
    }

    if (sbOption == kTelOptZmp) {
        // Real _zmp_telnet() (libtelnet.c): the payload must be
        // non-empty and end in a NUL byte or it is rejected outright as
        // an incomplete frame (no event fired at all, not a partial
        // one) - confirmed directly, not assumed. Matched here: a
        // malformed payload is silently dropped, same as GMCP/MSDP's
        // sibling branches drop anything they cannot make sense of.
        // Fields split on NUL: argv[0] is the command, argv[1..] are
        // the arguments, both already IAC-unescaped and NUL-safe by the
        // time they reach sbBuffer_ (see processTelnetBytes()'s own
        // SbIac-state comment; std::string::push_back() there already
        // preserves embedded 0x00 bytes correctly).
        std::string payload = sbBuffer_.substr(1);
        if (!payload.empty() && payload.back() == '\0') {
            std::vector<std::string> fields;
            size_t start = 0;
            while (start < payload.size()) {
                size_t nul = payload.find('\0', start);
                fields.push_back(payload.substr(start, nul - start));
                start = nul + 1;
            }
            if (!fields.empty()) {
                std::string command = std::move(fields.front());
                std::vector<std::string> args(fields.begin() + 1, fields.end());
                incomingZmp_.emplace_back(std::move(command), std::move(args));
            }
        }
        zmpEnabled_ = true;
        return;
    }

    if (sbOption != kTelOptNaws) return;
    if (sbBuffer_.size() < 5) return;
    auto byteAt = [&](size_t i) { return static_cast<unsigned char>(sbBuffer_[i]); };
    terminalWidth_ = (byteAt(1) << 8) | byteAt(2);
    terminalHeight_ = (byteAt(3) << 8) | byteAt(4);
    // Real comm.c fires APPLY_WINDOW_SIZE every time this branch runs,
    // not just when the values actually differ from before. matched
    // here via a plain one-shot flag Server::handleConnection() consumes.
    windowSizeUpdated_ = true;
}

void Connection::setPendingInputTo(std::shared_ptr<LpcObject> obj, Value function,
                                    std::vector<Value> extraArgs) {
    pendingInputTo_ = PendingInputTo{std::move(obj), std::move(function), std::move(extraArgs)};
}

std::optional<PendingInputTo> Connection::takePendingInputTo() {
    std::optional<PendingInputTo> result = std::move(pendingInputTo_);
    pendingInputTo_.reset();
    return result;
}

} // namespace kjdmud
