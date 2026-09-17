#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "amlp/vm/Value.hpp"

namespace amlp {

class VM;
class Server;
class LpcObject;
struct Closure;

// One pending call_out(). Real call_out.c's pending_call_t stores either a
// string function name bound to an owning object, or a bound closure
// (funptr_t) with no separate owner field -- reproduced here as
// std::shared_ptr<Closure> closure being non-null for the closure form
// (owner comes from Closure::owner, matching real "ob->function.f->hdr.owner"),
// and target/function used for the string form. handle is this driver's own
// simplification of real call_out.c's cycle-slot-encoded handle (see
// Scheduler.cpp's own comment on newCallOutHandle()) -- unique and stable is
// all any real call site actually needs from it.
struct CallOutEntry {
    int64_t handle = 0;
    std::weak_ptr<LpcObject> target;      // owner, for the string-name form
    std::string function;                  // function name, string form only
    std::shared_ptr<Closure> closure;      // set instead of target/function for the closure form
    std::vector<Value> args;
    std::chrono::steady_clock::time_point dueAt;
};

// One object with set_heart_beat() currently enabled. Mirrors real
// backend.c's heart_beats[] array entry (ob + time_to_heart_beat +
// heart_beat_ticks) -- ticksRemaining is heart_beat_ticks, LpcObject's own
// heartbeatInterval() is time_to_heart_beat (the reset value).
struct HeartbeatEntry {
    std::weak_ptr<LpcObject> target;
    int ticksRemaining = 1;
};

class Scheduler {
public:
    explicit Scheduler(VM& vm);

    void run(Server& server, int maxIterations = 0);

    // Real FluffOS default (options.h: "#define HEARTBEAT_INTERVAL 2" --
    // real seconds between heartbeat cycles, confirmed against this exact
    // vendored fluffos-2.9-ds2.08 tree, not assumed).
    static constexpr std::chrono::seconds kHeartbeatCycle{2};

    // int64_t call_out(...) -- returns the new entry's handle, matching
    // real new_call_out()'s own CALLOUT_HANDLES return value (confirmed
    // active in this vendored build's options.h). delay is clamped to >= 0
    // by the caller (the call_out efun), matching real new_call_out()'s
    // own "if (delay < 0) delay = 0;" -- not reproduced twice here.
    int64_t addCallOut(CallOutEntry entry);

    // Real remove_call_out(object_t*, const char*): removes the first
    // pending entry owned by "owner" whose function name matches "function"
    // -- never matches a closure-bound entry (real cop->ob is only set for
    // the string form). Returns the remaining delay in whole seconds, or -1
    // if nothing matched, matching real semantics exactly (not just a
    // boolean).
    int64_t removeCallOutByName(const std::shared_ptr<LpcObject>& owner, const std::string& function);
    // Real remove_call_out_by_handle(): removes by handle regardless of
    // owner (a handle is already globally unique). Same -1-or-remaining
    // return convention.
    int64_t removeCallOutByHandle(int64_t handle);

    // Real remove_all_call_out(object_t*) (call_out.c): removes every
    // pending entry targeting obj, both real forms -- a string-form
    // entry whose own target is obj, or a closure-form entry whose own
    // closure->owner is obj (real "(*copp)->function.f->hdr.owner ==
    // obj", confirmed directly). Real code also opportunistically prunes
    // any entry whose own target/owner is already destructed or gone
    // while it is walking the list anyway ("|| (*copp)->ob->flags &
    // O_DESTRUCTED" / "|| !(*copp)->function.f->hdr.owner ||
    // ...->flags & O_DESTRUCTED"), matched here the same way rather than
    // scoped narrowly to just obj, since that is the real function's own
    // complete behavior, not an unrelated side effect layered on top.
    // Backs reload_object() (EfunTable.cpp).
    void removeAllCallOutsForObject(const std::shared_ptr<LpcObject>& obj);

    // Real find_call_out()/find_call_out_by_handle(): same lookup rules as
    // the two remove methods above, without removing anything.
    int64_t findCallOutByName(const std::shared_ptr<LpcObject>& owner, const std::string& function) const;
    int64_t findCallOutByHandle(int64_t handle) const;

