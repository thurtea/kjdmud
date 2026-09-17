#pragma once
#include <array>
#include <chrono>
#include <coroutine>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "amlp/vm/Value.hpp"
#include "amlp/vm/Bytecode.hpp"
#include "amlp/scheduler/Task.hpp"

namespace amlp {

class ObjectManager;
class LpcObject;
class Config;
class Scheduler;

// Real origin.h's own enum, confirmed directly against efuns_main.c's
// f_origin() (push_constant_string(origin_name(caller_type))) and every
// real C caller_type/call_origin set site before writing any dispatch
// logic (function.c, interpret.c, add_action.c, backend.c, master.c,
// eoperators.c, comm.c, call_out.c, socket_efuns.c, object.c) -- see
// VM.cpp's own OriginGuard and each call site's own comment for the
// full per-value citation trail. Real caller_type is a single scalar
// saved/restored across the control stack around every function call
// (push_control_stack()/pop_control_stack()), the exact shape this
// driver's own originStack_ mirrors -- not a per-efun classification of
// any kind. FunctionPointer is kept for API completeness (origin_name()
// must be able to name it) even though no call path in either the real
// driver or this one appears to ever leave it as a *real* LPC function's
// own observable origin: real call_function_pointer() only ever sets it
// transiently for its own synthetic "fake frame" (setup_fake_frame()),
// immediately overwritten by FP_LOCAL/FP_SIMUL/FP_FUNCTIONAL's own
// explicit origin before any genuine LPC bytecode runs, and left
// unchanged only for FP_EFUN, whose raw C efun body never calls
// origin() on itself either.
enum class Origin {
    Driver,
    Local,
    CallOther,
    SimulEfun,
    Internal,
    Efun,
    FunctionPointer,
    Functional,
};

// origin_name()'s own exact string table (efuns_main.c), same index
// order as real origin.h's own bit-shift enum.
const char* originName(Origin origin);

class VM {
public:
    VM(ObjectManager& objects, Config& config);

    // Set once, right after Scheduler is constructed (main.cpp), same
    // "back-pointer set after construction" pattern ObjectManager::setVM()
    // already uses -- Scheduler itself takes VM& in its own constructor,
    // so the reverse edge can only be wired up afterward. Efuns that need
    // to reach the scheduler (call_out(), remove_call_out(), find_call_out(),
    // set_heart_beat()) go through this; null in any context that never
    // wires one up (e.g. a unit test harness that doesn't need scheduling).
    void setScheduler(Scheduler* scheduler) { scheduler_ = scheduler; }
    Scheduler* scheduler() const { return scheduler_; }

    // Read-only access to the driver's own configuration -- needed by
    // efuns whose real answer is a single, driver-wide fact rather than
    // per-connection state (e.g. query_ip_port(): this driver has exactly
    // one listening port, Config::port(), so any currently-interactive
    // object's real answer is that value, not something tracked per
    // Connection).
    Config& config() const { return config_; }

    // origin defaults to Origin::Driver, correct for the majority of
    // real call sites into this method (every "invoke a function on an
    // object from outside the VM" caller confirmed real-ORIGIN_DRIVER:
    // logon(), process_input(), net_dead(), window_size(), create(),
    // every master apply, moveObject()'s own init() propagation) --
    // callers whose real analog uses a different origin (call_other()
    // itself: CallOther; a call_out()/socket callback firing:
    // Internal; an efun's own C body invoking a mudlib-supplied
    // callback argument, e.g. map_array()/sort_array()/
    // unique_mapping(): Efun) pass it explicitly. See each call site's
    // own comment for its citation.
    Value callFunction(const std::shared_ptr<LpcObject>& obj,
                        const std::string& functionName,
                        std::vector<Value> args,
                        Origin origin = Origin::Driver);

    Value applyMaster(const std::string& applyName, std::vector<Value> args);

