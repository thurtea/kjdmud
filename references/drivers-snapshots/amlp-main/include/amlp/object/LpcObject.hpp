#pragma once
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "amlp/vm/Bytecode.hpp"
#include "amlp/vm/Value.hpp"

namespace amlp {

class LpcObject : public std::enable_shared_from_this<LpcObject> {
public:
    // `fluffosDialect` gates whether a freshly declared object variable
    // is tagged real FluffOS's own T_UNDEFINED (Value::isUndefined, see
    // Value.hpp's own comment) -- real LDMud has no equivalent concept,
    // so callers under any other dialect pass false and get a plain,
    // untagged 0 instead (see LpcObject.cpp's own constructor comment).
    // Defaults to true (Config::dialect()'s own default is "fluffos"),
    // so any construction site that genuinely does not care still gets
    // real FluffOS behavior rather than silently reverting to the old
    // untagged default.
    LpcObject(std::string filename, std::shared_ptr<CompiledProgram> program,
              bool fluffosDialect = true);

    const std::string& filename() const { return filename_; }

    // Real load_virtual_object() (simulate.c) renames a master-apply-
    // returned object to the virtual path that was actually requested
    // (SETOBNAME + object-hash reinsertion) so file_name()/base_name()
    // and any later find_object()/load_object() for that same path all
    // agree on identity. See ObjectManager::loadVirtualObject().
    void rebindFilename(std::string filename) { filename_ = std::move(filename); }

    CompiledProgram& program() { return *program_; }
    const CompiledProgram& program() const { return *program_; }

    // Reassigns which compiled program this object's own function/
    // variable-slot resolution runs against -- real replace_program()'s
    // own "r_ob->ob->prog = r_ob->new_prog;" (replace_program.c). Never
    // called directly by the replace_program() efun itself (real
    // replace_programs() defers this exact assignment to the next driver
    // tick, see VM::processPendingReplacePrograms()); exposed here as a
    // plain setter purely so that deferred-application code, which lives
    // in VM rather than this class, has somewhere to reach the
    // otherwise-read-only program_ pointer itself, not just what it
    // currently points to.
    void setProgram(std::shared_ptr<CompiledProgram> program) { program_ = std::move(program); }
    std::shared_ptr<CompiledProgram> programPtr() const { return program_; }

    // real object_t::replaced_program (object.h): the name last passed
    // to a successfully-applied replace_program() call, or unset if this
    // object was never replaced -- backs query_replaced_program()
    // (packages/contrib.c). Set only by VM::processPendingReplacePrograms(),
    // the same place setProgram() above is actually called from; cleared
    // on destruct alongside everything else object.c's own real
    // destructor clears (see ObjectManager::destructObject()), matching
    // real "if (ob->replaced_program) { FREE_MSTR(...); ob->replaced_program
    // = 0; }" exactly.
    const std::optional<std::string>& replacedProgramName() const { return replacedProgramName_; }
    void setReplacedProgramName(std::optional<std::string> name) {
        replacedProgramName_ = std::move(name);
    }

    std::vector<Value>& variables() { return variables_; }

    // real object_t's O_HEART_BEAT flag plus its heart_beats[]-entry own
    // "time_to_heart_beat" (backend.c's set_heart_beat()/query_heart_beat()):
    // 0 means disabled; a nonzero interval is how many heartbeat cycles
    // (real default HEARTBEAT_INTERVAL == 2 real seconds each, see
    // Scheduler) elapse between this object's own heart_beat() calls, not
    // just an on/off flag -- confirmed live-needed distinction: std/germ.c's
    // own set_heart_beat(5) relies on a slower cadence than the default,
    // and query_heart_beat() must report that real interval back (backend.c's
    // own query_heart_beat(object_t*) returns heart_beats[index].time_to_heart_beat,
    // not a bare 1). The actual per-cycle countdown state lives in
    // Scheduler's own heartbeat list, not here -- this field is only the
    // configured interval a fresh Scheduler entry resets its countdown to.
    bool hasHeartbeat() const { return heartbeatInterval_ != 0; }
    int heartbeatInterval() const { return heartbeatInterval_; }
    void setHeartbeatInterval(int interval) { heartbeatInterval_ = interval; }

