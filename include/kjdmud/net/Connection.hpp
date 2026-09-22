#pragma once
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "kjdmud/vm/Value.hpp"

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

namespace kjdmud {

class LpcObject;

// The per-connection "pending input_to callback" slot. Real FluffOS's
// interactive_t::input_to (a "sentence_t", see comm.c) reduced to just
// what this driver needs: which object registered it (simulate.c's
// input_to(): "s->ob = current_object", a weak_ptr since the object can
// be destructed out from under a still-pending registration, matching
// real FluffOS's own O_DESTRUCTED check in call_function_interactive()),
// which function to call, and any extra arguments captured at
// registration time (simulate.c's "command_giver->interactive->carryover").
struct PendingInputTo {
    std::weak_ptr<LpcObject> object;
    // string|function, real simulate.c's own input_to() accepting either
    // a function name or a closure/function pointer as its first
    // argument. The same two-shape Value already used for
    // notify_fail()'s own pending message/closure just below (see that
    // struct's own comment) and for socket callbacks
    // (Server.cpp's fireSocketCallback()). Real corpus: Dead Souls
    // 3.8.2's own installer, secure/lib/connect.first.c's own
    // "input_to((: InputName :), I_NOESC);", found live continuing the
    // same boot-then-live-verification session. This field used to be
    // a plain std::string, rejecting every closure-form call outright.
    Value function;
    std::vector<Value> extraArgs;
};

// The per-connection "pending notify_fail message" slot. real
// FluffOS's interactive_t::default_err_message (a "union string_or_func",
// comm.h), set by notify_fail(string|function) and consulted only if
// command dispatch for the current input line ends with no add_action
// handler ultimately claiming it (real notify_no_command(), add_action.c
// See Server::dispatchLine()'s own wiring for exactly where this
// driver mirrors that). A plain Value already covers both real forms (a
// string, or a function/Closure) without needing a bespoke union the
// way real FluffOS does.

class Connection {
public:
    explicit Connection(int fd);
    ~Connection();

    // TLS handshake on this fd. ctx is owned by Server.
    bool acceptTls(SSL_CTX* ctx);
    bool isTls() const { return ssl_ != nullptr; }

    // After this, the first inbound bytes are an HTTP upgrade. Once
    // the handshake finishes, send()/pollLines() speak RFC 6455 frames.
    void enableWebSocket() { useWebSocket_ = true; }
    bool isWebSocket() const { return useWebSocket_; }
    bool transportReady() const { return !useWebSocket_ || wsHandshakeDone_; }
    bool takeTelnetOfferNeeded() {
        if (!useWebSocket_ || !wsHandshakeDone_ || telnetOffered_) return false;
        telnetOffered_ = true;
        return true;
    }

    void setEncoding(std::string encoding) { encoding_ = std::move(encoding); }
    const std::string& encoding() const { return encoding_; }

    bool gmcpEnabled() const { return gmcpEnabled_; }
    void setGmcpEnabled(bool enabled) { gmcpEnabled_ = enabled; }
    void sendGmcp(const std::string& package);

    std::vector<std::string> takeIncomingGmcp() {
        std::vector<std::string> out = std::move(incomingGmcp_);
        incomingGmcp_.clear();
        return out;
    }

    // One-shot flag, same shape as takeWindowSizeUpdate()/
    // takeTerminalTypeUpdate() below: set once by handleNegotiation()
    // when the client replies "IAC DO MSSP" to this driver's own
    // proactive "IAC WILL MSSP" (sent by Server::onNewConnection() for
    // every telnet connection), consumed by Server::handleConnection()
    // to send the one-time MSSP data block (MsspHandler::sendServerInfo()),
    // which needs Config/player-count access this class deliberately
    // does not have.
    bool takeMsspNegotiated() {
        bool had = msspNegotiated_;
        msspNegotiated_ = false;
        return had;
    }

