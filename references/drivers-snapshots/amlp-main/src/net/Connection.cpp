#include "amlp/net/Connection.hpp"
#include "amlp/net/InteractiveRegistry.hpp"
#include "amlp/object/LpcObject.hpp"
#include <unistd.h>
#include <errno.h>
#include <cstring>

namespace amlp {

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
constexpr unsigned char kTelQualIs = 0;
constexpr unsigned char kTelQualSend = 1;
}  // namespace

// Real new_user() (comm.c): the freshly allocated interactive_t's own
// last_time is set to current_time right at setup, before any object is
// even bound to it -- matched here at Connection construction, the
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
        // object is ever bound to a connection, never cleared again --
        // see LpcObject::wasEverInteractive()'s own comment for why this
        // is a separate, sticky flag from InteractiveRegistry membership.
        boundObject_->setWasEverInteractive(true);
    }
}

void Connection::close() {
    if (boundObject_) {
        // Real remove_interactive() (comm.c): "if (ip->snooped_by) {
        // ip->snooped_by->flags &= ~O_SNOOP; ip->snooped_by = 0; }" --
        // runs unconditionally on every connection close (net death or a
        // destruct()-driven close alike, both routes end up here), not
        // just an explicit snoop(0) call. Once the victim is gone there is
        // nothing left to duplicate output from, so any snooper watching
        // it is unlinked. Deliberately one-directional: the closing
        // object's own outgoing snoop (snooping_, if it was itself acting
        // as a snooper) is untouched here, matching real remove_interactive()
        // exactly -- that side is only ever cleared by an actual destruct
        // (see ObjectManager::destructObject()'s own comment).
        if (auto snooper = boundObject_->snoopedBy().lock()) {
            snooper->setSnooping(std::weak_ptr<LpcObject>());
        }
        boundObject_->setSnoopedBy(std::weak_ptr<LpcObject>());

        InteractiveRegistry::remove(boundObject_);
        boundObject_.reset();
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    closed_ = true;
}

void Connection::send(const std::string& data) {
    if (fd_ < 0) return;
    size_t total = 0;
    while (total < data.size()) {
        ssize_t n = ::write(fd_, data.data() + total, data.size() - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        total += static_cast<size_t>(n);
    }
}

std::vector<std::string> Connection::pollLines() {
    std::vector<std::string> lines;
    if (fd_ < 0) return lines;

    char buf[4096];
    std::string rawChunk;
    for (;;) {
        ssize_t n = ::read(fd_, buf, sizeof(buf));
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

    // Telnet IAC processing (Phase 0.8): strips and acts on every IAC
    // sequence, leaving only plain data bytes -- key invariant this
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
        // the user input is received" -- IAC WONT ECHO fires the moment
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
                        // TS_DATA; ip->text[...] = from[i];" -- a
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
    // ...); break;" (comm.c:811-813) -- a client volunteering WILL TTYPE
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
        unsigned char resp[] = {kIac, kDont, option};
        send(std::string(reinterpret_cast<char*>(resp), sizeof(resp)));
    } else if (kind == TelnetState::Do) {
        if (option == kTelOptEcho) return;
        unsigned char resp[] = {kIac, kWont, option};
        send(std::string(reinterpret_cast<char*>(resp), sizeof(resp)));
    }
    // TS_WONT / TS_DONT: real comm.c sends no reply for any option this
    // task's scope covers (ECHO, NAWS); nothing to do.
}

void Connection::handleSubnegotiation() {
    // Real comm.c: sb_buf[0] is the option byte, payload follows.
    // Confirmed against the real NAWS handler directly ("case
    // TELOPT_NAWS: if (ip->sb_pos >= 5) { push_number(sb_buf[1]<<8 |
    // sb_buf[2]); push_number(sb_buf[3]<<8 | sb_buf[4]); ... }") --
    // RFC 1073's own big-endian 16-bit width then height, exactly.
    if (sbBuffer_.empty()) return;
    unsigned char sbOption = static_cast<unsigned char>(sbBuffer_[0]);

    if (sbOption == kTelOptTtype) {
        // Real "case TELOPT_TTYPE: if (!ip->sb_buf[1]) { copy_and_push_
        // string(ip->sb_buf + 2); apply(APPLY_TERMINAL_TYPE, ...); }"
        // (comm.c:1078-1083) -- sb_buf[1] is the TELQUAL byte, only a
        // TELQUAL_IS(0) response (never a client echoing TELQUAL_SEND(1)
        // back) carries an actual terminal-type string starting at index 2.
        if (sbBuffer_.size() >= 2 && static_cast<unsigned char>(sbBuffer_[1]) == kTelQualIs) {
            terminalType_ = sbBuffer_.substr(2);
            // Real comm.c fires APPLY_TERMINAL_TYPE every time this
            // branch runs, not just on a changed value -- matched here
            // via the same one-shot-flag shape windowSizeUpdated_ below
            // already uses for its own real "fires every time" apply.
            terminalTypeUpdated_ = true;
        }
        return;
    }

    if (sbOption != kTelOptNaws) return;
    if (sbBuffer_.size() < 5) return;
    auto byteAt = [&](size_t i) { return static_cast<unsigned char>(sbBuffer_[i]); };
    terminalWidth_ = (byteAt(1) << 8) | byteAt(2);
    terminalHeight_ = (byteAt(3) << 8) | byteAt(4);
    // Real comm.c fires APPLY_WINDOW_SIZE every time this branch runs,
    // not just when the values actually differ from before -- matched
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

} // namespace amlp