    // real object_t::living_name (set_living_name()). This driver's own
    // find_player() (EfunTable.cpp) does not consult this -- it walks
    // InteractiveRegistry and asks each object its own query_name()
    // instead, a deliberate, already-documented simplification -- so
    // nothing currently reads livingName_ back. It is still stored
    // (rather than a bare no-op efun) so a real consumer can be added
    // later without another round trip through this field's own
    // plumbing, and so query_living_name()-style introspection is at
    // least possible. Surfaced live: std/user.c's own setup() calling
    // set_living_name(query_name()) unconditionally, not gated behind
    // anything this driver's boot path could otherwise skip.
    const std::string& livingName() const { return livingName_; }
    void setLivingName(std::string name) { livingName_ = std::move(name); }

    // real object_t's "super" (environment) and "contains"/"next_inv"
    // (inventory), simplified from FluffOS's intrusive doubly-linked
    // list to a plain vector -- this driver already uses the same
    // simplification elsewhere (e.g. InteractiveRegistry) and nothing
    // confirmed live needs true O(1) unlink or FluffOS's specific
    // next_inv traversal order. environment_ is weak_ptr so an object
    // does not keep its own environment alive (matches real semantics:
    // environment() can legitimately return an object that is about to
    // be destructed once nothing else references it); inventory_ holds
    // real (owning) shared_ptrs, since being "in" something is exactly
    // the reference that keeps a mudlib object alive in this driver
    // (there is no separate global object table the way real FluffOS
    // has one). See VM::moveObject() for how these are kept consistent.
    std::weak_ptr<LpcObject> environment() const { return environment_; }
    void setEnvironment(std::weak_ptr<LpcObject> env) { environment_ = std::move(env); }
    std::vector<std::shared_ptr<LpcObject>>& inventory() { return inventory_; }
    const std::vector<std::shared_ptr<LpcObject>>& inventory() const { return inventory_; }

    // real O_ENABLE_COMMANDS flag (enable_commands()/disable_commands()).
    bool commandsEnabled() const { return commandsEnabled_; }
    void setCommandsEnabled(bool on) { commandsEnabled_ = on; }

    // real object_t's O_DESTRUCTED flag (object.h). Set once, by
    // ObjectManager::destructObject(), and checked at every "call into
    // this object from outside" entry point this driver has (see
    // VM::callFunction()/callClosure()/moveObject()/dispatchCommand(),
    // each citing this flag directly) -- matching real apply()'s own
    // "DEBUG_CHECK(ob->flags & O_DESTRUCTED, ...)" gate (interpret.c).
    // Previously this driver had no such flag at all: a destructed
    // LpcObject just kept working as an ordinary C++ object, reachable
    // and callable through any shared_ptr still pointing at it, until
    // the last one dropped -- confirmed live-reachable via a room's own
    // inventory (destruct() never unlinked the object from its
    // environment either, see destructObject()'s own comment), not just
    // a theoretical gap.
    bool isDestructed() const { return destructed_; }
    void setDestructed(bool d) { destructed_ = d; }

    // real object_t's O_ONCE_INTERACTIVE flag (object.h): set once, the
    // first time this object is ever bound to a connection
    // (Connection::attach(), which covers both the initial login-object
    // bind and a later exec() rebind onto the real player object), and
    // never cleared again -- not even once the connection later
    // disconnects. This is the flag userp()/query_once_interactive()
    // actually check (func_spec.c real aliases of the same efun),
    // distinct from interactive()'s own "is it interactive *right now*"
    // check (InteractiveRegistry membership, which does clear on
    // disconnect). See EfunTable.cpp's "userp" registration for why this
    // driver previously conflated the two.
    bool wasEverInteractive() const { return everInteractive_; }
    void setWasEverInteractive(bool v) { everInteractive_ = v; }

    // real object_t's O_HIDDEN flag (set_hide()/query_hidden()). Real
    // f_set_hide() (efuns_main.c) also maintains two global counters,
    // num_hidden and num_hidden_users, used elsewhere by real FluffOS's
    // own users()/heart_beat() bookkeeping for reporting how many
    // connected users are currently hidden -- not replicated here, since
    // this driver's users()/interactive() have no privileged-vs-
    // unprivileged visibility filtering of any kind yet (a hidden flag
    // with nothing reading it back is still a real improvement over
    // "undefined efun", matching this session's own explicitly narrow
    // scope: only the flag itself, not the users()-filtering feature it
    // would eventually gate). See EfunTable.cpp's "set_hide" registration
    // for the real valid_hide() master-apply gate this flag is set behind.
    bool isHidden() const { return hidden_; }
    void setHidden(bool h) { hidden_ = h; }

