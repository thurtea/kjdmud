#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "amlp/object/LpcObject.hpp"
#include "amlp/security/UidModel.hpp"

namespace amlp {

class Config;
class VM;

class ObjectManager {
public:
    explicit ObjectManager(Config& config);

    // Process-shutdown teardown: releases every object this manager
    // retains (the loaded_ blueprint/clone table, restoredObjects_, and
    // master_/simulEfunObject_). When this is the last ObjectManager
    // alive it then calls LiveObjectRegistry::releaseAll() to break the
    // intra-object strong reference cycles that would otherwise keep an
    // LpcObject (and its CompiledProgram and bytecode) alive past this
    // point. Does not touch vm_: in both real startup (main.cpp) and
    // every test harness (ObjectVarHarness) the VM is declared after the
    // ObjectManager and so is already gone when this runs. No LPC
    // applies, no master notification, this is not a destruct().
    ~ObjectManager();

    void setVM(VM* vm) { vm_ = vm; }

    // Real FluffOS's own driver-internal "efun_defined(name)" (distinct
    // from the standard cpp "defined(name)", which real system cpp
    // already handles correctly on its own): a real, hand-rolled
    // preprocessor construct (fluffos-2.23-ds03's own lex.c:3040, "if
    // (strcmp(yytext, \"defined\") == 0 || strcmp(yytext,
    // \"efun_defined\") == 0)") usable inside a real "#if"/"#elif" to
    // conditionally compile mudlib code based on whether a given optional
    // efun is actually compiled into the real driver -- real semantics:
    // "ihe->dn.efun_num != -1" against the real driver's own efun table.
    // This driver shells out to real, unmodified system cpp (see
    // ObjectManager.cpp's own runPreprocessor() module comment), which
    // has no knowledge of this driver-specific extension at all, so
    // "efun_defined(name)" must be rewritten to a literal "1"/"0" before
    // system cpp ever sees it -- exactly the same "system cpp cannot
    // resolve this, so this driver's own pre-pass must" discipline
    // rewriteAbsoluteIncludesRecursive() already established for a
    // mudlib-root-relative absolute include. That rewrite needs to ask
    // "does *this* driver actually implement efun X", i.e. EfunTable, but
    // this whole object library cannot link against efun at all (efun
    // already depends on object -- see src/efun/CMakeLists.txt -- so the
    // reverse dependency would be a real link cycle), so the answer is
    // injected here as a plain callback instead, wired from main.cpp
    // (which already links both, no cycle) right after
    // registerCoreEfuns() populates the real table. Defaults to "nothing
    // is a known efun" when never set (e.g. this driver's own test
    // harnesses, none of which exercise this real corpus construct) --
    // real, correct behavior for that case too: with no checker wired
    // in, nothing can honestly be confirmed available.
    void setEfunExistsChecker(std::function<bool(const std::string&)> checker) {
        efunExistsChecker_ = std::move(checker);
    }

    bool loadMasterObject();
    std::shared_ptr<LpcObject> masterObject() const { return master_; }

    // The FluffOS uid/euid model (ROADMAP row 3.1). Populated by
    // captureBootUids() the moment loadMasterObject() succeeds:
    // rootUid/backboneUid from master->get_root_uid()/get_bb_uid(), and
    // uidModel().active() is true iff get_root_uid() returned a string
    // (this driver's stand-in for real's PACKAGE_UIDS compile flag).
    // Read by main.cpp for its boot banner and by the seteuid()/
    // getuid() efuns' fallback behavior.
    const UidModel& uidModel() const { return uidModel_; }

    // Non-fatal by design (see .cpp): a missing/misconfigured simul_efun
    // file should not prevent the rest of the driver from booting while
    // this is still under incremental construction, unlike master_file.
    // Returns false if config_.simulEfunFile() is empty (not configured)
    // or the load failed; either way simulEfunObject() then just stays
    // nullptr and the simul_efun function-resolution tier is skipped.
    bool loadSimulEfunObject();
    std::shared_ptr<LpcObject> simulEfunObject() const { return simulEfunObject_; }

    std::shared_ptr<LpcObject> loadObject(const std::string& filename);
    std::shared_ptr<LpcObject> cloneObject(const std::string& filename);

    // Look-only, no compile-on-miss: real func_spec.c's own
    // "find_object(string, int default: 0)" -- unlike VM::findObject()
    // (which wraps loadObject() for call_other()'s string-target
    // overload, real find_object()'s own unconditional-compile
    // behavior as actually implemented in simulate.c), a bare
    // find_object(path) call from LPC code defaults to *not* compiling
    // a miss, only "load_object(path)" (a real alias with default arg
    // 1 instead of 0) does. See the find_object efun in EfunTable.cpp
    // for how both are exposed from this one lookup.
    std::shared_ptr<LpcObject> lookupLoadedObject(const std::string& filename) const;