    // Calls functionName only if it is declared *directly* in program
    // (never searching program's own inheritedPrograms the way
    // callFunction()'s tiered findFunctionInChain() does) -- needed for
    // ObjectManager::runObjectVarInitializers(), which must run each
    // inherit-chain level's own synthesized "$objvarinit" separately
    // (real object-variable-initializer semantics: a parent's own
    // initializers run too, not just a child's, and every level uses the
    // same fixed synthesized name -- see CodeGen::generate()'s own
    // comment -- so the normal shadowing-aware lookup would only ever
    // reach one level's copy). Silently returns void if functionName is
    // not declared at this exact level, matching callFunction()'s own
    // "undefined function is not an error" convention.
    Value callFunctionInProgram(const std::shared_ptr<LpcObject>& obj, const CompiledProgram& program,
                                 const std::string& functionName, std::vector<Value> args);

    // Backs the function_exists() efun: true if functionName is defined
    // anywhere in obj's own local/inherited chain (the same
    // findFunctionInChain() walk callFunction() itself uses to resolve a
    // bare or call_other call), false otherwise -- does not call it, and
    // does not distinguish public from real O_DESTRUCTED-adjacent
    // protected/private/hidden visibility the way real function_exists()'s
    // own third "flag" argument does (this driver's own FunctionEntry
    // carries no visibility modifier at all -- see function_exists()'s
    // own EfunTable.cpp comment for why nothing confirmed live in this
    // mudlib needs that distinction).
    bool functionExists(const std::shared_ptr<LpcObject>& obj, const std::string& functionName) const;

    std::shared_ptr<LpcObject> cloneObject(const std::string& filename);

    // real destruct(): removes obj from the object table (thin wrapper
    // over ObjectManager::destructObject(), which already exists for
    // this). This driver has no O_DESTRUCTED flag/deferred-free scheme
    // (LpcObject lifetime is plain shared_ptr refcounting): calling a
    // function on an already-"destructed" object here is not
    // specifically guarded against the way real FluffOS guards every
    // apply() with an O_DESTRUCTED check, it simply keeps working
    // against a live C++ object until the last shared_ptr reference
    // actually drops. Nothing this driver runs yet depends on that
    // distinction (see the destruct efun's own comment in EfunTable.cpp
    // for the interactive-connection-specific handling layered on top
    // of this).
    // onDestructed: see ObjectManager::destructObject()'s own header
    // comment -- forwarded straight through, VM has no `net` dependency
    // to close sockets with here either.
    void destructObject(const std::shared_ptr<LpcObject>& obj,
                         const std::function<void(const std::shared_ptr<LpcObject>&)>& onDestructed = {});

    // Thin wrapper over ObjectManager::reloadObject(), matching
    // cloneObject()/destructObject()'s own established shape -- see its
    // own header comment for the full real reload_object() derivation.
    // onDestructed forwarded straight through, same reasoning as
    // destructObject()'s own copy just above.
    void reloadObject(const std::shared_ptr<LpcObject>& obj,
                       const std::function<void(const std::shared_ptr<LpcObject>&)>& onDestructed = {});

    // real FluffOS's master() efun (func_spec.c: "object master();") --
    // just the already-loaded master object, no different from what
    // applyMaster() already dispatches against.
    std::shared_ptr<LpcObject> masterObject() const;

    // The configured simul_efun object (real simul_efun_ob), already
    // reached internally on every bare-call tier-3 lookup (see the Call
    // opcode's own VM.cpp comment) but not previously exposed to
    // EfunTable.cpp -- needed by replace_program()'s own real
    // "current_object == simul_efun_ob" guard (replace_program.c).
    std::shared_ptr<LpcObject> simulEfunObject() const;