    // real object_t's O_IS_WIZARD flag (enable_wizard()/disable_wizard()/
    // wizardp()). Same shape and same deliberately narrow scope as
    // hidden_/isHidden() just above: only the flag itself, not every
    // real behavior it gates (real enable_wizard() also grants
    // restricted-ed access and trace()/traceprefix() privilege; real
    // error_handler() also reads it to decide full-trace vs.
    // DEFAULT_ERROR_MESSAGE for a connected player -- none of that
    // consumes this flag yet, see EfunTable.cpp's "enable_wizard"
    // registration for the full citation). A wizard flag nothing reads
    // back yet is still a real improvement over "undefined efun",
    // matching this session's own explicitly narrow scope.
    bool isWizard() const { return isWizard_; }
    void setWizard(bool w) { isWizard_ = w; }

    // real object_t::privs (set_privs()/query_privs()) -- an arbitrary
    // per-object "privilege string" the mudlib sets and later checks for
    // permission gating (secure/daemon/master.c's own valid_write()-
    // style checks: "if(!(priv = query_privs(stack[i]))) return 0;").
    // std::optional distinguishes real FluffOS's "ob->privs == NULL"
    // (never set, or explicitly cleared with a non-string second
    // argument to set_privs()) from an explicitly-set value, matching
    // query_privs()'s own "return 0 if unset" contract precisely.
    const std::optional<std::string>& privs() const { return privs_; }
    void setPrivs(std::optional<std::string> privs) { privs_ = std::move(privs); }

    // real object_t::uid / object_t::euid (packages/uids.c, object.h:107-
    // 108). ROADMAP row 3.1. uid_ is the object's "owner" (getuid());
    // euid_ its "effective owner" (geteuid()), the identity a load or
    // clone from this object inherits and the one file-access checks
    // consult. std::nullopt on euid_ is real's "euid == NULL": geteuid()
    // returns 0 for it, and (next slice, row 3.2) clone_object() refuses
    // until the object seteuid()s itself. uid_ is unset only for an
    // object created before the uid model went active, or under a mudlib
    // that never defined get_root_uid() (real: built without
    // PACKAGE_UIDS). Interned-string sharing (real add_uid()'s AVL tree)
    // is not reproduced: every efun reads only the name, so a plain
    // owned string is observationally identical. See
    // include/amlp/security/UidModel.hpp for the full model.
    const std::optional<std::string>& uid() const { return uid_; }
    void setUid(std::optional<std::string> uid) { uid_ = std::move(uid); }
    const std::optional<std::string>& euid() const { return euid_; }
    void setEuid(std::optional<std::string> euid) { euid_ = std::move(euid); }

    // real sentence_t list (add_action.c) -- one entry per add_action()
    // registration currently active on this object as a command_giver.
    // front() is the most recently added entry: real add_action()
    // always prepends ("adding to the top of the list doesn't harm
    // anything", add_action.c's own comment) so the newest registration
    // is checked first at dispatch time, matching real precedence
    // between an inner room's own verbs and a more general handler
    // registered earlier. owner is the object whose function actually
    // gets called (real sentence_t::ob) -- not necessarily this object,
    // since add_action() registers onto command_giver but the function
    // named runs against whichever object called add_action(). weak_ptr
    // so an action does not keep its owner alive by itself; a
    // destructed owner is simply skipped at dispatch time (see
    // VM::dispatchCommand()), the same "not specifically guarded, just
    // stops mattering once the shared_ptr count drops" approach this
    // driver already takes for destruct() elsewhere.
    struct ActionEntry {
        std::string verb;
        std::string functionName;
        std::weak_ptr<LpcObject> owner;
        int flag = 0; // 0 = exact, 1 = V_SHORT, 2 = V_NOSPACE (real add_action.c flag & 3)
    };
    std::vector<ActionEntry>& actions() { return actions_; }
    void addAction(ActionEntry entry) { actions_.insert(actions_.begin(), std::move(entry)); }
    // real remove_action(): erases the first (most-recently-added, i.e.
    // lowest index) entry whose owner/function/verb all match, mirroring
    // add_action.c's own "*s = tmp->next" unlink -- see EfunTable.cpp's
    // "remove_action" registration for the exact match rule.
    bool removeAction(const std::shared_ptr<LpcObject>& owner, const std::string& functionName,
                       const std::string& verb) {
        for (auto it = actions_.begin(); it != actions_.end(); ++it) {
            if (it->owner.lock() == owner && it->functionName == functionName && it->verb == verb) {
                actions_.erase(it);
                return true;
            }
        }
        return false;
    }