    // ROADMAP.md row 2.1 (world statedump): a real gap found while
    // building StateSerializer::restoreState(), not part of the
    // original design note, recorded here rather than silently worked
    // around. cloneObject() itself never adds a clone to any persistent
    // registry (see its own header comment) -- an ordinary live clone
    // stays alive only because *some* LPC-reachable reference points at
    // it (a variable, an environment's inventory, ...), exactly this
    // driver's existing model. A restoreState() call reconstructs a
    // whole graph of objects from nothing, in one pass, with nothing
    // yet holding a reference to any of it once the call returns -- an
    // isolated restored object (or an acyclic restored graph) would
    // otherwise be correctly, and silently, freed the instant
    // restoreState() returns, before the caller ever gets to use it.
    // This method exists so restoreState() has somewhere real to put
    // its own reconstructed objects' ownership, mirroring how loaded_
    // below already keeps every blueprint alive for this same
    // ObjectManager's own lifetime -- not a change to ordinary
    // clone_object()'s own ownership semantics anywhere else in this
    // driver, only used by StateSerializer.
    void retainRestoredObjects(std::vector<std::shared_ptr<LpcObject>> objs);

    // onDestructed, if set, is called exactly once for every object this
    // call *actually* destructs -- both obj itself and, for the real
    // shadow-cascade case (see this method's own .cpp comment), every
    // shadowing object destructed along with it, each at the moment its
    // own real destruction steps run (matching real destruct_object()'s
    // own recursive calls, each independently running its own full body
    // for whichever object it was actually invoked on). Threaded through
    // this way, rather than called directly here, so that
    // ObjectManager itself never has to depend on `net` -- see
    // EfunTable.cpp's own destruct() registration, the one real caller
    // that needs this (closing every efun socket the destructed object
    // owns, real destruct_object()'s own close_referencing_sockets(ob)
    // call, simulate.c). Empty by default: every other caller of this
    // method (the recursive cascade calls inside this same class) simply
    // forwards whatever it was given, never invents its own.
    void destructObject(const std::shared_ptr<LpcObject>& obj,
                         const std::function<void(const std::shared_ptr<LpcObject>&)>& onDestructed = {});

    // real object.c's own reload_object(obj): resets obj back to a
    // freshly-cloned-looking state in place (same identity, same
    // shared_ptr) rather than reconstructing it. Handles every real
    // step that stays within this class's own dependency envelope --
    // zeroing every object variable, the real shadow-chain cascade/
    // splice (identical real semantics to destructObject()'s own, just
    // never destructing obj itself), remove_living_name(), and finally
    // re-running the synthesized "$objvarinit" initializers followed by
    // create() (real call_create()'s own "call___INIT(ob); ...;
    // apply(APPLY_CREATE, ob, 0, ORIGIN_DRIVER);", confirmed directly --
    // not just create() alone, so a top-level "int x = 5;" declaration
    // really does end up back at 5, not 0, after a reload). Two real
    // steps skipped, both confirmed real in this exact vendored build's
    // own options.h (`NO_LIGHT` undefined, `PACKAGE_UIDS`+`AUTO_SETEUID`
    // both defined) but with no equivalent model in this driver:
    // `add_light(obj, -(obj->total_light))` (no light-level system at
    // all, same gap already excluded elsewhere) and `obj->euid =
    // obj->uid;` (this driver has only a single `privs_` field, no
    // separate uid/euid pair to reset one from the other -- privs_ is
    // set once at construction and never diverges from it here anyway,
    // so there is nothing observably different to reset back to).
    // Socket-close and heart_beat/call_out removal are real steps too
    // (`close_referencing_sockets`, `set_heart_beat(obj, 0)`,
    // `remove_all_call_out(obj)`) but live outside this class's own
    // dependency envelope (`object` cannot depend on `net`/`scheduler`,
    // which both already depend on `object`) -- see
    // EfunTable.cpp's own reload_object() registration for where those
    // two run instead, and why doing them before this call rather than
    // real code's own interleaved order has no observable effect.
    //
    // onDestructed: forwarded straight through to every internal
    // destructObject() call this method's own shadow-chain cascade
    // makes (see that method's own header comment for the full
    // derivation) -- real destruct_object() itself unconditionally
    // closes referencing sockets for *any* object it destructs, so an
    // object cascade-destructed here (something that was shadowing obj,
    // not obj itself) needs the identical treatment a bare destruct()
    // call already gives it, not a narrower one just because the
    // destruction happened to be triggered by a reload.
    void reloadObject(const std::shared_ptr<LpcObject>& obj,
                       const std::function<void(const std::shared_ptr<LpcObject>&)>& onDestructed = {});

