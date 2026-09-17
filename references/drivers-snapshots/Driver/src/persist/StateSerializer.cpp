#include "amlp/persist/StateSerializer.hpp"
#include "amlp/object/ObjectManager.hpp"
#include "amlp/object/LpcObject.hpp"
#include "amlp/object/LiveObjectRegistry.hpp"
#include "amlp/core/Errors.hpp"
#include "amlp/vm/Value.hpp"

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace amlp {

namespace {

// Bumped only if this on-disk shape ever changes incompatibly --
// restoreState() rejects anything that does not start with exactly
// this literal instead of half-parsing a garbage or future-format file
// (a gap EfunTable.cpp's own deserializeValue() has never needed to
// guard against, since it only ever reads a string this same process
// just wrote moments earlier; a whole-world dump's own blast radius is
// large enough that this file's own precedent should not be repeated
// here).
const char* const kMagic = "AMLPSTATE1\n";

using IdOfObject = std::unordered_map<LpcObject*, int64_t>;
using ObjectOfId = std::unordered_map<int64_t, std::shared_ptr<LpcObject>>;

void serializeWorldValue(std::ostream& out, const Value& v, const IdOfObject& idOfObject);

// Real funptr_hdr_t::owner is an id into the same table object
// references use, not a separate namespace -- an owner whose weak_ptr
// has already expired (destructed before the dump ran) is written as
// -1, the same "no owner resolves" shape restoreState() below already
// treats a dangling environment/inventory id as tolerating.
void serializeClosure(std::ostream& out, const Closure& c, const IdOfObject& idOfObject) {
    // Matches this exact function's own header note on the ROADMAP.md
    // row 2.1 design: an unbound_lambda() (unboundUntilBound) or a
    // quoted-code lambda() body (lambdaBody non-void) has zero real
    // corpus evidence of ever living inside an ordinary object
    // variable (both of unbound_lambda()'s 4 known real call sites hand
    // straight to set_driver_hook()) -- fail loudly rather than
    // silently dropping it, the same convention this driver's own
    // save_object() already established for a width > 1 mapping.
    if (c.unboundUntilBound || !std::holds_alternative<std::monostate>(c.lambdaBody.data)) {
        throw LpcRuntimeError(
            "dump_state: cannot dump an unbound_lambda()/quoted-code closure -- "
            "see ROADMAP.md row 2.1's own note on this exact gap");
    }
    auto owner = c.owner.lock();
    int64_t ownerId = -1;
    if (owner) {
        auto it = idOfObject.find(owner.get());
        if (it != idOfObject.end()) ownerId = it->second;
    }
    out << 'C' << ownerId << ';' << (c.forceEfun ? 1 : 0) << ';'
        << c.functionName.size() << ':' << c.functionName
        << c.boundArgs.size() << ';';
    for (const auto& arg : c.boundArgs) serializeWorldValue(out, arg, idOfObject);
}

// Extends EfunTable.cpp's own serializeValue() tag-by-tag (I/F/S/A/M/N
// stay byte-for-byte identical) with two new tags: O<id> for an object
// reference and C for a closure, per ROADMAP.md row 2.1's own design.
// Anything the original function would already fall back to void for
// (Nil, Symbol -- neither variant is object-reference or closure
// shaped) gets the exact same fallback here; this is an additive
// extension of that scheme, not a parallel one.
void serializeWorldValue(std::ostream& out, const Value& v, const IdOfObject& idOfObject) {
    if (auto* iv = std::get_if<int64_t>(&v.data)) {
        out << 'I' << *iv << ';';
    } else if (auto* dv = std::get_if<double>(&v.data)) {
        out << 'F' << *dv << ';';
    } else if (auto* sv = std::get_if<std::string>(&v.data)) {
        out << 'S' << sv->size() << ':' << *sv;
    } else if (auto* av = std::get_if<std::shared_ptr<Array>>(&v.data)) {
        size_t count = *av ? (*av)->items.size() : 0;
        out << 'A' << count << ':';
        if (*av) {
            for (const auto& item : (*av)->items) serializeWorldValue(out, item, idOfObject);
        }
    } else if (auto* mv = std::get_if<std::shared_ptr<Mapping>>(&v.data)) {
        // Same bounded stopgap row 1.9's own addendum put in
        // save_object()'s serializeValue(): fail loudly rather than
        // silently writing column 0 only. See that function's own
        // comment (EfunTable.cpp) for the full derivation.
        if (*mv && (*mv)->width > 1) {
            throw LpcRuntimeError(
                "dump_state: cannot dump a mapping with width > 1 (real LDMud "
                "N-column mapping) -- this format only serializes column 0 "
                "today, matching save_object()'s own established error for "
                "this exact gap, see ROADMAP.md row 1.9");
        }
        size_t count = *mv ? (*mv)->entries.size() : 0;
        out << 'M' << count << ':';
        if (*mv) {
            for (const auto& entry : (*mv)->entries) {
                serializeWorldValue(out, entry.first, idOfObject);
                serializeWorldValue(out, entry.second, idOfObject);
            }
        }
    } else if (auto* ov = std::get_if<std::shared_ptr<LpcObject>>(&v.data)) {
        if (!*ov) {
            out << 'N';
            return;
        }
        auto it = idOfObject.find(ov->get());
        if (it == idOfObject.end()) {
            // LiveObjectRegistry::all() is exhaustive over every live
            // object, so a variable holding a live object reference
            // that is not in this same table should not be reachable --
            // fail loudly rather than silently writing a dangling id.
            throw LpcRuntimeError("dump_state: object reference not found in id table");
        }
        out << 'O' << it->second << ';';
    } else if (auto* cv = std::get_if<std::shared_ptr<Closure>>(&v.data)) {
        if (!*cv) {
            out << 'N';
            return;
        }
        serializeClosure(out, **cv, idOfObject);
    } else {
        // Nil, Symbol, and (as of row 2.33a) a buffer: none is object-
        // reference or closure shaped, and real FluffOS cannot save a
        // buffer at all (save_svalue() has no T_BUFFER case). Written as
        // void here; a future slice could add a real B<len>:<bytes> tag
        // so a buffer survives a hotboot, since this whole-world format
        // is this driver's own and not bound by real save_svalue().
        out << 'N';
    }
}

Value deserializeWorldValue(const std::string& s, size_t& pos, const ObjectOfId& objectOfId);

Closure deserializeClosure(const std::string& s, size_t& pos, const ObjectOfId& objectOfId) {
    Closure c;
    size_t semi = s.find(';', pos);
    int64_t ownerId = std::stoll(s.substr(pos, semi - pos));
    pos = semi + 1;

    semi = s.find(';', pos);
    bool forceEfun = s.substr(pos, semi - pos) == "1";
    pos = semi + 1;

    size_t colon = s.find(':', pos);
    size_t nameLen = static_cast<size_t>(std::stoull(s.substr(pos, colon - pos)));
    pos = colon + 1;
    c.functionName = s.substr(pos, nameLen);
    pos += nameLen;

    semi = s.find(';', pos);
    size_t argCount = static_cast<size_t>(std::stoull(s.substr(pos, semi - pos)));
    pos = semi + 1;

    c.boundArgs.reserve(argCount);
    for (size_t i = 0; i < argCount; ++i) {
        c.boundArgs.push_back(deserializeWorldValue(s, pos, objectOfId));
    }
    c.forceEfun = forceEfun;
    if (ownerId >= 0) {
        auto it = objectOfId.find(ownerId);
        if (it != objectOfId.end()) c.owner = it->second;
    }
    return c;
}

// The exact inverse of serializeWorldValue() above.
Value deserializeWorldValue(const std::string& s, size_t& pos, const ObjectOfId& objectOfId) {
    if (pos >= s.size()) return Value{};
    char kind = s[pos++];
    switch (kind) {
        case 'N':
            return Value{};
        case 'I': {
            size_t end = s.find(';', pos);
            int64_t v = std::stoll(s.substr(pos, end - pos));
            pos = end + 1;
            return Value(v);
        }
        case 'F': {
            size_t end = s.find(';', pos);
            double v = std::stod(s.substr(pos, end - pos));
            pos = end + 1;
            return Value(v);
        }
        case 'S': {
            size_t colon = s.find(':', pos);
            size_t len = static_cast<size_t>(std::stoull(s.substr(pos, colon - pos)));
            std::string v = s.substr(colon + 1, len);
            pos = colon + 1 + len;
            return Value(v);
        }
        case 'A': {
            size_t colon = s.find(':', pos);
            size_t count = static_cast<size_t>(std::stoull(s.substr(pos, colon - pos)));
            pos = colon + 1;
            auto arr = std::make_shared<Array>();
            arr->items.reserve(count);
            for (size_t i = 0; i < count; ++i) arr->items.push_back(deserializeWorldValue(s, pos, objectOfId));
            return Value(arr);
        }
        case 'M': {
            size_t colon = s.find(':', pos);
            size_t count = static_cast<size_t>(std::stoull(s.substr(pos, colon - pos)));
            pos = colon + 1;
            auto map = std::make_shared<Mapping>();
            map->entries.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                Value key = deserializeWorldValue(s, pos, objectOfId);
                Value val = deserializeWorldValue(s, pos, objectOfId);
                map->entries.emplace_back(std::move(key), std::move(val));
            }
            return Value(map);
        }
        case 'O': {
            size_t end = s.find(';', pos);
            int64_t id = std::stoll(s.substr(pos, end - pos));
            pos = end + 1;
            auto it = objectOfId.find(id);
            if (it == objectOfId.end()) return Value{};
            return Value(it->second);
        }
        case 'C': {
            auto c = std::make_shared<Closure>(deserializeClosure(s, pos, objectOfId));
            return Value(c);
        }
        default:
            throw LpcRuntimeError("restore_state: corrupt statedump data (unknown value kind)");
    }
}

// One placement-metadata record, Section A of the on-disk format --
// see StateSerializer::dumpState()'s own comment for the exact grammar.
struct PlacementEntry {
    int64_t id = 0;
    bool isClone = false;
    int64_t envId = -1;
    std::vector<int64_t> invIds;
    std::string filename;
};

} // namespace