    // real object_t's "shadowed" and "shadowing" fields (object.h), named
    // to match exactly rather than instruct.md's own proposed
    // "shadow_"/"shadowedBy_" pair, which had the relationship backwards
    // in one direction -- confirmed directly against interpret.c's
    // apply_low() and efuns_main.c's f_shadow() before naming these:
    // shadowedBy() is "the object currently shadowing me" (real
    // ob->shadowed; a victim's own field), shadowing() is "the object I
    // am myself shadowing" (real ob->shadowing; a shadow's own field).
    // Both weak_ptr: neither side of a shadow relationship keeps the
    // other alive on its own, matching every other cross-object
    // reference in this class. See VM::callFunction()'s own comment for
    // the real two-phase chain walk these back.
    std::weak_ptr<LpcObject> shadowedBy() const { return shadowedBy_; }
    void setShadowedBy(std::weak_ptr<LpcObject> ob) { shadowedBy_ = std::move(ob); }
    std::weak_ptr<LpcObject> shadowing() const { return shadowing_; }
    void setShadowing(std::weak_ptr<LpcObject> ob) { shadowing_ = std::move(ob); }

    // real object.h's O_VIRTUAL flag: set only by
    // ObjectManager::loadVirtualObject()'s own construction path (an
    // object returned by master()->compile_object() rather than compiled
    // directly from an on-disk file), never cleared. Backs virtualp() --
    // see EfunTable.cpp's own registration and std/virtual.c's real
    // "if(virtualp(this_object())) return 0;" recursion guard, the one
    // confirmed live call site.
    bool isVirtual() const { return isVirtual_; }
    void setIsVirtual(bool v) { isVirtual_ = v; }

    // real object_t::total_light (simulate.c's add_light()): a running
    // count of light sources this object and everything inside it
    // contribute, propagated up through every ancestor environment on
    // every change (see EfunTable.cpp's "set_light" registration, the
    // only mutator). Deprecated in real FluffOS itself (func_spec.c's own
    // "/* set_light should die a dark death */" comment right above its
    // declaration) but still a real, registered efun this repo's own
    // bundled Lil starter mudlib genuinely calls
    // (mudlib/single/tests/efuns/light.c's own create()).
    int totalLight() const { return totalLight_; }
    void setTotalLight(int v) { totalLight_ = v; }

    // Real interactive_t::snooped_by (comm.h) is a per-*connection* field,
    // one-directional: only "who is snooping me" is stored directly, and
    // "who am I snooping" (query_snooping()) is derived by a linear scan
    // over all_users[] for whichever entry's own snooped_by points back at
    // this object (comm.c's own query_snoop()/query_snooping(), confirmed
    // directly before implementing). This driver has no all_users[]
    // equivalent efun code can scan (InteractiveRegistry lives in the net
    // module, not reachable from here without a new cross-module
    // dependency for what is otherwise a pure bookkeeping choice), so
    // snoopedBy_/snooping_ are kept as a genuinely symmetric pair instead
    // -- an internal representation difference only, not an observable
    // semantics one: query_snoop()/query_snooping() report identical
    // results either way, this is just an O(1) lookup instead of real
    // FluffOS's own O(n) scan. Both weak_ptr, same non-owning convention
    // as shadowedBy_/shadowing_ just above.
    std::weak_ptr<LpcObject> snoopedBy() const { return snoopedBy_; }
    void setSnoopedBy(std::weak_ptr<LpcObject> ob) { snoopedBy_ = std::move(ob); }
    std::weak_ptr<LpcObject> snooping() const { return snooping_; }
    void setSnooping(std::weak_ptr<LpcObject> ob) { snooping_ = std::move(ob); }

    // real object_t::pinfo != 0 (packages/parser.h) -- the gate
    // parse_add_rule()/parse_refresh()/parse_sentence()/parse_my_rules()
    // all check before doing anything else ("/%s is not known by the
    // parser. Call parse_init() first."). parseInfoFlags() below is real
    // parse_info_t::flags, valid only while this is true (real code
    // frees the whole pinfo struct, flags included, once it goes back
    // to null -- see parse_free()'s own real body). See ParserPackage.hpp
    // for the verb/rule registry these flags gate, and its own
    // ParserInfoFlag namespace for the real PI_* bit values.
    bool hasParseInfo() const { return hasParseInfo_; }
    void setHasParseInfo(bool v) { hasParseInfo_ = v; }
    int parseInfoFlags() const { return parseInfoFlags_; }
    void setParseInfoFlags(int flags) { parseInfoFlags_ = flags; }

