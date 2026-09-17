#pragma once
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "amlp/vm/Value.hpp"

namespace amlp {

class LpcObject;

// The per-connection "pending input_to callback" slot -- real FluffOS's
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
    // argument -- the same two-shape Value already used for
    // notify_fail()'s own pending message/closure just below (see that
    // struct's own comment) and for socket callbacks
    // (Server.cpp's fireSocketCallback()). Real corpus: Dead Souls
    // 3.8.2's own installer, secure/lib/connect.first.c's own
    // "input_to((: InputName :), I_NOESC);", found live continuing the
    // same boot-then-live-verification session -- this field used to be
    // a plain std::string, rejecting every closure-form call outright.
    Value function;
    std::vector<Value> extraArgs;
};

// The per-connection "pending notify_fail message" slot -- real
// FluffOS's interactive_t::default_err_message (a "union string_or_func",
// comm.h), set by notify_fail(string|function) and consulted only if
// command dispatch for the current input line ends with no add_action
// handler ultimately claiming it (real notify_no_command(), add_action.c
// -- see Server::dispatchLine()'s own wiring for exactly where this
// driver mirrors that). A plain Value already covers both real forms (a
// string, or a function/Closure) without needing a bespoke union the
// way real FluffOS does.

class Connection {
public:
    explicit Connection(int fd);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    int fd() const { return fd_; }
    bool isOpen() const { return fd_ >= 0; }

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
    // clearing boundObject_) -- the same lightweight "just the flag"
    // shape pollLines() already uses internally for the ordinary peer-
    // EOF/read-error case (see its own body), exposed here so
    // Server::handleConnection()'s per-line dispatch-error catch can use
    // the identical two-phase shutdown: mark closed now, so
    // Server::fireNetDeadIfLinkDead() (called immediately after, still
    // seeing a valid boundObject()) gets a real chance to fire this
    // object's own net_dead() apply -- real remove_interactive()'s own
    // "the interactive object still exists to be notified" moment --
    // before anything actually tears the connection down. The real
    // teardown itself (this same close()) still happens moments later
    // either way, once ~Connection() runs (see its own body) after
    // Server::pollOnce()'s own closed()-connections pruning erases the
    // last owning shared_ptr. Calling the full close() directly instead
    // of this, from a context that still wants net_dead() to fire, was
    // a real bug: it clears boundObject_ immediately, so by the time
    // fireNetDeadIfLinkDead() runs its own "!obj -> return" check, the
    // object is already gone and net_dead() silently never fires -- see
    // Server.cpp's own comment at the fixed call site for the full
    // citation.
    void markClosed() { closed_ = true; }

    // Real set_call()'s own "if (flags & I_NOECHO) add_binary_message(ob,
    // telnet_yes_echo, ...)" (comm.c): sends IAC WILL ECHO immediately
    // (telling the client the server is taking over echo, so the client
    // stops local-echoing -- real telnet's own confusing-but-correct
    // "WILL ECHO" direction) and marks the connection so the next line
    // pulled in pollLines() re-enables it. Idempotent: calling this again
    // while already suppressed does not resend the negotiation bytes,
    // matching set_call()'s own single-fire-per-registration behavior
    // (only the input_to() efun call site, gated on the flag, decides
    // when this runs at all).
    void suppressEcho();

    // Real f_request_term_size() (comm.c): "add_binary_message(command_giver,
    // telnet_do_naws, sizeof(telnet_do_naws))" -- fires a bare IAC DO NAWS to
    // prompt a client that has not already volunteered WILL NAWS to start
    // sending window-size subnegotiations. This driver's own NAWS handling
    // (handleNegotiation()'s silent WILL NAWS accept, handleSubnegotiation(),
    // takeWindowSizeUpdate() below) already covers the receiving side in
    // full; this is the one missing piece, the proactive request itself.
    // No idempotency guard in the real efun either -- every call resends.
    void requestWindowSize();