    // real LDMud privilege_violation()/privilege_violation2()/
    // privilege_violation4()/privilege_violation_n()
    // (temp/ldmud/src/interpret.c:8492-8722), collapsed into one shared
    // helper: all four real wrappers are thin shells around the identical
    // "trust bypass, apply master's privilege_violation() lfun, interpret
    // the result" core, differing only in which/how-many extra data args
    // get pushed before the call -- callers assemble their own op-
    // specific `args` (matching each real call site's own exact shape,
    // e.g. call_out_info's bare `0`, bind_lambda's target object) and this
    // does the shared part. `what` is the real op name (e.g.
    // "bind_lambda", matching doc/master/privilege_violation's own
    // per-op catalog); `args` becomes everything in the real master apply
    // after `who` (current_object, inserted automatically here exactly
    // like real code's own unconditional `push_current_object`).
    //
    // Real trust bypass (interpret.c:8552-8553, identical in all four):
    // current_object == master_ob or == simul_efun_object grants
    // immediately, no apply at all.
    //
    // Real result interpretation (interpret.c:8570-8578, identical in all
    // four): a return > 0 grants (true); a *missing* lfun, a non-number
    // return, or a *negative* number all raise a hard "privilege
    // violation: %s" error (interpret.c:8573) -- a return of exactly 0
    // does NOT error, it is real code's own "gently denied" case
    // (interpret.c:8504-8505's own doc comment) and returns false with no
    // throw. This driver's own callFunction()/applyMaster() cannot
    // distinguish "master has no privilege_violation() lfun at all" from
    // "it exists and explicitly returned 0" at the Value{} level (both
    // collapse to the same default-constructed Value{}), so
    // functionExists() is checked explicitly first here, faithfully
    // reproducing real code's own "!svp" branch rather than losing it to
    // that collapse.
    bool privilegeViolation(const std::string& what, std::vector<Value> args);

    // real FluffOS's find_object(): an already-loaded lookup that falls
    // back to *compiling and loading the file on a miss*
    // (simulate.c's find_object(): "if ((ob = lookup_object_hash(
    // tmpbuf))) return ob; ob = load_object(tmpbuf, 0); ..." -- this is
    // NOT the separate "look only, never load" find_object2() also in
    // that same file, which is easy to confuse it with from
    // efuns_main.c's f__call_other() alone. This is exactly why real
    // master.c's own preload() function can force-load a daemon with
    // nothing more than "call_other(str, \"???\")": the call_other
    // itself, via find_object(), is what compiles it. Used by the
    // call_other efun's string-target overload; implemented as a thin
    // wrapper over ObjectManager::loadObject(), which already has
    // exactly this compile-if-needed/cache-by-filename behavior.
    std::shared_ptr<LpcObject> findObject(const std::string& filename) const;

    // Look-only counterpart to findObject() above -- see
    // ObjectManager::lookupLoadedObject()'s own comment. Backs the
    // find_object() efun's default (no-compile) behavior.
    std::shared_ptr<LpcObject> lookupObject(const std::string& filename) const;

    // ROADMAP.md row 2.1 (world statedump): dump_state()/restore_state()
    // construct a StateSerializer directly against the real
    // ObjectManager, the same way every other object-graph efun in this
    // table already reaches it, just via a named accessor instead of
    // each going through its own separate thin wrapper method here.
    ObjectManager& objectManager() const { return objects_; }

    // The object whose function body is currently executing (the
    // top of the C++-recursion call stack run() maintains -- see
    // run()'s own StackGuard comment). This is real FluffOS's
    // current_object, needed by efuns like input_to() (simulate.c's
    // input_to(): "s->ob = current_object;") that must know which
    // object registered them, not just which connection is driving the
    // call. Returns null if called outside any run() (e.g. efuns
    // invoked directly from ObjectManager::loadObject()'s create() call
    // do go through run(), so this is populated correctly there too).
    std::shared_ptr<LpcObject> currentObject() const;

    // Real call_stack()'s own mode-1 data source (real csp, the control
    // stack): every currently-active function-call frame's own object,
    // innermost (currentObject(), matching real current_object) last --
    // callers reverse-iterate for call_stack()'s own "current frame
    // first, walking outward" ordering. A plain accessor over the exact
    // same callStack_ currentObject() itself already reads from; not a
    // separate tracking structure. Mode 0 (real per-frame program
    // filenames) is derived from this same stack's own objects'
    // filenames, since this driver has no separate per-frame program
    // pointer distinct from "whichever object's function is running".
    // Mode 2 (per-frame function name) still has no backing data here at
    // all -- see call_stack()'s own EfunTable.cpp registration comment.
    // Mode 3 (per-frame origin) is a different story as of the origin()
    // implementation below: originStack_ now tracks exactly this, one
    // entry per still-active run() call in the same parallel shape as
    // callStack_ -- call_stack() mode 3 itself is still not wired up
    // (out of scope for the row that added origin() -- see STATUS.md),
    // but the data it would need now exists, a real, cheap follow-on
    // for whoever picks that up next rather than a fresh implementation
    // from scratch.
    const std::vector<std::shared_ptr<LpcObject>>& callFrames() const { return callStack_; }