    // real parse_info_t::ids/plurals/adjs (packages/parser.h) -- this
    // object's own cached noun/plural/adjective word lists, populated by
    // ParserPackage's real interrogate_object() port (ROADMAP.md row
    // 0.13a item 8, piece 1: the real parse_command_id_list()/
    // parse_command_plural_id_list()/parse_command_adjectiv_id_list()
    // applies -- note the real, faithfully-kept "adjectiv" typo in the
    // apply's own real name, confirmed directly against
    // fluffos-2.9-ds2.08/applies.h's APPLY_ADJECTIVE). Valid only while
    // hasParseInfo() is true, exactly like parseInfoFlags() above; the
    // ParserInfoFlag::Setup bit says whether these have ever actually
    // been populated (a freshly-parse_init()'d object has empty vectors
    // here, not yet distinguishable from "genuinely has no nouns" until
    // Setup is checked).
    const std::vector<std::string>& parseNounIds() const { return parseNounIds_; }
    void setParseNounIds(std::vector<std::string> v) { parseNounIds_ = std::move(v); }
    const std::vector<std::string>& parsePluralIds() const { return parsePluralIds_; }
    void setParsePluralIds(std::vector<std::string> v) { parsePluralIds_ = std::move(v); }
    const std::vector<std::string>& parseAdjIds() const { return parseAdjIds_; }
    void setParseAdjIds(std::vector<std::string> v) { parseAdjIds_ = std::move(v); }

    // real object_t's O_RESET_STATE flag (object.h: "is in a virgin
    // resetted state"): true immediately after reset()/create() has run
    // and nothing has touched the object since; Scheduler's own reset
    // sweep does a real reset() call only when this is false, otherwise
    // just silently reschedules (real backend.c's own "virtual" reset --
    // see object.c:75-82's own doc comment, "the backend simply sets a
    // new .time_reset time, but does not do any real action"). Cleared
    // by VM::callFunction() (real apply_low()'s own "The function call
    // will swap in the object and also unset its reset status.",
    // interpret.c:20311) and by set_environment()/VM::moveObject() (real
    // object.c:5188-5198's own three O_RESET_STATE clears on item/dest/
    // old-super) -- both cited directly at their own call sites.
    bool resetState() const { return resetState_; }
    void setResetState(bool v) { resetState_ = v; }

    // real object_t's O_WILL_CLEAN_UP flag (object.h): set unconditionally
    // on every freshly loaded/cloned object (real simulate.c:2263/2448,
    // confirmed directly -- not gated on whether clean_up() is actually
    // defined; a driver-fired call to an undefined clean_up() simply
    // clears the flag the same way a defined one returning 0 does) and
    // cleared once a clean_up() call returns falsy/undefined, so a
    // destructed-or-uninterested object stops being asked again. See
    // Scheduler::tickResetsAndCleanup().
    bool willCleanUp() const { return willCleanUp_; }
    void setWillCleanUp(bool v) { willCleanUp_ = v; }

    // real object_t's O_CLONE flag (object.h), set only by
    // ObjectManager::cloneObject(). Backs the single real clean_up()
    // argument this driver reproduces: real backend.c's own "push_number
    // (inter_sp, (obj->flags & (O_CLONE|O_REPLACED)) ? 0 :
    // O_PROG_SWAPPED(obj) ? 1 : obj->prog->ref)" -- confirmed real corpus
    // usage cares about this (lima/lib/obj/secure/cmd.c's own "int
    // clean_up(int instances) { if (instances) return ASK_AGAIN; ...
    // }"), so clones get 0 (safe to self-destruct) and every other
    // object gets a fixed truthy 1, standing in for real code's own
    // program->ref refcount (this driver does not track per-program
    // reference counts; a live blueprint's real ref is always >= 1
    // anyway, so the practical truthy/falsy outcome real mudlib clean_up()
    // bodies branch on matches without needing the exact count) -- see
    // Scheduler::tickResetsAndCleanup() for where this is read.
    // O_REPLACED (replace_program()) has no corpus evidence combined
    // with clean_up() and is not folded in here.
    bool isClone() const { return isClone_; }
    void setIsClone(bool v) { isClone_ = v; }