    // Real f_request_term_type() (comm.c): "add_binary_message(command_giver,
    // telnet_term_query, ...)" -- IAC SB TTYPE SEND IAC SE, asking the client
    // for its (next) terminal-type string. Real FluffOS's own driver-level
    // MTTS support is exactly this one efun plus terminal_type() below and
    // nothing else: the multi-round "ask again, compare to the previous
    // answer, stop once it repeats or a third round yields \"MTTS <n>\""
    // convention (the actual Mud Terminal Type Standard) is genuinely
    // mudlib-side in real FluffOS, driven by calling this efun repeatedly
    // from the mudlib's own terminal_type() handler -- confirmed directly:
    // grepped the whole vendored tree, there is no round-counting state, no
    // "MTTS" string comparison, and no bitmask table anywhere in the driver
    // itself. No idempotency guard in the real efun either.
    void requestTerminalType();

    // Real f_start_request_term_type() (comm.c): "add_binary_message(
    // command_giver, telnet_do_ttype, ...)" -- a bare IAC DO TTYPE, kicking
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
    // real push-based data) -- not a real FluffOS efun target itself, just
    // a queryable read of the same field the real apply already pushed.
    const std::string& terminalType() const { return terminalType_; }

    // One-shot flag mirroring real comm.c's own "apply(APPLY_WINDOW_SIZE,
    // ip->ob, 2, ORIGIN_DRIVER)" firing every time a NAWS subnegotiation
    // is actually parsed, not just when the values happen to change --
    // set by handleSubnegotiation(), consumed by Server::handleConnection()
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
    // input_to()") -- returning-and-clearing atomically here gives
    // callers that same ordering for free.
    std::optional<PendingInputTo> takePendingInputTo();

    // Real notify_fail()'s own "clear_notify() first, then store"
    // (add_action.c): overwriting is sufficient here since a plain
    // Value assignment already releases whatever was set before, no
    // separate clear step needed the way real FluffOS's own manual
    // ref-counting requires.
    void setPendingNotifyFail(Value message) { pendingNotifyFail_ = std::move(message); }

    // Real notify_no_command()'s own consult-and-clear -- one-shot, the
    // same "return the optional and reset the slot" shape
    // takePendingInputTo() already uses just above.
    std::optional<Value> takePendingNotifyFail() {
        std::optional<Value> result = std::move(pendingNotifyFail_);
        pendingNotifyFail_.reset();
        return result;
    }

    // Real query_notify_fail() (packages/contrib.c): reads back whatever
    // is currently sitting in interactive->default_err_message without
    // consuming it -- notify_no_command() (the real consumer, matched by
    // takePendingNotifyFail() above) still runs unaffected afterward. A
    // plain non-consuming peek, deliberately separate from the one-shot
    // take above rather than reusing it.
    const std::optional<Value>& peekPendingNotifyFail() const { return pendingNotifyFail_; }

    // Real clear_notify(): called unconditionally at the very start of
    // every new input line's own dispatch (process_user_command(),
    // comm.c), before even checking for a pending input_to() handler --
    // a notify_fail() set during an earlier, unrelated dispatch must
    // never leak into a later one.
    void clearPendingNotifyFail() { pendingNotifyFail_.reset(); }

    // Real comm.c's own "ip->last_time = current_time" -- set once when
    // the interactive struct is first set up (new_user(), the same
    // moment this driver constructs a Connection) and then re-set every
    // time a full command line is pulled off the buffer
    // (get_user_command(), before process_user_command() even runs --
    // see Server::dispatchLine()'s own call to this). query_idle()
    // (EfunTable.cpp) reads it back as "current_time - last_time".
    void touchActivity() { lastActivityTime_ = std::time(nullptr); }
    std::time_t lastActivityTime() const { return lastActivityTime_; }

private:
    // Telnet IAC state machine (Phase 0.8), confirmed directly against
    // fluffos-2.9-ds2.08/comm.c's own copy_chars() byte-by-byte states
    // before implementing (that real function is what net/instruct.md's
    // own "telnet_neg()" citation actually refers to -- no function by
    // that name exists anywhere in this vendored source; another stale
    // instruct.md citation, corrected here rather than silently trusted).
    // Persistent across pollLines() calls so a telnet sequence split
    // across two separate TCP reads still parses correctly.
    enum class TelnetState { Data, Iac, Will, Wont, Do, Dont, Sb, SbIac };
    void processTelnetBytes(const std::string& raw, std::string& plainOut);
    void handleNegotiation(TelnetState kind, unsigned char option);
    void handleSubnegotiation();

    int fd_;
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

} // namespace amlp