    // Strips one trailing ".c" if present, so "id_card" and "id_card.c"
    // resolve to the exact same cache entry and object identity. Real
    // LPC object paths never carry the ".c" extension internally (this
    // driver appends it itself to find the source file on disk -- see
    // compile()'s own "filename + \".c\"" below), but plenty of real
    // mudlib call sites still pass one anyway out of habit (e.g. daemon/
    // rifts_start_d.c's own give_item(player, "id_card.c")). Without this,
    // such a call resolved to a literal "id_card.c.c" file that never
    // exists. Same normalization shape as save_object()'s own path
    // handling (EfunTable.cpp), applied on the load/compile side instead.
    // Public: replace_program() (EfunTable.cpp) reuses this exact
    // canonicalization to match its own string argument against each
    // inherited program's own raw inherit-path string, rather than
    // inventing a second, potentially-diverging normalization rule.
    static std::string normalizeFilename(const std::string& filename);

private:

    // Row 0.15 fix: a cache hit here used to return unconditionally, with
    // no path that ever noticed a same-path recompile (e.g. eval.c's own
    // rm+destruct+write_file+reload cycle, or wand_of_creation.c's
    // cmd_create() doing the identical thing for a reused item name) --
    // see programCache_'s own comment below for how a hit is now
    // validated against the file's current on-disk content before being
    // trusted, and ObjectManager.cpp's own compile() comment for the full
    // derivation.
    std::shared_ptr<CompiledProgram> compile(const std::string& filename);

    // Runs "$objvarinit" (see CodeGen::generate()'s own comment) on obj
    // for program and, first, every program it inherits (parent-before-
    // child, matching real LPC's own object-variable-initializer
    // ordering) -- called once per new instance, immediately before
    // "create", from both loadObject() and cloneObject() below. Each
    // level uses VM::callFunctionInProgram() rather than the normal
    // tiered lookup specifically so a parent's own initializers still
    // run even when a child also declares (and therefore also
    // synthesizes its own same-named "$objvarinit" for) initialized
    // variables of its own.
    void runObjectVarInitializers(const std::shared_ptr<LpcObject>& obj, const CompiledProgram& program);

    // real int_load_object()'s own virtual-object fallback (simulate.c):
    // when a plain load_object()/find_object() names a path with no
    // matching ".c" file on disk, the real driver does not treat that
    // as a hard failure -- it calls the master apply
    // compile_object(path, 0) and, if that returns a real object,
    // rebinds it to the requested path and uses it as though it had
    // been compiled from there (secure/daemon/master.c uses exactly
    // this for player objects: "/secure/save/users/t/name" has no .c
    // file at all, compile_object()'s own DIR_USERS branch instead
    // clones /std/user and returns that clone). See ObjectManager.cpp's
    // own comment on this method for the full citation and the
    // rebinding this replicates.
    std::shared_ptr<LpcObject> loadVirtualObject(const std::string& filename);
    bool sourceFileExists(const std::string& filename) const;

    // Real simulate.c's own init_privs_for_object(), called from
    // init_object() for every freshly compiled or cloned object before
    // its own create() runs: "push_malloced_string(add_slash(ob->obname));
    // value = apply_master_ob(APPLY_PRIVS_FILE, 1); ob->privs = (value &&
    // value->type == T_STRING) ? value->u.string : NULL;" -- this
    // driver had no equivalent at all (LpcObject's own privs_ optional
    // simply stayed unset for every object unless a mudlib file called
    // set_privs() on itself directly), confirmed live needing this:
    // secure/SimulEfun/log_file.c's own "explode(query_privs(
    // previous_object()), \":\")" throws when query_privs() returns 0
    // (this driver's correct "unset" value, matching real semantics --
    // see query_privs()'s own EfunTable.cpp comment) for any object
    // whose compile-time privs were never actually assigned. Skipped
    // (leaving privs unset, the real driver's own "!current_object"
    // bootstrap branch outcome) when master_ itself is not loaded yet --
    // this only matters for master.c's own bootstrap load, nothing this
    // driver has run yet depends on the master object's own privs.
    void initPrivsForObject(const std::shared_ptr<LpcObject>& obj, const std::string& filename);