bool StateSerializer::dumpState(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    auto liveObjects = LiveObjectRegistry::all();

    IdOfObject idOfObject;
    idOfObject.reserve(liveObjects.size());
    for (size_t i = 0; i < liveObjects.size(); ++i) {
        idOfObject[liveObjects[i].get()] = static_cast<int64_t>(i);
    }

    out << kMagic;
    out << liveObjects.size() << ';';

    // Section A: placement metadata, one entry per object, in id
    // order. Grammar per entry:
    //   id ';' isClone(0|1) ';' envId(-1 if none) ';' invCount ';'
    //   (invId (',' invId)*)? ';' filenameLen ':' filenameBytes
    // filename is length-prefixed (matching the existing 'S' tag's own
    // convention) rather than relying on a delimiter character never
    // appearing in a real filename.
    for (const auto& obj : liveObjects) {
        int64_t envId = -1;
        if (auto env = obj->environment().lock()) {
            auto it = idOfObject.find(env.get());
            if (it != idOfObject.end()) envId = it->second;
        }
        out << idOfObject[obj.get()] << ';' << (obj->isClone() ? 1 : 0) << ';' << envId << ';';
        out << obj->inventory().size() << ';';
        bool first = true;
        for (const auto& child : obj->inventory()) {
            auto it = idOfObject.find(child.get());
            if (it == idOfObject.end()) continue; // exhaustive registry, should not happen
            if (!first) out << ',';
            out << it->second;
            first = false;
        }
        out << ';';
        out << obj->filename().size() << ':' << obj->filename();
    }

    // Section B: each object's variables(), same id order, written
    // after all of Section A so restore can rebuild the full id table
    // (and apply every placement) before resolving a single O<id>/C
    // reference out of this section. Grammar per entry:
    //   id ';' varCount ';' (serialized-value)*varCount
    for (const auto& obj : liveObjects) {
        auto& vars = obj->variables();
        out << idOfObject[obj.get()] << ';' << vars.size() << ';';
        for (const auto& v : vars) serializeWorldValue(out, v, idOfObject);
    }

    return static_cast<bool>(out);
}