    // See originStack_'s own comment. currentOrigin() defaults to
    // Origin::Driver when nothing has ever pushed (an origin() call
    // reached with no active frame at all -- should not happen from
    // genuine LPC code, but matches real backend.c's own "caller_type
    // defaults to driver when unset" fallback rather than an arbitrary
    // choice). pushOrigin()/popOrigin() are the RAII-guarded push/pop
    // pair every real call path below uses (see OriginGuard, VM.cpp).
    Origin currentOrigin() const { return originStack_.empty() ? Origin::Driver : originStack_.back(); }
    void pushOrigin(Origin origin) { originStack_.push_back(origin); }
    void popOrigin() { originStack_.pop_back(); }

    // real FluffOS's previous_object(int idx = 0) (efuns_main.c's
    // f_previous_object()): the object idx object-changing calls back
    // up the chain from here (0 = whoever was current_object right
    // before the most recent call_other/simul_efun-tier crossing into a
    // different object; the object doing that crossing's "prev_ob").
    // Backed by objectChangeStack_, a *separate* stack from
    // currentObject()'s callStack_: real FluffOS only pushes a
    // FRAME_OB_CHANGE frame when a call actually crosses into a
    // different object, not for every same-object local/inherited call
    // -- see run()'s own comment for how that distinction is detected.
    // Returns null if idx is out of range (no such frame -- e.g. idx is
    // past the top-level driver-originated call, which has no LPC
    // caller at all).
    std::shared_ptr<LpcObject> previousObject(int idx) const;

    // real previous_object(-1) / all_previous_objects(): every entry in
    // objectChangeStack_, nearest first, with the top-level "no LPC
    // caller" case simply absent (the real driver's own array
    // construction only counts frames that had a non-null prev_ob, see
    // efuns_main.c's f_previous_object() -1 branch: "i = previous_ob ?
    // 1 : 0" before ever incrementing further).
    std::vector<std::shared_ptr<LpcObject>> allPreviousObjects() const;

    // real FluffOS's evaluate()/funcall() (efuns_main.c's f__evaluate(),
    // registered under both names -- func_spec.c: "mixed evaluate
    // _evaluate(mixed, ...); mixed funcall _evaluate(mixed, ...);"):
    // invokes a Closure value with its own bound args followed by
    // extraArgs (see Value.hpp's Closure comment for the bound-args-
    // first ordering), resolving the closure's bare function name
    // lazily against its owner object -- local/inherited, then the
    // simul_efun object, then the core efun table, the same tiered
    // order OpCode::Call already uses for a bare name (see this
    // method's own .cpp comment for why re-resolving lazily here is a
    // deliberate, safe simplification rather than real FluffOS's
    // eager FP_LOCAL/FP_SIMUL/FP_EFUN classification at construction
    // time). Throws if the closure's owner object has been destructed
    // (real call_function_pointer()'s own "Owner ... is destructed"
    // check) or if the bare name resolves to nothing at all.
    Value callClosure(const std::shared_ptr<Closure>& closure, std::vector<Value> extraArgs);

    // Resolves an absolute-from-mudlib-root LPC path (e.g. the "cfg"
    // argument to read_file()/write_file()) to a real filesystem path,
    // the same way ObjectManager::compile() resolves a ".c" source file's
    // path -- just without appending ".c", since these are plain data
    // files, not compilable objects. File-I/O efuns use this so mudlib
    // paths never depend on the driver's own current working directory.
    std::string resolveMudlibPath(const std::string& lpcPath) const;

    // Config's own configured mud name (real rc.c's own MUD_NAME config
    // string, index 0 in get_config_item()'s own config_str[] table) --
    // exposed as a plain derived accessor rather than the whole Config&,
    // matching resolveMudlibPath()'s own established pattern just above.
    // get_config(0)'s only real, tested caller.
    const std::string& mudName() const;

