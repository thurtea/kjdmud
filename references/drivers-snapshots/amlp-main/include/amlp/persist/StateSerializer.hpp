#pragma once
#include <string>

namespace amlp {

class ObjectManager;

// World-level statedump (ROADMAP.md row 2.1, v1 first slice). This is
// exactly the "Concrete first-slice design" note recorded on that row
// (2026-08-22 scoping session), implemented, not a reinterpretation of
// it -- see that note for the full derivation of every choice below.
//
// The real hard sub-problem is reference identity: EfunTable.cpp's own
// save_object()/restore_object() format (serializeValue()/
// deserializeValue()) is a per-object format that cannot serialize an
// object reference or a closure at all -- correct for a single object's
// own save file, wrong for a whole-world snapshot, where two different
// objects that both reference a third live object need that reference
// to resolve back to the *same* restored instance, not two independent
// copies or two dangling nulls.
//
// This class extends that exact tag scheme (I/F/S/A/M/N) rather than
// replacing it, adding two new tags: O<id> (an object reference,
// resolved against a dump-scoped id table) and C (a closure, whose
// owner is also an id into the same table). The id table is built once
// per dump/restore call from LiveObjectRegistry::all() -- this driver
// has no persistent per-object id anywhere else, matching real
// save_object()'s own "no identity beyond one call" precedent, just
// widened to a whole-heap pass.
//
// v1 scope: every live object's variables() plus its environment()/
// inventory() placement. Explicitly deferred to a later slice (each its
// own independently bounded follow-up, not a blocker to this one):
// Scheduler's pending call_outs/heartbeats; LpcObject's actions_/
// shadowedBy_/shadowing_/snoopedBy_/snooping_ chains; parse-info; and
// every reset/cleanup scheduling field -- restoreState() lets each
// restored object re-arm those fresh via the same armResetAndCleanup()
// path an ordinary fresh load already uses, matching real-driver-reboot
// practice rather than round-tripping exact countdown state.
class StateSerializer {
public:
    // Constructed against ObjectManager& only for this slice --
    // deliberately not Scheduler&/Server& yet, see the deferred list
    // above.
    explicit StateSerializer(ObjectManager& objects) : objects_(objects) {}

    // Writes every live object (LiveObjectRegistry::all()) to path:
    // a magic/version header, then placement metadata (filename,
    // isClone, environment/inventory ids) for every object, then each
    // object's variables(). Returns false only on failing to open path
    // for writing; throws LpcRuntimeError (matching serializeValue()'s
    // own established convention) for a value this format cannot
    // represent, e.g. a width > 1 mapping or an unbound_lambda().
    bool dumpState(const std::string& path) const;

    // Restores from a file previously written by dumpState(). Returns
    // false if the file is missing or does not start with this format's
    // own magic/version header, so a garbage or future-format file is
    // rejected cleanly instead of half-parsed. Throws LpcRuntimeError
    // for a corrupt body past that point, matching deserializeValue()'s
    // own "corrupt save data" convention, rather than silently producing
    // a half-restored world.
    //
    // Two-pass by construction: every object named in the file's
    // placement section is fully reconstructed (via ObjectManager's own
    // lookupLoadedObject()/loadObject()/cloneObject(), re-deriving the
    // program from on-disk source exactly like any other load, never
    // serializing bytecode) and entered into the id table before any
    // environment/inventory id or object-reference/closure variable is
    // resolved against that table -- so a room and an item in its
    // inventory that references the room back end up pointing at the
    // same live instance, not two separate copies.
    bool restoreState(const std::string& path) const;

private:
    ObjectManager& objects_;
};

} // namespace amlp