    // MUD Server Status Protocol (telnet option 70/0x46) one-time data
    // block: "IAC SB MSSP MSSP_VAR name MSSP_VAL value ... IAC SE". A
    // public, client-facing telnet protocol (tintin.mudhalla.net/
    // protocols/mssp), not specific to any one real LP driver's own C
    // source; no vendored reference driver is present on disk to cite
    // against for this row either way. First-slice variable set: NAME,
    // PLAYERS, UPTIME, CODEBASE. The real spec has many more optional
    // variables (CONTACT, FAMILY, GENRE, ...); nothing in this driver
    // has a real value to report for any of them yet, so they are left
    // out rather than sent as fabricated placeholders. Lives here
    // rather than in src/proto/ like GmcpHandler: this is a one-way,
    // Server-triggered send with no receiving/parsing side, and
    // src/proto already depends on src/net (for Connection itself), so
    // a Server-called src/proto class would be a circular library
    // dependency.
    void sendMssp(const std::string& mudName, int playerCount, int uptimeSeconds);

    // Mud Server Data Protocol (telnet option 69/0x45). Unlike MSSP,
    // MSDP is bidirectional (a real client sends REPORT/UNREPORT/LIST
    // requests back, and the mudlib can send arbitrary named variables
    // at any time), so it follows GMCP's shape, not MSSP's: a plain
    // enabled flag plus a queued incoming list, both handled here in
    // src/net rather than needing Server-only state the way sendMssp()'s
    // own comment explains for that one-way protocol. v1 scope: single
    // scalar MSDP_VAR/MSDP_VAL pairs only, no MSDP_TABLE/MSDP_ARRAY
    // nesting (real spec section 3) since nothing in this driver has a
    // structured value to report yet; the wire format leaves room to add
    // that later without a shape change here.
    bool msdpEnabled() const { return msdpEnabled_; }
    void setMsdpEnabled(bool enabled) { msdpEnabled_ = enabled; }
    void sendMsdp(const std::string& varName, const std::string& value);

    std::vector<std::pair<std::string, std::string>> takeIncomingMsdp() {
        std::vector<std::pair<std::string, std::string>> out = std::move(incomingMsdp_);
        incomingMsdp_.clear();
        return out;
    }

    // MUD eXtension Protocol (telnet option 91/0x5B), same "not specific
    // to any one real LP driver's own C source" provenance note as
    // sendMssp()/sendMsdp() above: a public client protocol
    // (zuggsoft.com/zmud/mxp.htm), no vendored reference driver present
    // on disk to cite either way. v1 scope, matching src/proto/
    // instruct.md's own sketch: negotiate the enabled flag (both a
    // client-volunteered "IAC WILL MXP" and a reply to this driver's own
    // proactive offer both set it, same two-branch shape GMCP/MSDP
    // already use) and let src/proto/MxpHandler wrap text in the real
    // <B>/<COLOR>/<SEND> elements when enabled, plain text otherwise.
    // No incoming subnegotiation parsing: real MXP defines optional
    // client VERSION/SUPPORT replies over SB 91, but nothing in this
    // driver consumes them, so an incoming SB 91 payload is left to fall
    // through handleSubnegotiation()'s existing unrecognized-option
    // no-op rather than growing a parser with no reader.
    bool mxpEnabled() const { return mxpEnabled_; }
    void setMxpEnabled(bool enabled) { mxpEnabled_ = enabled; }

    // Mud Sound Protocol (telnet option 90/0x5A). Unlike GMCP/MSDP/MSSP/
    // MXP above, this one *is* verified against real, current FluffOS
    // driver source (github.com/fluffos/fluffos, cloned fresh for this
    // row since this repo's own vendored temp/ is absent on this
    // machine): src/net/msp.cc's own on_telnet_do_msp() sets a USING_MSP
    // flag and fires "safe_apply(APPLY_MSP_ENABLE, ip->ob, 0,
    // ORIGIN_DRIVER)" (src/vm/internal/applies: "MSP_ENABLE" with no
    // ":override", so the real LPC-side name is the lowercased
    // "msp_enable") every time negotiation completes, from either
    // direction (src/net/telnet.cc's on_telnet_do()/my_telopts[] both
    // list MSP the same WILL/DO-symmetric way GMCP/MSDP/MSSP/ZMP already
    // are, so a client-volunteered "IAC WILL MSP" and a reply to this
    // driver's own proactive offer both reach that one function). No
    // efun here fires that apply directly (real code does not either);
    // the one-shot flag below lets Server::handleConnection() fire it,
    // the same "needs VM access this class deliberately does not have"
    // shape takeMsspNegotiated() above already uses.
    bool mspEnabled() const { return mspEnabled_; }
    bool takeMspEnableNegotiated() {
        bool had = mspEnableNegotiated_;
        mspEnableNegotiated_ = false;
        return had;
    }