    // real command_giver (comm.h): the object whose typed input is
    // currently being parsed against an action table, and the object
    // add_action() registers onto. Tracked as an explicit stack (rather
    // than currentObject()'s callStack_) because it changes on a
    // different, coarser rhythm than current_object -- it is set once
    // per dispatched command line, and separately save/restored around
    // each leg of moveObject()'s init()-calling sequence (real
    // save_command_giver()/restore_command_giver(), add_action.c), not
    // pushed on every function call the way current_object is. Returns
    // null if nothing has set one (e.g. add_action() called outside any
    // dispatch or moveObject() context -- real add_action() silently
    // no-ops in that case too, see EfunTable.cpp's own comment).
    std::shared_ptr<LpcObject> commandGiver() const;
    void pushCommandGiver(const std::shared_ptr<LpcObject>& ob);
    void popCommandGiver();

    // real query_verb(): the first word of the line currently being
    // dispatched against an action table, exactly as typed (not the
    // matched verb prefix -- see dispatchCommand()'s own comment on
    // V_SHORT). Empty when nothing is currently being dispatched.
    std::string currentVerb() const;

    // LDMud driver_hook[NUM_DRIVER_HOOKS] (ROADMAP.md row 1.7/1.8; real
    // mudlib/sys/driver_hook.h's own real hook-number defines, 0..31 --
    // mirrored verbatim in this driver's own bundled mudlib/sys/
    // driver_hook.h). One VM-wide array, matching real semantics
    // exactly: driver_hook is a genuine process-global in real LDMud,
    // never object-scoped. Every slot starts void; every dialect's
    // pre-existing behavior is completely unaffected until a real
    // mudlib master object actually calls set_driver_hook() itself (see
    // set_driver_hook()'s own EfunTable.cpp registration and
    // moveObject()'s own comment for the one real trigger point this
    // slice wires up, H_MOVE_OBJECT0/1).
    static constexpr int kNumDriverHooks = 32;
    Value getDriverHook(int what) const;
    void setDriverHook(int what, Value arg);

    // Calls a driver-hook closure with an explicit bind object,
    // bypassing the "Uncallable closure" check callClosure() enforces
    // for an ordinary LPC-level funcall()/evaluate() call -- matching
    // real call_lambda()/call_lambda_ob()'s own mechanism (interpret.h:
    // "#define call_lambda(lsvp, num_arg) int_call_lambda(lsvp, num_arg,
    // true, NULL)", "#define call_lambda_ob(lsvp, num_arg, ob)
    // int_call_lambda(lsvp, num_arg, true, ob)"): the driver itself
    // supplies the missing bind object and mutates the closure's own
    // base.ob/owner in place immediately before invoking it, real
    // object.c's own move_object() static function being the exact
    // citation for the H_MOVE_OBJECT0/1 case this method backs (see
    // moveObject()'s own comment). See this method's own .cpp comment
    // for why mutating in place (not copying) matches real semantics.
    Value callDriverHookClosure(const std::shared_ptr<Closure>& closure,
                                 const std::shared_ptr<LpcObject>& bindTo,
                                 std::vector<Value> args);

    // real setup_new_commands() (add_action.c), the mechanism that
    // (re)builds the relevant objects' action tables whenever something
    // moves. Called by the move_object() efun with item = the object
    // being moved (current_object at the point of the efun call) and
    // dest = its new environment. See VM.cpp's own comment for exactly
    // which of real setup_new_commands()'s three visitation legs this
    // implements and why the rest were scoped out, and for the
    // H_MOVE_OBJECT0/1 driver-hook dispatch this method now tries first
    // (real object.c's own move_object() static function), falling back
    // to the hardcoded logic below only when neither hook is set -- the
    // one deliberate multi-dialect divergence from real LDMud, which has
    // no built-in fallback at all ("Don't know how to move objects.").
    void moveObject(const std::shared_ptr<LpcObject>& item, const std::shared_ptr<LpcObject>& dest);

    // real parse_command()/user_parser() (add_action.c): matches line's
    // first word against giver's currently-registered action table
    // (built by the moveObject() calls above, not rebuilt here) and
    // calls the first matching handler that returns truthy, trying
    // further matches if one returns falsy ("the parser will continue
    // searching for another command", add_action.c's own doc comment).
    // Returns true if any handler ran and returned truthy (a command
    // was actually handled), false otherwise (no match, or every match
    // declined) -- Server::dispatchLine uses this to decide whether to
    // fall back to a "what?" style message.
    bool dispatchCommand(const std::shared_ptr<LpcObject>& giver, const std::string& line);