    // Real set_heart_beat(object_t*, int): to == 0 disables (and forgets
    // the entry entirely, matching real backend.c freeing the heart_beats[]
    // slot); to != 0 on an object not yet enabled adds a fresh entry
    // (negative "to" clamps to 1); to != 0 on an object already enabled
    // updates the interval on a positive "to" or is rejected (a no-op) on a
    // negative one -- all four branches read directly from backend.c's own
    // set_heart_beat(), not guessed.
    void setHeartbeatInterval(const std::shared_ptr<LpcObject>& obj, int64_t to);

    // Process exactly one heartbeat cycle (decrement every enabled object's
    // countdown, fire heart_beat() on any that reach zero, reset it back to
    // its configured interval) and exactly one call_out sweep (fire every
    // entry whose dueAt has passed), each fired call isolated so one
    // object's runtime error cannot stop the rest -- matching real
    // SETJMP-per-call_out/per-heartbeat recovery in call_out.c/backend.c,
    // and this driver's own already-established per-connection/per-object
    // error-isolation convention (ObjectManager::loadObject()'s create()
    // guard, Server's per-line dispatch guard). Deliberately public and
    // free of any real-time gating of their own (see run()'s own comment
    // for where that gating lives) so unit tests can call either directly
    // and deterministically -- the same reasoning STATUS.md already
    // documents for why Server::dispatchLine() was pulled out as its own
    // directly-testable method.
    void tickHeartbeats();
    void tickCallOuts();

    // Real backend.c's own process_objects() (LDMud)/look_for_objects_to_
    // swap() (real FluffOS's own name for the same real mechanism,
    // confirmed against temp/reference/fluffos-2.9-ds2.08/backend.c:196-302
    // -- same O_RESET_STATE/O_WILL_CLEAN_UP flags under different literal
    // names, same "current_time + TIME_TO_RESET/2 + random_number(
    // TIME_TO_RESET/2)" formula in reset_object(), same clean_up() argument
    // rule (0 for a clone, else prog->ref) -- this is a real, dialect-
    // universal LPMud-family mechanism, not an LDMud-only hook, so this
    // method runs unconditionally, the same way tickHeartbeats() above
    // does not gate on dialect either. One rule inside it *is* dialect-
    // gated, though (real FluffOS and real LDMud genuinely disagree here):
    // whether a real reset() firing this same cycle suppresses clean_up()
    // this same cycle for the same object -- see tickResetsAndCleanup()'s
    // own header comment in Scheduler.cpp for the full citation on both
    // sides)). See LpcObject::resetState()/
    // willCleanUp()/isClone()/armReset()/timeOfRef()'s own header comments
    // in LpcObject.hpp for what each piece of per-object state here backs.
    // Deliberately public and directly callable, same reasoning as
    // tickHeartbeats()/tickCallOuts() above.
    void tickResetsAndCleanup();

    static void requestShutdown();

    // Query-only counterpart to requestShutdown(), for the shutdown()
    // efun's own test coverage and anything else that needs to observe
    // the flag without driving a full run() loop (run() only checks it
    // internally, once per iteration, and resets it to false at its own
    // start). Does not consume or reset the flag.
    static bool isShutdownRequested();

    // Real get_all_call_outs() (call_out.c): read-only access to every
    // still-pending entry, for the call_out_info() efun. Deliberately a
    // plain accessor, not a copy or a filtered view -- call_out_info()
    // itself does the destructed-owner skip real get_all_call_outs()
    // does, matching where that filter lives in the real source (the
    // efun's own loop, not the scheduler's storage).
    const std::vector<CallOutEntry>& pendingCallOuts() const { return callOuts_; }

    // Real get_heart_beats() (backend.c): read-only access to every
    // object with set_heart_beat() currently enabled, for the
    // heart_beats() efun. Same "plain accessor, filtering lives in the
    // efun's own loop" reasoning as pendingCallOuts() just above.
    const std::vector<HeartbeatEntry>& pendingHeartbeats() const { return heartbeats_; }

private:
    int64_t newCallOutHandle();

    VM& vm_;
    std::vector<CallOutEntry> callOuts_;
    std::vector<HeartbeatEntry> heartbeats_;
    std::chrono::steady_clock::time_point lastHeartbeat_;
    int64_t nextHandle_ = 1;
};

} // namespace amlp