    // Real telnet_send_msp_oob() (src/net/msp.cc): passthrough of an
    // already-composed MSP trigger string (e.g. "!!SOUND(bang.wav
    // V=50)"; the driver imposes no *trigger-grammar* structure on it,
    // matching real code exactly - see the .cpp's own comment for the
    // one thing that is not a raw byte-for-byte passthrough, IAC
    // escaping) as the option-90 subnegotiation payload, silently doing
    // nothing if MSP was never negotiated (real code's own "if (ip->
    // iflags & USING_MSP)" guard). v1 scope stops at this raw
    // passthrough, same as real FluffOS itself: MSP's actual "!!SOUND(...)"/
    // "!!MUSIC(...)" trigger grammar is composed mudlib-side there too
    // (dwlib.spec-style helper functions, not driver code), so there is
    // no real driver-side trigger-builder to port here either.
    void sendMspOob(const std::string& payload);

    // Zenith MUD Protocol (telnet option 93/0x5D). Verified against
    // current FluffOS source (github.com/fluffos/fluffos, this
    // machine's own temp/ still absent; see docs/dev/ROADMAP.md's own
    // "Verification note"), same as MSP: real src/net/telnet.cc's
    // on_telnet_do()/on_telnet_negotiate() treat option 93 with the
    // same WILL/DO-symmetric shape GMCP/MSDP/MSSP/MSP already are, so
    // this follows GMCP/MSDP's plain-enabled-flag-plus-queued-incoming
    // shape, not MSP's one-shot-apply-on-enable shape: real
    // on_telnet_do()'s own ZMP case ("ip->iflags |= USING_ZMP; break;")
    // fires no apply at all on negotiation, confirmed directly against
    // src/vm/internal/applies (only a bare "ZMP:zmp_command" entry for
    // the incoming-message apply exists there; no "ZMP_ENABLE" entry the
    // way "MSP_ENABLE"/"GMCP_ENABLE"/"MSDP_ENABLE" all do - the latter
    // two of which this driver's own already-shipped GMCP/MSDP rows
    // never fire either, a separate pre-existing gap flagged in
    // docs/dev/STATUS.md, not fixed here). Real on_telnet_dont() has no
    // ZMP case either (falls to the generic "log only, no action"
    // default), so no special DONT/WONT handling is added here.
    bool zmpEnabled() const { return zmpEnabled_; }

    // Real wire framing, confirmed directly against src/thirdparty/
    // libtelnet/libtelnet.c's telnet_send_zmp()/telnet_zmp_arg()/
    // _zmp_telnet(): "IAC SB ZMP <command>\0<arg1>\0<arg2>\0...\0 IAC
    // SE" - every field, the command included, is NUL-terminated on the
    // wire (telnet_zmp_arg() sends strlen(arg)+1 bytes), and the real
    // receiver rejects a buffer not ending in \0 as an incomplete
    // frame. Each field goes through the same IAC-doubling telnet_send()
    // already relied on for sendMspOob() above (see that method's own
    // .cpp comment); embedded NUL bytes need no escaping of their own,
    // telnet has no special meaning for 0x00.
    void sendZmp(const std::string& command, const std::vector<std::string>& args);