    // Registers (or, matching real retrieve_replace_program_entry(),
    // overwrites an already-pending one for the same object) a deferred
    // replace_program() swap -- see replace_program() efun's own
    // EfunTable.cpp registration comment for the full derivation. Never
    // applied immediately: real replace_program.c's own comment explains
    // why ("all kind of volatile data structures could result" if the
    // swap happened mid-execution), matched here by only ever staging it
    // for processPendingReplacePrograms() to apply later.
    void enqueueReplaceProgram(std::shared_ptr<LpcObject> ob,
                                std::shared_ptr<CompiledProgram> newProgram, int offset,
                                std::string name);

    // real remove_destructed_objects()'s own "if (obj_list_replace)
    // replace_programs();" (backend.c) -- called once per outer driver
    // tick (Scheduler::run()'s own for(;;) loop, the equivalent of real
    // backend.c's own while(1) loop this exact call site lives in), not
    // once per individual command/call_out/heartbeat dispatch the way
    // eval-cost reset is. Also directly callable from tests that never
    // run a full Scheduler::run() loop, the same "directly callable to
    // simulate one tick" shape tickHeartbeats()/tickCallOuts() already
    // establish. A no-op when nothing is pending (matching real code's
    // own "if (obj_list_replace)" guard, not an unconditional per-tick
    // walk).
    void processPendingReplacePrograms();

    // ROADMAP.md row 2.5's own first slice ("VM: a new parallel
    // runAsync() entry point beside the existing run()..."). Public,
    // unlike run() (see below), because this row's own regression
    // tests construct a CompiledProgram/FunctionEntry by hand (real
    // row 2.6 async/await grammar does not exist yet -- see
    // FunctionEntry::isAsync's own comment) and drive it directly,
    // with no existing name-resolution wrapper like callFunction() to
    // go through. fn.isAsync need not be true for this specific top-
    // level call (nothing checks it here); it matters only inside the
    // coroutine body itself, at each OpCode::Call site resolving a
    // *callee*, deciding whether that nested call should co_await
    // another runAsync() or fall through to the plain, unchanged
    // run() -- see VM.cpp's own implementation comment for the full
    // per-opcode scope of this first slice's own minimal interpreter
    // (a deliberately small subset of the real opcode set: real
    // row 2.6 codegen, once it exists, will need this expanded to the
    // rest, exactly the same way the plain run() loop already covers
    // it -- not attempted this row, see ROADMAP.md's own explicit
    // deferral list).
    Task<Value> runAsync(const CompiledProgram& program, const FunctionEntry& fn,
                          std::vector<Value> args, const std::shared_ptr<LpcObject>& obj);

    // ROADMAP.md row 2.5's own first slice ("Scheduler::run() gains one
    // new step..."). Called once per Scheduler::run() iteration,
    // mirroring processPendingReplacePrograms()'s own already-
    // established "VM owns the pending queue, Scheduler drains it every
    // tick" shape immediately above -- deliberately a VM method, not a
    // Scheduler one, and deliberately not named/shaped the way this
    // row's own ROADMAP.md note first sketched it
    // (`Scheduler::resumeReadyTasks(now)`): src/scheduler/CMakeLists.txt
    // links `scheduler` against `vm`, never the reverse, and VM.cpp has
    // no existing call site referencing Scheduler's own concrete class
    // at all (every real call_out()/heart_beat() efun bridges VM and
    // Scheduler from EfunTable.cpp instead, one layer up -- confirmed
    // directly before writing this, not assumed). Putting the parked-
    // handle queue and its own per-callback isolation here instead
    // avoids a real circular library dependency without needing a new
    // abstract interface just to preserve that layering -- a real scope
    // correction found while wiring the pieces together, the same
    // discipline every prior Phase 2 row's own "found before writing
    // any code" corrections already used, just found one step later
    // here since the scoping session's own docs-only mandate could not
    // have caught it before any code existed to reveal it.
    void resumeReadyAsyncTasks(std::chrono::steady_clock::time_point now);

private:
    Value run(const CompiledProgram& program, const FunctionEntry& fn,
              std::vector<Value> args, const std::shared_ptr<LpcObject>& obj);

