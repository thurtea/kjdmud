#pragma once
#include <memory>
#include <string>
#include <vector>

namespace amlp {

class LpcObject;

// Real FluffOS's hashed_living[] table (add_action.c): every object
// currently registered under a living name via set_living_name(). Kept
// separate from InteractiveRegistry (net/InteractiveRegistry.hpp)
// because the real mechanism is not connection-scoped at all -- an NPC
// that never had a connection can set_living_name() just as validly as
// a player (std/monster.c's own create(): "enable_commands();" then
// later "set_living_name(str);"). Confirmed live-reachable, not
// theoretical: 18+ real call sites resolve a name back to an object via
// find_living(), most of them targeting NPCs or a possibly-offline
// player, not just a currently-connected one -- cmds/mortal/_whisper.c,
// daemon/mail_d.c, cmds/mortal/_psi.c, several admin commands
// (_scan.c/_trans.c/_wizheal.c/_teleport.c), and several NPC-targeting
// object files under domains/Praxis/.
//
// Backs both find_living() (any name match) and find_player() (a name
// match that also has real O_ONCE_INTERACTIVE --
// LpcObject::wasEverInteractive(), the same flag userp()/
// query_once_interactive() already use) -- confirmed directly against
// add_action.c's find_living_object(str, user): the exact same function
// backs both real efuns, differing only in that one extra flag check.
// Real find_living_object() also requires O_ENABLE_COMMANDS on the
// candidate at lookup time -- checked here via LpcObject::
// commandsEnabled(), not baked into the registry entry itself, since
// enable_commands()/disable_commands() can toggle independently of the
// name after set_living_name() runs (real set_living_name() itself
// preserves the existing O_ENABLE_COMMANDS flag across a rename: "int
// flags = ob->flags & O_ENABLE_COMMANDS; ... ob->flags |= flags;").
//
// weak_ptr entries, the same tolerance InteractiveRegistry already
// documents: a stale (destructed-and-dropped) entry found during find()
// is simply skipped, never surfaced as a match.
class LivingNameRegistry {
public:
    // Real set_living_name(ob, str): "remove_living_name(ob);" first
    // (a rename replaces the old entry, it does not add a second one),
    // matched here by remove()-then-insert.
    static void set(const std::shared_ptr<LpcObject>& obj, const std::string& name);

    // Real remove_living_name(): called both by a fresh set_living_name()
    // (see set() above) and by destruct_object() itself (simulate.c,
    // right alongside its own "ob->super = 0" environment-unlink step --
    // see ObjectManager::destructObject()'s own call site).
    static void remove(const std::shared_ptr<LpcObject>& obj);

    // requireOnceInteractive selects find_player()'s own extra
    // O_ONCE_INTERACTIVE gate (real find_living_object(str, user=1))
    // versus find_living()'s plain name match (user=0). Returns null on
    // no match, a destructed candidate, or one missing O_ENABLE_COMMANDS
    // -- matching real find_living_object()'s own skip conditions.
    static std::shared_ptr<LpcObject> find(const std::string& name, bool requireOnceInteractive);

    // Backs named_livings() (packages/contrib.c's f_named_livings():
    // walks hashed_living[] end to end, keeping only entries with real
    // O_ENABLE_COMMANDS -- the O_HIDDEN/valid_hide() filter real
    // named_livings() also applies needs VM access this class does not
    // have, so it is left to the efun body, the same split
    // isVisibleToObserver()'s own call sites already use). Stale
    // (destructed-and-dropped) weak_ptr entries are skipped, same
    // tolerance find() already has. Order is registry insertion order,
    // not hash-bucket order -- real named_livings()'s own order is
    // itself just an implementation artifact of its hash table, never
    // documented or relied on by any real call site.
    static std::vector<std::shared_ptr<LpcObject>> allWithCommandsEnabled();
};

} // namespace amlp