    // real reset_object(ob, H_CREATE_OB|H_CREATE_CLONE|H_CREATE_SUPER, 0)
    // being called at load/clone time (object.c:800-885 -- see its own
    // header comment on LpcObject::armReset() for the exact real formula
    // this wraps) plus real simulate.c's own unconditional "ob->flags |=
    // O_WILL_CLEAN_UP;" right after creation succeeds (simulate.c:2263,
    // 2448). Called from loadObject()/cloneObject() after create() runs,
    // only if the object survived it (a create() that self-destructs has
    // nothing left to arm) -- matching real code's own "if (!(ob->flags &
    // O_DESTRUCTED))" guard around the O_WILL_CLEAN_UP assignment
    // (simulate.c:2261). Real TIME_TO_RESET default (1800 seconds,
    // temp/ldmud/autoconf/configure's own "DEFAULTwith_time_to_reset=1800",
    // confirmed against the real vendored ./configure --help text too:
    // "--with-time-to-reset=SECONDS  default=1800") -- not yet exposed as
    // a Config-tunable in this first slice, matching zero real corpus
    // evidence of any mudlib overriding it via the configure() efun.
    void armResetAndCleanup(const std::shared_ptr<LpcObject>& obj);

    // Real master.c:107-138 set_master(): right after the master object
    // loads, apply get_root_uid() / get_bb_uid() on it, stash the two
    // strings in uidModel_, and set master->uid = master->euid =
    // rootUid. A master with no get_root_uid() leaves uidModel_ inactive
    // (real: mudlib built without PACKAGE_UIDS). Called once, from
    // loadMasterObject(), after master_ is assigned.
    void captureBootUids();

    // Real simulate.c:132-206 give_uid_to_object(), run from
    // init_object() for every freshly compiled or cloned object before
    // its create(): ask master->creator_file("/path"), then
    // resolveObjectUids() (security/UidModel.hpp) decides the (uid,
    // euid) pair from the creator name and the loader's own uid/euid.
    // No-op unless uidModel_.active(). Named divergence from real: real
    // destructs the object and errors when creator_file() is missing or
    // returns a non-string; this driver instead assigns uid = rootUid
    // (root-owned) and keeps the object, so a partially-uid-aware mudlib
    // still boots.
    void assignObjectUid(const std::shared_ptr<LpcObject>& obj, const std::string& filename);

    Config& config_;
    VM* vm_ = nullptr;
    // See setEfunExistsChecker()'s own comment. Defaults to "nothing is
    // a known efun" until main.cpp wires the real EfunTable-backed one
    // in.
    std::function<bool(const std::string&)> efunExistsChecker_ =
        [](const std::string&) { return false; };
    UidModel uidModel_;
    std::shared_ptr<LpcObject> master_;
    std::shared_ptr<LpcObject> simulEfunObject_;
    std::unordered_map<std::string, std::shared_ptr<CompiledProgram>> programCache_;
    // Row 0.15: the exact raw (pre-preprocessing) source text that
    // produced each programCache_ entry, keyed by the same normalized
    // filename. compile() re-reads the file on every call (cheap -- LPC
    // source files are small, and this only runs at compile time, never
    // per-tick) and compares it against this before trusting a cache hit;
    // a mismatch means the same path was compiled before but its source
    // has since changed underneath it (destructed and rewritten, e.g.),
    // so the stale entry is replaced instead of silently reused. Byte
    // comparison rather than an mtime check deliberately: two writes to
    // the same path within one filesystem timestamp tick (routine in a
    // test harness, and not implausible live) would otherwise look
    // unchanged despite genuinely different content. Only covers the
    // recompiled file's own text -- a header it #includes changing
    // independently, with this file's own bytes untouched, is not
    // detected (out of scope for this row: see ROADMAP.md's 0.15 note).
    std::unordered_map<std::string, std::string> programSource_;
    std::unordered_map<std::string, std::shared_ptr<LpcObject>> loaded_;
    // See retainRestoredObjects()'s own header comment. destructObject()
    // below erases an entry from here too, so an explicit later
    // destruct() of a restored object still actually frees it instead of
    // pinning it in memory forever.
    std::vector<std::shared_ptr<LpcObject>> restoredObjects_;
    // Filenames currently mid-compile, so an inherit cycle (A inherits B
    // inherits A) is caught as a compile error instead of infinite
    // recursion. See compile()'s recursive inherit resolution.
    std::unordered_set<std::string> compiling_;
    // Same idea as compiling_ above, but for loadVirtualObject()'s own
    // compile_object() apply -- guards against compile_object() somehow
    // calling load_object() back on the exact same still-unresolved
    // virtual path (real driver has no such recursion either; this is
    // this driver's own safety net, not a replicated mechanism).
    std::unordered_set<std::string> virtualCompiling_;
};

} // namespace amlp