bool StateSerializer::restoreState(const std::string& path) const {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string s = buf.str();

    std::string magic(kMagic);
    if (s.size() < magic.size() || s.compare(0, magic.size(), magic) != 0) {
        // Garbage or future-format file -- rejected cleanly, matching
        // this function's own header comment, not half-parsed.
        return false;
    }
    size_t pos = magic.size();

    auto readIntField = [&](char delim) -> int64_t {
        size_t end = s.find(delim, pos);
        if (end == std::string::npos) {
            throw LpcRuntimeError("restore_state: corrupt statedump data (truncated field)");
        }
        int64_t v = std::stoll(s.substr(pos, end - pos));
        pos = end + 1;
        return v;
    };

    size_t count = static_cast<size_t>(readIntField(';'));

    std::vector<PlacementEntry> entries;
    entries.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        PlacementEntry e;
        e.id = readIntField(';');
        e.isClone = readIntField(';') != 0;
        e.envId = readIntField(';');
        size_t invCount = static_cast<size_t>(readIntField(';'));

        size_t listEnd = s.find(';', pos);
        if (listEnd == std::string::npos) {
            throw LpcRuntimeError("restore_state: corrupt statedump data (truncated inventory list)");
        }
        std::string invList = s.substr(pos, listEnd - pos);
        pos = listEnd + 1;
        size_t p2 = 0;
        for (size_t k = 0; k < invCount; ++k) {
            size_t comma = invList.find(',', p2);
            std::string tok = (comma == std::string::npos) ? invList.substr(p2) : invList.substr(p2, comma - p2);
            e.invIds.push_back(std::stoll(tok));
            p2 = (comma == std::string::npos) ? invList.size() : comma + 1;
        }

        size_t colon = s.find(':', pos);
        if (colon == std::string::npos) {
            throw LpcRuntimeError("restore_state: corrupt statedump data (truncated filename)");
        }
        size_t fnameLen = static_cast<size_t>(std::stoull(s.substr(pos, colon - pos)));
        pos = colon + 1;
        e.filename = s.substr(pos, fnameLen);
        pos += fnameLen;

        entries.push_back(std::move(e));
    }

    // Restore pass 1: reconstruct every object and build the id table
    // fully before resolving a single environment/inventory id or
    // object-reference/closure variable against it. Reuses the exact
    // existing reconstruction paths (never invents a third one):
    // lookupLoadedObject() if already loaded (the master/simul_efun
    // case), else loadObject() for a blueprint or cloneObject() for a
    // clone -- both re-derive the program from on-disk source via the
    // existing compile()/cache path, matching this driver's own
    // established precedent of never serializing bytecode itself.
    ObjectOfId objectOfId;
    objectOfId.reserve(entries.size());
    // A genuine gap found while building this (not part of the original
    // ROADMAP.md row 2.1 design note, recorded on ObjectManager::
    // retainRestoredObjects()'s own header comment): nothing keeps a
    // freshly reconstructed clone alive once this function returns
    // except whatever the restored graph itself happens to reference --
    // an isolated object, or an acyclic restored graph, would otherwise
    // be silently freed the instant restoreState() returns, before the
    // caller ever gets to use it. Every reconstructed object is handed
    // to the ObjectManager for real, persistent ownership below, once
    // pass 1 finishes, mirroring how loaded_ already keeps every
    // blueprint alive for that same ObjectManager's own lifetime.
    std::vector<std::shared_ptr<LpcObject>> reconstructed;
    reconstructed.reserve(entries.size());
    for (const auto& e : entries) {
        std::shared_ptr<LpcObject> obj;
        if (e.isClone) {
            obj = objects_.cloneObject(e.filename);
        } else {
            obj = objects_.lookupLoadedObject(e.filename);
            if (!obj) obj = objects_.loadObject(e.filename);
        }
        if (!obj) {
            throw LpcRuntimeError("restore_state: could not reconstruct object for " + e.filename);
        }
        objectOfId[e.id] = obj;
        reconstructed.push_back(obj);
    }
    objects_.retainRestoredObjects(std::move(reconstructed));

    // Placement: apply environment/inventory now that every id the
    // file could reference resolves to a real reconstructed object.
    for (const auto& e : entries) {
        auto obj = objectOfId[e.id];
        if (e.envId >= 0) {
            auto it = objectOfId.find(e.envId);
            if (it != objectOfId.end()) obj->setEnvironment(it->second);
        }
        obj->inventory().clear();
        for (int64_t invId : e.invIds) {
            auto it = objectOfId.find(invId);
            if (it != objectOfId.end()) obj->inventory().push_back(it->second);
        }
    }

    // Restore pass 2: now that the id table is complete, read Section B
    // and assign each object's variables(), resolving O<id>/C against
    // it. Values are decoded into a scratch vector first and only then
    // assigned by index, up to the shorter of the two lengths -- a
    // reconstructed object's own variable count always matches what was
    // dumped as long as its source is unchanged between dump and
    // restore, the same assumption this whole restore path already
    // makes by re-deriving the program from on-disk source rather than
    // serializing bytecode.
    for (size_t i = 0; i < entries.size(); ++i) {
        int64_t id = readIntField(';');
        size_t varCount = static_cast<size_t>(readIntField(';'));

        std::vector<Value> restoredVars;
        restoredVars.reserve(varCount);
        for (size_t k = 0; k < varCount; ++k) {
            restoredVars.push_back(deserializeWorldValue(s, pos, objectOfId));
        }

        auto it = objectOfId.find(id);
        if (it == objectOfId.end()) continue;
        auto& vars = it->second->variables();
        size_t n = std::min(vars.size(), restoredVars.size());
        for (size_t k = 0; k < n; ++k) vars[k] = restoredVars[k];
    }

    return true;
}

} // namespace amlp