    // LDMud unbound_lambda() (ROADMAP.md row 1.7/1.8). callClosure()'s own
    // "this closure carries a quoted-code body" branch -- see Value.hpp's
    // Closure::lambdaBody comment and these two methods' own .cpp comments
    // for the real-source citations and exactly what quoted-code shape is
    // (and is not) supported.
    Value callUnboundLambdaBody(const Closure& closure, std::vector<Value> argValues);
    Value evalQuotedLambdaNode(const Value& node, const std::vector<std::string>& params,
                                const std::vector<Value>& argValues);

    ObjectManager& objects_;
    Config& config_;
    Scheduler* scheduler_ = nullptr;
    std::vector<Value> stack_;
    // See kNumDriverHooks/getDriverHook()/setDriverHook()'s own comment
    // just above -- default-constructed (every slot void) until
    // set_driver_hook() actually stores something.
    std::array<Value, kNumDriverHooks> driverHooks_;
    // Accumulated instruction count for the current top-level LPC dispatch
    // (one player command, one call_out fire, one heartbeat call). Real
    // FluffOS accumulates across all nested apply/call_other/callClosure
    // calls within a single command rather than resetting per run() call
    // (interpret.c's own eval_cost global, incremented per instruction,
    // reset once by process_user_command() / call_heart_beat() /
    // call_out.c's call_call_out(), not per-invocation). resetEvalCost()
    // is called by Server::dispatchLine() at the top of each dispatch and
    // by Scheduler::tickCallOuts()/tickHeartbeats() before each fired
    // callback, matching those exact real reset points.
    int64_t evalCost_ = 0;
    // Per-dispatch ceiling. Initialized to Config::maxEvalCost() at
    // construction, changed only by the "default" branch of
    // set_eval_limit()'s own real 4-way switch (EfunTable.cpp's own
    // registration comment has the full citation) -- confirmed directly
    // against efuns_main.c's own f_set_eval_limit() before writing this:
    // real set_eval_limit(-1) is NOT a "restore the default" sentinel (an
    // earlier version of this comment assumed that, unverified); it is a
    // pure query of the *remaining* budget with no side effect on this
    // ceiling at all. There is no built-in "restore to default" mechanism
    // in real FluffOS's own C code -- a mudlib that wants that back
    // simply calls set_eval_limit() again with whatever value it
    // remembers.
    int64_t maxEvalCost_ = 1000000;

public:
    // Reset the accumulated eval cost to zero. Called at the start of
    // every top-level dispatch (player command, call_out, heartbeat), and
    // by the real "x == 0" branch of set_eval_limit()'s own dispatch
    // (reset_eval_cost()'s own real default argument).
    void resetEvalCost() { evalCost_ = 0; }
    int64_t evalCost() const { return evalCost_; }

    // Directly overwrites the eval-cost ceiling -- the real "default:
    // max_cost = sp->u.number;" branch of set_eval_limit()'s own switch,
    // confirmed directly against efuns_main.c's own f_set_eval_limit().
    // No special-casing of any particular argument value happens here;
    // the 0/-1/1 real special cases are handled entirely in
    // EfunTable.cpp's own shared dispatch lambda, matching where that
    // logic actually lives in real FluffOS too (inside f_set_eval_limit()
    // itself, not some lower interpret.c primitive).
    void setMaxEvalCost(int64_t limit);
    int64_t maxEvalCost() const { return maxEvalCost_; }

private:
    // One entry per still-active run() call, innermost last -- see
    // currentObject() and run()'s StackGuard.
    std::vector<std::shared_ptr<LpcObject>> callStack_;
    // One entry per still-active *object-changing* call, innermost
    // last -- see previousObject()/allPreviousObjects() and run()'s own
    // comment on how an object change is detected.
    std::vector<std::shared_ptr<LpcObject>> objectChangeStack_;
    // See commandGiver()/pushCommandGiver()/popCommandGiver().
    std::vector<std::shared_ptr<LpcObject>> commandGiverStack_;
    // OpCode::TimeExpressionStart/TimeExpressionEnd (Ast.hpp's
    // TimeExpressionExpr, "time_expression { <body> }"): one timestamp
    // per still-active (possibly nested) time_expression, innermost
    // last. A dedicated stack, not the ordinary LPC value stack real
    // FluffOS itself parks its own two timer values on (interpret.c's
    // F_TIME_EXPRESSION/F_END_TIME_EXPRESSION) -- avoids any dependency
    // on the body's own bytecode leaving the LPC stack exactly as it
    // found it; correctness here never has to reason about that.
    std::vector<std::chrono::steady_clock::time_point> timeExpressionStack_;
    // See currentVerb(); pushed/popped alongside dispatchCommand()'s own
    // handler calls, a separate stack from commandGiverStack_ since a
    // command() efun call (implemented in EfunTable.cpp, reusing this
    // same dispatchCommand() path) nests a new verb without necessarily
    // changing the command_giver.
    std::vector<std::string> verbStack_;