    // Real safe_apply(APPLY_ZMP, ip->ob, 2, ORIGIN_DRIVER) (src/net/
    // telnet.cc's on_telnet_do_zmp(argv, argc, ip)) fires once per
    // incoming ZMP message, pushing argv[0] (the command) and an array
    // of argv[1..argc-1] (the remaining arguments, always strings in
    // real code) as the apply's two arguments. Queued the same
    // one-entry-per-message shape incomingGmcp_/incomingMsdp_ above
    // already use, drained by Server::handleConnection() into the real
    // mapped LPC name "zmp_command" (src/vm/internal/applies:
    // "ZMP:zmp_command"), not a bare "zmp".
    std::vector<std::pair<std::string, std::vector<std::string>>> takeIncomingZmp() {
        std::vector<std::pair<std::string, std::vector<std::string>>> out = std::move(incomingZmp_);
        incomingZmp_.clear();
        return out;
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    int fd() const { return fd_; }
    bool isOpen() const { return fd_ >= 0; }

    // Local listen port this connection was accepted on. Multi-port
    // listen means query_ip_port() must read this, not Config::port().
    void setLocalPort(int port) { localPort_ = port; }
    int localPort() const { return localPort_; }

    // Registers the object in InteractiveRegistry (real FluffOS's
    // all_users[]/users() and find_player() need to find it later) in
    // addition to just recording it here.
    void attach(std::shared_ptr<LpcObject> obj);
    std::shared_ptr<LpcObject> boundObject() const { return boundObject_; }

    void send(const std::string& data);
    std::vector<std::string> pollLines();

    bool closed() const { return closed_; }
    void close();

    // Marks the connection closed *without* the rest of close()'s own
    // real cleanup (fd close, InteractiveRegistry removal, snoop unlink,
    // clearing boundObject_). The same lightweight "just the flag"
    // shape pollLines() already uses internally for the ordinary peer-
    // EOF/read-error case (see its own body), exposed here so
    // Server::handleConnection()'s per-line dispatch-error catch can use
    // the identical two-phase shutdown: mark closed now, so
    // Server::fireNetDeadIfLinkDead() (called immediately after, still
    // seeing a valid boundObject()) gets a real chance to fire this
    // object's own net_dead() apply. Real remove_interactive()'s own
    // "the interactive object still exists to be notified" moment.
    // Before anything actually tears the connection down. The real
    // teardown itself (this same close()) still happens moments later
    // either way, once ~Connection() runs (see its own body) after
    // Server::pollOnce()'s own closed()-connections pruning erases the
    // last owning shared_ptr. Calling the full close() directly instead
    // of this, from a context that still wants net_dead() to fire, was
    // a real bug: it clears boundObject_ immediately, so by the time
    // fireNetDeadIfLinkDead() runs its own "!obj -> return" check, the
    // object is already gone and net_dead() silently never fires. see
    // Server.cpp's own comment at the fixed call site for the full
    // citation.
    void markClosed() { closed_ = true; }

    // Real set_call()'s own "if (flags & I_NOECHO) add_binary_message(ob,
    // telnet_yes_echo, ...)" (comm.c): sends IAC WILL ECHO immediately
    // (telling the client the server is taking over echo, so the client
    // stops local-echoing. Real telnet's own confusing-but-correct
    // "WILL ECHO" direction) and marks the connection so the next line
    // pulled in pollLines() re-enables it. Idempotent: calling this again
    // while already suppressed does not resend the negotiation bytes,
    // matching set_call()'s own single-fire-per-registration behavior
    // (only the input_to() efun call site, gated on the flag, decides
    // when this runs at all).
    void suppressEcho();

    // Real f_request_term_size() (comm.c): "add_binary_message(command_giver,
    // telnet_do_naws, sizeof(telnet_do_naws))". Fires a bare IAC DO NAWS to
    // prompt a client that has not already volunteered WILL NAWS to start
    // sending window-size subnegotiations. This driver's own NAWS handling
    // (handleNegotiation()'s silent WILL NAWS accept, handleSubnegotiation(),
    // takeWindowSizeUpdate() below) already covers the receiving side in
    // full; this is the one missing piece, the proactive request itself.
    // No idempotency guard in the real efun either. Every call resends.
    void requestWindowSize();

    // Real f_request_term_type() (comm.c): "add_binary_message(command_giver,
    // telnet_term_query, ...)". IAC SB TTYPE SEND IAC SE, asking the client
    // for its (next) terminal-type string. Real FluffOS's own driver-level
    // MTTS support is exactly this one efun plus terminal_type() below and
    // nothing else: the multi-round "ask again, compare to the previous
    // answer, stop once it repeats or a third round yields \"MTTS <n>\""
    // convention (the actual Mud Terminal Type Standard) is genuinely
    // mudlib-side in real FluffOS, driven by calling this efun repeatedly
    // from the mudlib's own terminal_type() handler. Confirmed directly:
    // grepped the whole vendored tree, there is no round-counting state, no
    // "MTTS" string comparison, and no bitmask table anywhere in the driver
    // itself. No idempotency guard in the real efun either.
    void requestTerminalType();

    // Real f_start_request_term_type() (comm.c): "add_binary_message(
    // command_giver, telnet_do_ttype, ...)". A bare IAC DO TTYPE, kicking
    // off negotiation for a connection that was not offered it automatically
    // (or as an explicit mudlib-side restart). No idempotency guard, same
    // as the real efun.
    void startRequestTerminalType();

    int terminalWidth() const { return terminalWidth_; }
    int terminalHeight() const { return terminalHeight_; }

    // Real APPLY_TERMINAL_TYPE's own argument: the raw terminal-type string
    // handed to terminal_type(), unmodified. This driver-added query mirrors
    // query_screen_width()/query_screen_height() below it in EfunTable.cpp
    // (their own header comment: "not a port of the real apply-based
    // mechanism", a driver-added pull-based convenience over the same
    // real push-based data). Not a real FluffOS efun target itself, just
    // a queryable read of the same field the real apply already pushed.
    const std::string& terminalType() const { return terminalType_; }

    // One-shot flag mirroring real comm.c's own "apply(APPLY_WINDOW_SIZE,
    // ip->ob, 2, ORIGIN_DRIVER)" firing every time a NAWS subnegotiation
    // is actually parsed, not just when the values happen to change.
    // Set by handleSubnegotiation(), consumed by Server::handleConnection()
    // right after pollLines(), the same "optional one-shot value" shape
    // takePendingInputTo()/takePendingNotifyFail() already use. Kept
    // separate from terminalWidth()/terminalHeight() themselves (which
    // stay valid and queryable at any time via query_screen_width()/
    // query_screen_height()) since firing an LPC apply needs VM access
    // this class deliberately does not have.
    bool takeWindowSizeUpdate() {
        bool had = windowSizeUpdated_;
        windowSizeUpdated_ = false;
        return had;
    }

    // Same one-shot shape as takeWindowSizeUpdate() just above, for real
    // comm.c's own "apply(APPLY_TERMINAL_TYPE, ip->ob, 1, ORIGIN_DRIVER)",
    // fired every time an SB TTYPE IS subnegotiation is actually parsed
    // (handleSubnegotiation()), consumed by Server::handleConnection().
    bool takeTerminalTypeUpdate() {
        bool had = terminalTypeUpdated_;
        terminalTypeUpdated_ = false;
        return had;
    }

    // Registers/overwrites the pending input_to handler for this
    // connection (real FluffOS's set_call(), simulate.c).
    void setPendingInputTo(std::shared_ptr<LpcObject> obj, Value function,
                            std::vector<Value> extraArgs);
    bool hasPendingInputTo() const { return pendingInputTo_.has_value(); }

    // Returns and clears the pending handler in one step. Real FluffOS's
    // call_function_interactive() clears interactive_t::input_to *before*
    // invoking the registered function specifically so that function is
    // free to call input_to() again itself to set up the next prompt
    // (comm.c: "We must [clear] all references to input_to fields before
    // the call to apply(), because someone might want to set up a new
    // input_to()"). Returning-and-clearing atomically here gives
    // callers that same ordering for free.
    std::optional<PendingInputTo> takePendingInputTo();

    // Real notify_fail()'s own "clear_notify() first, then store"
    // (add_action.c): overwriting is sufficient here since a plain
    // Value assignment already releases whatever was set before, no
    // separate clear step needed the way real FluffOS's own manual
    // ref-counting requires.
    void setPendingNotifyFail(Value message) { pendingNotifyFail_ = std::move(message); }

    // Real notify_no_command()'s own consult-and-clear. One-shot, the
    // same "return the optional and reset the slot" shape
    // takePendingInputTo() already uses just above.
    std::optional<Value> takePendingNotifyFail() {
        std::optional<Value> result = std::move(pendingNotifyFail_);
        pendingNotifyFail_.reset();
        return result;
    }

    // Real query_notify_fail() (packages/contrib.c): reads back whatever
    // is currently sitting in interactive->default_err_message without
    // consuming it. notify_no_command() (the real consumer, matched by
    // takePendingNotifyFail() above) still runs unaffected afterward. A
    // plain non-consuming peek, deliberately separate from the one-shot
    // take above rather than reusing it.
    const std::optional<Value>& peekPendingNotifyFail() const { return pendingNotifyFail_; }

    // Real clear_notify(): called unconditionally at the very start of
    // every new input line's own dispatch (process_user_command(),
    // comm.c), before even checking for a pending input_to() handler.
    // A notify_fail() set during an earlier, unrelated dispatch must
    // never leak into a later one.
    void clearPendingNotifyFail() { pendingNotifyFail_.reset(); }

    // Real comm.c's own "ip->last_time = current_time". Set once when
    // the interactive struct is first set up (new_user(), the same
    // moment this driver constructs a Connection) and then re-set every
    // time a full command line is pulled off the buffer
    // (get_user_command(), before process_user_command() even runs.
    // See Server::dispatchLine()'s own call to this). query_idle()
    // (EfunTable.cpp) reads it back as "current_time - last_time".
    void touchActivity() { lastActivityTime_ = std::time(nullptr); }
    std::time_t lastActivityTime() const { return lastActivityTime_; }

private:
    // Telnet IAC state machine (Phase 0.8), confirmed directly against
    // fluffos-2.9-ds2.08/comm.c's own copy_chars() byte-by-byte states
    // before implementing (that real function is what net/instruct.md's
    // own "telnet_neg()" citation actually refers to. No function by
    // that name exists anywhere in this vendored source; another stale
    // instruct.md citation, corrected here rather than silently trusted).
    // Persistent across pollLines() calls so a telnet sequence split
    // across two separate TCP reads still parses correctly.
    enum class TelnetState { Data, Iac, Will, Wont, Do, Dont, Sb, SbIac };
    void processTelnetBytes(const std::string& raw, std::string& plainOut);
    void handleNegotiation(TelnetState kind, unsigned char option);
    void handleSubnegotiation();
    ssize_t rawRead(char* buf, size_t n);
    ssize_t rawWrite(const char* buf, size_t n);
    bool tryCompleteWebSocketHandshake();
    std::string encodeWsFrame(const std::string& payload) const;
    std::string decodeWsFrames(std::string incoming);

    int fd_;
    int localPort_ = 0;
    SSL* ssl_ = nullptr;
    bool useWebSocket_ = false;
    bool wsHandshakeDone_ = false;
    bool telnetOffered_ = false;
    std::string wsHttpBuffer_;
    std::string wsFrameBuffer_;
    std::string wsPendingSend_;
    std::string encoding_ = "utf-8";
    bool gmcpEnabled_ = false;
    std::vector<std::string> incomingGmcp_;
    bool msspNegotiated_ = false;
    bool msdpEnabled_ = false;
    std::vector<std::pair<std::string, std::string>> incomingMsdp_;
    bool mxpEnabled_ = false;
    bool mspEnabled_ = false;
    bool mspEnableNegotiated_ = false;
    bool zmpEnabled_ = false;
    std::vector<std::pair<std::string, std::vector<std::string>>> incomingZmp_;
    std::string inputBuffer_;
    std::shared_ptr<LpcObject> boundObject_;
    bool closed_ = false;
    std::optional<PendingInputTo> pendingInputTo_;
    std::optional<Value> pendingNotifyFail_;
    std::time_t lastActivityTime_ = 0;
    TelnetState telnetState_ = TelnetState::Data;
    std::string sbBuffer_;
    bool echoSuppressed_ = false;
    int terminalWidth_ = 0;
    int terminalHeight_ = 0;
    bool windowSizeUpdated_ = false;
    std::string terminalType_;
    bool terminalTypeUpdated_ = false;
};

} // namespace kjdmud