    // real object_t::time_reset (object.h doc comment, object.c:75-82).
    // armReset() is real reset_object()'s own "Be sure to update time
    // first!" step (object.c:825-828): "current_time + time_to_reset/2 +
    // random_number(time_to_reset/2)", the real randomized reset-delay
    // window -- also sets resetState() true, matching real reset_object()'s
    // own unconditional "ob->flags |= O_RESET_STATE;" at its own end
    // (object.c:884). Called once at creation (ObjectManager, standing in
    // for real reset_object(ob, H_CREATE_OB, 0) always being called at
    // load/clone time) and again by Scheduler::tickResetsAndCleanup()
    // every time a reset (real or virtual) is due.
    std::chrono::steady_clock::time_point timeReset() const { return timeReset_; }
    void armReset(std::chrono::seconds timeToReset);

    // real reset_object()'s own "if (!sapply_ign_prot(...) && arg ==
    // H_RESET) ob->time_reset = 0;" (object.c:869-870) -- confirmed
    // identical in real FluffOS too, just spelled differently ("ob->flags
    // &= ~O_WILL_RESET; /* don't call it next time */", object.c:1905):
    // an object with no reset() lfun defined at all gets permanently
    // excluded from further reset attempts the very first time the
    // driver notices, in both real drivers. Represented here as an
    // unreachably-far-future timeReset() (steady_clock::time_point::max())
    // rather than a magic zero, matching the "0 means never" real
    // semantics without needing a separate sentinel check at every
    // timeReset() read site.
    void disableReset() { timeReset_ = std::chrono::steady_clock::time_point::max(); }

    // real object_t::time_of_ref (object.h doc comment: "the time() of
    // the last apply on this object"). touchTimeOfRef() is called by
    // VM::callFunction() on every call into this object from outside,
    // matching real apply_low()'s own "ob->time_of_ref = current_time;"
    // (interpret.c:20345, a few lines after the same function's own
    // O_RESET_STATE clear cited above). Backs the clean_up() eligibility
    // check ("time_since_ref > time_to_cleanup", backend.c:1403).
    std::chrono::steady_clock::time_point timeOfRef() const { return timeOfRef_; }
    void touchTimeOfRef() { timeOfRef_ = std::chrono::steady_clock::now(); }
    // Direct setter, real usage the same as CallOutEntry::dueAt/
    // HeartbeatEntry::ticksRemaining being plain public fields elsewhere
    // in this driver (Scheduler.hpp): lets a regression test simulate
    // "an hour has genuinely passed since this object was last touched"
    // without a real driver process ever waiting an hour, the same way
    // the call_out regression tests construct an already-past dueAt
    // directly rather than sleeping.
    void setTimeOfRef(std::chrono::steady_clock::time_point t) { timeOfRef_ = t; }

private:
    std::string filename_;
    std::shared_ptr<CompiledProgram> program_;
    std::optional<std::string> replacedProgramName_;
    std::vector<Value> variables_;
    int heartbeatInterval_ = 0;
    std::weak_ptr<LpcObject> environment_;
    std::vector<std::shared_ptr<LpcObject>> inventory_;
    bool commandsEnabled_ = false;
    bool destructed_ = false;
    bool everInteractive_ = false;
    bool hidden_ = false;
    bool isWizard_ = false;
    std::vector<ActionEntry> actions_;
    std::optional<std::string> privs_;
    std::optional<std::string> uid_;
    std::optional<std::string> euid_;
    std::string livingName_;
    std::weak_ptr<LpcObject> shadowedBy_;
    std::weak_ptr<LpcObject> shadowing_;
    std::weak_ptr<LpcObject> snoopedBy_;
    std::weak_ptr<LpcObject> snooping_;
    bool isVirtual_ = false;
    int totalLight_ = 0;
    bool hasParseInfo_ = false;
    int parseInfoFlags_ = 0;
    std::vector<std::string> parseNounIds_;
    std::vector<std::string> parsePluralIds_;
    std::vector<std::string> parseAdjIds_;
    bool resetState_ = false;
    bool willCleanUp_ = false;
    bool isClone_ = false;
    std::chrono::steady_clock::time_point timeReset_{};
    std::chrono::steady_clock::time_point timeOfRef_{};
};

} // namespace amlp