    // See currentOrigin()/pushOrigin()/popOrigin() -- real caller_type
    // (interpret.c), saved/restored across the control stack around
    // every function call (push_control_stack()/pop_control_stack()).
    // One entry per still-active run() call, the same parallel shape
    // callStack_ already has, pushed/popped via the RAII OriginGuard
    // (VM.cpp) at every real call path that pushes a genuine LPC frame
    // -- OpCode::Call's local/simul_efun tiers, OpCode::CallParent,
    // callClosure()'s own tiered resolution, and callFunction()'s own
    // final dispatch. Deliberately *not* pushed for a bare call that
    // resolves to the core efun table: real efuns never get their own
    // control-stack frame at all (confirmed directly -- no
    // push_control_stack() call anywhere in an ordinary efun
    // dispatch), so the origin stays whatever the calling function's
    // own frame already has, unchanged.
    std::vector<Origin> originStack_;

    // See enqueueReplaceProgram()/processPendingReplacePrograms(). Real
    // obj_list_replace (replace_program.c), a plain linked list of
    // pending swaps; one entry per object with a swap staged, matching
    // real retrieve_replace_program_entry()'s "find or create" reuse
    // (a second replace_program() call before the next tick overwrites
    // the first, rather than queuing both).
    struct PendingReplaceProgram {
        std::shared_ptr<LpcObject> ob;
        std::shared_ptr<CompiledProgram> newProgram;
        int offset = 0;
        std::string name;
    };
    std::vector<PendingReplaceProgram> pendingReplacePrograms_;

    // ROADMAP.md row 2.5's own first slice. One entry per runAsync()
    // coroutine currently parked on OpCode::Suspend, resumed once
    // resumeReadyAsyncTasks() sees now >= resumeAt -- a plain vector,
    // scanned linearly each call, matching tickCallOuts()'s own
    // established "collect due entries, then fire each" style
    // (Scheduler.cpp) rather than a literal std::priority_queue: this
    // row's own first slice never has more than a handful of tasks
    // parked at once, and a real heap only pays for itself at a scale
    // nothing in this slice's own scope reaches.
    struct PendingAsyncResume {
        std::chrono::steady_clock::time_point resumeAt;
        std::coroutine_handle<> handle;
    };
    std::vector<PendingAsyncResume> pendingAsyncResumes_;

    // Awaitable returned by suspendFor() below -- OpCode::Suspend's own
    // `co_await` target inside runAsync(). await_suspend() records this
    // coroutine's own handle into pendingAsyncResumes_ above rather
    // than touching anything on Scheduler, for the same layering
    // reason resumeReadyAsyncTasks() documents just above.
    struct AsyncTimerAwaiter {
        VM* vm;
        std::chrono::steady_clock::time_point resumeAt;
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> h) const {
            vm->pendingAsyncResumes_.push_back({resumeAt, h});
        }
        void await_resume() const noexcept {}
    };
    AsyncTimerAwaiter suspendFor(std::chrono::steady_clock::duration delay) {
        return AsyncTimerAwaiter{this, std::chrono::steady_clock::now() + delay};
    }
};

} // namespace amlp
