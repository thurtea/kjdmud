#include "amlp/efun/EfunTable.hpp"
#include "amlp/efun/DbRegistry.hpp"
#include "amlp/core/Errors.hpp"
#include "amlp/vm/VM.hpp"
#include "amlp/config/Config.hpp"
#include "amlp/object/LivingNameRegistry.hpp"
#include "amlp/object/LiveObjectRegistry.hpp"
#include "amlp/object/LpcObject.hpp"
#include "amlp/object/ObjectManager.hpp"
#include "amlp/net/OutputContext.hpp"
#include "amlp/net/Connection.hpp"
#include "amlp/net/InteractiveRegistry.hpp"
#include "amlp/net/SocketRegistry.hpp"
#include "amlp/net/SnoopRelay.hpp"
#include "amlp/scheduler/Scheduler.hpp"
#include "amlp/efun/ParserPackage.hpp"
#include "amlp/persist/StateSerializer.hpp"
#include <algorithm>
#include <chrono>
#include <arpa/inet.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fnmatch.h>
#include <fstream>
#include <functional>
#include <unordered_set>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <pcre2.h>
#include <random>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>

namespace amlp {

EfunTable& EfunTable::instance() {
    static EfunTable table;
    return table;
}

void EfunTable::registerEfun(const std::string& name, EfunFn fn) {
    table_[name] = std::move(fn);
}

Value EfunTable::call(const std::string& name, VM& vm, std::vector<Value>& args) const {
    auto it = table_.find(name);
    if (it == table_.end()) {
        throw LpcRuntimeError("undefined efun: " + name);
    }
    return it->second(vm, args);
}

bool EfunTable::exists(const std::string& name) const {
    return table_.count(name) > 0;
}

namespace {

// Resolves real command_giver for the add_action()/enable_commands()
// subsystem's own efuns (this_player(), add_action(), remove_action()):
// VM::commandGiver()'s own explicit stack when one is active (set by
// VM::moveObject()'s init()-calling sequence or VM::dispatchCommand()'s
// own handler call), falling back to whichever connection is currently
// driving this call otherwise -- e.g. code running from create()/
// setup() during login/account creation, before any command has ever
// been dispatched, which is exactly when this mudlib's own std/
// living.c calls add_action("cmd_hook", "", 1) (via init_living(),
// called directly from std/user.c's setup(), not through a driver-
// invoked init() apply -- see STATUS.md's own recon notes). This lives
// here, in the efun layer, rather than inside VM::commandGiver() itself,
// because OutputContext is part of the "net" library, which "vm" cannot
// depend on without creating a circular link (net already depends on
// vm) -- see message()'s own comment for the same OutputContext access
// pattern already established at this layer.
std::shared_ptr<LpcObject> resolveCommandGiver(VM& vm) {
    if (auto giver = vm.commandGiver()) return giver;
    if (auto* conn = OutputContext::current()) {
        if (auto bound = conn->boundObject()) return bound;
    }
    return nullptr;
}

// real interrogate_master()'s own literal-fetching third (packages/
// parser.c): calls master()->parse_command_prepos_list(), which despite
// the real APPLY_LITERALS macro's own name is NOT "literals" in any
// generic sense -- confirmed directly against the real vendored
// `applies` source file (not assumed from the macro name, which would
// have been actively misleading here): "LITERALS:parse_command_prepos_list".
// It is specifically the mudlib's own list of legal *preposition* words
// usable as bare literals in a rule string (real usage: "give OBJ to
// LIV", OBJ then the bare literal "to"). Real interrogate_master() also
// caches this behind a master_state bit, invalidated only by
// parse_refresh() on the master object -- not ported yet
// (parse_refresh() itself is a later slice, ROADMAP.md row 0.13a), so
// this always re-applies; harmless for now, since nothing calls it more
// than a handful of times per rare, setup-time-only parse_add_rule()/
// parse_add_synonym() call. Real interrogate_master() also fetches
// parse_command_users() and populates the special-word table
// (the/me/myself/all/of/ordinals) in the same pass -- neither is needed
// until the sentence-matching slice (both are purely about resolving
// player input against live objects, never touched by rule
// registration), so neither is called here. Shared by parse_add_rule
// and parse_add_synonym, both of which can tokenize a fresh rule string.
std::vector<std::string> fetchParserLiterals(VM& vm) {
    std::vector<std::string> literals;
    if (vm.masterObject()) {
        Value litResult = vm.applyMaster("parse_command_prepos_list", {});
        if (auto* arr = std::get_if<std::shared_ptr<Array>>(&litResult.data)) {
            if (*arr) {
                for (const auto& v : (*arr)->items) {
                    if (auto* s = std::get_if<std::string>(&v.data)) literals.push_back(*s);
                }
            }
        }
    }
    return literals;
}

// sprintf()/printf()'s "%O" specifier -- LPC's generic value-dump format,
// used for debugging arbitrary values. Confirmed directly against
// fluffos-2.9-ds2.08/sprintf.c's own svalue_to_string() before writing
// anything (not guessed from the specifier's general reputation): %O
// itself just builds this string then hands it to the exact same field-
// width/precision/justification code %s already uses (real sprintf.c's
// own "svalue_to_string(carg, &outbuf, 0, 0, 0); ... finfo |=
// INFO_T_STRING;" -- INFO_T_LPC is converted into INFO_T_STRING right
// after building the string, not a separate formatting path), which is
// why this driver's own %O handling in sprintfImpl below only needs to
// produce the piece string here and can otherwise fall straight into
// the %s field-width logic already there.
//
// Per-kind formats, each confirmed against the real switch:
// - int: plain decimal (real T_NUMBER via numadd()).
// - float: C's own "%f" (six decimal places, real T_REAL: "outbuf_addv
//   (outbuf, \"%f\", obj->u.real)").
// - string: wrapped in literal double quotes, no escaping of embedded
//   quotes/backslashes/newlines (real T_STRING: "\"" + string + "\"",
//   confirmed -- there really is no escaping step in the real function).
// - object: destructed -> "0" (real T_OBJECT's own "if (obj->u.ob->flags
//   & O_DESTRUCTED) { numadd(outbuf, 0); break; }"); otherwise "/" +
//   filename. Real code also appends a master()->object_name() apply
//   result in parens when that apply is defined and returns a string
//   ("obj->u.ob->obname" plus an optional " (\"...\")" suffix) -- not
//   implemented here (this is a pure string-formatting helper with no
//   VM access to fire an apply through, and no real call site in this
//   mudlib defines object_name at all, confirmed by grep), so this
//   driver's own %O on an object is always just "/" + filename with no
//   parenthetical suffix, a known, narrow, documented gap versus real
//   FluffOS's own optional decoration.
// - array: "({ })" empty; otherwise "({ /* sizeof() == N */\n" then
//   each element indented two spaces deeper than its own array/mapping,
//   comma-and-newline-terminated except the last (real T_ARRAY's own
//   "trailing" flag: every element but the last is drawn with
//   trailing=1, the last with trailing=0, then one more bare "\n" is
//   added after the loop regardless), closing "})" back at the array's
//   own indent level.
// - mapping: "([ ])" empty; otherwise "([ /* sizeof() == N */\n" then
//   each entry as "  key : value,\n" (key gets its own indent, the
//   value is drawn inline right after " : " with indent2 set so it adds
//   no leading spaces of its own, and *every* entry -- not just non-last
//   ones -- gets a trailing ",\n", confirmed directly against real
//   T_MAPPING's own unconditional trailing=1 on the value draw), closing
//   "])" back at the mapping's own indent level. This driver's own
//   Mapping is an insertion-ordered vector, not a hash table, so %O's
//   own entry order here is insertion order -- a real, deliberate
//   divergence from real FluffOS's own hash-bucket iteration order
//   (which no real mudlib code could depend on being any particular
//   order anyway), not an attempt to replicate real hash placement.
// - closure: "(: " + bare function name + ", " + each bound arg's own
//   %O rendering + " :)" (real T_FUNCTION's own FP_LOCAL/FP_SIMUL/
//   FP_EFUN cases all reduce to "print the function's own name" the
//   same way this driver's own simplified, always-bare-name Closure
//   model already does -- see Value.hpp's own Closure comment for why
//   this driver has no separate FP_FUNCTIONAL "compiled code with
//   numbered $1/$2 placeholders" closure kind to also handle here).
// - monostate (this driver's own void/undefined marker): rendered as
//   "0", matching this driver's own pre-existing, established
//   convention elsewhere (e.g. a destructed object read out of a
//   variable self-healing to a real int 0) rather than inventing a new
//   representation specific to this one efun.
//
// Real recursion/depth guard, replicated exactly: "if (indent > 20) {
// outbuf_add(outbuf, \"...\"); return; }" -- a genuine, deliberate
// truncation quirk (guards a self-referential or extremely deep
// structure), not an oversight; indent grows by 2 per array/mapping
// nesting level (4 for a mapping's own value column), so this caps out
// around 10 levels of plain array nesting.
std::string valueToDebugString(const Value& v, int indent) {
    if (indent > 20) return "...";
    std::string pad(static_cast<size_t>(indent), ' ');

    if (std::holds_alternative<std::monostate>(v.data)) {
        return "0";
    }
    if (auto* iv = std::get_if<int64_t>(&v.data)) {
        return std::to_string(*iv);
    }
    if (auto* dv = std::get_if<double>(&v.data)) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%f", *dv);
        return buf;
    }
    if (auto* sv = std::get_if<std::string>(&v.data)) {
        return "\"" + *sv + "\"";
    }
    if (auto* ov = std::get_if<std::shared_ptr<LpcObject>>(&v.data)) {
        if (!*ov || (*ov)->isDestructed()) return "0";
        return "/" + (*ov)->filename();
    }
    if (auto* av = std::get_if<std::shared_ptr<Array>>(&v.data)) {
        if (!*av || (*av)->items.empty()) return "({ })";
        std::string out = "({ /* sizeof() == " + std::to_string((*av)->items.size()) + " */\n";
        for (size_t i = 0; i < (*av)->items.size(); ++i) {
            out += pad + "  " + valueToDebugString((*av)->items[i], indent + 2);
            out += (i + 1 < (*av)->items.size()) ? ",\n" : "\n";
        }
        out += pad + "})";
        return out;
    }
    if (auto* mv = std::get_if<std::shared_ptr<Mapping>>(&v.data)) {
        if (!*mv || (*mv)->entries.empty()) return "([ ])";
        std::string out = "([ /* sizeof() == " + std::to_string((*mv)->entries.size()) + " */\n";
        for (const auto& entry : (*mv)->entries) {
            out += pad + "  " + valueToDebugString(entry.first, indent + 2) + " : " +
                   valueToDebugString(entry.second, indent + 4) + ",\n";
        }
        out += pad + "])";
        return out;
    }
    if (auto* cv = std::get_if<std::shared_ptr<Closure>>(&v.data)) {
        if (!*cv) return "(: :)";
        std::string out = "(: " + (*cv)->functionName;
        for (const auto& bound : (*cv)->boundArgs) {
            out += ", " + valueToDebugString(bound, indent);
        }
        out += " :)";
        return out;
    }
    // Real svalue_to_string() (sprintf.c) renders a T_BUFFER as the
    // literal "<buffer>" -- no byte contents, no length. This is what
    // %O, printf("%O"), and identify() show for one.
    if (std::holds_alternative<std::shared_ptr<Buffer>>(v.data)) {
        return "<buffer>";
    }
    return "!ERROR: GARBAGE SVALUE!";
}

// Recursive, self-delimiting save format used by the save_object()/
// restore_object() efuns below -- see their own comment for why this
// driver does not attempt to match real FluffOS's own on-disk save
// format. Every value is tagged with a one-character kind and, for the
// variable-length kinds (string/array/mapping), an explicit element/
// byte count, so nested arrays and mappings round-trip without needing
// to escape delimiter characters inside string content.
void serializeValue(std::ostream& out, const Value& v) {
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
            for (const auto& item : (*av)->items) serializeValue(out, item);
        }
    } else if (auto* mv = std::get_if<std::shared_ptr<Mapping>>(&v.data)) {
        // ROADMAP.md row 1.9's own addendum, 2026-08-21 ("Save/restore
        // silent-truncation finding"): this format has no encoding for
        // Mapping::width/extraColumns at all -- every entry below only
        // ever wrote/read column 0. Before this check, a width > 1
        // mapping (real LDMud N-column, the rune-wall.c width-2 shape)
        // saved silently, writing only column 0 with every other column
        // discarded, and restore_object() always rebuilt a width-1
        // Mapping regardless of what had actually been saved -- a real,
        // live "accepts input and silently produces wrong results"
        // hazard, the same category this project already treated as
        // worse than "not implemented" once before
        // (`parse_sentence()`'s `nicks` argument). Bounded stopgap only,
        // not full width-aware serialization (that remains its own,
        // separately-scoped, larger item, still open on row 1.9): fail
        // loudly instead of truncating silently. Checked here, inside
        // the recursive writer, not only at save_object()'s own top-
        // level per-variable loop, so a width > 1 mapping nested inside
        // an array or another mapping is caught too, not just one sitting
        // directly in an object variable.
        if (*mv && (*mv)->width > 1) {
            throw LpcRuntimeError(
                "save_object: cannot save a mapping with width > 1 (real "
                "LDMud N-column mapping) -- this driver's save_object()/"
                "restore_object() only serialize column 0 today, so saving "
                "one would silently discard every other column instead of "
                "raising this error; see ROADMAP.md row 1.9's own note");
        }
        size_t count = *mv ? (*mv)->entries.size() : 0;
        out << 'M' << count << ':';
        if (*mv) {
            for (const auto& entry : (*mv)->entries) {
                serializeValue(out, entry.first);
                serializeValue(out, entry.second);
            }
        }
    } else if (std::holds_alternative<std::shared_ptr<Buffer>>(v.data)) {
        // Real object.c save_svalue()'s switch has no T_BUFFER case at
        // all (confirmed directly: it falls straight through and writes
        // nothing), so real FluffOS cannot save a buffer in an object
        // variable either. This driver writes void in its place, the
        // same as it does for an object reference or closure just below,
        // so the save file stays well formed and round-trips as 0.
        out << 'N';
    } else {
        // Object references and closures cannot survive a save/restore
        // round trip (a saved object reference has nothing to point to
        // after a reboot) -- real save_object() cannot serialize these
        // either, so this driver just writes void in their place rather
        // than throwing and failing the whole save.
        out << 'N';
    }
}

// Reads one value starting at pos (mutated to just past what was
// consumed), the exact inverse of serializeValue() above.
Value deserializeValue(const std::string& s, size_t& pos) {
    if (pos >= s.size()) return Value{};
    char kind = s[pos++];
    switch (kind) {
        case 'N':
            // 'N' means "this slot held an object reference or closure,
            // neither of which can survive a save/restore round trip"
            // (see serializeValue()'s own comment just above). Real LPC
            // has no separate "undefined"/"void" runtime value at all --
            // real save_svalue() (object.c) has no T_OBJECT/T_CLOSURE
            // case, so it writes nothing for one at all, and restore's
            // own parser reads the resulting empty text the exact same
            // way it reads any other missing/blank token: as plain
            // integer 0, restoring the object-typed slot to real,
            // ordinary T_NUMBER 0 -- the same "no object here" value an
            // unset object variable always holds. Returning this
            // driver's own std::monostate here instead (a C++-only
            // concept with no real LPC equivalent) meant a restored
            // slot's value no longer compared "== 0" true against a
            // literal 0 the way real LPC guarantees, live-confirmed via
            // TMI-2's own std/user.c::clean_up_attackers() ("if
            // (attackers[i] == 0 || ...) continue;", attackers[] being a
            // real object* array that had just round-tripped through a
            // save/restore with a since-cleared attacker in it): the
            // check silently failed to skip the cleared slot, falling
            // through to call_other() a non-object value further down.
            return Value(int64_t{0});
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
            for (size_t i = 0; i < count; ++i) arr->items.push_back(deserializeValue(s, pos));
            return Value(arr);
        }
        case 'M': {
            size_t colon = s.find(':', pos);
            size_t count = static_cast<size_t>(std::stoull(s.substr(pos, colon - pos)));
            pos = colon + 1;
            auto map = std::make_shared<Mapping>();
            map->entries.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                Value key = deserializeValue(s, pos);
                Value val = deserializeValue(s, pos);
                map->entries.emplace_back(std::move(key), std::move(val));
            }
            return Value(map);
        }
        default:
            throw LpcRuntimeError("restore_object: corrupt save data (unknown value kind)");
    }
}

// Real FluffOS on-disk save_object()/restore_object() text format --
// read-only support. This driver only ever *writes* its own format
// (serializeValue() above); this is purely a fallback reader for
// restore_object() so a genuine, pre-existing FluffOS save file (one
// that shipped with a real mudlib, never touched by this driver) loads
// its real historical data instead of being silently skipped line by
// line (see restore_object's own comment for how a line is dispatched
// to this parser vs. deserializeValue() above). Grounded directly in
// fluffos-2.9-ds2.08/object.c's own save_svalue() (the writer) and
// restore_string()/restore_array()/restore_mapping()/parse_numeric()
// (the readers) -- not guessed. Real "class" values ("(/ ... /)") are
// not implemented (this driver has no class/struct type anywhere else
// either) -- throws rather than silently mishandling, matching this
// codebase's existing convention for other unimplemented shapes.

// Real restore_string(): reads up to an unescaped '"'. save_svalue()
// itself only ever backslash-escapes '"' and '\\', but the real reader
// is more lenient than its own writer -- a backslash before *any*
// character is taken literally, not just those two -- so this mirrors
// the reader's actual leniency rather than only the two escapes the
// paired writer happens to produce. A raw '\r' byte (real save_svalue()'s
// own on-disk encoding of an embedded '\n', done so a literal newline
// in the string can't be mistaken for the end of the save-file line) is
// translated back to '\n'.
std::string parseRealSaveString(const std::string& s, size_t& pos) {
    std::string result;
    while (pos < s.size() && s[pos] != '"') {
        char c = s[pos++];
        if (c == '\\' && pos < s.size()) {
            result += s[pos++];
        } else if (c == '\r') {
            result += '\n';
        } else {
            result += c;
        }
    }
    if (pos < s.size() && s[pos] == '"') ++pos; // consume closing quote
    return result;
}

// Real parse_numeric(): an optional leading '-', digits, and an
// optional '.'-led fractional part -- present only when the source is
// genuinely a float, matching save_svalue()'s own T_REAL case
// ("sprintf(*buf, \"%f\", ...)", always a fixed-notation decimal point,
// never exponential notation) versus its plain-digits T_NUMBER case.
// No exponent handling: real save_svalue() never writes one, so a
// faithful reader for genuine on-disk data from this exact vendored
// driver doesn't need to parse one either.
Value parseRealSaveNumber(const std::string& s, size_t& pos) {
    size_t start = pos;
    if (pos < s.size() && s[pos] == '-') ++pos;
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
    bool isFloat = false;
    if (pos < s.size() && s[pos] == '.') {
        isFloat = true;
        ++pos;
        while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
    }
    std::string token = s.substr(start, pos - start);
    if (isFloat) return Value(std::stod(token));
    return Value(static_cast<int64_t>(std::stoll(token)));
}

Value parseRealSaveValue(const std::string& s, size_t& pos) {
    if (pos >= s.size()) return Value(static_cast<int64_t>(0));
    char c = s[pos];
    if (c == '"') {
        ++pos;
        return Value(parseRealSaveString(s, pos));
    }
    if (c == '(') {
        ++pos;
        if (pos < s.size() && s[pos] == '{') {
            ++pos;
            auto arr = std::make_shared<Array>();
            // Real restore_array(): a bare ',' with nothing before it
            // is a skipped element, defaulting to int 0 -- the trailing
            // comma save_svalue() always writes after every element
            // (including the last) is what this same leniency correctly
            // consumes as "no more elements follow", not a real gap.
            while (pos < s.size() && s[pos] != '}') {
                if (s[pos] == ',') { arr->items.emplace_back(static_cast<int64_t>(0)); ++pos; continue; }
                arr->items.push_back(parseRealSaveValue(s, pos));
                if (pos < s.size() && s[pos] == ',') ++pos;
            }
            if (pos < s.size()) ++pos; // '}'
            if (pos < s.size() && s[pos] == ')') ++pos;
            return Value(arr);
        }
        if (pos < s.size() && s[pos] == '[') {
            ++pos;
            auto map = std::make_shared<Mapping>();
            while (pos < s.size() && s[pos] != ']') {
                Value key = parseRealSaveValue(s, pos);
                if (pos < s.size() && s[pos] == ':') ++pos;
                Value val = parseRealSaveValue(s, pos);
                map->entries.emplace_back(std::move(key), std::move(val));
                if (pos < s.size() && s[pos] == ',') ++pos;
            }
            if (pos < s.size()) ++pos; // ']'
            if (pos < s.size() && s[pos] == ')') ++pos;
            return Value(map);
        }
        throw LpcRuntimeError("restore_object: class-typed save data is not supported");
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
        return parseRealSaveNumber(s, pos);
    }
    // Real restore_svalue()'s own default branch: anything else is a
    // plain int 0, not an error.
    return Value(static_cast<int64_t>(0));
}

// save_variable(mixed)'s own writer -- real object.c's own save_svalue(),
// the exact paired writer for parseRealSaveValue()/parseRealSaveString()
// above (confirmed by reading it directly, not guessed): a string is
// wrapped in '"', with only '"' and '\\' backslash-escaped, and a literal
// embedded '\n' byte is substituted for a literal '\r' byte -- not a
// backslash escape at all, which is why parseRealSaveString() above
// reverses that exact substitution rather than treating '\r' as a
// two-character escape. Arrays/mappings always write a trailing ','
// after *every* element, including the last, matching real
// save_svalue()'s own unconditional per-element ',' inside the loop --
// parseRealSaveValue()'s own array/mapping readers already tolerate
// this leftover trailing comma. Floats use plain "%f" (six decimal
// places, no exponent), the same convention this driver's own %O
// sprintf specifier already uses for T_REAL. Real save_svalue()'s own
// switch has no case at all for an object reference or a function
// pointer (not even a default) -- confirmed by reading it directly, this
// is a genuine gap in real FluffOS itself, not a deliberately handled
// shape -- so this driver throws a clear error for either rather than
// silently emitting nothing (which would produce truncated, unparsable
// output), matching this codebase's own established convention for
// shapes real FluffOS itself does not support saving.
void writeRealSaveValue(std::string& out, const Value& v) {
    if (std::holds_alternative<std::monostate>(v.data)) {
        out += '0';
    } else if (auto* iv = std::get_if<int64_t>(&v.data)) {
        out += std::to_string(*iv);
    } else if (auto* dv = std::get_if<double>(&v.data)) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%f", *dv);
        out += buf;
    } else if (auto* sv = std::get_if<std::string>(&v.data)) {
        out += '"';
        for (char c : *sv) {
            if (c == '"' || c == '\\') {
                out += '\\';
                out += c;
            } else if (c == '\n') {
                out += '\r';
            } else {
                out += c;
            }
        }
        out += '"';
    } else if (auto* av = std::get_if<std::shared_ptr<Array>>(&v.data)) {
        out += "({";
        if (*av) {
            for (const auto& item : (*av)->items) {
                writeRealSaveValue(out, item);
                out += ',';
            }
        }
        out += "})";
    } else if (auto* mv = std::get_if<std::shared_ptr<Mapping>>(&v.data)) {
        out += "([";
        if (*mv) {
            for (const auto& entry : (*mv)->entries) {
                writeRealSaveValue(out, entry.first);
                out += ':';
                writeRealSaveValue(out, entry.second);
                out += ',';
            }
        }
        out += "])";
    } else {
        // Real object.c save_svalue()'s switch has no case for an
        // object reference, a function pointer, or a buffer (it falls
        // through and writes nothing, producing truncated unparsable
        // output). This driver raises a clear error for all three
        // instead, matching the established choice for the first two.
        throw LpcRuntimeError(
            "save_variable: cannot save an object, function-pointer, or buffer value");
    }
}

// restore_variable(string)'s own top-level string reader -- deliberately
// separate from parseRealSaveString() above, not a shared helper, because
// real restore_svalue()'s (object.c) top-level restore_string() enforces
// a real, specific quirk that a nested array/mapping-element string must
// NOT be held to: the closing '"' must be immediately followed by the
// end of the *entire* input buffer, or it is a real ROB_STRING_ERROR.
// Confirmed directly by reading restore_string()'s own two exit paths
// (the plain, no-escape path: "if (*cp--) return ROB_STRING_ERROR;"
// right after the loop that found the closing quote; the escaped path:
// "if ((c == '\\0') || (*cp != '\\0')) return ROB_STRING_ERROR;") -- both
// require genuine end-of-buffer, not just "the next delimiter char".
// restore_object()'s own array/mapping-element strings have no such
// requirement (there a string is always followed by a real ',' or ':'
// delimiter, never raw end-of-input), which is exactly why
// parseRealSaveString() above stays lenient and this one does not.
Value parseRestoreVariableString(const std::string& s, size_t& pos) {
    std::string result;
    while (pos < s.size() && s[pos] != '"') {
        char c = s[pos];
        if (c == '\\') {
            ++pos;
            if (pos >= s.size()) {
                throw LpcRuntimeError("restore_variable(): Illegal string format.");
            }
            char esc = s[pos++];
            result += (esc == '\r') ? '\n' : esc;
        } else if (c == '\r') {
            result += '\n';
            ++pos;
        } else {
            result += c;
            ++pos;
        }
    }
    if (pos >= s.size()) {
        throw LpcRuntimeError("restore_variable(): Illegal string format.");
    }
    ++pos; // consume the closing '"'
    if (pos != s.size()) {
        throw LpcRuntimeError("restore_variable(): Illegal string format.");
    }
    return Value(result);
}

// restore_variable(string)'s own top-level number reader -- real
// parse_numeric() (object.c), confirmed directly: a leading '-' with no
// digit after it is a real failure (ROB_NUMERAL_ERROR), not "-0" or a
// silent 0 -- parseRealSaveNumber() above has no such check (real
// restore_array()/restore_mapping() element parsing never hits this
// specific shape in practice, so that reader was never made to validate
// it). Exponent notation is not implemented, matching
// parseRealSaveNumber()'s own already-documented reasoning: real
// save_svalue() (the writer) never produces one, so a faithful reader for
// genuine on-disk data from this exact vendored driver doesn't need to
// parse one either.
Value parseRestoreVariableNumber(const std::string& s, size_t& pos) {
    bool neg = false;
    if (s[pos] == '-') {
        neg = true;
        ++pos;
        if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) {
            throw LpcRuntimeError("restore_variable(): Illegal numeric format.");
        }
    }
    size_t start = pos;
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
    bool isFloat = false;
    if (pos < s.size() && s[pos] == '.') {
        isFloat = true;
        ++pos;
        while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
    }
    std::string token = (neg ? "-" : "") + s.substr(start, pos - start);
    if (isFloat) return Value(std::stod(token));
    return Value(static_cast<int64_t>(std::stoll(token)));
}

// restore_variable(string)'s own top-level dispatcher -- real
// restore_svalue() (object.c), confirmed directly: only the T_STRING
// case has any "did this consume the whole buffer" check at all (see
// parseRestoreVariableString()'s own comment) -- a top-level array,
// mapping, or number with trailing garbage after it is silently accepted
// by real restore_svalue() exactly as if the garbage were not there, so
// this dispatcher reuses parseRealSaveValue() (already correctly lenient
// there) for the '(' and default-to-zero cases rather than adding a
// trailing check real FluffOS itself does not have.
Value parseRestoreVariableTopLevel(const std::string& s) {
    if (s.empty()) return Value(static_cast<int64_t>(0));
    size_t pos = 0;
    char c = s[pos];
    if (c == '"') {
        ++pos;
        return parseRestoreVariableString(s, pos);
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
        return parseRestoreVariableNumber(s, pos);
    }
    return parseRealSaveValue(s, pos);
}

// Real object.c's save_object() normalization (confirmed live: see
// save_object's own comment on why this exists): strip a trailing
// ".c", strip a trailing ".o" if already present so this stays
// idempotent, then always append ".o".
std::string normalizeSavePath(const std::string& path) {
    std::string result = path;
    if (result.size() >= 2 && result.compare(result.size() - 2, 2, ".c") == 0) {
        result.erase(result.size() - 2);
    }
    if (result.size() >= 2 && result.compare(result.size() - 2, 2, ".o") == 0) {
        result.erase(result.size() - 2);
    }
    return result + ".o";
}

// Real object.c's own object_visible() (only compiled when F_SET_HIDE
// is defined -- confirmed live in the vendored fluffos-2.9-ds2.08: "if
// (ob->flags & O_HIDDEN) { if (current_object->flags & O_HIDDEN) return
// 1; return valid_hide(current_object); } else return 1;"). An object
// that is not hidden is always visible; a hidden object is visible to
// another hidden object unconditionally (hidden things can see each
// other), and otherwise only if master()->valid_hide() grants the
// *observer* (current_object -- not the hidden object itself)
// permission -- the same master apply set_hide() is already gated
// behind (see its own registration above). Used by first_inventory()/
// next_inventory() below, both of which walk this exact skip when
// F_SET_HIDE is defined (efuns_main.c/simulate.c, confirmed directly)
// -- a real coupling this driver's own prior set_hide() slice makes
// observable here for the first time, not a separate feature.
bool isVisibleToObserver(VM& vm, const std::shared_ptr<LpcObject>& ob) {
    if (!ob || !ob->isHidden()) return true;
    auto observer = vm.currentObject();
    if (observer && observer->isHidden()) return true;
    auto master = vm.masterObject();
    return master && isTruthy(vm.callFunction(master, "valid_hide", {Value(observer)}));
}

// PCRE2-backed regexp/regexplode/reg_assoc support (Phase 0 row 0.11).
// The real fluffos-2.9-ds2.08 driver implements its own regexp() and
// reg_assoc() (efuns_main.c f_regexp()/f_reg_assoc(), array.c
// match_single_regexp()/match_regexp()/reg_assoc()) on top of a
// bundled Henry Spencer regex engine (regexp.c), not PCRE -- the
// PCRE2 wrapping directed by this row's own instruct.md/ROADMAP.md
// text is a deliberate modern substitution for that engine, not a
// literal port of it. The *behavioral* contract below (what each
// efun selects/splits/tokenizes and in what order) is reproduced
// exactly from those real functions, confirmed by reading them
// directly rather than assumed.
using Pcre2CodePtr = std::unique_ptr<pcre2_code, void (*)(pcre2_code*)>;

// compileOptions defaults to 0 (regexp()/regexplode()/reg_assoc()'s own
// original Phase 0 behavior, unchanged). pcre_match()/pcre_assoc() below
// (Phase 2 row 2.12) are the only callers that ever pass anything else.
Pcre2CodePtr compileRegex(const std::string& pattern, uint32_t compileOptions = 0) {
    int errorcode = 0;
    PCRE2_SIZE erroroffset = 0;
    pcre2_code* code = pcre2_compile(
        reinterpret_cast<PCRE2_SPTR>(pattern.data()),
        static_cast<PCRE2_SIZE>(pattern.size()),
        compileOptions, &errorcode, &erroroffset, nullptr);
    if (!code) {
        PCRE2_UCHAR msg[256];
        pcre2_get_error_message(errorcode, msg, sizeof(msg) / sizeof(PCRE2_UCHAR));
        throw LpcRuntimeError("regexp: bad pattern \"" + pattern + "\": " +
                               reinterpret_cast<char*>(msg));
    }
    return Pcre2CodePtr(code, [](pcre2_code* c) { pcre2_code_free(c); });
}

// Finds the next match at or after byteOffset in subject (a plain
// forward scan, not an anchored match at exactly byteOffset -- the
// same "search the remainder of the string" semantics real
// regexec(reg, tmp) has when tmp is a pointer into the middle of the
// original string). Returns false with no match found.
// matchOptions defaults to 0, same "unchanged for every pre-existing
// caller" rule as compileRegex()'s compileOptions above; only
// pcre_match()/pcre_assoc()'s own PCRE_A (anchored) flag passes anything
// else, via PCRE2_ANCHORED.
bool regexFindNext(pcre2_code* code, const std::string& subject, size_t byteOffset,
                    size_t& matchStart, size_t& matchEnd, uint32_t matchOptions = 0) {
    if (byteOffset > subject.size()) return false;
    pcre2_match_data* md = pcre2_match_data_create_from_pattern(code, nullptr);
    int rc = pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(subject.data()),
                          static_cast<PCRE2_SIZE>(subject.size()),
                          static_cast<PCRE2_SIZE>(byteOffset), matchOptions, md, nullptr);
    bool found = false;
    if (rc >= 0) {
        PCRE2_SIZE* ov = pcre2_get_ovector_pointer(md);
        matchStart = static_cast<size_t>(ov[0]);
        matchEnd = static_cast<size_t>(ov[1]);
        found = true;
    } else if (rc != PCRE2_ERROR_NOMATCH) {
        pcre2_match_data_free(md);
        throw LpcRuntimeError("regexp: match error");
    }
    pcre2_match_data_free(md);
    return found;
}

// Backs pluralize() below. Real strcasecmp(): a full-string,
// case-insensitive equality check, not a prefix match.
bool ciEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// Mechanical, line-by-line port of packages/contrib.c's real pluralize()
// (fluffos-2.9-ds2.08, ~440 lines: exception switch on the last word's
// first letter, general suffix rules on its last letter, then chop-and-
// append assembly) -- not a reimplementation from general English
// pluralization knowledge. std::nullopt matches real "if (!sz) return 0"
// (empty input).
std::optional<std::string> pluralizeWord(const std::string& str) {
    if (str.empty()) return std::nullopt;

    // "X of Y" -> only pluralize X, the " of Y" tail rides along verbatim.
    std::string ofSuffix;
    size_t xLen = str.size();
    if (auto pos = str.find(" of "); pos != std::string::npos) {
        ofSuffix = str.substr(pos);
        xLen = pos;
    }

    // Strip a leading "a "/"an " determiner (real code checks 'a'/'A'
    // only, never "the"). Bounded to the X-part (xLen) here, unlike the
    // real C body's own str[1]/str[2] reads against the full unbounded
    // buffer -- that only differs from this bounded version for a
    // degenerate X-part shorter than the determiner itself (e.g. the
    // literal input "a of thing"), where the real C code's own plen
    // computation goes negative: undefined behavior in the original, not
    // a portable behavior to replicate, and not a shape any real call
    // site across this row's six corpora ever produces.
    std::string pre;
    if (xLen > 0 && (str[0] == 'a' || str[0] == 'A')) {
        if (xLen > 1 && str[1] == ' ') {
            pre = str.substr(2, xLen - 2);
        } else if (xLen > 2 && str[1] == 'n' && str[2] == ' ') {
            pre = str.substr(3, xLen - 3);
        } else {
            pre = str.substr(0, xLen);
        }
    } else {
        pre = str.substr(0, xLen);
    }

    // Only the last word is actually pluralized -- "lose adjectives".
    std::string rel = pre;
    if (auto sp = pre.rfind(' '); sp != std::string::npos) rel = pre.substr(sp + 1);

    bool same = false;
    bool found = false;
    int chop = 0;
    std::string suffix = "s";

    if (!rel.empty()) {
        std::string rest = rel.substr(1);
        switch (rel[0]) {
        case 'A': case 'a':
            if (ciEquals(rest, "re")) { chop = 3; suffix = "is"; found = true; }
            break;
        case 'B': case 'b':
            if (ciEquals(rest, "us")) { suffix = "es"; found = true; break; }
            if (ciEquals(rest, "onus")) { suffix = "es"; found = true; }
            break;
        case 'C': case 'c':
            if (ciEquals(rest, "hild")) { suffix = "ren"; found = true; break; }
            if (ciEquals(rest, "liff")) { suffix = "s"; found = true; }
            break;
        case 'D': case 'd':
            if (ciEquals(rest, "atum")) { chop = 2; suffix = "a"; found = true; break; }
            if (ciEquals(rest, "ie")) { chop = 1; suffix = "ce"; found = true; break; }
            if (ciEquals(rest, "eer")) { same = true; found = true; break; }
            if (ciEquals(rest, "o")) { suffix = "es"; found = true; break; }
            if (ciEquals(rest, "ynamo")) { found = true; }
            break;
        case 'F': case 'f':
            if (ciEquals(rest, "oot")) { chop = 3; suffix = "eet"; found = true; break; }
            if (ciEquals(rest, "ish")) { same = true; found = true; break; }
            if (ciEquals(rest, "orum")) { chop = 2; suffix = "a"; found = true; break; }
            if (ciEquals(rest, "ife")) { found = true; }
            break;
        case 'G': case 'g':
            if (ciEquals(rest, "oose")) { chop = 4; suffix = "eese"; found = true; break; }
            if (ciEquals(rest, "o")) { suffix = "es"; found = true; break; }
            if (ciEquals(rest, "um")) { found = true; break; }
            if (ciEquals(rest, "iraffe")) { suffix = "s"; found = true; }
            break;
        case 'H': case 'h':
            if (ciEquals(rest, "uman")) { found = true; break; }
            if (ciEquals(rest, "ave")) { chop = 2; suffix = "s"; found = true; }
            break;
        case 'I': case 'i':
            if (ciEquals(rest, "ndex")) { chop = 2; suffix = "ices"; found = true; }
            break;
        case 'L': case 'l':
            if (ciEquals(rest, "ouse")) { chop = 4; suffix = "ice"; found = true; break; }
            if (ciEquals(rest, "otus")) { found = true; }
            break;
        case 'M': case 'm':
            if (ciEquals(rest, "ackerel")) { same = true; found = true; break; }
            if (ciEquals(rest, "oose")) { same = true; found = true; break; }
            if (ciEquals(rest, "ouse")) { chop = 4; suffix = "ice"; found = true; break; }
            if (ciEquals(rest, "atrix")) { chop = 1; suffix = "ces"; found = true; }
            break;
        case 'O': case 'o':
            if (ciEquals(rest, "x")) { suffix = "en"; found = true; }
            break;
        case 'P': case 'p':
            if (ciEquals(rest, "ants")) { same = true; found = true; }
            break;
        case 'Q': case 'q':
            if (ciEquals(rest, "uaff")) { found = true; }
            break;
        case 'R': case 'r':
            if (ciEquals(rest, "oof")) { found = true; }
            break;
        case 'S': case 's':
            if (ciEquals(rest, "niff")) { found = true; break; }
            if (ciEquals(rest, "heep")) { same = true; found = true; break; }
            if (ciEquals(rest, "phinx")) { chop = 1; suffix = "ges"; found = true; break; }
            if (ciEquals(rest, "taff")) { chop = 2; suffix = "ves"; found = true; break; }
            if (ciEquals(rest, "afe")) { found = true; break; }
            if (ciEquals(rest, "haman")) { found = true; }
            break;
        case 'T': case 't':
            if (ciEquals(rest, "hief")) { chop = 1; suffix = "ves"; found = true; break; }
            if (ciEquals(rest, "ooth")) { chop = 4; suffix = "eeth"; found = true; break; }
            if (ciEquals(rest, "alisman")) { suffix = "s"; found = true; }
            break;
        case 'V': case 'v':
            if (ciEquals(rest, "ax")) { suffix = "en"; found = true; break; }
            if (ciEquals(rest, "irus")) { suffix = "es"; found = true; }
            break;
        case 'W': case 'w':
            if (ciEquals(rest, "as")) { chop = 2; suffix = "ere"; found = true; }
            break;
        default:
            break;
        }
    }

    // General suffix rules on the last letter -- only when no exception
    // above already matched, and pre is non-empty.
    if (!found && !pre.empty()) {
        size_t len = pre.size();
        char last = pre[len - 1];
        char c2 = len > 1 ? pre[len - 2] : '\0';
        char c3 = len > 2 ? pre[len - 3] : '\0';
        switch (last) {
        case 'E': case 'e':
            if (len > 1 && (c2 == 'f' || c2 == 'F')) { chop = 2; suffix = "ves"; }
            break;
        case 'F': case 'f':
            if (len > 1 && (c2 == 'e' || c2 == 'E')) break;
            chop = 1;
            if (len > 1 && (c2 == 'f' || c2 == 'F')) chop++;
            suffix = "ves";
            break;
        case 'H': case 'h':
            if (len > 1 && (c2 == 'c' || c2 == 's')) suffix = "es";
            break;
        case 'N': case 'n':
            if (len > 2 && c2 == 'a' && c3 == 'm') { chop = 3; suffix = "men"; }
            break;
        case 'O': case 'o':
            if (len > 1 && c2 != 'o') suffix = "es";
            break;
        case 'S': case 's':
            if (len > 1 && c2 == 'i') { chop = 2; suffix = "es"; break; }
            if (len > 1 && c2 == 'u') { chop = 2; suffix = "i"; break; }
            if (len > 1 && (c2 == 'a' || c2 == 'e' || c2 == 'o')) suffix = "ses";
            else suffix = "es";
            break;
        case 'X': case 'x':
            suffix = "es";
            break;
        case 'Y': case 'y':
            if (len > 1 && c2 != 'a' && c2 != 'e' && c2 != 'i' && c2 != 'o' && c2 != 'u') {
                chop = 1; suffix = "ies";
            }
            break;
        case 'Z': case 'z':
            if (len > 1 && (c2 == 'a' || c2 == 'e' || c2 == 'o' || c2 == 'i' || c2 == 'u')) {
                suffix = "zes";
            } else {
                suffix = "es";
            }
            break;
        default:
            break;
        }
    }

    if (same) return pre + ofSuffix;

    std::string base = pre;
    if (chop > 0 && static_cast<size_t>(chop) <= base.size()) {
        base = base.substr(0, base.size() - static_cast<size_t>(chop));
    }
    return base + suffix + ofSuffix;
}

// Backs reclaim_objects() below. Recursively walks a Value in place,
// rewriting any reference to a now-destructed LpcObject to a plain int
// 0 -- the same self-healing coercion VM.cpp's own coerceIfDestructed()
// already applies lazily, one read at a time, at PushLocal/
// PushObjectVar/Index (see that function's own comment); this instead
// runs eagerly across an entire value graph, matching real
// reclaim.c's own check_svalue()/gc_mapping() recursion (array/mapping/
// closure-bound-args), not just this driver's four existing lazy read
// points. A mapping whose own *key* is a destructed object reference is
// a special case, matched exactly against real gc_mapping(): the whole
// entry is erased (real map_delete()), not rewritten to a 0 key --
// unlike this driver's Mapping (a plain vector<pair>, so an in-place key
// rewrite would risk colliding with an already-present real 0 key,
// which real FluffOS's own hash table structurally cannot produce
// either). depth mirrors real check_svalue()'s own MAX_RECURSION (25)
// guard against a pathological self-referential array/mapping.
int reclaimSweepValue(Value& v, int depth) {
    if (depth > 25) return 0;
    if (auto* obPtr = std::get_if<std::shared_ptr<LpcObject>>(&v.data)) {
        if (*obPtr && (*obPtr)->isDestructed()) {
            v = Value(int64_t{0});
            return 1;
        }
        return 0;
    }
    if (auto* arrPtr = std::get_if<std::shared_ptr<Array>>(&v.data)) {
        int cleaned = 0;
        if (*arrPtr) {
            for (auto& item : (*arrPtr)->items) cleaned += reclaimSweepValue(item, depth + 1);
        }
        return cleaned;
    }
    if (auto* mapPtr = std::get_if<std::shared_ptr<Mapping>>(&v.data)) {
        int cleaned = 0;
        if (*mapPtr) {
            auto& entries = (*mapPtr)->entries;
            for (size_t i = 0; i < entries.size();) {
                auto* keyOb = std::get_if<std::shared_ptr<LpcObject>>(&entries[i].first.data);
                if (keyOb && *keyOb && (*keyOb)->isDestructed()) {
                    (*mapPtr)->eraseAt(i);
                    ++cleaned;
                    continue;
                }
                cleaned += reclaimSweepValue(entries[i].first, depth + 1);
                cleaned += reclaimSweepValue(entries[i].second, depth + 1);
                if (!(*mapPtr)->extraColumns.empty()) {
                    for (auto& extra : (*mapPtr)->extraColumns[i]) {
                        cleaned += reclaimSweepValue(extra, depth + 1);
                    }
                }
                ++i;
            }
        }
        return cleaned;
    }
    if (auto* closPtr = std::get_if<std::shared_ptr<Closure>>(&v.data)) {
        int cleaned = 0;
        if (*closPtr) {
            for (auto& arg : (*closPtr)->boundArgs) cleaned += reclaimSweepValue(arg, depth + 1);
        }
        return cleaned;
    }
    return 0;
}

// ROADMAP.md row 1.16's own real cross-cutting gap, confirmed by real
// corpus evidence, not assumption: `temp/core-lib/secure/master/
// security.c` (RealmsMUD, the one confirmed genuinely LDMud-targeting
// mudlib this repo has) defines real, non-trivial `valid_read()`/
// `valid_write()` master applies -- privilege checks, a per-user access
// list, a real access-result cache -- backing its own automated security
// test suite (`lib/tests/secure/securityTest.c`). Before this, this
// driver gated **zero** of its 11 file-touching efuns with either
// dialect's real apply at all, so every file operation from a real
// mudlib's own code silently ran fully permissive regardless of what
// `valid_read()`/`valid_write()` were actually written to enforce -- not
// a "mudlib fails to run" gap the way `#'` closures or (initially
// suspected, then ruled out) `parse_*` are, but a real, confirmed
// security-and-test-suite-correctness one, affecting both dialects
// equally (real FluffOS has the identical applies -- `applies.h`'s own
// `APPLY_VALID_READ`(33)/`APPLY_VALID_WRITE`(38)).
//
// The two real dialects' own actual call conventions genuinely differ,
// confirmed by reading both real sources directly, not assumed from the
// shared apply names alone:
// - Real FluffOS (`file.c`'s own `check_valid_path()`): three arguments,
//   `(path, call_object, call_fun)` -- no uid concept at all.
// - Real LDMud (`doc/master/valid_read`/`valid_write`'s own SYNOPSIS,
//   already confirmed in an earlier session): four arguments, `(path,
//   uid-or-0, func, ob)` -- confirmed directly matching core-lib's own
//   real definition just cited, `valid_write(string path, string uid,
//   string method, object caller)`, same four names, same order. Calling
//   a real LDMud-shaped 4-parameter function with only 3 positional
//   arguments would silently leave its own real `caller` parameter unset
//   -- exactly the parameter core-lib's own real `isPriviledgedObject
//   (caller)` check depends on -- so this driver's own bare-vs-privileged
//   access distinction would never actually trigger for the one real
//   confirmed consumer this whole row exists for. Genuinely dialect-
//   gated here, not a single unified shape, the same discipline already
//   established for `shadow()`/`snoop()`/`replace_program()`.
//
// "uid" under the LDMud shape: real LDMud's own `check_valid_path()`
// (`temp/ldmud/src/simulate.c:3778-3795`) pushes the calling object's
// *effective* user name as this second argument -- literally `eff_user =
// caller.u.ob->eff_user;` then `if (eff_user != NULL && eff_user->name
// != NULL) push_ref_string(inter_sp, eff_user->name); else
// push_number(inter_sp, 0);`. `eff_user` is LDMud's euid (`efuns.c:4985`
// `geteuid()` returns `ob->eff_user->name`), so the faithful argument is
// `caller->euid()` (ROADMAP row 3.1's per-object euid, `std::nullopt`
// pushed as `0` exactly as real pushes `push_number(0)` for a NULL
// `eff_user`), not a uid string faked from `privs()`. Used only when the
// uid model is active (row 3.1's `get_root_uid`/`get_master_uid` boot
// apply fired). When it is inactive -- an LDMud mudlib that never
// defined that apply, so no object ever gets an euid -- this falls back
// to the original `privs()` approximation this row shipped with, so such
// a mudlib's `valid_read`/`valid_write` see exactly what they saw
// before. Real FluffOS's own `check_valid_path()` (`file.c:705-750`)
// pushes no uid argument at all, so the fluffos branch below is
// unaffected either way.
//
// Real semantics for the *result*, matching `check_valid_path()`
// exactly: the master not defining `valid_read`/`valid_write` at all
// (this driver's own real "undefined function call returns void"
// contract, distinct at the `Value` level from an explicit `int` `0` --
// confirmed via `std::monostate` vs `int64_t{0}`, no separate sentinel
// needed the way real FluffOS's own `-1` return is) is a real,
// permissive default: every existing mudlib that never defined either
// apply (this driver's own bundled "library" mudlib included) keeps
// working completely unchanged. An explicit integer `0` return denies.
// A string return replaces the path (`sanitizedPath`, matching
// `check_valid_path()`'s own `path = v->u.string;` and core-lib's own
// real `valid_write()` return value on success). Anything else (e.g. a
// bare truthy `1`) allows with the original path unchanged, matching
// `check_valid_path()`'s own final `else` branch. ROADMAP row 3.2's own
// last named gap, closed here: after the master-apply gate above,
// `legal_path()`'s own `..`/`#`-character sanity check (`legalPathFluffos`/
// `legalPathLdmud` just above, ported from real `file.c:295-334` /
// `simulate.c:1734-1776`) now runs on whatever path the master approved,
// exactly where real `check_valid_path()` runs it -- after the apply,
// on the (possibly rewritten) path, with one leading '/' stripped for the
// check only (`checkPath` below; the leading-'/'-keeping return value
// itself is unchanged, matching this driver's own established
// `resolveMudlibPath()` convention rather than adopting real code's own
// separate "return without a leading '/'" contract). Real FluffOS denies
// silently (`file.c:750`'s bare `return 0`, indistinguishable from a
// master `valid_write`/`valid_read` denial to every existing call site
// here); real LDMud instead throws a catchable runtime error
// (`simulate.c:3846-3849`'s own `errorf("Illegal path '%s' for %s() by
// %s\n", ...)`), a genuine dialect divergence ported faithfully below
// rather than flattened to one shared behavior.
// Mechanical port of real FluffOS `file.c:295-334`'s own `legal_path()` --
// the traversal/injection gate `check_valid_path()` (`file.c:747`) runs
// AFTER the `valid_read`/`valid_write` master apply, on whatever path that
// apply returned, stripped of one leading '/' (`file.c:743-746`, mirrored
// locally by `checkValidPath()` below rather than changing this driver's
// own established `resolveMudlibPath()` leading-'/'-keeping convention).
// Ported line by line off real `p[0]`/`p[1]` pointer-walk semantics
// (`std::string::operator[]` is UB past `size()`, so a bounds-checked `at`
// lambda stands in for C's own null-terminator read), not reimplemented
// from a general "block dot-dot-slash" idea -- this preserves every real
// quirk exactly, including ones a from-scratch rewrite would likely lose:
// a bare "." is accepted (real: "trailing '.' ok"), but embedded "/./" and
// even a *leading* "./" are both rejected right along with "..", because
// real code's own `if (p[1] == '/' || p[1] == '\0') return 0;` fires for
// any single-dot component, not only a double-dot one. Real corpus
// evidence this is not academic: this driver's own `resolveMudlibPath()`
// (`VM.cpp`) is a bare `config_.mudlibRoot() + lpcPath` string
// concatenation with no sandboxing of its own -- every mudlib in this
// project's corpus builds file-efun paths straight from LPC string
// concatenation, much of it player-influenced (e.g. `secure/daemon/
// master.c`-style `valid_write()` bodies gate by prefix, and callers like
// `"/save/" + name + ".o"` in login/creation flows before this gate
// existed would have let a crafted `name` -- literally "..#etc#passwd"-
// shaped -- walk `write_file()`/`read_file()` straight past `mudlibRoot`
// on real disk I/O). The real `#`-rejection (`file.c:307-309`) is its own
// documented real-world motivation, not invented here: "disallowing #
// seems the easiest way to solve a bug involving loading files containing
// that character" -- real FluffOS filenames use trailing `#<clone-number>`
// for cloned-object identity (`object.c`'s own `#` clone-suffix
// convention), so a raw `#` reaching a file efun risks colliding with or
// forging that internal naming scheme, a truncation/confusion attack
// distinct from plain `../` traversal.
bool legalPathFluffos(const std::string& path) {
    if (path.empty()) return false;   // real: `path == NULL` guard
    if (path[0] == '/') return false; // real: dead code once upstream
                                       // strips one leading '/', kept for
                                       // a doubled "//..." to still reject
    if (path.find('#') != std::string::npos) return false;

    auto at = [&](size_t i) -> char { return i < path.size() ? path[i] : '\0'; };
    for (size_t p = 0;;) {
        if (at(p) == '.') {
            if (at(p + 1) == '\0') break;              // trailing '.' ok
            if (at(p + 1) == '.') p++;                 // '..' or '../'
            if (at(p + 1) == '/' || at(p + 1) == '\0') return false;
        }
        size_t next = path.find("/.", p);
        if (next == std::string::npos) break;
        p = next + 1; // step over '/'
    }
    return true;
}

// Mechanical port of real LDMud `simulate.c:1762-1776`'s own `legal_path()`
// plus the `check_no_parentdirs()` helper it calls (`simulate.c:1734-1759`),
// invoked the same way real `check_valid_path()` does
// (`simulate.c:3841`), after the master apply and the leading-'/' strip.
// A genuinely different dialect shape from FluffOS's own version above,
// confirmed by reading both real bodies directly rather than assuming a
// shared algorithm behind the shared name: LDMud never rejects '#' at all
// (grepped the whole file, no mention), a leading '/' is checked the same
// way, and only a literal anchored ".." *component* is rejected -- real
// `check_no_parentdirs()` only fires when the second dot is followed by
// end-of-string or '/' AND the first dot is at the start of the path or
// immediately after a '/' -- so unlike FluffOS's own version, a plain
// embedded "/./ " or a leading "./" is left alone here, matching real
// LDMud's narrower check exactly rather than harmonizing the two dialects.
// LDMud's own `legal_path()` also rejects a bare space unless
// `allow_filename_spaces` (`main.c:139`) was set, which defaults
// `MY_FALSE` and is a command-line-only toggle (`main.c:372,2863-2867`)
// this driver exposes no equivalent config key for, so the space check
// below is applied unconditionally, matching the real default build.
bool legalPathLdmud(const std::string& path) {
    if (path.find(' ') != std::string::npos) return false;
    if (!path.empty() && path[0] == '/') return false;

    size_t p = path.find('.');
    while (p != std::string::npos) {
        char afterFirst = (p + 1 < path.size()) ? path[p + 1] : '\0';
        if (afterFirst != '.') {
            p = path.find('.', p + 1);
            continue;
        }
        char afterSecond = (p + 2 < path.size()) ? path[p + 2] : '\0';
        bool atComponentStart = (p == 0) || (path[p - 1] == '/');
        if ((afterSecond == '\0' || afterSecond == '/') && atComponentStart) {
            return false;
        }
        // real: `p++;` (simulate.c:1756) then the for-loop's own
        // `strchr(p+1, '.')` -- together a search resuming from `p + 2`.
        p = path.find('.', p + 2);
    }
    return true;
}

std::optional<std::string> checkValidPath(VM& vm, const std::string& rawPath, bool writeFlag,
                                           const std::string& funcName) {
    // Same "not loaded yet" skip already established for privs
    // (ObjectManager::initPrivsForObject()'s own comment: "Skipped...
    // when master_ itself is not loaded yet -- this only matters for
    // master.c's own bootstrap load") -- real boot always has a master
    // loaded well before any mudlib file efun could plausibly run, so
    // this is not a real-world gap, only a real one for this driver's
    // own test harness (many existing tests construct a bare VM/
    // ObjectManager with no master loaded at all) and any equally bare
    // embedding. VM::applyMaster() itself throws hard when no master is
    // loaded (a real, intentional guard for callers that *need* a real
    // master) -- checked here first instead so a missing master reads
    // as the same permissive default as a master that simply never
    // defined valid_read/valid_write, not a new hard failure mode this
    // row would otherwise introduce into every file efun at once.
    if (!vm.masterObject()) return rawPath;

    auto caller = vm.currentObject();
    std::vector<Value> args;
    if (vm.config().dialect() == "ldmud") {
        // real LDMud check_valid_path(): the effective user name, or 0
        // when there is none. euid() once the uid model is active
        // (simulate.c:3792-3795); the pre-row-3.1 privs() stand-in only
        // while it is inactive.
        std::optional<std::string> uidName;
        if (caller) {
            uidName = vm.objectManager().uidModel().active() ? caller->euid()
                                                             : caller->privs();
        }
        Value uidArg = uidName.has_value() ? Value(*uidName) : Value{};
        args = {Value(rawPath), uidArg, Value(funcName), Value(caller)};
    } else {
        args = {Value(rawPath), Value(caller), Value(funcName)};
    }
    Value result = vm.applyMaster(writeFlag ? "valid_write" : "valid_read", std::move(args));

    std::optional<std::string> gated;
    if (std::holds_alternative<std::monostate>(result.data)) {
        gated = rawPath;
    } else if (auto* denied = std::get_if<int64_t>(&result.data)) {
        gated = (*denied == 0) ? std::nullopt : std::optional<std::string>(rawPath);
    } else if (auto* rewritten = std::get_if<std::string>(&result.data)) {
        gated = *rewritten;
    } else {
        gated = rawPath;
    }
    if (!gated) return std::nullopt;

    // real: `if (path[0] == '/') path++;` (file.c:743-744 /
    // simulate.c:3825-3832) -- for the legality check only, see this
    // function's own header comment on why the returned path itself keeps
    // its leading '/'.
    const std::string& approved = *gated;
    std::string checkPath = (!approved.empty() && approved[0] == '/') ? approved.substr(1) : approved;

    bool ldmud = vm.config().dialect() == "ldmud";
    bool legal = ldmud ? legalPathLdmud(checkPath) : legalPathFluffos(checkPath);
    if (legal) return gated;

    if (ldmud) {
        // real: `errorf("Illegal path '%s' for %s() by %s\n", ...)`
        // (simulate.c:3846-3849) -- a catchable runtime error, not a
        // silent denial. `caller` may be null here in ways real code's own
        // `fatal("Illegal caller for check_valid_path.\n")` guard would
        // not permit (this driver already reaches this point for calls
        // with no current object); "?" stands in rather than crashing.
        throw LpcRuntimeError("Illegal path '" + checkPath + "' for " + funcName + "() by " +
                               (caller ? caller->filename() : "?"));
    }
    // real: `file.c:750`'s bare `return 0` -- silent denial,
    // indistinguishable from a master-apply deny to every call site.
    return std::nullopt;
}

// Backs replace_program() below. Real search_inherited() (replace_program.c):
// a depth-first walk of prog's own direct inherits, checking each one's
// own name for a match *before* recursing into it (matching real code's
// "if (match) return; else if (recurse into it succeeds) return;" order,
// not a breadth-first or recurse-first search). Matched here against
// each inherit's own raw, as-written path string
// (CompiledProgram::inherits[i], parallel to inheritedPrograms[i], same
// order -- confirmed directly in ObjectManager::compile()) rather than a
// stored canonical filename the way real prog->filename works, since
// this driver's CompiledProgram carries no filename field of its own;
// both sides go through the exact same ObjectManager::normalizeFilename()
// this driver already uses everywhere else for path identity, so a
// caller writing "std/room" or "std/room.c" matches an ancestor's own
// "inherit \"std/room\";" either way. Once a match is found, its real
// "var_offset" is not accumulated by hand here -- CompiledProgram::
// ancestorBaseOffsets on the *searching object's own top-level program*
// already has a direct entry for every transitive ancestor (built once,
// recursively, at compile time -- see its own Bytecode.hpp comment), so
// the caller does a single map lookup instead.
std::shared_ptr<CompiledProgram> searchInheritedProgram(const CompiledProgram& prog,
                                                          const std::string& normalizedTarget) {
    for (size_t i = 0; i < prog.inherits.size() && i < prog.inheritedPrograms.size(); ++i) {
        const auto& child = prog.inheritedPrograms[i];
        if (!child) continue;
        if (ObjectManager::normalizeFilename(prog.inherits[i]) == normalizedTarget) {
            return child;
        }
        if (auto found = searchInheritedProgram(*child, normalizedTarget)) {
            return found;
        }
    }
    return nullptr;
}

// Backs query_num() below. Real number_as_string() (packages/contrib.c):
// converts n in [0,99] to English cardinal words, appended to buf (not
// overwritten). hi[1] is genuine real dead code -- n/10 == 1 only ever
// happens for n in [10,19], already handled by the low[] branch above
// this one, ported faithfully rather than "cleaned up" out of the port.
void appendNumberWord(std::string& buf, int64_t n) {
    static const char* low[] = {"ten", "eleven", "twelve", "thirteen",
        "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
    static const char* hi[] = {"", "", "twenty", "thirty", "forty", "fifty",
        "sixty", "seventy", "eighty", "ninety"};
    static const char* single[] = {"", "one", "two", "three", "four", "five",
        "six", "seven", "eight", "nine"};
    if (n == 0) { buf += "zero"; return; }
    if (n < 20 && n > 9) { buf += low[n - 10]; return; }
    buf += hi[n / 10];
    if (n > 20 && (n % 10)) buf += "-";
    n %= 10;
    buf += single[n];
}

// Mechanical, line-by-line port of packages/contrib.c's real query_num()/
// f_query_num(): converts n to English cardinal words up to 99999 (or a
// caller-supplied lower ceiling), "many" past that or for a negative n.
// Not a reimplementation from general English-number-formatting
// knowledge -- ported directly against the real C body, including its
// own real thousands/hundreds/units assembly order (a comma before a
// following hundreds group only when thousands already contributed, an
// "and" before the final units group whenever anything higher-order
// did).
std::string queryNumWord(int64_t n, int64_t limit) {
    if ((limit && n > limit) || n < 0 || n > 99999) {
        return "many";
    }
    std::string ret;
    bool changed = false;

    int64_t thousands = n / 1000;
    if (thousands) {
        n %= 1000;
        appendNumberWord(ret, thousands);
        ret += " thousand";
        if (!n) return ret;
        changed = true;
    }

    int64_t hundreds = n / 100;
    if (hundreds) {
        n %= 100;
        if (changed) {
            if (!n) {
                ret += " and ";
                appendNumberWord(ret, hundreds);
                ret += " hundred";
                return ret;
            }
            ret += ", ";
            appendNumberWord(ret, hundreds);
            ret += " hundred";
        } else {
            if (!n) {
                appendNumberWord(ret, hundreds);
                ret += " hundred";
                return ret;
            }
            appendNumberWord(ret, hundreds);
            ret += " hundred";
            changed = true;
        }
    }

    if (changed) ret += " and ";
    appendNumberWord(ret, n);
    return ret;
}

} // namespace

void registerCoreEfuns() {
    auto& t = EfunTable::instance();

    t.registerEfun("write", [](VM& vm, std::vector<Value>& args) -> Value {
        if (!args.empty() && std::holds_alternative<std::string>(args[0].data)) {
            const std::string& s = std::get<std::string>(args[0].data);
            if (Connection* conn = OutputContext::current()) {
                deliverToConnection(vm, conn, s);
            } else {
                std::cout << s;
            }
        }
        return Value{int64_t{1}};
    });

    // object this_object() -- was a permanent void stub before
    // VM::currentObject() existed (added for input_to()'s own needs);
    // now a direct read of it. Confirmed live: secure/std/login.c's
    // whole account-creation flow sends its prompts via "message(type,
    // text, this_object())".
    t.registerEfun("this_object", [](VM& vm, std::vector<Value>&) -> Value {
        auto ob = vm.currentObject();
        if (!ob) return Value{};
        return Value(ob);
    });

    // object clone_object(string) and its real alias "new" (func_spec.c:
    // "object clone_object _new(string, ...);") -- confirmed live:
    // secure/daemon/master.c's own player_object() calls "new(OB_USER)"
    // to create the actual player object behind a virtual player path.
    auto cloneObjectImpl = [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("clone_object: expected a string filename argument");
        }
        const std::string& filename = std::get<std::string>(args[0].data);
        auto obj = vm.cloneObject(filename);
        if (!obj) {
            throw LpcRuntimeError("clone_object: failed to load " + filename);
        }
        return Value(obj);
    };
    t.registerEfun("clone_object", cloneObjectImpl);
    t.registerEfun("new", cloneObjectImpl);

    // int sizeof(mixed) -- real FluffOS's sizeof() also measures a
    // string's length (func_spec.c literally defines strlen/strstr... no
    // wait, strlen is its own alias: "int strlen sizeof(string);" --
    // confirmed live against secure/daemon/account_d.c, a since-
    // discarded early scratch mudlib object used only for live driver
    // verification, not real vendored corpus content and not this
    // driver's own real, shipped /single/account_d.c (built 2026-08-21,
    // notes/ACCOUNT_LOGIN_PLAN.md, which checks name == "" instead of
    // this idiom): its own "!name || !sizeof(name)" idiom, used
    // throughout that scratch mudlib as the standard "is this string
    // empty" check, was silently always taking the empty-string branch
    // before this string case existed here, since a missing case fell
    // through to a plain 0 rather than throwing -- masked because "0"
    // also happens to be account_exists()'s own correct answer for a
    // brand new account, not because the string branch was actually
    // running).
    auto sizeofImpl = [](VM&, std::vector<Value>& args) -> Value {
        if (!args.empty()) {
            if (auto* arr = std::get_if<std::shared_ptr<Array>>(&args[0].data)) {
                return Value(static_cast<int64_t>(*arr ? (*arr)->items.size() : 0));
            }
            if (auto* map = std::get_if<std::shared_ptr<Mapping>>(&args[0].data)) {
                return Value(static_cast<int64_t>(*map ? (*map)->entries.size() : 0));
            }
            if (auto* str = std::get_if<std::string>(&args[0].data)) {
                return Value(static_cast<int64_t>(str->size()));
            }
            // sizeof(buffer) is the buffer's byte length in real
            // FluffOS (row 2.33a). The corpus-common "read_buffer(b,
            // sizeof(b))" idiom depends on this.
            if (auto* buf = std::get_if<std::shared_ptr<Buffer>>(&args[0].data)) {
                return Value(static_cast<int64_t>(*buf ? (*buf)->bytes.size() : 0));
            }
        }
        return Value(int64_t{0});
    };
    t.registerEfun("sizeof", sizeofImpl);
    t.registerEfun("strlen", sizeofImpl);
    // int strwidth(string) -- real efun_defs.c: F_SIZEOF | F_ALIAS_FLAG,
    // the exact same code as sizeof() (func_spec.c's own "int strwidth
    // sizeof(string);" alias line, confirmed directly), not a real
    // display-width calculation of its own (no wide-character/ANSI-code
    // awareness in real FluffOS's own implementation despite the name).
    t.registerEfun("strwidth", sizeofImpl);

    // int *str_to_arr(string) / string arr_to_str(int *) -- func_spec.c's
    // USE_ICONV conversion pair
    // (temp/reference/fluffos-2.9-ds2.08/func_spec.c:398-399), bodied in
    // fliconv.c: f_str_to_arr() at :159, f_arr_to_str() at :178. The
    // current locally-vendored clone (temp/fluffos/src/) dropped both
    // when FluffOS moved to always-on Unicode -- ChangeLog.fluffos-2.x
    // ("added str_to_arr, and arr_to_str efuns to convert between strings
    // and UTF-32 arrays") is the last mention -- so the 2.9 ds2.08 tree
    // is the sole reference, the same single-tree basis
    // string_difference() (row 2.52) noted for itself. strwidth() just
    // above is the third name in the same USE_ICONV block; it is left as
    // its already-registered non-USE_ICONV `sizeof` alias, a pre-existing
    // choice with its own row, not revisited here.
    //
    // Real semantics, traced from fliconv.c:
    //   - str_to_arr(s): iconv-converts s from UTF-8 to UTF-32 over
    //     SVALUE_STRLEN(sp)+1 bytes -- i.e. INCLUDING the terminating NUL
    //     -- then returns an int array of the resulting 32-bit units
    //     (len /= 4). Because the NUL is part of the converted input, the
    //     returned array always carries a trailing 0 element:
    //     str_to_arr("AB") -> ({ 65, 66, 0 }), str_to_arr("") -> ({ 0 }).
    //     An embedded NUL byte likewise becomes a 0 element in place.
    //   - arr_to_str(a): treats each array element as a UTF-32 code
    //     point, appends a 0, iconv-converts UTF-32 -> UTF-8, and returns
    //     the result via copy_and_push_string(), which stops at the first
    //     NUL. So arr_to_str(({ 65, 66 })) -> "AB", an embedded 0
    //     truncates: arr_to_str(({ 65, 0, 66 })) -> "A", and
    //     arr_to_str(({})) -> "".
    //   The two are inverses for valid text: arr_to_str(str_to_arr(s))
    //   == s, since the trailing 0 str_to_arr appends is exactly the NUL
    //   arr_to_str stops on.
    //
    // Named local choices, none a silent divergence:
    //   - Implemented as a direct UTF-8 <-> code-point codec rather than
    //     through a live iconv dependency. iconv "UTF-8" <-> "UTF-32" is
    //     exactly a code-point transcode; real's translator name is
    //     "UTF-32//TRANSLIT//IGNORE" on Linux and str_to_arr's one-time
    //     warm-up call (translate_easy(newt->outgoing, " ")) exists only
    //     to consume iconv's leading UTF-32 BOM, so the observable
    //     LPC-level values are BOM-free code points -- what this codec
    //     produces.
    //   - Malformed input is ignored, matching real's "//IGNORE": a byte
    //     that is not valid UTF-8 (bad lead byte, truncated or bad
    //     continuation, overlong, surrogate, or > U+10FFFF) is skipped by
    //     str_to_arr; an out-of-range, surrogate, or negative code point
    //     is skipped by arr_to_str. A float array element is truncated to
    //     an integer code point; any other non-integer element throws.
    //   - max_string_length is not enforced (this driver has none, the
    //     same situation add_a()/replace_html() are in).
    //   A non-string argument to str_to_arr, or a non-array argument to
    //   arr_to_str, throws LpcRuntimeError, this codebase's established
    //   bad-shape precedent.
    //
    // Corpus call-site frequency, checked before implementing: grepped
    // every vendored corpus under temp/ (core-lib, dead-souls,
    // es2_mudlib, lima, nightmare3, reference-lpc-mud-library, wiz_tools,
    // lil) plus the bundled mudlib/ for str_to_arr( / arr_to_str(: zero
    // real LPC call sites. Motivation is FluffOS-surface parity, the same
    // honestly-named basis as rows 2.46/2.50/2.51/2.52/2.53/2.54/2.55;
    // UTF-8 round-tripping is independently verifiable by hand against
    // known code points with no live-instance dependency.
    {
        auto decodeUtf8ToCodePoints = [](const std::string& s) -> std::vector<int64_t> {
            std::vector<int64_t> out;
            const size_t n = s.size();
            size_t i = 0;
            while (i < n) {
                unsigned char c = static_cast<unsigned char>(s[i]);
                int64_t cp;
                int extra;
                if (c < 0x80) {
                    cp = c;
                    extra = 0;
                } else if ((c & 0xE0) == 0xC0) {
                    cp = c & 0x1F;
                    extra = 1;
                } else if ((c & 0xF0) == 0xE0) {
                    cp = c & 0x0F;
                    extra = 2;
                } else if ((c & 0xF8) == 0xF0) {
                    cp = c & 0x07;
                    extra = 3;
                } else {
                    ++i;  // stray continuation byte or 5/6-byte lead: skip
                    continue;
                }
                if (i + static_cast<size_t>(extra) >= n) {
                    ++i;  // truncated multibyte sequence at end of string
                    continue;
                }
                bool ok = true;
                for (int k = 1; k <= extra; ++k) {
                    unsigned char cc = static_cast<unsigned char>(s[i + k]);
                    if ((cc & 0xC0) != 0x80) {
                        ok = false;
                        break;
                    }
                    cp = (cp << 6) | (cc & 0x3F);
                }
                if (!ok) {
                    ++i;
                    continue;
                }
                // Reject overlong encodings, UTF-16 surrogates, and
                // anything past the Unicode ceiling.
                bool overlong = (extra == 1 && cp < 0x80) ||
                                (extra == 2 && cp < 0x800) ||
                                (extra == 3 && cp < 0x10000);
                if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                    i += static_cast<size_t>(extra) + 1;
                    continue;
                }
                out.push_back(cp);
                i += static_cast<size_t>(extra) + 1;
            }
            return out;
        };
        auto encodeCodePointUtf8 = [](std::string& out, int64_t cp) {
            if (cp < 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                return;  // real's //IGNORE
            }
            if (cp < 0x80) {
                out += static_cast<char>(cp);
            } else if (cp < 0x800) {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        };
        t.registerEfun("str_to_arr", [decodeUtf8ToCodePoints](VM&, std::vector<Value>& args) -> Value {
            if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
                throw LpcRuntimeError("str_to_arr: expected a string argument");
            }
            auto out = std::make_shared<Array>();
            for (int64_t cp : decodeUtf8ToCodePoints(std::get<std::string>(args[0].data))) {
                out->items.push_back(Value(cp));
            }
            // Real converts SVALUE_STRLEN(sp)+1 bytes, so the terminating
            // NUL always lands as a trailing 0 element.
            out->items.push_back(Value(int64_t{0}));
            return Value(out);
        });
        t.registerEfun("arr_to_str", [encodeCodePointUtf8](VM&, std::vector<Value>& args) -> Value {
            if (args.empty() || !std::holds_alternative<std::shared_ptr<Array>>(args[0].data)) {
                throw LpcRuntimeError("arr_to_str: expected an int array argument");
            }
            const auto& arr = std::get<std::shared_ptr<Array>>(args[0].data);
            std::string out;
            if (arr) {
                for (const Value& el : arr->items) {
                    int64_t cp;
                    if (auto* n = std::get_if<int64_t>(&el.data)) {
                        cp = *n;
                    } else if (auto* d = std::get_if<double>(&el.data)) {
                        cp = static_cast<int64_t>(*d);
                    } else {
                        throw LpcRuntimeError("arr_to_str: array element is not an integer");
                    }
                    if (cp == 0) break;  // copy_and_push_string stops at the NUL
                    encodeCodePointUtf8(out, cp);
                }
            }
            return Value(out);
        });
    }

    // mixed *map_array map(mixed *arr, string | function func, ...) --
    // real func_spec.cpp signature ("map_array map(...)": map_array is
    // the alias name, map is the real one -- same alias-before-real
    // ordering already confirmed for nullp/undefinedp above -- both
    // registered against this one implementation). Two real shapes,
    // both confirmed live needed by std/user/nmsh.c's own do_nickname()
    // ("map_array(explode(str, \" \"), \"replace_nickname\",
    // this_object())") and process_request()'s USERS/PRESENT cases
    // ("map_array(filter_array(...), \"user_names\", this_object())"):
    // a string function name plus a target object calls
    // target->func(element, extra_args...) for each element; a real
    // Closure value calls it directly via evaluate()'s own
    // VM::callClosure(), matching the bound-args-before-extra-args order
    // Value.hpp's Closure comment already documents. Only array (not
    // mapping) input is implemented -- no real call site in this
    // mudlib's confirmed path maps over a mapping.
    auto mapArrayImpl = [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::shared_ptr<Array>>(args[0].data)) {
            throw LpcRuntimeError("map_array: expected an array first argument");
        }
        auto arr = std::get<std::shared_ptr<Array>>(args[0].data);
        auto result = std::make_shared<Array>();
        if (!arr) return Value(result);

        if (auto* closurePtr = std::get_if<std::shared_ptr<Closure>>(&args[1].data)) {
            if (!*closurePtr) return Value(result);
            std::vector<Value> extra(args.begin() + 2, args.end());
            for (const auto& item : arr->items) {
                std::vector<Value> callArgs;
                callArgs.reserve(1 + extra.size());
                callArgs.push_back(item);
                callArgs.insert(callArgs.end(), extra.begin(), extra.end());
                result->items.push_back(vm.callClosure(*closurePtr, std::move(callArgs)));
            }
            return Value(result);
        }

        if (!std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("map_array: expected a string or function second argument");
        }
        if (args.size() < 3 || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[2].data)) {
            throw LpcRuntimeError("map_array: string function name requires an object third argument");
        }
        const std::string& funcName = std::get<std::string>(args[1].data);
        auto target = std::get<std::shared_ptr<LpcObject>>(args[2].data);
        std::vector<Value> extra(args.begin() + 3, args.end());
        for (const auto& item : arr->items) {
            std::vector<Value> callArgs;
            callArgs.reserve(1 + extra.size());
            callArgs.push_back(item);
            callArgs.insert(callArgs.end(), extra.begin(), extra.end());
            // Real array.c's own map_array()/f_map() calls this exact
            // string-target-object shape via "apply(func, ob, 1+numex,
            // ORIGIN_EFUN)" (confirmed directly) -- a mudlib-supplied
            // callback argument invoked from inside an efun's own C
            // body, the real, narrow category ORIGIN_EFUN actually
            // covers (not "any efun calling into LPC for any reason at
            // all" -- present()'s own id() check and every master apply
            // below use ORIGIN_DRIVER instead, confirmed separately,
            // each at its own call site). Every other map_array/
            // filter_array/sort_array/unique_array/unique_mapping/
            // map_mapping/filter_mapping string-form callback below
            // shares this same real citation, not repeated at each one.
            result->items.push_back(
                vm.callFunction(target, funcName, std::move(callArgs), Origin::Efun));
        }
        return Value(result);
    };
    t.registerEfun("map_array", mapArrayImpl);
    t.registerEfun("map", mapArrayImpl);

    // mixed filter_array filter(mixed *arr, string | function func, ...)
    // -- same alias-before-real naming as map_array/map above
    // (func_spec.cpp: "mixed *filter_array filter(mixed *, ...);"), same
    // two call shapes as map_array, keeping only the elements for which
    // the call returns truthy. Surfaced alongside map_array in the same
    // std/user/nmsh.c call sites (both always used together there:
    // "map_array(filter_array(...), ...)"). Real filter() also accepts
    // a string or mapping first argument (func_spec.cpp's own "string |
    // mixed * | mapping, ..."); only the array form is implemented,
    // matching every real call site this mudlib actually uses.
    auto filterArrayImpl = [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::shared_ptr<Array>>(args[0].data)) {
            throw LpcRuntimeError("filter_array: expected an array first argument");
        }
        auto arr = std::get<std::shared_ptr<Array>>(args[0].data);
        auto result = std::make_shared<Array>();
        if (!arr) return Value(result);

        if (auto* closurePtr = std::get_if<std::shared_ptr<Closure>>(&args[1].data)) {
            if (!*closurePtr) return Value(result);
            std::vector<Value> extra(args.begin() + 2, args.end());
            for (const auto& item : arr->items) {
                std::vector<Value> callArgs;
                callArgs.reserve(1 + extra.size());
                callArgs.push_back(item);
                callArgs.insert(callArgs.end(), extra.begin(), extra.end());
                if (isTruthy(vm.callClosure(*closurePtr, std::move(callArgs)))) {
                    result->items.push_back(item);
                }
            }
            return Value(result);
        }

        if (!std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("filter_array: expected a string or function second argument");
        }
        if (args.size() < 3 || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[2].data)) {
            throw LpcRuntimeError("filter_array: string function name requires an object third argument");
        }
        const std::string& funcName = std::get<std::string>(args[1].data);
        auto target = std::get<std::shared_ptr<LpcObject>>(args[2].data);
        std::vector<Value> extra(args.begin() + 3, args.end());
        for (const auto& item : arr->items) {
            std::vector<Value> callArgs;
            callArgs.reserve(1 + extra.size());
            callArgs.push_back(item);
            callArgs.insert(callArgs.end(), extra.begin(), extra.end());
            // Origin::Efun -- see map_array's own comment above.
            if (isTruthy(vm.callFunction(target, funcName, std::move(callArgs), Origin::Efun))) {
                result->items.push_back(item);
            }
        }
        return Value(result);
    };
    t.registerEfun("filter_array", filterArrayImpl);
    t.registerEfun("filter", filterArrayImpl);

    // mixed *sort_array(mixed *arr, int | string | function cmp, ...) --
    // real func_spec.c signature. Only the "string function name plus a
    // target object" shape is implemented, mirroring map_array/
    // filter_array's own scoping above (a Closure comparator is also
    // accepted, same as those two) -- confirmed the only shape this
    // mudlib's own path uses live: secure/daemon/player.c's own
    // add_player_info(), "sort_array(player_list, \"sort_list\",
    // this_object())". The comparator is called as target->func(a, b)
    // (or evaluate()'d for a closure) for each pair during a stable
    // sort, and must return an int: negative if a sorts before b,
    // positive if after, 0 if equal -- confirmed against array.c's own
    // sort_array_cmp()/quickSort() (the callback return value is used
    // directly as the comparator result, real C qsort convention). The
    // real efun's own plain-int first-mode ("ascending"/"descending" of
    // a homogeneous string/int/float array, no callback) is not
    // implemented -- nothing on this driver's live path uses it.
    t.registerEfun("sort_array", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::shared_ptr<Array>>(args[0].data)) {
            throw LpcRuntimeError("sort_array: expected an array first argument");
        }
        auto arr = std::get<std::shared_ptr<Array>>(args[0].data);
        auto result = std::make_shared<Array>();
        if (!arr) return Value(result);
        result->items = arr->items;

        auto compare = [&](const Value& a, const Value& b) -> int64_t {
            Value cmp;
            if (auto* closurePtr = std::get_if<std::shared_ptr<Closure>>(&args[1].data)) {
                if (!*closurePtr) return 0;
                cmp = vm.callClosure(*closurePtr, {a, b});
            } else if (std::holds_alternative<std::string>(args[1].data)) {
                if (args.size() < 3 || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[2].data)) {
                    throw LpcRuntimeError("sort_array: string function name requires an object third argument");
                }
                const std::string& funcName = std::get<std::string>(args[1].data);
                auto target = std::get<std::shared_ptr<LpcObject>>(args[2].data);
                // Origin::Efun -- real f_sort_array() (array.c) also
                // dispatches its comparator via the same generic
                // process_efun_callback()/call_efun_callback()
                // mechanism map_array's own comment above cites,
                // confirmed directly (array.c's own f_sort_array()).
                cmp = vm.callFunction(target, funcName, {a, b}, Origin::Efun);
            } else {
                throw LpcRuntimeError("sort_array: expected a string or function second argument");
            }
            if (!std::holds_alternative<int64_t>(cmp.data)) {
                throw LpcRuntimeError("sort_array: comparator must return an int");
            }
            return std::get<int64_t>(cmp.data);
        };
        std::stable_sort(result->items.begin(), result->items.end(),
                          [&](const Value& a, const Value& b) { return compare(a, b) < 0; });
        return Value(result);
    });

    // mixed *unique_array(mixed *arr, string | function classifier,
    // void|mixed skip) -- confirmed against array.c's own
    // f_unique_array(): each element is passed to the classifier (a
    // Closure called directly, or a string function name applied on
    // the element itself when the element is an object, matching real
    // "apply(func, v->item[i].u.ob, 0, ORIGIN_EFUN)" exactly -- unlike
    // map_array/filter_array/sort_array above, the function is called
    // ON the element, not on a separately supplied target object, and a
    // non-object element with a string classifier is simply excluded,
    // matching real "else sv = 0"). Elements whose classifier result
    // equals skip (default int 0) are excluded; the rest are grouped by
    // equal classifier result into sub-arrays, each keeping its
    // elements' original relative order. Real group order comes from
    // array.c's own reverse-linked-list bucket construction and is not
    // documented as contractual anywhere; this implementation orders
    // groups by first-appearance instead (a different but equally
    // valid deterministic order), flagged here rather than silently
    // assumed to match. Zero real call sites in this mudlib (confirmed
    // by grep; the only hit is the doc page), implemented anyway per
    // this row's own Tier 1 list.
    t.registerEfun("unique_array", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::shared_ptr<Array>>(args[0].data)) {
            throw LpcRuntimeError("unique_array: expected an array first argument");
        }
        auto arr = std::get<std::shared_ptr<Array>>(args[0].data);
        Value skip = args.size() > 2 ? args[2] : Value(int64_t{0});
        auto result = std::make_shared<Array>();
        if (!arr || arr->items.empty()) return Value(result);

        auto* closurePtr = std::get_if<std::shared_ptr<Closure>>(&args[1].data);
        const std::string* funcName = std::get_if<std::string>(&args[1].data);
        if (!closurePtr && !funcName) {
            throw LpcRuntimeError("unique_array: expected a string or function second argument");
        }

        std::vector<Value> keys;
        std::vector<std::shared_ptr<Array>> groups;
        for (const auto& item : arr->items) {
            Value key;
            if (closurePtr) {
                if (!*closurePtr) continue;
                key = vm.callClosure(*closurePtr, {item});
            } else {
                auto* obPtr = std::get_if<std::shared_ptr<LpcObject>>(&item.data);
                if (!obPtr || !*obPtr) continue;
                // Origin::Efun -- real f_unique_array() (array.c) calls
                // this exact classifier-on-the-element-itself shape via
                // "apply(func, v->item[i].u.ob, 0, ORIGIN_EFUN)",
                // confirmed directly.
                key = vm.callFunction(*obPtr, *funcName, {}, Origin::Efun);
            }
            if (valuesEqual(key, skip)) continue;

            size_t groupIdx = keys.size();
            for (size_t i = 0; i < keys.size(); ++i) {
                if (valuesEqual(keys[i], key)) { groupIdx = i; break; }
            }
            if (groupIdx == keys.size()) {
                keys.push_back(key);
                groups.push_back(std::make_shared<Array>());
            }
            groups[groupIdx]->items.push_back(item);
        }
        for (auto& g : groups) result->items.emplace_back(g);
        return Value(result);
    });

    // mapping unique_mapping(mixed *arr, string | function fun, mixed
    // extra_args...) -- confirmed against mapping.c's own
    // f_unique_mapping(): groups arr's elements by fun(element,
    // extra_args...)'s result into `([ key: ({ matching elements }) ])`,
    // same shape and same callback conventions unique_array/filter_array
    // above already establish (a Closure called directly, or a string
    // function name requiring an explicit object third argument -- this
    // driver's own established simplification of real
    // process_efun_callback()'s "defaults to current_object" convenience
    // form, matching filter_array's own scoping exactly, confirmed the
    // only shape any of this row's six corpora actually use: dead-souls'
    // own verbs/items/{get,wield,unwield}.c call sites all pass a bare
    // closure, no string form observed anywhere). Real group order comes
    // from mapping.c's own reverse-linked-list hash bucket construction
    // (traced directly: elements are walked backward through the array
    // during bucketing, so each group's own indices end up collected
    // highest-original-index first, and are then re-emitted in that same
    // reversed order) and is not documented as contractual anywhere --
    // same non-issue unique_array's own comment above already settles
    // for the identical real ambiguity: this implementation orders both
    // the mapping's own keys and each group's elements by first
    // appearance instead, a different but equally valid deterministic
    // order, flagged here rather than silently assumed to match.
    t.registerEfun("unique_mapping", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::shared_ptr<Array>>(args[0].data)) {
            throw LpcRuntimeError("unique_mapping: expected an array first argument");
        }
        auto arr = std::get<std::shared_ptr<Array>>(args[0].data);
        auto result = std::make_shared<Mapping>();
        if (!arr || arr->items.empty()) return Value(result);

        auto* closurePtr = std::get_if<std::shared_ptr<Closure>>(&args[1].data);
        const std::string* funcName = std::get_if<std::string>(&args[1].data);
        if (!closurePtr && !funcName) {
            throw LpcRuntimeError("unique_mapping: expected a string or function second argument");
        }
        std::shared_ptr<LpcObject> target;
        size_t extraStart = 2;
        if (funcName) {
            if (args.size() < 3 || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[2].data)) {
                throw LpcRuntimeError("unique_mapping: string function name requires an object third argument");
            }
            target = std::get<std::shared_ptr<LpcObject>>(args[2].data);
            extraStart = 3;
        }
        std::vector<Value> extra(args.begin() + static_cast<long>(extraStart), args.end());

        std::vector<std::shared_ptr<Array>> groups;
        for (const auto& item : arr->items) {
            std::vector<Value> callArgs;
            callArgs.reserve(1 + extra.size());
            callArgs.push_back(item);
            callArgs.insert(callArgs.end(), extra.begin(), extra.end());
            // Origin::Efun -- real f_unique_mapping() (mapping.c) also
            // dispatches via the same generic process_efun_callback()/
            // call_efun_callback() mechanism map_array's own comment
            // above cites.
            Value key = closurePtr ? vm.callClosure(*closurePtr, std::move(callArgs))
                                    : vm.callFunction(target, *funcName, std::move(callArgs), Origin::Efun);

            size_t entryIdx = result->entries.size();
            for (size_t i = 0; i < result->entries.size(); ++i) {
                if (valuesEqual(result->entries[i].first, key)) { entryIdx = i; break; }
            }
            if (entryIdx == result->entries.size()) {
                result->entries.emplace_back(key, Value(std::make_shared<Array>()));
                groups.push_back(std::get<std::shared_ptr<Array>>(result->entries.back().second.data));
            }
            groups[entryIdx]->items.push_back(item);
        }
        return Value(result);
    });

    // string *explode(string, string) -- confirmed against
    // fluffos-2.9-ds2.08's own array.c explode_string(), and against
    // this exact vendored reference's own options.h (neither
    // REVERSIBLE_EXPLODE_STRING nor SANE_EXPLODE_STRING defined, the
    // default build any of this mudlib's own content was written
    // against): every LEADING occurrence of the separator is stripped
    // before splitting (repeatedly, not just one -- SANE_EXPLODE_STRING
    // is what would limit it to one), and the final chunk is only kept
    // if non-empty, so a string ending in the separator never produces
    // a trailing "" element either. This driver's original
    // implementation did a naive split with neither behavior. Found
    // live root-causing why secure/SimulEfun/security.c's own
    // file_privs() never matched any of its switch(path[0]) cases for a
    // real object path ("/domains/..." exploded on "/" produced a
    // leading "" as path[0] instead of "domains", shifting every real
    // segment one index late) -- this is what actually blocked every
    // object's compile-time privs from ever being assigned (see
    // ObjectManager::initPrivsForObject()), which in turn is what made
    // secure/SimulEfun/log_file.c's own "explode(query_privs(...), \":\")"
    // throw for any object reached through it. The trailing-empty-
    // element half of this same bug was worked around locally in
    // daemon/race.c (LIMB_DIR file reading) before this root cause was
    // found; that guard is left in place as a harmless, independently
    // reasonable defensive check (matching this mudlib's own
    // database_filter() convention, per its own comment) rather than
    // reverted.
    t.registerEfun("explode", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("explode: expected (string, string) arguments");
        }
        const std::string& str = std::get<std::string>(args[0].data);
        const std::string& sep = std::get<std::string>(args[1].data);

        auto result = std::make_shared<Array>();
        if (sep.empty()) {
            result->items.emplace_back(str);
            return Value(result);
        }

        size_t begin = 0;
        while (str.compare(begin, sep.size(), sep) == 0) {
            begin += sep.size();
            if (begin >= str.size()) {
                return Value(result); // all separators, matches the_null_array
            }
        }

        size_t start = begin;
        for (;;) {
            size_t pos = str.find(sep, start);
            if (pos == std::string::npos) {
                if (start < str.size()) result->items.emplace_back(str.substr(start));
                break;
            }
            result->items.emplace_back(str.substr(start, pos - start));
            start = pos + sep.size();
        }
        return Value(result);
    });

    // string *explode_reversible(string str, string delimiter) --
    // current FluffOS's own real, genuinely new-since-2.9 efun
    // (confirmed absent from temp/reference/fluffos-2.9-ds2.08 entirely:
    // no explode_reversible anywhere in that tree). Signature confirmed
    // directly against real current source: src/packages/core/core.spec's
    // own "string *explode_reversible(string, string);". Real
    // f_explode_reversible() (src/packages/core/string.cc) calls the
    // same explode_string() explode() itself uses, but with its
    // "reversible" flag forced true rather than reading the
    // RC_REVERSIBLE_EXPLODE_STRING config that gates explode()'s own
    // optional lossless mode -- unlike plain explode() above (which
    // matches this driver's own already-verified default, config-off
    // FluffOS behavior: drop a leading delimiter run and a trailing
    // empty field), this never drops anything: every delimiter
    // occurrence produces a split point, including leading/trailing/
    // adjacent ones, so implode(explode_reversible(str, delim), delim)
    // == str always holds for a non-empty delim (real doc's own stated
    // guarantee, confirmed live below and in the regression tests, not
    // just assumed) -- confirmed this holds even when str is made
    // entirely of delim (real explode_string()'s own "issue #968"
    // comment needed a special case for exactly that; a plain
    // split-at-every-occurrence algorithm produces the correct n+1
    // empty fields for n delimiter occurrences by construction, with no
    // special-casing needed here). Real doc leaves an empty delimiter's
    // own behavior in reversible mode unstated (real explode_string()
    // takes a structurally different, UTF-8-grapheme-splitting path
    // there this driver has no equivalent machinery for elsewhere) --
    // throws instead, matching this codebase's own established
    // precedent of throwing rather than silently mishandling an
    // unsupported shape (replace_string()'s own comment, sscanf's
    // %f/%x handling).
    t.registerEfun("explode_reversible", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("explode_reversible: expected (string, string) arguments");
        }
        const std::string& str = std::get<std::string>(args[0].data);
        const std::string& sep = std::get<std::string>(args[1].data);
        if (sep.empty()) {
            throw LpcRuntimeError("explode_reversible: delimiter must be non-empty");
        }

        auto result = std::make_shared<Array>();
        size_t start = 0;
        for (;;) {
            size_t pos = str.find(sep, start);
            if (pos == std::string::npos) {
                result->items.emplace_back(str.substr(start));
                break;
            }
            result->items.emplace_back(str.substr(start, pos - start));
            start = pos + sep.size();
        }
        return Value(result);
    });

    // mixed implode(mixed *arr, string | function sep, void | mixed) --
    // real func_spec.c:76, efun_defs.c:99 (2..3 args; 2nd arg
    // T_STRING|T_FUNCTION). Real efuns_main.c f_implode() dispatches on
    // the 2nd argument's type:
    //   - string  -> join form, real array.c:395 implode_string(): only
    //     T_STRING elements contribute, the separator goes between
    //     consecutive string elements only, non-string elements are
    //     skipped (not stringified, not an error), zero string elements
    //     yields "". A 3rd argument here is an error ("Third argument to
    //     implode() is illegal with implode(array, string)").
    //   - function -> left-fold form, real array.c:428 implode_array():
    //     with a seed (3 args) acc = seed, then acc = f(acc, elem) for
    //     every element (empty array returns the seed unchanged); without
    //     a seed (2 args) an empty array returns int 0, a one-element
    //     array returns that element unchanged (f is not called),
    //     otherwise acc = arr[0] then acc = f(acc, arr[i]) for i in
    //     1..n-1. f is called with exactly two arguments each step.
    // Semantics pinned by temp/lil/single/tests/efuns/implode.c's own 9
    // assertions. check_for_destr(arr) (real drops destructed object refs
    // to 0 before folding) is not reproduced: no corpus call site relies
    // on it and the prior body did not do it either.
    t.registerEfun("implode", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args.size() > 3 ||
            !std::holds_alternative<std::shared_ptr<Array>>(args[0].data)) {
            throw LpcRuntimeError("implode: expected (array, string | function"
                                   "[, start]) arguments");
        }
        auto arr = std::get<std::shared_ptr<Array>>(args[0].data);

        if (auto* sepPtr = std::get_if<std::string>(&args[1].data)) {
            if (args.size() == 3) {
                throw LpcRuntimeError("implode: third argument is illegal with "
                                       "implode(array, string)");
            }
            const std::string& sep = *sepPtr;
            std::string result;
            bool any = false;
            if (arr) {
                for (const auto& item : arr->items) {
                    if (auto* s = std::get_if<std::string>(&item.data)) {
                        if (any) result += sep;
                        result += *s;
                        any = true;
                    }
                }
            }
            return Value(result);
        }

        if (auto* closurePtr = std::get_if<std::shared_ptr<Closure>>(&args[1].data)) {
            auto& closure = *closurePtr;
            const size_t n = arr ? arr->items.size() : 0;
            if (args.size() == 3) {
                Value acc = args[2];
                for (size_t i = 0; i < n; ++i) {
                    if (!closure) return Value(int64_t{0});
                    acc = vm.callClosure(closure, {acc, arr->items[i]});
                }
                return acc;
            }
            if (n == 0) return Value(int64_t{0});
            Value acc = arr->items[0];
            for (size_t i = 1; i < n; ++i) {
                if (!closure) return Value(int64_t{0});
                acc = vm.callClosure(closure, {acc, arr->items[i]});
            }
            return acc;
        }

        throw LpcRuntimeError("implode: second argument must be a string or a function");
    });

    // string repeat_string(string, int) -- func_spec.c/efun_defs.c
    // declare it (F_REPEAT_STRING), and real fluffos-2.9-ds2.08's own
    // f_repeat_string() (packages/contrib.c) confirms the semantics:
    // the string concatenated with itself "repeat" times; repeat <= 0
    // yields "". Found live needing this: cmds/mortal/_score.c's own
    // panel_border(), called while finish_creation() auto-displays the
    // score sheet for a freshly created character -- caught by setter.c's
    // own catch() around finish_creation() rather than fatal, but still
    // a real gap (the score panel silently never rendered).
    t.registerEfun("repeat_string", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<int64_t>(args[1].data)) {
            throw LpcRuntimeError("repeat_string: expected (string, int) arguments");
        }
        const std::string& str = std::get<std::string>(args[0].data);
        int64_t repeat = std::get<int64_t>(args[1].data);
        if (repeat <= 0 || str.empty()) return Value(std::string());
        std::string result;
        result.reserve(str.size() * static_cast<size_t>(repeat));
        for (int64_t i = 0; i < repeat; ++i) result += str;
        return Value(result);
    });

    t.registerEfun("keys", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<Mapping>>(args[0].data)) {
            throw LpcRuntimeError("keys: expected a mapping argument");
        }
        auto map = std::get<std::shared_ptr<Mapping>>(args[0].data);
        auto result = std::make_shared<Array>();
        if (map) {
            for (const auto& entry : map->entries) {
                result->items.push_back(entry.first);
            }
        }
        return Value(result);
    });

    // mixed *values(mapping) -- keys()'s own value-side counterpart,
    // same entry order.
    t.registerEfun("values", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<Mapping>>(args[0].data)) {
            throw LpcRuntimeError("values: expected a mapping argument");
        }
        auto map = std::get<std::shared_ptr<Mapping>>(args[0].data);
        auto result = std::make_shared<Array>();
        if (map) {
            for (const auto& entry : map->entries) {
                result->items.push_back(entry.second);
            }
        }
        return Value(result);
    });

    // ROADMAP.md row 1.9's own first real slice: real LDMud's own names
    // for keys()/values() (`temp/ldmud/src/func_spec:479`: "mixed
    // *m_indices(mapping);", one argument, no width concept at all --
    // confirmed real LDMud does not even have a plain FluffOS-style
    // "keys" efun, grepped `temp/reference/fluffos-2.9-ds2.08` for
    // "m_indices"/"m_values" too, zero hits either direction: FluffOS has
    // no LDMud-style m_-prefixed mapping efuns at all, LDMud has no plain
    // keys()/values(). The first N-column slice (width-2 literals,
    // map[key, n], m_values(map, col) against a real matching width) is
    // now also real; remaining row 1.9 scope is `m_allocate`/`m_entry`/
    // `m_reallocate`/`m_add`/`m_contains`, `([: N ])`, and mapping range
    // index. `m_indices` alone has 544 real call sites in `temp/core-lib`
    // (the one confirmed genuinely LDMud-targeting corpus this repo has),
    // `m_values` has 17. No dialect gate on *availability* here, matching
    // this table's own already-established convention (see `unshadow()`'s
    // own comment on this exact point) -- registered unconditionally like
    // every other efun, not withheld under `dialect: fluffos`/`dgd`.
    t.registerEfun("m_indices", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<Mapping>>(args[0].data)) {
            throw LpcRuntimeError("m_indices: expected a mapping argument");
        }
        auto map = std::get<std::shared_ptr<Mapping>>(args[0].data);
        auto result = std::make_shared<Array>();
        if (map) {
            for (const auto& entry : map->entries) {
                result->items.push_back(entry.first);
            }
        }
        return Value(result);
    });

    // mixed *m_values(mapping, int width_col default: 0) -- real LDMud
    // signature (func_spec:481: "mixed *m_values(mapping, int default:
    // F_CONST0);"). Column 0 is the ordinary single-value mapping; a
    // non-zero column is real as of row 1.9's first width slice, against
    // mappings whose own width actually has that column (real
    // f_m_values() mapping.c:3188-3190 errors if the column is out of
    // range -- the man page's "else the values of the first column"
    // fallback does not match the C; the C is authoritative). Confirmed
    // real call sites: 15 of 17 in temp/core-lib are the bare /
    // explicit-0 form; the other 2 are both m_values(wall, 1) in
    // rune-wall.c against a real width-2 mapping literal.
    t.registerEfun("m_values", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<Mapping>>(args[0].data)) {
            throw LpcRuntimeError("m_values: expected a mapping argument");
        }
        int64_t col = 0;
        if (args.size() > 1) {
            if (!std::holds_alternative<int64_t>(args[1].data)) {
                throw LpcRuntimeError("m_values: width column argument must be an int");
            }
            col = std::get<int64_t>(args[1].data);
        }
        auto map = std::get<std::shared_ptr<Mapping>>(args[0].data);
        int width = map ? map->width : 1;
        if (col < 0 || col >= width) {
            throw LpcRuntimeError(
                "Illegal index " + std::to_string(col) +
                " to m_values(): should be in 0.." + std::to_string(width - 1) + ".");
        }
        auto result = std::make_shared<Array>();
        if (map) {
            for (size_t i = 0; i < map->entries.size(); ++i) {
                result->items.push_back(map->getColumn(i, static_cast<int>(col)));
            }
        }
        return Value(result);
    });

    // void map_delete(mapping, mixed key) -- func_spec.c's primary
    // declared form ("void map_delete(mapping, mixed);", the other two
    // overloads there are compat-only, see line 155/156/160 in the
    // reference source). Real efuns_main.c's own f_map_delete() calls
    // mapping_delete() in place and returns nothing -- this efun
    // mutates the mapping argument itself, matching that (not a copy,
    // same as m_indices()/sizeof() already treat a mapping by shared
    // reference elsewhere in this driver). Missing key is a silent
    // no-op, matching mapping_delete()'s own "not found" branch. Found
    // live needing this: domains/Praxis/setter.c's own alignment_cmd()
    // -> remove_env(), which calls it directly (not wrapped in a
    // catch()), so this one was fatal to the connection rather than
    // silently swallowed -- it is what actually stopped the STEP 4
    // alignment pick from ever reaching STEP 5.
    // "m_delete" -- the other spelling func_spec.c declares for the same
    // function ("mapping m_delete map_delete(mapping, mixed);"). Checked
    // against real usage before adding: a raw grep for "m_delete(" only
    // ever matches as a substring of "confirm_delete(" in this mudlib
    // (secure/std/post.c) -- zero genuine call sites -- but the alias
    // costs nothing once map_delete's own real, heavily-used
    // implementation already exists, and func_spec.c declares it as a
    // real second name, so it is registered anyway.
    auto mapDeleteImpl = [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<Mapping>>(args[0].data)) {
            throw LpcRuntimeError("map_delete: expected a mapping argument");
        }
        if (args.size() < 2) {
            throw LpcRuntimeError("map_delete: expected a key argument");
        }
        auto map = std::get<std::shared_ptr<Mapping>>(args[0].data);
        if (map) {
            const Value& key = args[1];
            for (size_t i = 0; i < map->entries.size();) {
                if (valuesEqual(map->entries[i].first, key)) map->eraseAt(i);
                else ++i;
            }
        }
        return Value{};
    };
    t.registerEfun("map_delete", mapDeleteImpl);
    t.registerEfun("m_delete", mapDeleteImpl);

    // mixed match_path(mapping paths, string str) -- real efuns_main.c's
    // f_match_path() traced instruction by instruction before
    // implementing, not guessed at (its own source comment warns "DO NOT
    // CHANGE THIS EFUN TIL YOU UNDERSTAND IT" for a reason): walks str
    // left to right, splitting on '/' (collapsing repeats, matching the
    // real "while (*++src=='/');" skip), and after every slash boundary
    // looks up the prefix accumulated SO FAR in the mapping; each hit
    // overwrites the previous one, so the final result is whichever
    // matching prefix was the DEEPEST (most specific), not the first or
    // the exact full string -- "implements the search loop in TMI's
    // access object as a single efun", per the real source's own
    // comment. Returns 0 (real const0u) if no prefix ever matched.
    t.registerEfun("match_path", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::shared_ptr<Mapping>>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("match_path: expected (mapping, string)");
        }
        auto map = std::get<std::shared_ptr<Mapping>>(args[0].data);
        const std::string& str = std::get<std::string>(args[1].data);

        auto lookup = [&](const std::string& prefix) -> const Value* {
            if (!map) return nullptr;
            for (const auto& entry : map->entries) {
                if (std::holds_alternative<std::string>(entry.first.data) &&
                    std::get<std::string>(entry.first.data) == prefix) {
                    return &entry.second;
                }
            }
            return nullptr;
        };

        Value value(int64_t{0});
        std::string built;
        size_t i = 0;
        while (i < str.size()) {
            while (i < str.size() && str[i] != '/') built.push_back(str[i++]);
            if (i < str.size() && str[i] == '/') {
                ++i;
                while (i < str.size() && str[i] == '/') ++i;
                if (i < str.size() || built.empty()) built.push_back('/');
            }
            if (const Value* found = lookup(built)) {
                value = *found;
            }
        }
        return value;
    });

    // string read_file(string file, void|int start, void|int numLines).
    // Real signature and behavior confirmed against the FluffOS
    // reference driver's file.c: returns 0 (not an error) if the file
    // does not exist, start defaults to 1 (first line) and clamps up to
    // 1, and numLines defaults to "the rest of the file"; a negative
    // numLines also returns 0.
    t.registerEfun("read_file", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("read_file: expected a string filename argument");
        }
        int64_t start = 1;
        if (args.size() > 1 && std::holds_alternative<int64_t>(args[1].data)) {
            start = std::get<int64_t>(args[1].data);
        }
        if (start < 1) start = 1;

        int64_t numLines = 0; // 0 means "rest of the file"
        if (args.size() > 2 && std::holds_alternative<int64_t>(args[2].data)) {
            numLines = std::get<int64_t>(args[2].data);
        }
        if (numLines < 0) return Value(int64_t{0});

        auto gated = checkValidPath(vm, std::get<std::string>(args[0].data), false, "read_file");
        if (!gated) return Value(int64_t{0});
        std::string path = vm.resolveMudlibPath(*gated);
        struct stat st;
        if (::stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) {
            return Value(int64_t{0});
        }
        std::ifstream f(path);
        if (!f) return Value(int64_t{0});

        // Whole-file read is the common case (every call in this mudlib's
        // early-boot files uses it) and is exactly what a plain slurp
        // already gives, so it skips the line-splitting path entirely.
        if (args.size() <= 1) {
            std::ostringstream buf;
            buf << f.rdbuf();
            return Value(buf.str());
        }

        std::ostringstream result;
        std::string line;
        int64_t lineNo = 0;
        int64_t linesTaken = 0;
        while (std::getline(f, line)) {
            ++lineNo;
            if (lineNo < start) continue;
            if (numLines > 0 && linesTaken >= numLines) break;
            result << line << "\n";
            ++linesTaken;
        }
        return Value(result.str());
    });

    // int write_file(string file, string content, void|int flags).
    // Appends content to the file, creating it if it does not exist
    // (real FluffOS default; flags == 1 truncates first, per file.c) --
    // matching real semantics rather than always-truncate keeps repeated
    // log-style writes (this mudlib's own dominant use, e.g.
    // "write_file(DIR_LOGS+\"/crashes\", ...)") from clobbering earlier
    // entries.
    t.registerEfun("write_file", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("write_file: expected (string file, string content) arguments");
        }
        bool truncate = args.size() > 2 && std::holds_alternative<int64_t>(args[2].data) &&
                        std::get<int64_t>(args[2].data) == 1;

        auto gated = checkValidPath(vm, std::get<std::string>(args[0].data), true, "write_file");
        if (!gated) return Value(int64_t{0});
        std::string path = vm.resolveMudlibPath(*gated);
        std::ofstream f(path, truncate ? std::ios::trunc : std::ios::app);
        if (!f) return Value(int64_t{0});
        f << std::get<std::string>(args[1].data);
        return Value(int64_t{1});
    });

    t.registerEfun("call_other", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2) {
            throw LpcRuntimeError("call_other: requires (target, function_name, ...) arguments");
        }
        if (!std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("call_other: second argument must be a string function name");
        }
        const std::string& functionName = std::get<std::string>(args[1].data);
        std::vector<Value> forwardedArgs(args.begin() + 2, args.end());

        // Real f__call_other()'s own array-of-targets form (efuns_main.c:
        // "else if (arg[0].type == T_ARRAY) { ret = call_all_other(v,
        // funcname, num_arg - 2); ... }", confirmed directly against the
        // real interpreter-level call_all_other() (interpret.c) before
        // writing any of this, not guessed from func_spec.c's own
        // signature ("object | string | object *") alone. Real corpus
        // evidence this is genuinely needed, not a speculative
        // generalization: the "array_var->method(args)" idiom is real
        // and widespread across every vendored corpus this project
        // tracks (dead-souls, lima, es2_mudlib, nightmare3, this
        // project's own bundled mudlib, and AetherMUD's own std/room/
        // exits.c reinitiate(): "obs->move(ROOM_VOID);
        // obs->move(this_object());" where obs is all_inventory()'s own
        // real object* return -- the exact real call site row 3.9's
        // broader pass found erroring here).
        if (auto* targetArray = std::get_if<std::shared_ptr<Array>>(&args[0].data)) {
            if (!*targetArray) {
                throw LpcRuntimeError("call_other: target array is null");
            }
            // Real call_all_other() (interpret.c): allocates a result
            // array of the *same size* as the input, index-aligned (one
            // slot per input element, same order: "for (vptr = v->item,
            // rptr = ret->item; size--; vptr++, rptr++)") -- not a
            // "drop the skipped entries" shape. Each slot starts as real
            // allocate_array()'s own plain "const0" (array.c: "while
            // (n--) p->item[n] = const0;"), an ordinary, subtype-less
            // int 0 -- distinct from the T_UNDEFINED-tagged "const0u" a
            // missing mapping key returns (row 3.9's own prior sprintf
            // fix) -- overwritten only when apply_low() actually
            // succeeds for that element. Real corpus evidence this
            // default value specifically matters, not just a
            // theoretical default: lima/lib/secure/simul_efun/
            // userfuncs.c's own "users()->query_body() - ({0})"
            // explicitly filters those literal-0 non-hits back out,
            // confirming real mudlib code relies on exactly this
            // fallback value.
            auto result = std::make_shared<Array>();
            result->items.reserve((*targetArray)->items.size());
            for (const Value& element : (*targetArray)->items) {
                std::shared_ptr<LpcObject> elementTarget;
                if (auto* ob = std::get_if<std::shared_ptr<LpcObject>>(&element.data)) {
                    elementTarget = *ob;
                } else if (auto* path = std::get_if<std::string>(&element.data)) {
                    // Real call_all_other()'s own T_STRING branch: "ob =
                    // find_object(vptr->u.string); if (!ob ||
                    // !object_visible(ob)) continue;" -- resolved the
                    // same way this efun's own existing scalar-target
                    // form already does (vm.findObject()), but a miss
                    // here skips only this one element rather than
                    // throwing, matching real call_all_other()'s own
                    // per-element "continue", not the scalar form's own
                    // "abort the whole call" behavior just below.
                    elementTarget = vm.findObject(*path);
                }
                // Real call_all_other(): any other element type (int,
                // mapping, etc.) falls through its own "else continue;"
                // -- matched here by elementTarget staying null, which
                // the shared path below already treats as "skip, leave
                // the default 0".
                //
                // vm.callFunction() already implements every one of
                // real call_all_other()'s own remaining per-element
                // rules for free: a null/destructed target (real "if
                // (ob->flags & O_DESTRUCTED) continue;") and a target
                // with no such function defined (real "if
                // (apply_low(...))" false) both return this driver's
                // own default Value{} rather than throwing -- see
                // VM::callFunction()'s own header comment, and neither
                // aborts the surrounding loop, matching real semantics
                // exactly: one bad array element never aborts the whole
                // call. Real code's own "skip, leave the default"
                // outcome and real code's own "function found and ran
                // but is void" outcome are observably identical on real
                // FluffOS too (a void function's own real bytecode
                // still pushes a real int 0 before returning), so
                // collapsing this driver's own Value{} into a real
                // int64_t 0 for every result slot -- not just the
                // genuinely-skipped ones -- is the faithful choice, not
                // an approximation.
                Value callResult = elementTarget
                    ? vm.callFunction(elementTarget, functionName, forwardedArgs, Origin::CallOther)
                    : Value{};
                if (std::holds_alternative<std::monostate>(callResult.data)) {
                    result->items.emplace_back(Value(int64_t{0}));
                } else {
                    result->items.push_back(std::move(callResult));
                }
            }
            return Value(result);
        }

        std::shared_ptr<LpcObject> target;
        if (std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            target = std::get<std::shared_ptr<LpcObject>>(args[0].data);
            if (!target) {
                throw LpcRuntimeError("call_other: target object is null (destructed?)");
            }
        } else if (std::holds_alternative<std::string>(args[0].data)) {
            // real FluffOS's f__call_other() (efuns_main.c): a string
            // target is resolved with find_object() -- "ob =
            // find_object(arg[0].u.string); if (!ob ...) error(\"call_
            // other() couldn't find object\n\")". find_object() itself
            // (simulate.c) compiles and loads the file on a cache miss
            // rather than only ever looking one up, which is exactly
            // what VM::findObject() wraps ObjectManager::loadObject()
            // to provide -- see both of their own comments. A path that
            // fails to compile, or whose create() throws, still surfaces
            // as this same "couldn't find object" error either way,
            // matching real find_object() returning 0 for either reason.
            target = vm.findObject(std::get<std::string>(args[0].data));
            if (!target) {
                throw LpcRuntimeError("call_other() couldn't find object");
            }
        } else {
            throw LpcRuntimeError("call_other: first argument must be an object or a string path");
        }

        // Real f__call_other()'s own "call_origin = ORIGIN_CALL_OTHER;"
        // right before apply_low() (efuns_main.c), confirmed directly --
        // this is the one real call site that actually sets
        // ORIGIN_CALL_OTHER anywhere in the reference source.
        return vm.callFunction(target, functionName, std::move(forwardedArgs), Origin::CallOther);
    });

    // object master() -- func_spec.c: "object master();". Just the
    // already-loaded master object; no different from what
    // VM::applyMaster() already dispatches against.
    t.registerEfun("master", [](VM& vm, std::vector<Value>&) -> Value {
        auto master = vm.masterObject();
        if (!master) return Value{};
        return Value(master);
    });

    // void receive(string) -- efuns_main.c's f_receive(): "if
    // (current_object->interactive) add_message(current_object, ...)".
    // Writes straight to whichever connection is driving the currently
    // executing call, same as this driver's own write() efun already
    // does via OutputContext -- current_object->interactive and "the
    // connection whose OutputContext is active" are the same thing here,
    // since every call into an interactive object's code (logon(),
    // input_to() dispatch, etc) happens with that connection's
    // OutputContext set for its whole duration.
    t.registerEfun("receive", [](VM& vm, std::vector<Value>& args) -> Value {
        if (!args.empty() && std::holds_alternative<std::string>(args[0].data)) {
            const std::string& s = std::get<std::string>(args[0].data);
            if (Connection* conn = OutputContext::current()) {
                deliverToConnection(vm, conn, s);
            }
        }
        return Value{};
    });

    // int input_to(string func, void|int flags, void|mixed extra_args...)
    // -- simulate.c's input_to()/f_input_to() (efuns_main.c): the flag
    // slot is only consumed when arg[1] is actually a number ("!(arg[1].
    // type == T_NUMBER)" gate in f_input_to), otherwise every argument
    // after the function name is carried over verbatim and handed back
    // as extra leading... no, *trailing* arguments to the callback
    // (simulate.c: "command_giver->interactive->carryover = x", appended
    // after the raw input line -- see comm.c's
    // call_function_interactive()). Registers against
    // OutputContext::current() (this driver's stand-in for
    // command_giver->interactive) and vm.currentObject() (real FluffOS's
    // current_object, "s->ob = current_object").
    //
    // Echo/escape flags (I_NOECHO, I_NOESC, I_SINGLE_CHAR, I_NORMAL) are
    // accepted, positionally consumed exactly like the real efun; I_NOECHO
    // (bit 0x1) is genuinely applied below via Connection::suppressEcho(),
    // the rest have no behavior attached (this driver does not negotiate
    // telnet charmode/no-escape yet).
    //
    // Real LDMud's own f_input_to() additionally gates on a privilege
    // violation, but only when the caller passes the LDMud-only
    // `INPUT_IGNORE_BANG` flag bit (real value 128,
    // `mudlib/sys/input_to.h`): "((flags & IGNORE_BANG) &&
    // !privilege_violation4(STR_INPUT_TO, svalue_object(command_giver), 0,
    // flags, sp))" (`comm.c:7315-7317`) -- real `privilege_violation4()`'s
    // own "whom && !how_str" branch (`interpret.c:8578-8621`) resolves
    // that to `master->privilege_violation("input_to", current_object,
    // command_giver, flags)`. Real FluffOS's own input_to() flag bits
    // (`I_NOECHO`=0x1, `I_NOESC`=0x2, `I_SINGLE_CHAR`=0x4, get_char only,
    // confirmed against `comm.h`) never define bit 128 at all, so gating
    // on that one bit is safe under either dialect: a FluffOS-targeting
    // caller has no reason to ever set it, and only setting it now
    // genuinely risks a denial, wired 2026-08-21 via the same shared
    // `VM::privilegeViolation()` the `bind_lambda()`/`set_driver_hook()`/
    // `call_out_info()` gates already use. A denial here mirrors the real
    // efun exactly: silently returns 0, the same as any other input_to()
    // failure, no pending handler registered.
    t.registerEfun("input_to", [](VM& vm, std::vector<Value>& args) -> Value {
        // Real simulate.c's own input_to() accepts either a function
        // name or a closure/function pointer as its first argument (the
        // same string|function shape PendingInputTo::function now
        // carries -- see Connection.hpp's own comment). Real corpus:
        // Dead Souls 3.8.2's own installer, secure/lib/connect.first.c's
        // own "input_to((: InputName :), I_NOESC);", found live
        // continuing the same Dead Souls boot-then-live-verification
        // session -- this efun previously rejected every closure-form
        // call outright, breaking new-connection logon entirely for
        // this mudlib.
        bool isClosureArg = !args.empty() &&
                             std::holds_alternative<std::shared_ptr<Closure>>(args[0].data) &&
                             std::get<std::shared_ptr<Closure>>(args[0].data);
        if (args.empty() ||
            !(std::holds_alternative<std::string>(args[0].data) || isClosureArg)) {
            throw LpcRuntimeError(
                "input_to: expected a string function name or a closure as the first argument");
        }

        // "if (!command_giver || ...) return 0" -- simulate.c.
        Connection* conn = OutputContext::current();
        if (!conn) return Value(int64_t{0});

        auto currentObj = vm.currentObject();
        if (!currentObj) return Value(int64_t{0});

        Value function = args[0];

        size_t extraStart = 1;
        int64_t flags = 0;
        if (args.size() > 1 && std::holds_alternative<int64_t>(args[1].data)) {
            flags = std::get<int64_t>(args[1].data);
            extraStart = 2;
        }
        std::vector<Value> extraArgs(args.begin() + static_cast<long>(extraStart), args.end());

        // real "(flags & IGNORE_BANG) && !privilege_violation4(...)"
        // (comm.c:7316-7317) -- see this efun's own registration comment
        // above for the full citation and dialect reasoning.
        if (flags & 128) {
            if (!vm.privilegeViolation("input_to", {Value(conn->boundObject()), Value(flags)})) {
                return Value(int64_t{0});
            }
        }

        conn->setPendingInputTo(currentObj, std::move(function), std::move(extraArgs));
        // Phase 0.8: real set_call()'s own "if (flags & I_NOECHO)
        // add_binary_message(ob, telnet_yes_echo, ...)" (comm.c) --
        // I_NOECHO is real bit 0x1 (comm.h), confirmed directly.
        if (flags & 1) conn->suppressEcho();
        return Value(int64_t{1});
    });

    // int call_out(string|function func, int|float delay, mixed
    // extra_args...) -- registers a pending call, matching the real
    // efun's argument shape (call_out.c/efuns_main.c), including the
    // closure-instead-of-function-name-string form real call_out()
    // itself also accepts (confirmed live in this mudlib: daemon/
    // intermud.c's own "call_out((: Setup :), 2)"). Now wired all the
    // way through to a live Scheduler (see scheduler/Scheduler.cpp):
    // negative delay clamps to 0 (real new_call_out()'s own "if (delay <
    // 0) delay = 0;"), which is exactly why "call_out(fn, 0)" is this
    // mudlib's pervasive "run on the next tick" idiom (confirmed ~30
    // real call sites, e.g. every NPC's own deferred equip_gear()).
    // Returns the new entry's real, unique handle (CALLOUT_HANDLES is
    // confirmed active in this exact vendored build's options.h),
    // needed live by cmds/mortal/_trade.c's own "tid = call_out(...);
    // ...; remove_call_out(tid)".
    auto callOutImpl = [](const char* efunName, VM& vm, std::vector<Value>& args) -> Value {
        bool validTarget = !args.empty() &&
            (std::holds_alternative<std::string>(args[0].data) ||
             std::holds_alternative<std::shared_ptr<Closure>>(args[0].data));
        if (args.size() < 2 || !validTarget) {
            throw LpcRuntimeError(
                std::string(efunName) + ": expected (string|function target, int|float delay, ...) arguments");
        }
        double delaySeconds;
        if (std::holds_alternative<int64_t>(args[1].data)) {
            delaySeconds = static_cast<double>(std::get<int64_t>(args[1].data));
        } else if (std::holds_alternative<double>(args[1].data)) {
            delaySeconds = std::get<double>(args[1].data);
        } else {
            throw LpcRuntimeError(std::string(efunName) + ": second argument must be an int or float delay");
        }
        if (delaySeconds < 0) delaySeconds = 0;

        CallOutEntry entry;
        entry.args.assign(args.begin() + 2, args.end());
        entry.dueAt = std::chrono::steady_clock::now() +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(delaySeconds));
        if (auto* closurePtr = std::get_if<std::shared_ptr<Closure>>(&args[0].data)) {
            entry.closure = *closurePtr;
        } else {
            entry.target = vm.currentObject();
            entry.function = std::get<std::string>(args[0].data);
        }
        if (!vm.scheduler()) {
            throw LpcRuntimeError(std::string(efunName) + ": no scheduler attached to this VM");
        }
        return Value(vm.scheduler()->addCallOut(std::move(entry)));
    };
    t.registerEfun("call_out", [callOutImpl](VM& vm, std::vector<Value>& args) -> Value {
        return callOutImpl("call_out", vm, args);
    });

    // int call_out_walltime(string|function func, int|float delay, mixed
    // extra_args...) -- current FluffOS's own real, genuinely
    // new-since-2.9 efun (confirmed absent from
    // temp/reference/fluffos-2.9-ds2.08 entirely). Signature confirmed
    // directly against real current source: src/packages/core/core.spec's
    // own "int call_out_walltime(string | function, int|float, ...);".
    // Real doc (docs/efun/calls/call_out_walltime.md, fetched live):
    // "This efun is identical to call_out except it does not schedule
    // the call on the game loop. Rather, in real seconds." -- real
    // call_out() schedules against current_time, a coarse, once-per-
    // backend()-loop-iteration integer-seconds variable, so real
    // call_out_walltime() exists specifically to give a delay measured
    // against actual wall-clock time instead. This driver's own
    // call_out() (above) was never built with that limitation: it
    // already schedules against std::chrono::steady_clock directly,
    // confirmed by reading callOutImpl's own body just above rather
    // than assumed -- so call_out_walltime() here is a real, honest
    // alias of the exact same implementation, not a stripped-down or
    // approximated port. The real distinction call_out_walltime() exists
    // to draw in real FluffOS genuinely does not apply to this driver's
    // own architecture; named here explicitly rather than silently
    // pretending a byte-identical mechanism was ported instead of an
    // already-equivalent one.
    t.registerEfun("call_out_walltime", [callOutImpl](VM& vm, std::vector<Value>& args) -> Value {
        return callOutImpl("call_out_walltime", vm, args);
    });

    // int remove_call_out(int | void | string) -- func_spec.c's real
    // signature. Cancels a pending call_out by handle or by function
    // name (scoped to current_object() for the name form, matching real
    // remove_call_out(object_t*, const char*)'s own "(*copp)->ob == ob"
    // check -- see Scheduler::removeCallOutByName()'s own comment) and
    // returns the time remaining, or -1 if none was found. Found live
    // needing this: domains/Praxis/obj/mon/rift_survivor.c's own init()
    // (cancel-then-reschedule a repeating call_out), and std/user.c's
    // own "while(remove_call_out(\"rifts_regen_tick\") != -1);"
    // dedup-clear idiom on reconnect.
    t.registerEfun("remove_call_out", [](VM& vm, std::vector<Value>& args) -> Value {
        if (!vm.scheduler()) return Value(int64_t{-1});
        if (!args.empty() && std::holds_alternative<int64_t>(args[0].data)) {
            return Value(vm.scheduler()->removeCallOutByHandle(std::get<int64_t>(args[0].data)));
        }
        if (!args.empty() && std::holds_alternative<std::string>(args[0].data)) {
            return Value(vm.scheduler()->removeCallOutByName(
                vm.currentObject(), std::get<std::string>(args[0].data)));
        }
        return Value(int64_t{-1});
    });

    // int find_call_out(int|string) -- func_spec.c's real signature.
    // Same lookup rules as remove_call_out() above, without removing
    // anything. Found live needing this: domains/LoneStar/areas/
    // lone_star_support_row.c's own dedup-before-scheduling idiom
    // ("if(find_call_out(\"sweep_resolve\") != -1)
    // remove_call_out(\"sweep_resolve\");"), and secure/daemon/mcp_d.c's
    // own "if (find_call_out(\"write_tick\") == -1) call_out(...)".
    t.registerEfun("find_call_out", [](VM& vm, std::vector<Value>& args) -> Value {
        if (!vm.scheduler()) return Value(int64_t{-1});
        if (!args.empty() && std::holds_alternative<int64_t>(args[0].data)) {
            return Value(vm.scheduler()->findCallOutByHandle(std::get<int64_t>(args[0].data)));
        }
        if (!args.empty() && std::holds_alternative<std::string>(args[0].data)) {
            return Value(vm.scheduler()->findCallOutByName(
                vm.currentObject(), std::get<std::string>(args[0].data)));
        }
        return Value(int64_t{-1});
    });

    // mixed *call_out_info() -- real call_out.c's own get_all_call_outs():
    // one ({owner, function_name_or_string, remaining_delay}) triple per
    // still-pending entry, skipping any whose owner is gone or destructed
    // (matching real "if (!ob || (ob->flags & O_DESTRUCTED)) continue;"
    // exactly). Confirmed against the real 2 call sites in this mudlib
    // (cmds/creator/_callouts.c, domains/Praxis/sage_room.c) that this is
    // the shape they read from -- element[0]/[1]/[2]. Note found while
    // verifying: _callouts.c's own display code actually checks
    // "sizeof(element) != 4" and expects a 4th "args" element that real
    // get_all_call_outs() never produces (it is genuinely a 3-element
    // array in the real source) -- a pre-existing mismatch on the mudlib
    // side, not a driver gap; this implementation matches the real
    // 3-element driver output, not the mudlib's own inconsistent
    // expectation of it. The closure-bound form's own "stringify the
    // function pointer" (real svalue_to_string()) is approximated as the
    // closure's own bare functionName, not a byte-exact format match --
    // no real call site parses that string, both just print it.
    //
    // Real LDMud's own f_call_out_info() (call_out.c:805-829) additionally
    // gates the whole call behind a real privilege_violation() first:
    // "if (privilege_violation(STR_CALL_OUT_INFO, &const0, sp))
    // push_array(sp, get_all_call_outs()); else push_ref_array(sp,
    // &null_vector);" -- real master->privilege_violation("call_out_info",
    // current_object, 0) (privilege_violation2()'s own real 3-arg shape,
    // interpret.c:8518-8524, arg here is the constant 0, not the object
    // itself). Real FluffOS's own f_call_out_info() (efuns_main.c:292ff)
    // has no such gate at all -- FluffOS never had a privilege_violation()
    // mechanism (confirmed 2026-08-20, ROADMAP.md row 1.7/1.8's own
    // scoping investigation), so this driver's pre-existing ungated
    // behavior above already matches real FluffOS exactly and stays
    // unconditional under that dialect; only under `dialect: ldmud` is
    // the gate real, wired 2026-08-21 via the same shared
    // VM::privilegeViolation() the bind_lambda()/set_driver_hook() gates
    // already use.
    t.registerEfun("call_out_info", [](VM& vm, std::vector<Value>&) -> Value {
        if (vm.config().dialect() == "ldmud") {
            if (!vm.privilegeViolation("call_out_info", {Value(int64_t{0})})) {
                return Value(std::make_shared<Array>());
            }
        }
        auto result = std::make_shared<Array>();
        if (!vm.scheduler()) return Value(result);
        auto now = std::chrono::steady_clock::now();
        for (const auto& entry : vm.scheduler()->pendingCallOuts()) {
            std::shared_ptr<LpcObject> owner;
            std::string funcName;
            if (entry.closure) {
                owner = entry.closure->owner.lock();
                funcName = entry.closure->functionName;
            } else {
                owner = entry.target.lock();
                funcName = entry.function;
            }
            if (!owner || owner->isDestructed()) continue;
            int64_t remaining = std::chrono::duration_cast<std::chrono::seconds>(
                entry.dueAt - now).count();
            if (remaining < 0) remaining = 0;
            auto triple = std::make_shared<Array>();
            triple->items.emplace_back(owner);
            triple->items.emplace_back(funcName);
            triple->items.emplace_back(remaining);
            result->items.emplace_back(triple);
        }
        return Value(result);
    });

    // int command(string str) -- real add_action.c's own f_command():
    // executes str as a command for current_object (parse_command()
    // against current_object's own action table), returning a truthy
    // cost on success or falsy 0 on failure. This driver's own
    // VM::dispatchCommand() is exactly that mechanism, already used by
    // Server::dispatchLine() for real player input -- reused directly
    // here rather than a second, separately-approximated dispatch path.
    // Real per-command eval-cost accounting is not reproduced (this
    // driver's own accumulated eval-cost model resets per top-level
    // command dispatch, not per nested command() call); a plain 1/0
    // truthy result is returned instead, matching every one of this
    // mudlib's own 7 real bare command() call sites, which only ever
    // branch on truthiness (std/living.c's force_me(): "res =
    // command(cmd);", std/monster.c's own NPC wander/patrol AI,
    // std/user/nmsh.c's alias system: "if(!command(lines[i]))"), never
    // the exact numeric cost value.
    t.registerEfun("command", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("command: expected a string argument");
        }
        auto giver = vm.currentObject();
        if (!giver) return Value(int64_t{0});
        bool handled = vm.dispatchCommand(giver, std::get<std::string>(args[0].data));
        return Value(static_cast<int64_t>(handled ? 1 : 0));
    });

    // void shutdown(void|int exit_code) -- real efuns_main.c's
    // f_shutdown(): calls shutdownMudOS(exit_code), terminating the
    // driver process. This driver's own Scheduler::requestShutdown()
    // (a static signal the run loop checks once per iteration, per
    // scheduler/instruct.md's own Key invariants) is the equivalent
    // mechanism -- reused directly rather than calling std::exit()
    // in-line, which would skip this driver's own graceful per-
    // iteration shutdown path.
    t.registerEfun("shutdown", [](VM&, std::vector<Value>&) -> Value {
        Scheduler::requestShutdown();
        return Value{};
    });

    // string capitalize(string) -- efuns_main.c's f_capitalize(): the
    // first character uppercased if (and only if) it is currently
    // lowercase; everything else, including an already-uppercase or
    // non-alphabetic first character, is left alone.
    t.registerEfun("capitalize", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("capitalize: expected a string argument");
        }
        std::string s = std::get<std::string>(args[0].data);
        if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') {
            s[0] = static_cast<char>(s[0] - 'a' + 'A');
        }
        return Value(s);
    });

    // string pluralize(string) -- see pluralizeWord()'s own comment
    // above for the full derivation. Real 0 (not an error) on an empty
    // input string.
    t.registerEfun("pluralize", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("pluralize: expected a string argument");
        }
        auto result = pluralizeWord(std::get<std::string>(args[0].data));
        if (!result) return Value(int64_t{0});
        return Value(*result);
    });

    // string query_num(int n, int limit default: 0) -- real
    // packages/contrib_spec.c's own signature (efun_defs.c's own "2,2"
    // is the post-default-expansion arity, not the real callable
    // minimum -- confirmed by checking contrib_spec.c directly rather
    // than trusting efun_defs.c's raw numeric fields alone, the same
    // kind of default-argument-per-alias trap this row's own earlier
    // set_eval_limit() fix already documents). See queryNumWord()'s own
    // comment above for the full derivation. Genuinely real and
    // load-bearing under this exact driver: dead-souls' own
    // secure/sefun/english.c guards its own hand-rolled 70-line
    // cardinal() with "#ifndef __FLUFFOS__ ... #else return
    // sign+query_num(x); #endif" -- this driver's own compiler defines
    // __FLUFFOS__ (ObjectManager.cpp's own preprocessor macro table),
    // so that mudlib's real, deliberate "prefer the driver's own real
    // efun when available" branch is exactly the one this driver takes.
    t.registerEfun("query_num", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("query_num: expected an int argument");
        }
        int64_t n = std::get<int64_t>(args[0].data);
        int64_t limit = 0;
        if (args.size() > 1) {
            if (!std::holds_alternative<int64_t>(args[1].data)) {
                throw LpcRuntimeError("query_num: expected an int second argument");
            }
            limit = std::get<int64_t>(args[1].data);
        }
        return Value(queryNumWord(n, limit));
    });

    // string sprintf(string fmt, mixed args...) -- real FluffOS's
    // sprintf() is a large efun (field widths, padding, table columns,
    // a dozen-plus specifiers). Started from only bare "%s"/"%d",
    // positionally, with no width/precision/flags -- confirmed by grep
    // across secure/std/login.c, secure/daemon/account_d.c (a since-
    // discarded early scratch object, see this file's own sizeof()
    // comment above), and daemon/banish.c, the only shapes used on the
    // original login/
    // account-creation path this driver exercised first. Grown live
    // since, each addition confirmed against a real call site rather
    // than spun ahead speculatively: "%c" (daemon/terminal.c's own
    // ANSI(p)/ESC(p) macros), "-"/leading-zero field width (domains/
    // Praxis/setter.c's own show_rolled_attributes(), "%-3d"), and "%%"
    // plus the ":" field-size-and-precision modifier (secure/SimulEfun/
    // strings.c's own arrange_string()/center()/wrap(), the mechanism
    // this mudlib uses throughout for column-aligned list output).
    //
    // Extended (general LPC-compliance pass, not driven by a new real
    // call site this time) against fluffos-2.9-ds2.08/sprintf.c's own
    // doc comment and its field-size/precision parsing loop, read
    // directly rather than guessed: a separate "."n precision modifier
    // (distinct from ":", which ties precision to the field size --
    // "."n truncates a %s argument on its own, and widens the field to
    // match if the precision given is larger than an explicit field
    // size, matching the doc's own "if precision is greater than field
    // size, then field size = precision"), "*" in place of either a
    // literal field-size or precision digit sequence to pull that value
    // from the next argument instead (confirmed via sprintf.c's own
    // GET_NEXT_ARG-on-'*' handling: the size/precision argument is
    // consumed *before* the value argument itself, so "%*d" is called
    // as sprintf("%*d", width, value)), and the "%o"/"%x" integer
    // specifiers (plain C octal/hex via snprintf, matching sprintf.c's
    // INFO_T_OCT/INFO_T_HEX doing the equivalent in C). "%0*d" (a
    // zero-padded dynamic width) is explicitly not implemented -- rare
    // enough combination that it is not worth the added parsing
    // ambiguity with the plain "*" case, throws its own clear error
    // rather than being silently misparsed as a stray "%*" specifier.
    // "|" (centre-justify) added this slice, confirmed real-reachable:
    // secure/SimulEfun/misc.c's own dump_socket_status()
    // ("%2d  %|9s  %|8s  %-21s  %-21s\n") -- grounded directly in
    // sprintf.c's own add_justified(): when the padding does not split
    // evenly, the extra character goes on the *leading* side ("i = fs /
    // 2 + fs % 2"), not the trailing one.
    //
    // "=" (column / word-wrap mode) added for its single-column form:
    // real sprintf.c's INFO_COLS, add_column(), add_justified(). A "%=Ns"
    // (or "%=NO") specifier word-wraps its string argument to a column N
    // wide, honouring an embedded '\n', with each wrapped line joined by
    // a newline. A precision ("%.=" / "%:=" / "%=*.*") sets the wrap
    // width, clamped to the field size (real "if (pres > fs) pres = fs;
    // (pres) ? pres : fs"); a field size is required (real "if (!fs)
    // ERROR(ERR_CST_REQUIRES_FS)"). "-" left-justifies each wrapped line,
    // "|" centres it, neither right-justifies (real add_justified()'s
    // default), and trailing pad on a wrapped line is added only when
    // more non-newline format text follows the specifier (real's
    // "trailing" flag). Continuation lines are indented to the output
    // column where "%=" began, so "%s%-=*s" over "prefix" + body gives a
    // real hanging indent. The MULTI-column interleaved form (two or more
    // "%=" string specifiers in one format, the wizard file-listing
    // table idiom) is still scoped out: it needs the row-at-a-time
    // rebuild real string_print() does, throws a clear error rather than
    // mislaying the columns.
    // "%f" (float, six-place default, precision, "+"/" " sign flags,
    // field width and "-"/"|"/"0" justification via the shared block),
    // "%i" (a plain alias of "%d"), and the "+"/" " pad-prefix flags on
    // "%d"/"%i" are implemented. Still scoped, not the full real modifier
    // set: "#" (table mode), "@" (array-spread), "'X'" (custom pad
    // string), the ":"/precision combination on "%f", and capital "%X"
    // are not implemented; throws rather than silently mishandling
    // anything else, matching this codebase's existing convention for
    // other partially-implemented efuns.
    // sprintf()'s own %d/%o/%x/%c: real sprintf.c's type check for every
    // one of these (fluffos-2.9-ds2.08/sprintf.c:1180, "carg->type !=
    // T_NUMBER") tests the real svalue's *type tag*, not a separate "is
    // this genuinely assigned" bit -- and real T_UNDEFINED (lpc.h:
    // "#define T_UNDEFINED 0x4 /* undefinedp() returns true */") is a
    // *subtype* flag on an ordinary T_NUMBER svalue, confirmed directly
    // against main.c's own "const0u.subtype = T_UNDEFINED;", not a
    // distinct top-level type -- the exact same const0u a missing,
    // non-column mapping[key] lookup returns (mapping.c's own
    // find_in_mapping(): "return &const0u;"). So a missing-mapping-key
    // value passes real sprintf()'s own %d/%o/%x/%c check and prints as
    // plain 0, the same way it already passes this driver's own
    // asArithmeticOperand() (VM.cpp) for +/-/* and
    // formatNumberForConcat() for string+number concatenation -- this
    // driver's own monostate (its own T_UNDEFINED-equivalent, see those
    // two functions' own comments) needs the identical exemption here,
    // not the hard rejection every genuinely non-numeric kind (string/
    // array/mapping/object/closure) still correctly gets. Found live:
    // row 3.9's own "AetherMUD" Rifts mudlib boot, std/living.c's own
    // query_exp() ("return player_data[\"general\"][\"experience\"];")
    // on a freshly created character whose experience key has never
    // been written, reaching cmds/mortal/_score.c's own
    // show_experience() -- see ROADMAP.md row 3.9's own dated note for
    // the full trace.
    auto sprintfNumericArg = [](const Value& v, int64_t& out) -> bool {
        if (auto* i = std::get_if<int64_t>(&v.data)) {
            out = *i;
            return true;
        }
        if (std::holds_alternative<std::monostate>(v.data)) {
            out = 0;
            return true;
        }
        return false;
    };

    auto sprintfImpl = [sprintfNumericArg](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("sprintf: expected a string format argument");
        }
        const std::string& fmt = std::get<std::string>(args[0].data);
        std::string result;
        size_t argIdx = 1;

        // Count "%=" column specifiers that target a string ('s'/'O').
        // Two or more means the interleaved multi-column table form,
        // which this driver does not build (see the top comment); one
        // is the single-column word-wrap handled inline below.
        auto countColStringSpecs = [](const std::string& f) -> int {
            int n = 0;
            for (size_t k = 0; k + 1 < f.size(); ++k) {
                if (f[k] != '%') continue;
                if (f[k + 1] == '%') { ++k; continue; }
                size_t m = k + 1;
                bool eq = false;
                while (m < f.size() &&
                       (f[m] == '-' || f[m] == ':' || f[m] == '|' || f[m] == '=' ||
                        f[m] == '.' || f[m] == '*' || (f[m] >= '0' && f[m] <= '9'))) {
                    if (f[m] == '=') eq = true;
                    ++m;
                }
                if (m < f.size() && eq && (f[m] == 's' || f[m] == 'O')) ++n;
                k = m;
            }
            return n;
        };
        const int colStringSpecCount = countColStringSpecs(fmt);

        // Greedy word-wrap of `text` into segments at most `width` wide,
        // matching real sprintf.c's add_column(): scan up to `width`
        // characters or an embedded '\n'; if that lands in the middle of
        // a word and a space was seen in the window, back up to the last
        // space. A forced '\n' is consumed; otherwise the run of spaces
        // at the break point is skipped.
        auto wrapForColumn = [](const std::string& text, int width) -> std::vector<std::string> {
            std::vector<std::string> segs;
            size_t pos = 0;
            while (pos < text.size()) {
                int done = 0;
                int lastSpace = -1;
                while (pos + static_cast<size_t>(done) < text.size() &&
                       text[pos + static_cast<size_t>(done)] != '\n' &&
                       done < width) {
                    if (text[pos + static_cast<size_t>(done)] == ' ') lastSpace = done;
                    ++done;
                }
                if (done == width && lastSpace != -1 &&
                    pos + static_cast<size_t>(done) < text.size() &&
                    text[pos + static_cast<size_t>(done)] != '\n' &&
                    text[pos + static_cast<size_t>(done)] != ' ') {
                    done = lastSpace;
                }
                segs.push_back(text.substr(pos, static_cast<size_t>(done)));
                pos += static_cast<size_t>(done);
                if (pos < text.size() && text[pos] == '\n') {
                    ++pos;
                } else {
                    while (pos < text.size() && text[pos] == ' ') ++pos;
                }
            }
            if (segs.empty()) segs.emplace_back();
            return segs;
        };

        for (size_t i = 0; i < fmt.size(); ++i) {
            if (fmt[i] != '%') {
                result += fmt[i];
                continue;
            }
            if (i + 1 >= fmt.size()) {
                throw LpcRuntimeError("sprintf: trailing '%' with no specifier");
            }
            // "%%" -- real sprintf.c: "in which case no arguments are
            // interpreted, and a '%' is inserted, and all modifiers are
            // ignored." Found live: secure/SimulEfun/strings.c's own
            // arrange_string()/center()/wrap(), each building a second,
            // dynamic format string via a first sprintf() call whose own
            // format is "%%:-%ds" etc -- the literal "%%" there has to
            // resolve to a literal "%" before the *result* is used as a
            // format string in a second sprintf() call.
            if (fmt[i + 1] == '%') {
                result += '%';
                ++i;
                continue;
            }
            // Field-width modifiers, confirmed against real sprintf.c's
            // own documented modifier set (this driver implements "-",
            // a leading-zero field width, and ":" -- not the full "|"/
            // "="/"#"/"'X'"/"@"/separate-precision set, nothing on this
            // driver's live path uses those yet): an optional "-"
            // (left-adjust; real sprintf.c's own default is
            // right-justify, "unnatural in a mainly string-based
            // language but retained for compatibility"), an optional
            // ":" (real sprintf.c: "n specifies the fs _and_ the
            // precision" -- field size and precision set to the same
            // value; precision truncates a %s argument longer than the
            // field, "all other types ignore this"), in either order,
            // then an optional digit sequence giving the field size
            // ("if n is prepended with a zero, then is padded with
            // zeros, else... spaces"). Found live: domains/Praxis/
            // setter.c's own show_rolled_attributes() ("%-3d") and
            // secure/SimulEfun/strings.c's own arrange_string()
            // ("%:-Ns", built at runtime as described above -- field
            // size N, left-justified, truncated to N if longer, the
            // real mechanism this mudlib uses throughout for
            // column-aligned list output like the race/OCC lists).
            bool leftJustify = false;
            bool centreJustify = false;
            bool colonMode = false;
            bool colMode = false;
            // Pad-prefix flags, real sprintf.c INFO_PP_PLUS ('+') /
            // INFO_PP_SPACE (' '), parsed in the same modifier scan as
            // '-'/'|'/'='/':'. Real applies them only in the numeric
            // branch: '+' forces a leading sign on a non-negative number,
            // ' ' a leading space. Threaded through to %d/%i and %f here
            // (a lone ' ' or '+' right after '%' or another modifier is
            // the only place this scan can consume one, so ordinary
            // literal spaces in a format string are untouched).
            bool plusFlag = false;
            bool spaceFlag = false;
            while (i + 1 < fmt.size() &&
                   (fmt[i + 1] == '-' || fmt[i + 1] == ':' || fmt[i + 1] == '|' ||
                    fmt[i + 1] == '=' || fmt[i + 1] == '+' || fmt[i + 1] == ' ')) {
                if (fmt[i + 1] == '-') leftJustify = true;
                else if (fmt[i + 1] == '|') centreJustify = true;
                else if (fmt[i + 1] == '=') colMode = true;
                else if (fmt[i + 1] == '+') plusFlag = true;
                else if (fmt[i + 1] == ' ') spaceFlag = true;
                else colonMode = true;
                ++i;
            }
            bool zeroPad = false;
            int fieldWidth = 0;
            bool haveWidth = false;
            if (i + 1 < fmt.size() && fmt[i + 1] == '*') {
                // Dynamic field width: pulls the size from the next
                // argument instead of a literal digit sequence, consumed
                // here -- before the value argument itself -- matching
                // real sprintf.c's own GET_NEXT_ARG-on-'*' order (see this
                // efun's own top comment).
                if (argIdx >= args.size() || !std::holds_alternative<int64_t>(args[argIdx].data)) {
                    throw LpcRuntimeError("sprintf: '*' field width argument is not an int");
                }
                fieldWidth = static_cast<int>(std::get<int64_t>(args[argIdx++].data));
                if (fieldWidth < 0) fieldWidth = 0;
                haveWidth = true;
                ++i;
            } else {
                if (i + 1 < fmt.size() && fmt[i + 1] == '0') {
                    if (i + 2 < fmt.size() && fmt[i + 2] == '*') {
                        // "%0*d" -- zero-padded dynamic width. Real
                        // sprintf.c supports this combination; this driver
                        // does not, deliberately (see this efun's own top
                        // comment) -- throws its own clear error instead of
                        // falling through and misparsing the '*' as a
                        // stray, unsupported type specifier.
                        throw LpcRuntimeError(
                            "sprintf: zero-padded dynamic field width ('%0*') is not implemented");
                    }
                    zeroPad = true;
                }
                while (i + 1 < fmt.size() && fmt[i + 1] >= '0' && fmt[i + 1] <= '9') {
                    haveWidth = true;
                    fieldWidth = fieldWidth * 10 + (fmt[i + 1] - '0');
                    ++i;
                }
            }
            // "."n -- precision, distinct from ":" (which ties precision
            // to the field size). Only meaningful for %s (real sprintf.c:
            // "all other types ignore this"), parsed for every specifier
            // regardless so a stray "." on a non-%s call still consumes
            // its digits/arg correctly rather than being misread as
            // literal text or the type-specifier character itself.
            bool havePrecision = false;
            int precision = 0;
            if (i + 1 < fmt.size() && fmt[i + 1] == '.') {
                ++i;
                if (i + 1 < fmt.size() && fmt[i + 1] == '*') {
                    if (argIdx >= args.size() || !std::holds_alternative<int64_t>(args[argIdx].data)) {
                        throw LpcRuntimeError("sprintf: '.*' precision argument is not an int");
                    }
                    precision = static_cast<int>(std::get<int64_t>(args[argIdx++].data));
                    if (precision < 0) precision = 0;
                    havePrecision = true;
                    ++i;
                } else {
                    while (i + 1 < fmt.size() && fmt[i + 1] >= '0' && fmt[i + 1] <= '9') {
                        havePrecision = true;
                        precision = precision * 10 + (fmt[i + 1] - '0');
                        ++i;
                    }
                    if (!havePrecision) {
                        throw LpcRuntimeError("sprintf: expected precision digits after '.'");
                    }
                }
            }
            char spec = fmt[++i];
            if (argIdx >= args.size()) {
                throw LpcRuntimeError("sprintf: too few arguments for format string");
            }
            const Value& argVal = args[argIdx++];
            std::string piece;
            if (spec == 's') {
                if (!std::holds_alternative<std::string>(argVal.data)) {
                    throw LpcRuntimeError("sprintf: %s argument is not a string");
                }
                piece = std::get<std::string>(argVal.data);
            } else if (spec == 'd' || spec == 'i') {
                // real sprintf.c: "case 'd': case 'i': finfo |=
                // INFO_T_INT;" -- 'i' is a plain alias of 'd'. Corpus:
                // "%i"/"%3i" in FTP-daemon and status-report code across
                // several corpora. The '+'/' ' pad-prefix flags build a
                // real C "%+lld"/"% lld" here, matching real's own cheat
                // string; "%+d" alone is about 8 corpus call-site lines
                // (signed stat deltas).
                int64_t n;
                if (!sprintfNumericArg(argVal, n)) {
                    throw LpcRuntimeError("sprintf: %d argument is not an int");
                }
                if (plusFlag || spaceFlag) {
                    char sbuf[32];
                    std::snprintf(sbuf, sizeof(sbuf), plusFlag ? "%+lld" : "% lld",
                                  static_cast<long long>(n));
                    piece = sbuf;
                } else {
                    piece = std::to_string(n);
                }
            } else if (spec == 'o' || spec == 'x') {
                // sprintf.c's own INFO_T_OCT/INFO_T_HEX: the integer arg
                // printed in octal/hex, plain C conversion, no leading
                // "0"/"0x" prefix added (real sprintf.c does not add one
                // either -- confirmed by its own doc comment describing
                // these as the plain "printed in octal"/"printed in hex").
                int64_t n;
                if (!sprintfNumericArg(argVal, n)) {
                    throw LpcRuntimeError(std::string("sprintf: %") + spec + " argument is not an int");
                }
                char buf[32];
                std::snprintf(buf, sizeof(buf), spec == 'o' ? "%llo" : "%llx",
                              static_cast<long long>(n));
                piece = buf;
            } else if (spec == 'c') {
                // sprintf.c's own INFO_T_CHAR handling (fluffos-2.9-ds2.08/
                // sprintf.c line 1165 assigns 'c' into a real C
                // sprintf(..., "%c", ...) cheat-buffer format, and line
                // 1180 requires carg->type == T_NUMBER for it, an int
                // argument, not a string) -- confirmed live needed by
                // daemon/terminal.c's own ANSI(p)/ESC(p) macros:
                // sprintf("%c["+(p)+"m", 27), which builds a raw ESC
                // (ASCII 27) byte ahead of an ANSI escape sequence.
                int64_t n;
                if (!sprintfNumericArg(argVal, n)) {
                    throw LpcRuntimeError("sprintf: %c argument is not an int");
                }
                piece = std::string(1, static_cast<char>(n));
                haveWidth = false; // real sprintf.c: field width is not meaningful for %c
            } else if (spec == 'O') {
                // Real sprintf.c: accepts any value kind (no type check
                // the way %s/%d/%c/%o/%x each require one) -- svalue_to_
                // string() itself has a case for every real svalue type.
                piece = valueToDebugString(argVal, 0);
            } else if (spec == 'f') {
                // real sprintf.c INFO_T_FLOAT (line 911 sets it, line
                // 1162/1203 formats it): a C "%[+ ][.pres]f" cheat string
                // run on carg->u.real, then add_justified() applies the
                // field width afterward (so width is handled by the
                // shared field-width block below, not baked in here).
                // The argument must be a float: real errors "Incorrect
                // argument type to %f" for anything else (sprintf.c:1180,
                // cheat ends in 'f' && carg->type != T_REAL), so an int
                // is NOT silently coerced. A precision of 0, whether from
                // a bare "%f" or an explicit "%.0f", means C's default 6
                // places: real only appends ".pres" when "if (pres)" is
                // true, i.e. pres is nonzero. Corpus: about 20 files,
                // "%.2f"/"%9.2f"/"%3.1f"/"%+4.2f" for weights, money, and
                // percentages, e.g. a weight-to-string simul_efun's
                // sprintf("%.2f lbs", w).
                const double* dv = std::get_if<double>(&argVal.data);
                if (!dv) {
                    throw LpcRuntimeError("sprintf: %f argument is not a float");
                }
                std::string cfmt = "%";
                if (plusFlag) cfmt += '+';
                else if (spaceFlag) cfmt += ' ';
                if (havePrecision && precision > 0) {
                    int p = precision > 128 ? 128 : precision;
                    cfmt += '.';
                    cfmt += std::to_string(p);
                }
                cfmt += 'f';
                int need = std::snprintf(nullptr, 0, cfmt.c_str(), *dv);
                if (need < 0) {
                    throw LpcRuntimeError("sprintf: %f formatting failed");
                }
                piece.resize(static_cast<size_t>(need));
                std::snprintf(&piece[0], static_cast<size_t>(need) + 1, cfmt.c_str(), *dv);
            } else {
                throw LpcRuntimeError(
                    std::string("sprintf: unsupported format specifier '%") + spec +
                    "' (only %s, %d, %i, %f, %c, %o, %x, and %O are implemented)");
            }

            // "%=" column / word-wrap mode. Only meaningful for a
            // string-ish type ('s'/'O'); for a numeric type real
            // sprintf.c never enters its INFO_COLS branch, so the "="
            // is a silent no-op there and the normal field-width path
            // below handles it. Handled inline here for the single
            // "%=" case; the interleaved multi-column form is scoped
            // out (see the top comment).
            if (colMode && (spec == 's' || spec == 'O')) {
                if (colStringSpecCount > 1) {
                    throw LpcRuntimeError(
                        "sprintf: multi-column '%=' layout is not implemented "
                        "(only a single '%=' word-wrap column per format string)");
                }
                if (!haveWidth || fieldWidth <= 0) {
                    throw LpcRuntimeError(
                        "sprintf: '%=' column mode requires a positive field width");
                }
                // Wrap width: the precision if one was given, clamped to
                // the field width; otherwise the field width itself
                // (real: "if (pres > fs) pres = fs; (pres) ? pres : fs").
                int wrapWidth = fieldWidth;
                if (havePrecision) {
                    wrapWidth = precision < fieldWidth ? precision : fieldWidth;
                    if (wrapWidth <= 0) wrapWidth = fieldWidth;
                }
                // Output column where this "%=" begins: characters since
                // the last newline already in `result`. Continuation
                // lines are indented to match (real add_pad(0, start -
                // curpos)).
                size_t lastNl = result.find_last_of('\n');
                size_t startCol = (lastNl == std::string::npos)
                                      ? result.size()
                                      : result.size() - lastNl - 1;
                // Real's "trailing" flag for the first wrapped line: set
                // when more format text (other than a newline) follows
                // the specifier.
                bool trailingText = (i + 1 < fmt.size() && fmt[i + 1] != '\n');
                auto justifyColLine = [&](const std::string& seg, bool trailing) -> std::string {
                    int extra = fieldWidth - static_cast<int>(seg.size());
                    if (extra <= 0) return seg;
                    if (centreJustify) {
                        int lead = extra / 2 + extra % 2;  // real: leading half gets the odd one
                        std::string out(static_cast<size_t>(lead), ' ');
                        out += seg;
                        if (trailing) out.append(static_cast<size_t>(extra - lead), ' ');
                        return out;
                    }
                    if (leftJustify) {
                        std::string out = seg;
                        if (trailing) out.append(static_cast<size_t>(extra), ' ');
                        return out;
                    }
                    return std::string(static_cast<size_t>(extra), ' ') + seg;  // default: right
                };
                std::vector<std::string> segs = wrapForColumn(piece, wrapWidth);
                for (size_t s = 0; s < segs.size(); ++s) {
                    if (s == 0) {
                        result += justifyColLine(segs[0], trailingText);
                    } else {
                        result += '\n';
                        result.append(startCol, ' ');
                        result += justifyColLine(segs[s], false);
                    }
                }
                continue;
            }

            // ":" sets precision == field size, truncating a %s
            // argument longer than the field ("all other types ignore
            // this" -- real sprintf.c; %d/%c are never truncated here).
            // %O is included here too: real sprintf.c converts INFO_T_LPC
            // into INFO_T_STRING immediately after building the dump
            // string (see valueToDebugString()'s own top comment), so
            // from this point on %O is genuinely indistinguishable from
            // %s to the field-width/precision/justify code below.
            if (colonMode && (spec == 's' || spec == 'O') && haveWidth &&
                static_cast<int>(piece.size()) > fieldWidth) {
                piece = piece.substr(0, static_cast<size_t>(fieldWidth));
            }
            // "."n -- independent precision. Truncates on its own, and
            // (real sprintf.c's own doc: "if precision is greater than
            // field size, then field size = precision") widens an
            // already-explicit field width to match, but does not
            // conjure a field width out of nothing when none was given.
            if (havePrecision && (spec == 's' || spec == 'O')) {
                if (static_cast<int>(piece.size()) > precision) {
                    piece = piece.substr(0, static_cast<size_t>(precision));
                }
                if (haveWidth && precision > fieldWidth) {
                    fieldWidth = precision;
                }
            }
            if (haveWidth && static_cast<int>(piece.size()) < fieldWidth) {
                int padLen = fieldWidth - static_cast<int>(piece.size());
                char padChar = (zeroPad && !leftJustify && !centreJustify) ? '0' : ' ';
                if (centreJustify) {
                    // real sprintf.c's own add_justified(): the leading
                    // half gets the extra character when padLen is odd
                    // ("i = fs / 2 + fs % 2"), not the trailing half.
                    int lead = padLen / 2 + padLen % 2;
                    piece = std::string(static_cast<size_t>(lead), padChar) + piece +
                        std::string(static_cast<size_t>(padLen - lead), padChar);
                } else {
                    std::string pad(static_cast<size_t>(padLen), padChar);
                    piece = leftJustify ? (piece + pad) : (pad + piece);
                }
            }
            result += piece;
        }
        return Value(result);
    };
    t.registerEfun("sprintf", sprintfImpl);

    // void printf(string, ...) -- real efuns_main.c's own f_printf():
    // "if (command_giver) { ret = string_print_formatted(...); ...
    // tell_object(command_giver, ret, ...); }" -- formats exactly like
    // sprintf() (confirmed same underlying string_print_formatted() /
    // sprintf.c machinery, not a separate format engine) and writes the
    // result to command_giver, silently doing nothing when there is
    // none. This driver's own write() efun (see its own registration
    // above) already approximates real write()'s own "target is
    // command_giver, falling back to current_object" semantics as
    // "whichever connection is currently driving the call"
    // (OutputContext::current()) -- real write()'s own do_write()
    // (simulate.c) targets command_giver the same way printf() does, so
    // printf() reuses write()'s own already-proven target resolution
    // rather than introducing a second, separately-approximated one.
    t.registerEfun("printf", [sprintfImpl](VM& vm, std::vector<Value>& args) -> Value {
        Value formatted = sprintfImpl(vm, args);
        if (auto* s = std::get_if<std::string>(&formatted.data)) {
            if (Connection* conn = OutputContext::current()) {
                deliverToConnection(vm, conn, *s);
            } else {
                std::cout << *s;
            }
        }
        return Value{};
    });

    // void message(mixed type, mixed msg, mixed targets, void|mixed
    // excludes) -- real message() routes msg to one or more objects
    // (or a whole room's inventory, matched by category through
    // catch_tell()) with type used for client-side categorization/
    // filtering this driver does not implement. Confirmed live: every
    // "message(...)" call reachable from secure/std/login.c's account-
    // creation flow uses the same one shape -- a plain string msg sent
    // to a single object target, always "this_object()" (the login
    // object driving the currently active connection). Scoped to
    // exactly that: msg is written straight to the connection actually
    // driving the current call (OutputContext::current(), same as
    // receive()/write()), and type/targets/excludes are accepted for
    // signature compatibility but not otherwise inspected -- there is
    // no reverse "object -> its connection" lookup in this driver to
    // route a message to a *different* object's connection than the
    // one currently active, and nothing on this driver's current path
    // needs one.
    // void message(mixed type, mixed msg, mixed targets, mixed excludes...)
    // -- real func_spec.c signature. type and excludes are still ignored
    // (nothing on any path reached live needs message-type filtering or
    // an exclude list). targets now genuinely routes to that specific
    // object's own connection (via InteractiveRegistry::find(), see its
    // own header comment), a single object or an array of objects,
    // falling back to OutputContext::current() only when no targets
    // argument was given at all -- the original, still-correct behavior
    // for message()'s own real default-target convention. This was a
    // real, confirmed-live bug, not a hypothetical gap: this efun
    // previously always wrote to "whichever connection is currently
    // active" and completely ignored targets, which happened to be
    // unobservable on every path reached live before this slice (every
    // real call site so far was "message(type, text, this_object())"
    // where this_object() already was the currently-active connection's
    // own bound object) -- until call_out()/heart_beat() actually started
    // firing (see Scheduler.cpp), which runs with no active connection at
    // all. secure/SimulEfun/communications.c's own tell_object(ob, str)
    // ("message(\"tell\", str+\"\", ob)") is exactly this shape, and is
    // exactly how a delayed call_out message ever reaches a player (this
    // mudlib's single most common notification pattern: "your effect
    // wears off", combat messages, timers). Found live via this
    // project's own throwaway scheduler-verification command
    // (cmds/mortal/_testscheduler.c, not part of the game) producing no
    // output at all despite the call_out itself firing correctly.
    t.registerEfun("message", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("message: expected (mixed type, string msg, mixed targets, ...) arguments");
        }
        const std::string& text = std::get<std::string>(args[1].data);

        auto sendTo = [&vm, &text](const std::shared_ptr<LpcObject>& target) {
            if (Connection* conn = InteractiveRegistry::find(target)) deliverToConnection(vm, conn, text);
        };

        if (args.size() > 2 && std::holds_alternative<std::shared_ptr<LpcObject>>(args[2].data)) {
            sendTo(std::get<std::shared_ptr<LpcObject>>(args[2].data));
            return Value{};
        }
        if (args.size() > 2 && std::holds_alternative<std::shared_ptr<Array>>(args[2].data)) {
            if (auto arr = std::get<std::shared_ptr<Array>>(args[2].data)) {
                for (auto& item : arr->items) {
                    if (auto* ob = std::get_if<std::shared_ptr<LpcObject>>(&item.data)) {
                        sendTo(*ob);
                    }
                }
            }
            return Value{};
        }
        // No targets argument at all: real default is command_giver, this
        // driver's nearest equivalent being whichever connection is
        // currently active.
        if (Connection* conn = OutputContext::current()) {
            deliverToConnection(vm, conn, text);
        }
        return Value{};
    });

    // string lower_case(string) -- efuns_main.c's f_lower_case(): every
    // ASCII uppercase letter folded to lowercase, everything else
    // unchanged.
    t.registerEfun("lower_case", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("lower_case: expected a string argument");
        }
        std::string s = std::get<std::string>(args[0].data);
        for (char& c : s) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        return Value(s);
    });

    // string upper_case(string) -- real packages/contrib.c's own
    // f_upper_case(): exactly lower_case()'s own mirror (uislower/toupper
    // instead of uisupper/tolower in the real source; every ASCII
    // lowercase letter folded to uppercase, everything else unchanged).
    // Confirmed real and active in this vendored build (efun_defs.c's own
    // F_UPPER_CASE entry), not just declared -- and real-reachable here:
    // cmds/mortal/_guild.c's own guild create/join/leave/list commands
    // (7 call sites), cmds/mortal/_setenv.c, cmds/adm/_repairchar.c, and
    // daemon/guild_d.c all call it unconditionally, previously throwing
    // "undefined function or efun: upper_case" the instant any of those
    // ran.
    t.registerEfun("upper_case", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("upper_case: expected a string argument");
        }
        std::string s = std::get<std::string>(args[0].data);
        for (char& c : s) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        }
        return Value(s);
    });

    // int string_difference(string, string) -- current FluffOS's own real,
    // genuinely new-since-2.9 contrib efun (confirmed absent from
    // temp/reference/fluffos-2.9-ds2.08 entirely: no string_difference /
    // levenshtein anywhere in that tree; the current locally-vendored
    // clone temp/fluffos/src/packages/contrib/contrib.cc adds it). Same
    // category as sha1() (row 2.46), the log2()/round() row, and the
    // math.spec vector efuns (row 2.51): real current-FluffOS surface the
    // 2.9 reference never carried.
    //
    // Signature from temp/fluffos/src/packages/contrib/contrib.spec:
    //   int string_difference(string, string);
    // The doc (docs/efun/contrib/string_difference.md) says only "return
    // levenshtein difference".
    //
    // Semantics from contrib.cc's own levenshtein() + f_string_difference():
    //   - Equal strings (strcmp == 0) short-circuit to 0.
    //   - Otherwise the classic Levenshtein edit distance (insert / delete /
    //     substitute each cost 1). The real code strips the common prefix
    //     and suffix and passes the shorter string first purely for speed;
    //     the distance is symmetric and prefix/suffix stripping does not
    //     change it, so a plain single-row DP gives the identical result.
    //   - The real code treats both arguments as C strings (strcmp /
    //     strlen), so a literal embedded NUL byte terminates them; this
    //     driver truncates each argument at the first NUL to match, rather
    //     than silently scoring the bytes past it.
    // Verified against FluffOS's own testsuite
    // (testsuite/single/tests/efuns/string_difference.lpc): ("abc","abc")
    // -> 0, ("abc","abd") -> 1, ("kitten","sitting") -> 3, ("","abc") -> 3.
    //
    // Corpus call-site frequency, checked before implementing: grepped
    // every vendored corpus under temp/ (core-lib, dead-souls, es2_mudlib,
    // lima, nightmare3, reference-lpc-mud-library, wiz_tools, lil) plus the
    // bundled mudlib/ for string_difference(: zero real LPC call sites (the
    // only hits anywhere under temp/ are FluffOS's own docs and testsuite).
    // Motivation is FluffOS-surface parity, the same honestly-named basis
    // as rows 2.16/2.24/2.25/2.46/2.47/2.48/2.49/2.50/2.51; edit distance
    // is independently verifiable by hand with no live-instance dependency.
    t.registerEfun("string_difference", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 ||
            !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("string_difference: expected (string, string)");
        }
        // C-string semantics: stop at the first embedded NUL, like strlen.
        std::string a(std::get<std::string>(args[0].data).c_str());
        std::string b(std::get<std::string>(args[1].data).c_str());
        if (a == b) return Value(static_cast<int64_t>(0));

        // Single running row of costs, O(min(|a|,|b|)) space.
        const std::string& x = a.size() <= b.size() ? a : b;
        const std::string& y = a.size() <= b.size() ? b : a;
        const std::size_t n = x.size();
        const std::size_t m = y.size();
        std::vector<int> row(m + 1);
        for (std::size_t j = 0; j <= m; ++j) row[j] = static_cast<int>(j);
        for (std::size_t i = 1; i <= n; ++i) {
            int diag = row[0];
            row[0] = static_cast<int>(i);
            for (std::size_t j = 1; j <= m; ++j) {
                int up = row[j];
                int sub = diag + (x[i - 1] == y[j - 1] ? 0 : 1);
                row[j] = std::min({row[j - 1] + 1, up + 1, sub});
                diag = up;
            }
        }
        return Value(static_cast<int64_t>(row[m]));
    });

    // string trim(string str, string|void ch) / ltrim(...) / rtrim(...)
    // -- current FluffOS's own real, genuinely new-since-2.9 string
    // efuns (confirmed absent from temp/reference/fluffos-2.9-ds2.08
    // entirely: no trim/ltrim/rtrim anywhere in that tree). Signatures
    // confirmed directly against real current source, not guessed:
    // src/packages/trim/trim.spec's own "string trim(string,
    // string|void);" (same shape for ltrim/rtrim), and
    // src/packages/trim/trim.cc's own trim_impl()/ltrim_func()/
    // rtrim_func()/trim_func(): a missing or empty 2nd argument defaults
    // the strip set to "\t\n\v\f\r " (the classic C isspace() set for
    // the "C" locale, ported verbatim below); otherwise every character
    // in the given 2nd-argument string is a member of the strip set
    // (strspn/strcspn-style character-class trimming, not a substring
    // match). Independently verifiable via plain string identities with
    // zero live-current-FluffOS-instance dependency at all: idempotence
    // (trimming an already-trimmed string is a no-op), the trim/ltrim/
    // rtrim relationship (trim(s) == rtrim(ltrim(s))), and an explicit
    // custom charset stripping exactly the given characters and nothing
    // else -- an even more airtight verification surface than log2()/
    // round()'s own standard math identities, since there is no
    // floating-point precision question at all here.
    auto stripSet = [](const std::string& s, const std::string& charset, bool fromStart, bool fromEnd) -> std::string {
        size_t begin = 0;
        size_t end = s.size();
        if (fromStart) {
            while (begin < end && charset.find(s[begin]) != std::string::npos) ++begin;
        }
        if (fromEnd) {
            while (end > begin && charset.find(s[end - 1]) != std::string::npos) --end;
        }
        return s.substr(begin, end - begin);
    };
    auto trimArgs = [](std::vector<Value>& args) -> std::pair<std::string, std::string> {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("trim: expected a string argument");
        }
        std::string str = std::get<std::string>(args[0].data);
        std::string charset;
        if (args.size() > 1 && std::holds_alternative<std::string>(args[1].data)) {
            charset = std::get<std::string>(args[1].data);
        }
        if (charset.empty()) charset = "\t\n\v\f\r ";
        return {str, charset};
    };
    t.registerEfun("trim", [stripSet, trimArgs](VM&, std::vector<Value>& args) -> Value {
        auto [str, charset] = trimArgs(args);
        return Value(stripSet(str, charset, true, true));
    });
    t.registerEfun("ltrim", [stripSet, trimArgs](VM&, std::vector<Value>& args) -> Value {
        auto [str, charset] = trimArgs(args);
        return Value(stripSet(str, charset, true, false));
    });
    t.registerEfun("rtrim", [stripSet, trimArgs](VM&, std::vector<Value>& args) -> Value {
        auto [str, charset] = trimArgs(args);
        return Value(stripSet(str, charset, false, true));
    });

    // string replace_string(string str, string pattern, string
    // replacement [, int first [, int last]]) -- efuns_main.c's
    // f_replace_string() (func_spec.c: "string replace_string(string,
    // string, string,...);"). With three arguments every non-overlapping
    // occurrence of pattern is replaced left to right. The optional
    // 4th/5th arguments select an occurrence range, exactly as real does:
    //   - 4 args: the 4th is `last`, `first` defaults to 0, so
    //     occurrences 1..last are replaced ("replace_string("xyxx", "x",
    //     "z", 2)" -> "zyzx").
    //   - 5 args: the 4th is `first`, the 5th is `last`, so occurrences
    //     first..last are replaced ("replace_string("xyxxy", "x", "z",
    //     2, 3)" -> "xyzzy").
    // Occurrences are counted whether or not they fall in range; an
    // out-of-range match is copied through verbatim. Real stops scanning
    // the instant the `last`-th match is handled ("if (cur == last)
    // break;") and copies the rest of the string unchanged.
    // Real edge cases, all reproduced: an empty pattern returns the
    // string unchanged ("if (!plen) ... just return it"); "last == 0"
    // means "no upper bound" ("if (!last) last = max_string_length;"),
    // and since this driver has no max_string_length (same as add_a() /
    // replace_html()) that simply means unbounded; "first > last"
    // (evaluated after that default, so an unbounded upper bound never
    // trips it) returns the string unchanged; a negative bound is left
    // as-is (it either trips the "first > last" return or makes the
    // "cur <= last" test never true). Each occurrence bound must be an
    // int, matching real's CHECK_TYPES(..., T_NUMBER, ...); more than
    // five arguments is an error ("Too many args to replace_string.").
    // The real max_string_length overflow path (a T_UNDEFINED 0 return)
    // has no analogue here, this driver having no such limit.
    t.registerEfun("replace_string", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 3 || args.size() > 5 ||
            !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data) ||
            !std::holds_alternative<std::string>(args[2].data)) {
            throw LpcRuntimeError("replace_string: expected (string str, string pattern, "
                                   "string replacement [, int first [, int last]])");
        }
        const std::string& str = std::get<std::string>(args[0].data);
        const std::string& pattern = std::get<std::string>(args[1].data);
        const std::string& replacement = std::get<std::string>(args[2].data);

        long long first = 0;
        long long last = 0;
        if (args.size() >= 4) {
            if (!std::holds_alternative<int64_t>(args[3].data)) {
                throw LpcRuntimeError("Bad argument 4 to replace_string() (expected int)");
            }
            if (args.size() == 4) {
                last = static_cast<long long>(std::get<int64_t>(args[3].data));
                first = 0;
            } else {  // args.size() == 5
                if (!std::holds_alternative<int64_t>(args[4].data)) {
                    throw LpcRuntimeError("Bad argument 5 to replace_string() (expected int)");
                }
                first = static_cast<long long>(std::get<int64_t>(args[3].data));
                last = static_cast<long long>(std::get<int64_t>(args[4].data));
            }
        }

        const bool boundedAbove = (last != 0);
        if (boundedAbove && first > last) return Value(str);
        if (pattern.empty()) return Value(str);

        std::string result;
        size_t start = 0;
        long long cur = 0;
        for (;;) {
            size_t pos = str.find(pattern, start);
            if (pos == std::string::npos) {
                result.append(str, start, std::string::npos);
                break;
            }
            result.append(str, start, pos - start);
            ++cur;
            if (cur >= first && (!boundedAbove || cur <= last)) {
                result += replacement;
            } else {
                result += pattern;
            }
            start = pos + pattern.size();
            if (boundedAbove && cur == last) {
                result.append(str, start, std::string::npos);
                break;
            }
        }
        return Value(result);
    });

    // mixed evaluate(mixed f, mixed extra_args...) and its alias
    // mixed funcall(mixed f, mixed extra_args...) -- efuns_main.c's
    // f__evaluate(), registered under both names in the real driver too
    // (func_spec.c: "mixed evaluate _evaluate(mixed, ...); mixed funcall
    // _evaluate(mixed, ...);"). A non-function first argument is a
    // silent no-op returning void, not an error -- real f__evaluate():
    // "if (arg->type != T_FUNCTION) { pop_n_elems(...); return; }".
    auto evaluateImpl = [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty()) return Value{};
        auto* closurePtr = std::get_if<std::shared_ptr<Closure>>(&args[0].data);
        if (!closurePtr || !*closurePtr) return Value{};
        std::vector<Value> extra(args.begin() + 1, args.end());
        return vm.callClosure(*closurePtr, std::move(extra));
    };
    t.registerEfun("evaluate", evaluateImpl);
    t.registerEfun("funcall", evaluateImpl);

    // closure unbound_lambda(mixed *args, mixed) -- LDMud-only (real
    // func_spec:500's own declaration; ROADMAP.md row 1.7/1.8, greenlit
    // by real corpus evidence: secure/master/hooks.c's own 4 real call
    // sites, each handed straight to set_driver_hook(), itself still
    // unimplemented -- see this row's own ROADMAP.md note). Real
    // f_unbound_lambda() (closure.c:6889-6941): builds a closure with no
    // home object at all ("l->base.ob = const0") from a declared-
    // parameter-symbol array and a quoted-code body -- see VM.cpp's own
    // callUnboundLambdaBody()/evalQuotedLambdaNode() for exactly which
    // real quoted-code shapes this driver's own body walker supports.
    // Registered unconditionally rather than dialect-gated, matching
    // this table's own established convention (unshadow()'s own comment:
    // efun *availability* is never withheld by dialect here).
    t.registerEfun("unbound_lambda", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2) {
            throw LpcRuntimeError("unbound_lambda() requires 2 arguments");
        }
        auto closure = std::make_shared<Closure>();
        closure->unboundUntilBound = true;
        // real f_unbound_lambda(): "if (sp[-1].type != T_POINTER) { if
        // (sp[-1].type != T_NUMBER || sp[-1].u.number) efun_gen_arg_error;
        // args = ref_array(&null_vector); }" -- int 0 (or void) is
        // accepted as shorthand for "no parameters", anything else that
        // is not an array is a real argument-type error.
        if (auto* arr = std::get_if<std::shared_ptr<Array>>(&args[0].data)) {
            if (*arr) {
                for (const auto& item : (*arr)->items) {
                    auto* sym = std::get_if<Symbol>(&item.data);
                    if (!sym) {
                        throw LpcRuntimeError(
                            "Bad argument 1 to unbound_lambda(): parameter array must "
                            "contain only 'name symbols");
                    }
                    closure->lambdaParams.push_back(sym->name);
                }
            }
        } else if (!args[0].isVoid() &&
                   !(std::holds_alternative<int64_t>(args[0].data) &&
                     std::get<int64_t>(args[0].data) == 0)) {
            throw LpcRuntimeError("Bad argument 1 to unbound_lambda()");
        }
        closure->lambdaBody = args[1];
        return Value(closure);
    });

    // closure bind_lambda(closure cl [, object|lwobject ob]) -- LDMud-
    // only (real func_spec:496). Real v_bind_lambda() (closure.c:6368-
    // 6519) is a real, general "rebind any closure kind's own owner"
    // efun (CLOSURE_LFUN/CLOSURE_BOUND_LAMBDA get rebound in place;
    // CLOSURE_UNBOUND_LAMBDA becomes bound for the first time, real
    // CLOSURE_BOUND_LAMBDA), not something unbound_lambda()-specific --
    // ported that generally here too rather than narrowly to just the
    // one real corpus shape, since the real efun itself is not narrower.
    // The 1-argument form (bind to this_object(), what every real
    // corpus-adjacent use of unbound_lambda()/bind_lambda() anywhere in
    // temp/'s vendored corpora would need, though bind_lambda() itself
    // has zero real corpus call sites at all -- hooks.c's own real usage
    // never calls it directly, real driver-hook dispatch auto-binds
    // internally, see VM.cpp's own callClosure() comment) needs no
    // privilege check at all (real v_bind_lambda(): "If the argument ob
    // is omitted, the closure is bound to this_object()"). The 2-
    // argument cross-object form gates on a real privilege_violation()
    // call in real LDMud (closure.c:6394-6396) -- real and wired as of
    // 2026-08-20, ROADMAP.md row 1.7/1.8's own privilege_violation()
    // scoping investigation, ported via the shared VM::privilegeViolation()
    // helper (VM.hpp/VM.cpp), the first real trigger point this driver
    // implements. See the call site just below for the exact real arg
    // shape and denial behavior.
    t.registerEfun("bind_lambda", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty()) {
            throw LpcRuntimeError("bind_lambda() requires at least 1 argument");
        }
        auto* closurePtr = std::get_if<std::shared_ptr<Closure>>(&args[0].data);
        if (!closurePtr || !*closurePtr) {
            throw LpcRuntimeError("Bad argument 1 to bind_lambda()");
        }
        auto closure = *closurePtr;
        std::shared_ptr<LpcObject> target;
        if (args.size() >= 2 && !args[1].isVoid()) {
            auto* obPtr = std::get_if<std::shared_ptr<LpcObject>>(&args[1].data);
            if (!obPtr || !*obPtr) {
                throw LpcRuntimeError("Bad argument 2 to bind_lambda()");
            }
            // real "if (!is_current_object(*sp) && !privilege_violation(
            // STR_BIND_LAMBDA, sp, sp)) { free_svalue(sp--); return sp; }"
            // (closure.c:6396-6400) -- the check only runs when ob is
            // given AND differs from current_object (the common
            // "bind to myself" case, already handled by the `target =
            // *obPtr` fallthrough below, never even reaches the apply);
            // a denial (not an error, just a false/"0" answer) is not an
            // error either, it silently hands back the original closure
            // untouched, exactly like real code's own "Return closure
            // unharmed" comment. Real extra data arg is `ob` itself
            // (`sp`, the target object) -- matches this driver's own
            // *obPtr directly.
            if (*obPtr != vm.currentObject()) {
                if (!vm.privilegeViolation("bind_lambda", {Value(*obPtr)})) {
                    return Value(closure);
                }
            }
            target = *obPtr;
        } else {
            target = vm.currentObject();
        }
        closure->owner = target;
        closure->unboundUntilBound = false;
        return Value(closure);
    });

    // void set_driver_hook(int what, closure|string|string*|mapping arg)
    // -- LDMud-only (real func_spec, `void set_driver_hook(int, closure|
    // string|string *|mapping);`; real f_set_driver_hook(), simulate.c:
    // 5056-5228). ROADMAP.md row 1.7/1.8's own real trigger-point
    // investigation: real secure/master/hooks.c's own 4 real
    // unbound_lambda() call sites are all passed straight here, all from
    // its own inaugurate_master()-invoked addDriverHooks() -- confirmed
    // by reading hooks.c in full again this session, not assumed from
    // the prior session's own summary. This driver has no
    // inaugurate_master() boot-apply wiring at all yet (confirmed by
    // grep -- a separate, still-open gap named in ROADMAP.md, not
    // silently worked around here), so a real mudlib master object must
    // still call set_driver_hook() itself from wherever its own boot
    // path already runs (this driver's own bundled mudlib/single/
    // master.c's create(), or an explicit eval/apply) rather than
    // relying on that automatic real wiring. See VM::setDriverHook()'s
    // own comment for exactly which real validation this deliberately
    // does not replicate (per-hook type-map checking, the eager
    // unbound-lambda-to-master rebind optimization) and why each is a
    // safe, flagged simplification rather than a silent gap; the
    // privilege_violation() authorization gate itself is real as of
    // 2026-08-20 (ROADMAP.md row 1.7/1.8's own investigation), wired
    // right below via VM::privilegeViolation(). Registered
    // unconditionally, matching this table's own established
    // dialect-neutral-availability convention.
    t.registerEfun("set_driver_hook", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2) {
            throw LpcRuntimeError("set_driver_hook() requires 2 arguments");
        }
        if (!std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("Bad argument 1 to set_driver_hook()");
        }
        int64_t what = std::get<int64_t>(args[0].data);
        // real "n = sp[-1].u.number; if (n < 0 || n >= NUM_DRIVER_HOOKS)
        // errorf(\"Bad hook number...\");" (simulate.c:5080-5088) runs
        // BEFORE the privilege check below (simulate.c:5091) -- checked
        // explicitly here, ahead of privilegeViolation(), rather than
        // relying on VM::setDriverHook()'s own later range check, to
        // match that real ordering exactly (a real out-of-range call
        // must fail with "Bad hook number", not a privilege-violation
        // error, regardless of what the master's own privilege_violation()
        // lfun would have said).
        if (what < 0 || what >= VM::kNumDriverHooks) {
            throw LpcRuntimeError(
                "Bad hook number: " + std::to_string(what) + ", expected 0.." +
                std::to_string(VM::kNumDriverHooks - 1));
        }
        // real "if (!privilege_violation(STR_SET_DRIVER_HOOK, sp-1, sp))
        // { free_svalue(sp); return sp - 2; }" (simulate.c:5091-5095).
        // Real extra data arg is the hook number itself, `what` (see
        // this efun's own header comment on why not `arg` too). A
        // denial is not an error -- real code silently returns void
        // with the hook left unchanged, ported here as a silent no-op.
        if (!vm.privilegeViolation("set_driver_hook", {Value(what)})) {
            return Value{};
        }
        vm.setDriverHook(static_cast<int>(what), args[1]);
        return Value{};
    });

    // int functionp(mixed) -- true only for a real Closure value (real
    // FluffOS's f_functionp() also distinguishes several function-
    // pointer sub-kinds via a bitmask return; this driver has only the
    // one kind, so a plain 0/1 is enough for every "if(functionp(x))"
    // truth-check use this mudlib actually makes, e.g. std/Object.c's
    // own query_long()/query_short()).
    t.registerEfun("functionp", [](VM&, std::vector<Value>& args) -> Value {
        bool isFn = !args.empty() &&
            std::holds_alternative<std::shared_ptr<Closure>>(args[0].data) &&
            std::get<std::shared_ptr<Closure>>(args[0].data) != nullptr;
        return Value(static_cast<int64_t>(isFn ? 1 : 0));
    });

    // int objectp(mixed) -- true only for a real (non-null) object
    // reference.
    t.registerEfun("objectp", [](VM&, std::vector<Value>& args) -> Value {
        bool isObj = !args.empty() &&
            std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data) &&
            std::get<std::shared_ptr<LpcObject>>(args[0].data) != nullptr;
        return Value(static_cast<int64_t>(isObj ? 1 : 0));
    });

    // int stringp(mixed) -- true only for a string value (func_spec.c:
    // "int stringp(mixed);"). Confirmed live: secure/SimulEfun/
    // base_name.c's own "if(stringp(val)) name = val; else name =
    // file_name(val);" -- base_name() accepts either a path string or
    // an object reference.
    t.registerEfun("stringp", [](VM&, std::vector<Value>& args) -> Value {
        bool isStr = !args.empty() && std::holds_alternative<std::string>(args[0].data);
        return Value(static_cast<int64_t>(isStr ? 1 : 0));
    });

    // int mapp(mixed) -- true only for a mapping value.
    t.registerEfun("mapp", [](VM&, std::vector<Value>& args) -> Value {
        bool isMap = !args.empty() && std::holds_alternative<std::shared_ptr<Mapping>>(args[0].data);
        return Value(static_cast<int64_t>(isMap ? 1 : 0));
    });

    // int intp(mixed) -- func_spec.c: "int intp(mixed);", the remaining
    // type predicate missing alongside stringp/objectp/mapp/pointerp/
    // functionp above (real FluffOS registers all of these together in
    // the same "T_*p()" family). Found live: /domains/Praxis/equipment/
    // id_card.c's own set_value(), reached by daemon/rifts_start_d.c's
    // give_item() while granting starting equipment during
    // finish_creation() -- the actual blocker stopping a fresh
    // character from ever reaching a real room. A monostate "no value"
    // (an unset object variable slot before this driver's own real-0
    // default fix, or an efun's explicit "nothing found" return) does
    // not count as an int, matching real FluffOS's own T_NUMBER-only
    // check.
    t.registerEfun("intp", [](VM&, std::vector<Value>& args) -> Value {
        bool isInt = !args.empty() && std::holds_alternative<int64_t>(args[0].data);
        return Value(static_cast<int64_t>(isInt ? 1 : 0));
    });

    // int floatp(mixed) -- func_spec.c: "int floatp(mixed);". Confirmed
    // directly against fluffos-2.9-ds2.08/efuns_main.c's own f_floatp():
    // "if (sp->type == T_REAL) { ... 1 } else { ... 0 }" -- a plain
    // type-tag check, same shape as intp/stringp/mapp above, no
    // conversion or coercion involved. This driver's Value variant
    // already has a real `double` alternative (Value.hpp), so this is
    // the identical std::holds_alternative pattern the other type
    // predicates already use. Picked from the efun-coverage audit's
    // Tier 1 quick-win list (docs/source-audits/efun-coverage.md):
    // real, reachable call sites include secure/SimulEfun/percent.c's
    // own shared simul_efun helper (called from several other files),
    // so an undefined-efun error here was reachable from any of
    // percent()'s own callers, not just floatp()'s own 6 direct sites.
    t.registerEfun("floatp", [](VM&, std::vector<Value>& args) -> Value {
        bool isFloat = !args.empty() && std::holds_alternative<double>(args[0].data);
        return Value(static_cast<int64_t>(isFloat ? 1 : 0));
    });

    // int undefinedp(mixed) / int nullp(mixed) -- real f__undefinedp():
    // true only for real FluffOS's distinct "T_UNDEFINED" zero subtype
    // (a failed lookup/uninitialized value), never for a plain literal
    // 0 or any other type (func_spec.cpp: "int undefinedp(mixed); int
    // nullp undefinedp(mixed);" -- nullp is a real alias, not a
    // separate efun). Two real cases now checked, matching real
    // find_in_mapping()'s and setup_variables()'s own shared const0u
    // exactly (see Value.hpp's own isUndefined/makeUndefinedNumber()
    // comments): a missing mapping key (this driver's own monostate,
    // its pre-existing "no value" sentinel for that case, unrelated to
    // the flag below) and, as of the isUndefined field, a genuinely
    // never-assigned FluffOS-dialect local or object variable (real
    // const0u, T_NUMBER 0 with the T_UNDEFINED subtype). The previous
    // version of this comment claimed monostate was "currently what an
    // object variable reads as" -- stale the moment LpcObject.cpp/
    // VM.cpp moved that default to a real, isUndefined-tagged 0 instead
    // of monostate; fixed here to check the actual current
    // representation rather than the one this comment used to describe.
    // Surfaced live: daemon/multi.c's own query_prevent_login(), and
    // secure/daemon/master.c's own privs_file() (ROADMAP.md row 3.10).
    auto undefinedpImpl = [](VM&, std::vector<Value>& args) -> Value {
        bool isUndefined = !args.empty() &&
            (std::holds_alternative<std::monostate>(args[0].data) || args[0].isUndefined);
        return Value(static_cast<int64_t>(isUndefined ? 1 : 0));
    };
    t.registerEfun("undefinedp", undefinedpImpl);
    t.registerEfun("nullp", undefinedpImpl);

    // int to_int(string | float | int) -- efuns_main.c's f__to_int(),
    // confirmed against the reference source directly (func_spec.cpp:
    // "int to_int _to_int(string | float | int | buffer);"). Surfaced as
    // undefined during a live boot test on std/user.c's own inherit
    // chain (std/user/more.c, std/living.c, std/user.c itself all call
    // it directly, not just through editor.c, so this is a real,
    // separate gap, not one that resolves itself once the closure chain
    // compiles). This driver has no buffer type (see Value.hpp's
    // ValueVariant), so that case is dropped; the other three match
    // f__to_int() exactly: an int argument passes through unchanged, a
    // float truncates toward zero (real f__to_int() does a plain C
    // "(long) sp->u.real" cast, not round-to-nearest), and a string
    // parses a leading base-10 integer via strtol() semantics, ignoring
    // trailing non-digit garbage and returning 0 for a string with no
    // parseable leading number at all (real f__to_int()'s own comment:
    // "this means to_int(\"10x\") == 10"). Any other argument type
    // throws, matching the declared signature rejecting it under real
    // FluffOS's exact_types argument checking.
    t.registerEfun("to_int", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(static_cast<int64_t>(0));
        const Value& v = args[0];
        if (std::holds_alternative<int64_t>(v.data)) {
            return Value(std::get<int64_t>(v.data));
        }
        if (std::holds_alternative<double>(v.data)) {
            return Value(static_cast<int64_t>(std::get<double>(v.data)));
        }
        if (std::holds_alternative<std::string>(v.data)) {
            const std::string& s = std::get<std::string>(v.data);
            try {
                size_t consumed = 0;
                long parsed = std::stol(s, &consumed, 10);
                if (consumed == 0) return Value(static_cast<int64_t>(0));
                return Value(static_cast<int64_t>(parsed));
            } catch (const std::exception&) {
                return Value(static_cast<int64_t>(0));
            }
        }
        throw LpcRuntimeError("Bad argument 1 to to_int()");
    });

    // int pointerp(mixed) / int arrayp(mixed) -- real aliases of the
    // same efun (func_spec.c: "int pointerp(mixed); int arrayp
    // pointerp(mixed);") -- true only for an array value.
    auto pointerpImpl = [](VM&, std::vector<Value>& args) -> Value {
        bool isArr = !args.empty() && std::holds_alternative<std::shared_ptr<Array>>(args[0].data);
        return Value(static_cast<int64_t>(isArr ? 1 : 0));
    };
    t.registerEfun("pointerp", pointerpImpl);
    t.registerEfun("arrayp", pointerpImpl);

    // int classp(mixed) -- real efuns_main.c's f_classp(): "if (sp->type
    // == T_CLASS) { ...; *sp = const1; } else { ...; *sp = const0; }" --
    // true only for a real class instance (real LPC's "class <name> { }"
    // struct-type declaration, ROADMAP.md row 3.10's class scoping
    // report and its own follow-up implementation), never for a plain
    // array or any other type, no per-class identity check. This
    // driver's own analog of that same T_CLASS/T_ARRAY reuse is
    // Value::isClassInstance (see its own comment) -- a class instance
    // is still, underneath, a shared_ptr<Array> the same way real
    // FluffOS's own T_CLASS svalue still stores a plain array_t*, so
    // this checks both the alternative and the flag together, not the
    // flag alone. Previously an always-false stub (this driver had no
    // class representation at all yet). Confirmed real, live-reachable:
    // secure/std/client.c's own socket-argument dispatch calls it twice
    // ("if(classp(arg)) sock = arg; if(!classp(arg)){...}").
    t.registerEfun("classp", [](VM&, std::vector<Value>& args) -> Value {
        bool isClass = !args.empty() &&
            std::holds_alternative<std::shared_ptr<Array>>(args[0].data) &&
            args[0].isClassInstance;
        return Value(static_cast<int64_t>(isClass ? 1 : 0));
    });

    // ---------------------------------------------------------------------
    // ROADMAP.md row 2.33a: the buffer value type plus the buffer efuns
    // that need only the type and no new dependency. Semantics traced
    // from fluffos-2.9-ds2.08/buffer.c and efuns_main.c (f_allocate_buffer
    // / f_bufferp / f_read_buffer / f_write_buffer), plus the current
    // clone's f__to_buffer() / svalue_to_buffer_bytes() for to_buffer
    // (2.9 has no such efun). The iconv-dependent charset efuns
    // (string_encode/string_decode/buffer_transcode/set_encoding/
    // query_encoding), zlib compression, binary socket mode, and the VM
    // operators on buffers (b[i], b[i..j], buffer + x) are all
    // deliberately out of this slice, staying on row 2.33 and their own
    // follow-ups.
    // ---------------------------------------------------------------------

    // int bufferp(mixed) -- real f_bufferp(): 1 for a T_BUFFER value,
    // 0 for anything else. Same plain type-tag shape as intp/stringp.
    t.registerEfun("bufferp", [](VM&, std::vector<Value>& args) -> Value {
        bool isBuf = !args.empty() &&
                     std::holds_alternative<std::shared_ptr<Buffer>>(args[0].data);
        return Value(static_cast<int64_t>(isBuf ? 1 : 0));
    });

    // buffer allocate_buffer(int size) -- real buffer.c allocate_buffer():
    // "size < 0 || size > max_buffer_size" errors "Illegal buffer size.",
    // size 0 returns a shared zero-length buffer, otherwise a calloc-
    // zeroed buffer of `size` bytes. This driver has no max_buffer_size
    // config (the same situation add_a()/replace_html() are in for
    // max_string_length), so a generous fixed cap stands in for it; a
    // non-int argument throws, this codebase's bad-shape precedent.
    t.registerEfun("allocate_buffer", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("allocate_buffer: expected an int size argument");
        }
        int64_t size = std::get<int64_t>(args[0].data);
        // Local stand-in for real's max_buffer_size, purely a sanity
        // ceiling so a bogus size cannot request a multi-gigabyte
        // allocation; real's own default cap is configuration-defined.
        constexpr int64_t kMaxBufferSize = 256 * 1024 * 1024;
        if (size < 0 || size > kMaxBufferSize) {
            throw LpcRuntimeError("Illegal buffer size.");
        }
        auto buf = std::make_shared<Buffer>();
        buf->bytes.assign(static_cast<size_t>(size), 0);
        return Value(buf);
    });

    // mixed read_buffer(string | buffer src, void | int start,
    //                   void | int len)
    // -- real f_read_buffer() + buffer.c read_buffer(). Two forms:
    //   - src is a buffer: returns a STRING of the bytes in
    //     [start, start+len), len 0 meaning "to the end of the buffer",
    //     a negative start counting back from the end. Real stops at the
    //     first NUL byte inside that range ("for (...; *str && size <
    //     len; ...)"). len < 0, start still negative after adjustment,
    //     or start >= size all return int 0.
    //   - src is a string: reads that FILE's bytes over the same
    //     start/len window and returns them as a NEW buffer; a missing
    //     file or an empty read returns int 0.
    t.registerEfun("read_buffer", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty()) {
            throw LpcRuntimeError("read_buffer: expected (string | buffer, void | int, void | int)");
        }
        int64_t start = 0, len = 0;
        if (args.size() > 1) {
            if (!std::holds_alternative<int64_t>(args[1].data)) {
                throw LpcRuntimeError("read_buffer: start argument must be an int");
            }
            start = std::get<int64_t>(args[1].data);
        }
        if (args.size() > 2) {
            if (!std::holds_alternative<int64_t>(args[2].data)) {
                throw LpcRuntimeError("read_buffer: len argument must be an int");
            }
            len = std::get<int64_t>(args[2].data);
        }

        if (auto* bp = std::get_if<std::shared_ptr<Buffer>>(&args[0].data)) {
            const std::vector<unsigned char>& b = (*bp)->bytes;
            int64_t size = static_cast<int64_t>(b.size());
            if (len < 0) return Value(int64_t{0});
            if (start < 0) {
                start = size + start;
                if (start < 0) return Value(int64_t{0});
            }
            if (len == 0) len = size;
            if (start >= size) return Value(int64_t{0});
            if (start + len > size) len = size - start;
            std::string out;
            for (int64_t i = 0; i < len; ++i) {
                unsigned char c = b[static_cast<size_t>(start + i)];
                if (c == 0) break;  // real reader stops at the first NUL
                out.push_back(static_cast<char>(c));
            }
            return Value(out);
        }

        if (auto* sp = std::get_if<std::string>(&args[0].data)) {
            // File form: read the file's bytes and hand them back as a
            // buffer. Same path gating and offset rules read_bytes uses.
            auto gated = checkValidPath(vm, *sp, false, "read_buffer");
            if (!gated) return Value(int64_t{0});
            std::string path = vm.resolveMudlibPath(*gated);
            if (len < 0) return Value(int64_t{0});
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (!f) return Value(int64_t{0});
            int64_t fsize = static_cast<int64_t>(f.tellg());
            if (start < 0) start = fsize + start;
            if (start < 0 || start >= fsize) return Value(int64_t{0});
            if (len == 0) len = fsize;
            if (start + len > fsize) len = fsize - start;
            f.seekg(start);
            auto buf = std::make_shared<Buffer>();
            buf->bytes.resize(static_cast<size_t>(len));
            f.read(reinterpret_cast<char*>(buf->bytes.data()), len);
            auto got = f.gcount();
            if (got <= 0) return Value(int64_t{0});
            buf->bytes.resize(static_cast<size_t>(got));
            return Value(buf);
        }

        throw LpcRuntimeError("read_buffer: first argument must be a string or a buffer");
    });

    // int write_buffer(string | buffer dest, int start,
    //                  string | buffer | int data)
    // -- real f_write_buffer() + buffer.c write_buffer(). dest a string
    // delegates to the file writer (write_bytes), which takes a string
    // data argument only. dest a buffer writes `data` into it in place:
    // an int is written as its 4 bytes in network byte order (htonl), a
    // string or buffer as its raw bytes. A negative start counts back
    // from the end; a write that would run past the end of the (fixed
    // size) buffer is refused and returns 0, a successful write returns
    // 1. The buffer is mutated in place, not reallocated.
    t.registerEfun("write_buffer", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 3 || !std::holds_alternative<int64_t>(args[1].data)) {
            throw LpcRuntimeError("write_buffer: expected (string | buffer, int, string | buffer | int)");
        }
        int64_t start = std::get<int64_t>(args[1].data);

        if (auto* dp = std::get_if<std::string>(&args[0].data)) {
            if (!std::holds_alternative<std::string>(args[2].data)) {
                throw LpcRuntimeError("write_buffer: writing to a file requires a string data argument");
            }
            const std::string& data = std::get<std::string>(args[2].data);
            auto gated = checkValidPath(vm, *dp, true, "write_buffer");
            if (!gated) return Value(int64_t{0});
            std::string path = vm.resolveMudlibPath(*gated);
            std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
            if (!f) {
                std::ofstream create(path, std::ios::binary);
                if (!create) return Value(int64_t{0});
                create.close();
                f.open(path, std::ios::binary | std::ios::in | std::ios::out);
                if (!f) return Value(int64_t{0});
            }
            f.seekg(0, std::ios::end);
            int64_t fsize = static_cast<int64_t>(f.tellg());
            if (start < 0) start = fsize + start;
            if (start < 0 || start > fsize) return Value(int64_t{0});
            f.seekp(start);
            f.write(data.data(), static_cast<std::streamsize>(data.size()));
            return Value(static_cast<int64_t>(f.good() ? 1 : 0));
        }

        auto* dbp = std::get_if<std::shared_ptr<Buffer>>(&args[0].data);
        if (!dbp || !*dbp) {
            throw LpcRuntimeError("write_buffer: first argument must be a string or a buffer");
        }
        std::vector<unsigned char>& dst = (*dbp)->bytes;

        // Assemble the bytes to write from the third argument.
        std::vector<unsigned char> payload;
        if (auto* iv = std::get_if<int64_t>(&args[2].data)) {
            uint32_t net = htonl(static_cast<uint32_t>(*iv & 0xFFFFFFFFu));
            payload.resize(sizeof(uint32_t));
            std::memcpy(payload.data(), &net, sizeof(uint32_t));
        } else if (auto* svp = std::get_if<std::string>(&args[2].data)) {
            payload.assign(svp->begin(), svp->end());
        } else if (auto* bvp = std::get_if<std::shared_ptr<Buffer>>(&args[2].data)) {
            if (*bvp) payload = (*bvp)->bytes;
        } else {
            throw LpcRuntimeError("write_buffer: data argument must be a string, a buffer, or an int");
        }

        int64_t size = static_cast<int64_t>(dst.size());
        if (start < 0) {
            start = size + start;
            if (start < 0) return Value(int64_t{0});
        }
        if (start + static_cast<int64_t>(payload.size()) > size) {
            return Value(int64_t{0});  // real refuses to write past the end
        }
        std::memcpy(dst.data() + start, payload.data(), payload.size());
        return Value(int64_t{1});
    });

    // buffer to_buffer(string | buffer | mixed *) and its internal name
    // _to_buffer -- real current-clone f__to_buffer() /
    // svalue_to_buffer_bytes(): a buffer passes through unchanged; a
    // string becomes a buffer of its raw bytes; an array must contain
    // only ints 0..255 and becomes one byte per element, otherwise
    // "Illegal array item in buffer conversion: must be ints 0..255.".
    // Any other argument type errors.
    auto toBufferImpl = [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) {
            throw LpcRuntimeError("to_buffer: expected (string | buffer | int *)");
        }
        if (std::holds_alternative<std::shared_ptr<Buffer>>(args[0].data)) {
            return args[0];
        }
        if (auto* sp = std::get_if<std::string>(&args[0].data)) {
            auto buf = std::make_shared<Buffer>();
            buf->bytes.assign(sp->begin(), sp->end());
            return Value(buf);
        }
        if (auto* ap = std::get_if<std::shared_ptr<Array>>(&args[0].data)) {
            auto buf = std::make_shared<Buffer>();
            if (*ap) {
                buf->bytes.reserve((*ap)->items.size());
                for (const Value& item : (*ap)->items) {
                    auto* n = std::get_if<int64_t>(&item.data);
                    if (!n || *n < 0 || *n > 255) {
                        throw LpcRuntimeError(
                            "Illegal array item in buffer conversion: must be ints 0..255.");
                    }
                    buf->bytes.push_back(static_cast<unsigned char>(*n));
                }
            }
            return Value(buf);
        }
        throw LpcRuntimeError(
            "Cannot convert value to buffer: expected string or array of ints 0..255.");
    };
    t.registerEfun("to_buffer", toBufferImpl);
    t.registerEfun("_to_buffer", toBufferImpl);

    // int clonep(mixed default: this_object()) -- real efuns_main.c's
    // f_clonep(): true unless the argument is the master/blueprint
    // object itself (real "!(ob->flags & O_CLONE)"); a non-object
    // argument is always false (real arg type is T_ANY, not T_OBJECT,
    // confirmed in efun_defs.c). This driver tracks no separate
    // O_CLONE-equivalent flag -- needed none: ObjectManager::loadObject()
    // is the only path that ever registers an object into its own
    // `loaded_[filename]` blueprint slot, and clone_object()/new() never
    // do, so comparing VM::lookupObject(ob->filename()) against ob
    // itself already distinguishes "I am the registered blueprint for my
    // own filename" from "I am not" (a fresh clone, or a blueprint whose
    // own file was never separately load_object()'d) with no new state
    // to track or keep in sync.
    t.registerEfun("clonep", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> ob;
        if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else if (args.empty()) {
            ob = vm.currentObject();
        }
        if (!ob) return Value(static_cast<int64_t>(0));
        auto blueprint = vm.lookupObject(ob->filename());
        bool isClone = (blueprint != ob);
        return Value(static_cast<int64_t>(isClone ? 1 : 0));
    });

    // int virtualp(object default: this_object()) -- real object.h's
    // O_VIRTUAL flag, set only for an object returned by
    // master()->compile_object() rather than compiled directly from an
    // on-disk file (ObjectManager::loadVirtualObject()'s own
    // LpcObject::setIsVirtual(true) call, the only place this flag is
    // ever set). Confirmed real, live-reachable: std/virtual.c's own
    // "if(virtualp(this_object())) return 0;" recursion guard.
    t.registerEfun("virtualp", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> ob;
        if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else if (args.empty()) {
            ob = vm.currentObject();
        }
        if (!ob) return Value(static_cast<int64_t>(0));
        return Value(static_cast<int64_t>(ob->isVirtual() ? 1 : 0));
    });

    // mixed *allocate(int size, void|mixed initial) -- an array of size
    // elements, each set to initial (default int 0, real func_spec.c's
    // own default -- confirmed live: secure/SimulEfun/copy.c's own
    // recursive array copy relies on a freshly allocate()'d array
    // starting at a stable default before each slot is overwritten).
    t.registerEfun("allocate", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("allocate: expected an int size argument");
        }
        int64_t size = std::get<int64_t>(args[0].data);
        if (size < 0) throw LpcRuntimeError("allocate: size must be non-negative");
        Value initial = args.size() > 1 ? args[1] : Value(int64_t{0});
        auto result = std::make_shared<Array>();
        result->items.assign(static_cast<size_t>(size), initial);
        return Value(result);
    });

    // mapping allocate_mapping(int|mixed* size, void|mixed) -- real
    // FluffOS pre-sizes the mapping's hash table with this hint (and,
    // given an array instead of a plain int, keys the new mapping with
    // each of that array's elements -- func_spec.c's "int | mixed *"
    // first-argument type). Nothing on this driver's current path
    // passes an array here (confirmed by grep), so only the plain-int
    // capacity-hint form is implemented, and the hint itself has no
    // observable effect on this driver's Mapping (a plain vector of
    // entries, not a hash table) -- always just an empty mapping.
    t.registerEfun("allocate_mapping", [](VM&, std::vector<Value>&) -> Value {
        return Value(std::make_shared<Mapping>());
    });

    // string file_name(object default: this_object()) -- efuns_main.c's
    // f_file_name(): "add_slash(sp->u.ob->obname)", a leading '/' added
    // if not already present. This driver's LpcObject::filename() is
    // already stored with a leading slash (every path this driver
    // compiles from -- config paths, inherit targets, clone_object()
    // arguments -- is written that way in this mudlib's own source), so
    // no add_slash-equivalent is needed. Real obname also carries a
    // "#<clone id>" suffix distinguishing multiple clones of the same
    // file; this driver's LpcObject has no clone-id concept (see
    // ObjectManager::cloneObject()), so two clones of the same file are
    // not distinguishable through file_name() here -- nothing this
    // driver runs yet depends on telling them apart this way (base_name()
    // strips any "#<id>" suffix right back off again regardless).
    t.registerEfun("file_name", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> ob;
        if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else {
            ob = vm.currentObject();
        }
        if (!ob) return Value{};
        return Value(ob->filename());
    });

    // string base_name(void|object ob default: this_object()) -- real
    // signature confirmed against efun_defs.c ground truth
    // ("base_name",F_BASE_NAME,...,T_STRING|T_OBJECT,...,DEFAULT_THIS_OBJECT)
    // since neither func_spec.c's text nor any .c file in the vendored
    // tree carries an actual f_base_name() body (a real gap in this
    // specific archived copy, same situation as debug_info). Confirmed
    // implementable anyway, not guessed: real base_name() is
    // file_name()'s own real algorithm minus its "#<clone id>" suffix
    // strip (both ultimately reach the same obname string in real
    // FluffOS), and file_name()'s own comment directly above already
    // documents that this driver's LpcObject has no clone-id concept at
    // all, so that strip is already a no-op here -- base_name() and
    // file_name() are therefore identical on this driver, by this
    // driver's own prior, already-verified reasoning, not a fresh
    // assumption. The real string-argument overload (T_STRING|T_OBJECT)
    // is not implemented -- every one of the 95 real call sites in this
    // mudlib passes an object or nothing, confirmed by grep, never a
    // bare path string. By far the single highest-usage gap found in
    // the 2026-08-16 Tier 1 ranking.
    t.registerEfun("base_name", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> ob;
        if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else {
            ob = vm.currentObject();
        }
        if (!ob) return Value{};
        return Value(ob->filename());
    });

    // string *shallow_inherit_list(object default: this_object()) /
    // string *inherit_list(...) -- real efun_defs.c confirms
    // inherit_list is a genuine alias (F_SHALLOW_INHERIT_LIST |
    // F_ALIAS_FLAG, same F_CODE as shallow_inherit_list itself, not a
    // separate implementation). Direct parents only, one level.
    //
    // string *deep_inherit_list(object default: this_object()) -- the
    // full transitive closure, real array.c's own recursive walk.
    //
    // Real output format, read directly from real array.c's own
    // deep_inherit_list()/inherit_list() rather than inferred from any
    // one mudlib's own usage: "ret->item[il].u.string =
    // add_slash(pr->filename);" for every entry -- real add_slash()
    // (interpret.c) unconditionally prepends '/', always. A prior
    // version of this comment claimed "no leading slash" from a
    // different corpus's own "member_array(\"std/armour.c\",
    // deep_inherit_list(ob))"-style call sites -- that claim was never
    // checked against the real driver source directly and was wrong;
    // found live against a real THIRD mudlib corpus (row 3.8's TMI-2
    // boot attempt): adm/simul_efun/overrides.c's own real security-gated
    // "exec" simul_efun override does "member_array(\"/std/body.c\",
    // deep_inherit_list(to_obj)) == -1" -- *with* a leading slash,
    // matching real add_slash() exactly, not that other corpus's own
    // convention. This driver's own prior "strip the leading slash"
    // behavior meant this real, load-bearing security check could never
    // find a match, always denying: every new character's own body
    // object failed "exec()"'s own ownership handoff, so the connection
    // never actually reached the created player object at all (confirmed
    // live: "Error connecting to your body..." on every character
    // creation, cascading into a real call_other-on-a-destructed-object
    // crash moments later in adm/daemons/newuserd.c's own
    // get_real_name()).
    //
    // Real filenames come from each ancestor CompiledProgram's own
    // canonical on-disk path; this driver's CompiledProgram carries no
    // such field of its own (only the *raw*, as-written "inherit ...;"
    // path text, CompiledProgram::inherits, parallel-indexed with the
    // already-resolved CompiledProgram::inheritedPrograms) -- so the
    // best-effort normalization here is: ensure exactly one leading '/'
    // (stripping one first if already present, matching
    // ObjectManager::normalizeFilename()'s own identical "ensure a
    // leading slash" fix, not blindly prepending a second one onto an
    // already-absolute "inherit \"/std/room\";"-style path), append ".c"
    // if not already there. Not a full path-resolution pass (relative
    // "../" segments, include-dir search order) the way
    // ObjectManager::compile()'s own file-loading normalization is --
    // flagged here rather than silently assumed identical.
    auto normalizeInheritPath = [](std::string path) -> std::string {
        if (!path.empty() && path.front() == '/') path.erase(0, 1);
        if (path.size() < 2 || path.substr(path.size() - 2) != ".c") path += ".c";
        return "/" + path;
    };
    auto collectDeepInherits = [normalizeInheritPath](const CompiledProgram& prog, auto&& self,
                                                        std::vector<Value>& out) -> void {
        for (size_t i = 0; i < prog.inherits.size(); ++i) {
            out.push_back(Value(normalizeInheritPath(prog.inherits[i])));
            if (i < prog.inheritedPrograms.size() && prog.inheritedPrograms[i]) {
                self(*prog.inheritedPrograms[i], self, out);
            }
        }
    };
    auto resolveInheritTarget = [](VM& vm, std::vector<Value>& args) -> std::shared_ptr<LpcObject> {
        if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            return std::get<std::shared_ptr<LpcObject>>(args[0].data);
        }
        return vm.currentObject();
    };
    auto shallowInheritListImpl = [resolveInheritTarget, normalizeInheritPath](
                                       VM& vm, std::vector<Value>& args) -> Value {
        auto ob = resolveInheritTarget(vm, args);
        auto result = std::make_shared<Array>();
        if (!ob) return Value(result);
        for (const auto& raw : ob->program().inherits) {
            result->items.push_back(Value(normalizeInheritPath(raw)));
        }
        return Value(result);
    };
    t.registerEfun("shallow_inherit_list", shallowInheritListImpl);
    t.registerEfun("inherit_list", shallowInheritListImpl);
    t.registerEfun("deep_inherit_list", [resolveInheritTarget, collectDeepInherits](
                                             VM& vm, std::vector<Value>& args) -> Value {
        auto ob = resolveInheritTarget(vm, args);
        auto result = std::make_shared<Array>();
        if (!ob) return Value(result);
        collectDeepInherits(ob->program(), collectDeepInherits, result->items);
        return Value(result);
    });

    // string function_exists(string fun, void|object ob, void|int flag)
    // -- real interpret.c's own function_exists(): "similar to apply(),
    // except that it will not call the function, only return object
    // name if the function exists, or 0 otherwise." Confirmed against
    // efuns_main.c's own f_function_exists(): `ob` defaults to
    // current_object when omitted (not this_object() by any different
    // rule -- same default), and the returned string is the *defining*
    // program's own filename with a leading "/" and the trailing ".c"
    // stripped (`res[0] = '/'; strncpy(res + 1, str, l);` where
    // `l = SHARED_STRLEN(str) - 2`) -- i.e. real LPC's own bare object-
    // path convention, exactly what this driver's own file_name() above
    // already returns unmodified.
    //
    // Two real, deliberate simplifications, not fully replicated:
    // (1) the returned path is always `ob`'s own filename, not
    // necessarily the specific ancestor file that actually defines the
    // function when it is inherited-but-not-overridden -- this driver's
    // `CompiledProgram` has no filename of its own to report (only
    // `ObjectManager`'s cache maps a filename to a compiled program, not
    // the reverse), and every one of the 34 real call sites across this
    // mudlib only ever checks `stringp(function_exists(...))` or a bare
    // truthy check, never the actual path value, so this is
    // behaviorally identical for everything confirmed live. (2) the
    // `flag` argument (real: admit protected/private/hidden functions
    // as "existing" only when set) is accepted but ignored --
    // `FunctionEntry` carries no visibility modifier at all, and no
    // real call site in this mudlib ever passes a third argument
    // (confirmed by grep), so the default (flag unset) is the only case
    // ever exercised; the one function name found declared `private`
    // anywhere in this mudlib (`secure/std/post.c`'s own internal
    // `help()`) is never a real target of any `function_exists("help",
    // ob)` check (those all target room items or command files, never
    // post.c), so this narrower-than-real-default visibility handling
    // does not diverge from real output for anything confirmed live.
    t.registerEfun("function_exists", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("function_exists: expected a string function-name argument");
        }
        const std::string& name = std::get<std::string>(args[0].data);
        std::shared_ptr<LpcObject> ob;
        if (args.size() > 1 && std::holds_alternative<std::shared_ptr<LpcObject>>(args[1].data)) {
            ob = std::get<std::shared_ptr<LpcObject>>(args[1].data);
        } else {
            ob = vm.currentObject();
        }
        if (!ob || !vm.functionExists(ob, name)) return Value(static_cast<int64_t>(0));
        return Value(ob->filename());
    });

    // int strsrch(string str, string | int pat, int flag default: 0) --
    // efuns_main.c's f_strsrch() (func_spec.c:125 "int strsrch(string,
    // string | int, int default: 0);", efun_defs.c:251 arg types
    // T_STRING, T_STRING|T_NUMBER, T_NUMBER). Returns the byte offset of
    // pat within str, or -1 if not found. Written 930706 by Luke
    // Mewburn.
    //   - The 2nd argument may be an int single-character code, not just
    //     a string. Real does "buf[0] = (char) sp->u.number", i.e. it
    //     takes the low 8 bits; if that byte is NUL (an int 0, or any
    //     multiple of 256) the needle length is 0.
    //   - The 3rd argument is a DIRECTION FLAG, not a start index. 0
    //     (the default) searches left to right and returns the first
    //     match; any non-zero value (the corpus overwhelmingly passes
    //     -1) searches right to left and returns the LAST match. Real:
    //     strchr/strstr for the forward case, strrchr / a hand-rolled
    //     reverse substring scan for the backward case. This driver's
    //     earlier implementation mistook this argument for a MudOS-style
    //     start index, so "strsrch(path, \"/\", -1)" (get the offset of
    //     the final slash, an extremely common path-splitting idiom)
    //     silently returned -1, and "strsrch(flags, '1')" (int-char
    //     needle) threw. Both shapes are widespread across the vendored
    //     corpora.
    //   - An empty needle, or a needle longer than str, returns -1
    //     ("if (!llen || blen < llen) pos = NULL").
    // strstr is a real alias (func_spec.c:127 "int strstr strsrch(...);")
    // and is registered the same way. Real's strchr/strstr/strrchr stop
    // at an embedded NUL, so str and a string needle are taken up to
    // their first NUL here, the same named local choice string_difference()
    // and replace_html() made.
    auto strsrchImpl = [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args.size() > 3 ||
            !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("strsrch: expected (string str, string|int pat, int flag)");
        }
        std::string big = std::get<std::string>(args[0].data);
        size_t bigNul = big.find('\0');
        if (bigNul != std::string::npos) big.resize(bigNul);

        std::string needle;
        if (const auto* s = std::get_if<std::string>(&args[1].data)) {
            needle = *s;
            size_t nNul = needle.find('\0');
            if (nNul != std::string::npos) needle.resize(nNul);
        } else if (const auto* n = std::get_if<int64_t>(&args[1].data)) {
            char c = static_cast<char>(*n & 0xFF);
            if (c != '\0') needle.assign(1, c);
        } else {
            throw LpcRuntimeError(
                "strsrch: second argument must be a string or an int char code");
        }

        bool fromRight = false;
        if (args.size() == 3) {
            if (!std::holds_alternative<int64_t>(args[2].data)) {
                throw LpcRuntimeError(
                    "strsrch: third argument (direction flag) must be an int");
            }
            fromRight = std::get<int64_t>(args[2].data) != 0;
        }

        if (needle.empty() || big.size() < needle.size()) {
            return Value(int64_t{-1});
        }
        size_t pos = fromRight ? big.rfind(needle) : big.find(needle);
        return Value(pos == std::string::npos ? int64_t{-1} : static_cast<int64_t>(pos));
    };
    t.registerEfun("strsrch", strsrchImpl);
    t.registerEfun("strstr", strsrchImpl);

    // int strcmp(string, string) -- func_spec.c: "int strcmp(string,
    // string);", backed by real efuns_main.c's own f_strcmp(): a plain
    // C strcmp() call, returning C's own negative/zero/positive result
    // (not clamped to -1/0/1). Found live needing this: /secure/daemon/
    // player.c's own sort_list(), called from add_player_info(), called
    // from std/user.c's setup() -- silently swallowed by login.c's
    // catch(__Player->setup()) with no console trace (the same "quiet
    // cascade" shape as the earlier __HistorySize investigation, see
    // STATUS.md), so a fresh player's setup() never actually finished
    // registering itself in whatever online-player list player.c
    // maintains, with nothing on the client side ever showing an error.
    t.registerEfun("strcmp", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 ||
            !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("strcmp: expected (string, string) arguments");
        }
        const std::string& a = std::get<std::string>(args[0].data);
        const std::string& b = std::get<std::string>(args[1].data);
        return Value(static_cast<int64_t>(a.compare(b)));
    });

    // object previous_object(int idx default: 0) and its -1 ("every
    // frame") form -- see VM::previousObject()/allPreviousObjects()'s
    // own comments for the real semantics this reproduces
    // (efuns_main.c's f_previous_object()).
    t.registerEfun("previous_object", [](VM& vm, std::vector<Value>& args) -> Value {
        int64_t idx = 0;
        if (!args.empty() && std::holds_alternative<int64_t>(args[0].data)) {
            idx = std::get<int64_t>(args[0].data);
        }
        if (idx == -1) {
            auto objs = vm.allPreviousObjects();
            auto arr = std::make_shared<Array>();
            for (auto& o : objs) arr->items.emplace_back(o);
            return Value(arr);
        }
        if (idx < 0) return Value{};
        auto ob = vm.previousObject(static_cast<int>(idx));
        if (!ob) return Value{};
        return Value(ob);
    });

    // object *all_previous_objects(void) -- the other spelling
    // func_spec.c declares for the same underlying function ("object
    // *all_previous_objects previous_object(int default: -1);", i.e.
    // this name is real previous_object() called with the -1 flag
    // baked in as its own default, exactly what "idx == -1" already
    // does above). Zero real call sites in this mudlib (confirmed by
    // grep), registered anyway since it costs nothing on top of the
    // already-implemented VM::allPreviousObjects() and func_spec.c
    // declares it as a genuine second name, not a driver invention.
    t.registerEfun("all_previous_objects", [](VM& vm, std::vector<Value>&) -> Value {
        auto objs = vm.allPreviousObjects();
        auto arr = std::make_shared<Array>();
        for (auto& o : objs) arr->items.emplace_back(o);
        return Value(arr);
    });

    // mixed *call_stack(int flag) -- real efuns_main.c's f_call_stack(),
    // confirmed directly (not guessed): flag selects what each frame
    // reports, current frame first (index 0), walking outward. Real
    // modes: 0 = per-frame program filename, 1 = per-frame object, 2 =
    // per-frame function name, 3 = per-frame origin. This driver's own
    // VM::callFrames() (a plain accessor over the same callStack_
    // currentObject() itself reads) only ever tracked *objects* per
    // frame -- confirmed directly, not assumed -- so mode 1 is exactly
    // real, and mode 0 is derived from those same objects' own
    // filenames (this driver has no separate "program" identity from
    // "the object currently running", unlike real FluffOS's own
    // current_prog/csp->prog distinction, but every real call site this
    // mudlib has -- secure/SimulEfun/misc.c's own get_stack() -- only
    // ever indexes call_stack(0)/call_stack(2) results through
    // identify(), which does not care about that distinction for a
    // plain object). Mode 2 has no backing data anywhere in this driver
    // (no per-frame function-name tracking exists). Mode 3 is a
    // separate story since origin() was implemented (VM::originStack_):
    // per-frame origin data now exists, but not in a form mode 3 could
    // safely zip against callStack_ index-for-index -- originStack_ is
    // only ever pushed alongside a real run() call, while callStack_
    // also gets a bookkeeping-only push with no run() at all for a
    // closure that resolves to a core efun (see callClosure()'s own
    // ObjectFrameGuard comment), so the two can differ in length right
    // at that point and a naive same-index pairing would silently
    // misalign. Both modes throw a clear error naming the gap rather
    // than guessing -- get_stack()'s own real use is a wizard debug
    // tool, not gameplay logic, so a hard failure there is an
    // acceptable, honest outcome versus silently returning wrong data.
    t.registerEfun("call_stack", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("call_stack: expected an int argument");
        }
        int64_t mode = std::get<int64_t>(args[0].data);
        if (mode < 0 || mode > 3) {
            throw LpcRuntimeError("call_stack: first argument must be 0, 1, 2, or 3");
        }
        if (mode == 2 || mode == 3) {
            throw LpcRuntimeError(
                "call_stack: mode 2 (function names) and mode 3 (origin) are not "
                "implemented -- this driver's call stack tracks per-frame objects "
                "only, no per-frame function-name or origin tagging exists");
        }
        const auto& frames = vm.callFrames();
        auto result = std::make_shared<Array>();
        result->items.reserve(frames.size());
        // frames.back() is the innermost/current frame (matches
        // currentObject()'s own read) -- reverse-iterate for real
        // call_stack()'s own "current first, walking outward" order.
        for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
            const auto& ob = *it;
            if (mode == 1) {
                result->items.push_back(ob ? Value(ob) : Value{});
            } else {
                result->items.push_back(Value(ob ? ob->filename() : std::string()));
            }
        }
        return Value(result);
    });

    // string origin() -- real efuns_main.c's own f_origin():
    // "push_constant_string(origin_name(caller_type))". Reads
    // VM::currentOrigin(), set by the same real per-call-path tagging
    // VM.hpp's own Origin enum and VM.cpp's own OriginGuard document in
    // full (OpCode::Call's local/simul_efun tiers, OpCode::CallParent,
    // callClosure()'s own tiered resolution, and callFunction()'s own
    // explicit origin parameter, threaded through every real caller of
    // it -- see each one's own citation). The one real call site this
    // row's own six-corpus ranking ever found (secure/daemon/chat.c's
    // own "origin() != ORIGIN_LOCAL" security gate) is exactly what this
    // implementation was verified against most carefully: OpCode::Call's
    // own local-tier branch is the one path that must report "local"
    // correctly and never anything else for an ordinary same-object bare
    // call, confirmed directly against real F_CALL_FUNCTION_BY_ADDRESS.
    t.registerEfun("origin", [](VM& vm, std::vector<Value>&) -> Value {
        return Value(std::string(originName(vm.currentOrigin())));
    });

    // void error(string msg) -- raises a real runtime error carrying
    // msg, catchable by catch() exactly like any other LpcRuntimeError
    // this driver already throws internally (func_spec.c: "void error
    // _error(string);").
    t.registerEfun("error", [](VM&, std::vector<Value>& args) -> Value {
        std::string msg = "error";
        if (!args.empty() && std::holds_alternative<std::string>(args[0].data)) {
            msg = std::get<std::string>(args[0].data);
        }
        throw LpcRuntimeError(msg);
    });

    // void throw(mixed) -- real FluffOS: a real efun (func_spec.c: "void
    // throw(mixed);", not special grammar the way catch() is), taking
    // exactly one argument of any type (efun_defs.c's own min/max arg
    // count for F_THROW is 1/1). Unlike error(), which is always a
    // string, throw() hands the *exact* value given back to the nearest
    // enclosing catch() -- see Value.hpp's own LpcThrownValue comment for
    // the full citation trail and why this is a dedicated exception type
    // rather than a change to LpcRuntimeError itself.
    t.registerEfun("throw", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw LpcRuntimeError("throw: expected exactly one argument");
        }
        throw LpcThrownValue(args[0]);
    });

    // int set_eval_limit(int x) / int reset_eval_cost(int default: 0) /
    // int eval_cost(int default: -1) / int max_eval_cost(int default: 1)
    // -- real efuns_main.c's own f_set_eval_limit(), confirmed directly
    // before rewriting this (an earlier version of this registration got
    // the semantics wrong, unverified against the real switch): all four
    // names share one real 4-way dispatch on the argument value itself,
    // not on which name was used to call it -- func_spec.c's own default-
    // argument-per-alias lines (confirmed: "int reset_eval_cost
    // set_eval_limit(int default: 0);", "int eval_cost set_eval_limit(int
    // default: -1);", "int max_eval_cost set_eval_limit(int default:
    // 1);") only decide what value is substituted when that particular
    // name is called with zero arguments -- calling any of the four with
    // an explicit argument behaves identically. The real switch itself
    // (confirmed directly, not assumed from the names' own reputation):
    //   x == 0:  reset the accumulated cost back to zero (a real full
    //            reset, this is reset_eval_cost()'s own default meaning),
    //            returning the *unchanged* ceiling, not the reset value.
    //   x == -1: pure query of the *remaining* budget (ceiling minus
    //            accumulated cost so far), no mutation at all -- despite
    //            the plausible-sounding assumption an earlier version of
    //            this comment made, real set_eval_limit(-1) does **not**
    //            restore any kind of default ceiling; there is no such
    //            mechanism anywhere in this real function. A mudlib that
    //            wants the old ceiling back has to remember and re-set it
    //            explicitly. (This means master.c's own real "set_eval_
    //            limit(1000000000); ...; set_eval_limit(-1);" bracket,
    //            still the one confirmed live call site for the base
    //            name, genuinely leaves the ceiling permanently raised
    //            after the first player-object compile -- a real quirk of
    //            the reference driver, not a misreading here.)
    //   x == 1:  pure query of the current ceiling itself, no mutation.
    //   anything else: sets the ceiling to x directly, returning x.
    // Guarded in real mudlibs so only master() can call set_eval_limit()
    // at all (secure/SimulEfun/SimulEfun.c's own "if (previous_object()
    // != master()) return;" wrapper) -- this driver applies the change
    // immediately and trusts the mudlib's own guard, the same posture
    // already established for every other master-gated efun with no
    // driver-side permission check of its own.
    auto evalLimitDispatch = [](VM& vm, int64_t x) -> Value {
        switch (x) {
            case 0: {
                int64_t unchanged = vm.maxEvalCost();
                vm.resetEvalCost();
                return Value(unchanged);
            }
            case -1:
                return Value(vm.maxEvalCost() - vm.evalCost());
            case 1:
                return Value(vm.maxEvalCost());
            default:
                vm.setMaxEvalCost(x);
                return Value(x);
        }
    };
    t.registerEfun("set_eval_limit", [evalLimitDispatch](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("set_eval_limit: expected an int argument");
        }
        return evalLimitDispatch(vm, std::get<int64_t>(args[0].data));
    });
    // Real, live call site for reset_eval_cost(): mudlib/command/speed.c's
    // own "START" benchmarking macro ("reset_eval_cost(); set_eval_limit(
    // 0x7fffffff); ..."), which now correctly reads as "zero out whatever
    // cost this dispatch has already accumulated, then separately raise
    // the ceiling to effectively unlimited" -- two real, distinct steps,
    // not one call implying the other.
    t.registerEfun("reset_eval_cost", [evalLimitDispatch](VM& vm, std::vector<Value>& args) -> Value {
        int64_t x = (!args.empty() && std::holds_alternative<int64_t>(args[0].data))
                        ? std::get<int64_t>(args[0].data) : 0;
        return evalLimitDispatch(vm, x);
    });
    t.registerEfun("eval_cost", [evalLimitDispatch](VM& vm, std::vector<Value>& args) -> Value {
        int64_t x = (!args.empty() && std::holds_alternative<int64_t>(args[0].data))
                        ? std::get<int64_t>(args[0].data) : -1;
        return evalLimitDispatch(vm, x);
    });
    t.registerEfun("max_eval_cost", [evalLimitDispatch](VM& vm, std::vector<Value>& args) -> Value {
        int64_t x = (!args.empty() && std::holds_alternative<int64_t>(args[0].data))
                        ? std::get<int64_t>(args[0].data) : 1;
        return evalLimitDispatch(vm, x);
    });

    // void destruct(object ob) -- removes ob from the object table
    // (VM::destructObject(), a thin wrapper over the ObjectManager
    // method that already existed for this). Confirmed live:
    // secure/std/login.c's own internal_remove() ends a failed login
    // attempt with "destruct(this_object())" on the login shell itself
    // -- matching real remove_interactive()'s own end result for a
    // destructed interactive object (nothing left to route further
    // input to). Destructing some other, non-connection-bound object
    // just removes it from the object table, same as real
    // destruct_object() does before backend.c gets around to actually
    // freeing it.
    t.registerEfun("destruct", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            throw LpcRuntimeError("destruct: expected an object argument");
        }
        auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        if (!ob) return Value{};

        // Real destruct_object()'s own "if (ob->flags & O_EFUN_SOCKET)
        // close_referencing_sockets(ob);" (simulate.c) -- confirmed real,
        // and confirmed the one piece this driver's own destructObject()
        // was still missing when reload_object() added the exact same
        // capability for its own real call site (SocketRegistry.hpp's
        // own comment used to flag this specifically). Fires once for
        // ob itself and, via ObjectManager::destructObject()'s own
        // recursively-forwarded callback, once for every object the
        // real shadow-chain cascade destructs along with it too -- see
        // that method's own header comment for why this has to be a
        // callback threaded down from here rather than a direct call
        // inside ObjectManager itself.
        // real "if (ob->pinfo) parse_free(ob->pinfo);" (object.c's own
        // free_object(), called once destructObject()'s own refcount
        // actually drops -- reached from the same callback, not a
        // separate real call site, since this driver's own memory
        // model has no separate free_object() step to hook into
        // directly, see ParserPackage::onObjectDestroyed()'s own
        // comment).
        vm.destructObject(ob, [](const std::shared_ptr<LpcObject>& destructed) {
            SocketRegistry::closeAllOwnedBy(destructed);
            ParserPackage::onObjectDestroyed(destructed);
        });

        // Real destruct_object(): "if (ob->interactive)
        // remove_interactive(ob, 1);" -- always the destructed object's
        // OWN interactive connection, never necessarily the one driving
        // this call. Previously this looked up OutputContext::current()
        // (the caller's own connection) instead of the destructed
        // object's own, and only closed it when the two happened to be
        // the same connection -- so destructing some OTHER, still-
        // connected object's player (an admin "boot"/kick command, one
        // player's own code destructing a different player's object)
        // removed it from InteractiveRegistry (users()/find_player()
        // correctly stopped listing it) but left its actual socket open
        // and still bound to the now-destructed object. Found live-
        // reachable, not theoretical: the O_DESTRUCTED guard added the
        // previous slice then made every one of that connection's own
        // further commands a silent no-op (VM::callFunction()/
        // dispatchCommand() both already refuse a destructed target), so
        // a "kicked" player's own session went inert instead of actually
        // closing -- no disconnect message, no error, nothing dispatched,
        // forever, until they closed the client themselves. Looking the
        // connection up via InteractiveRegistry::find(ob) (the same
        // object-to-Connection* lookup message()/tell_object() already
        // use to reach an arbitrary target) and closing that fixes it;
        // Connection::close() itself does the InteractiveRegistry removal
        // (see its own comment), so this replaces the previous separate,
        // unconditional InteractiveRegistry::remove(ob) call entirely --
        // there is nothing left to remove once close() has run, and
        // nothing to remove at all when ob was never interactive.
        if (Connection* conn = InteractiveRegistry::find(ob)) {
            conn->close();
        }
        return Value{};
    });

    // void reload_object(object ob) -- real object.c's own reload_object():
    // resets ob back to a freshly-cloned-looking state in place (same
    // identity, same shared_ptr) -- every object variable back to a real
    // int 0, then re-initialized (the synthesized "$objvarinit" runs
    // again before create(), confirmed directly against real
    // call_create()'s own body, not just create() alone), every efun
    // socket ob owns force-closed with no close_callback firing, ob's
    // own shadow chain cascade-destructed or spliced out (identical real
    // semantics to destruct()'s own, just never destructing ob itself),
    // its living name removed, its heart_beat disabled, and every
    // pending call_out targeting it removed -- confirmed genuinely real
    // (efun_defs.c's own F_RELOAD_OBJECT) but zero real call sites
    // across all six of this row's mudlib corpora, re-checked fresh
    // rather than assumed unchanged from the previous pass that flagged
    // this efun.
    //
    // Split across two layers by this driver's own existing module
    // boundaries, not real code's own single procedural body: socket-
    // close (real step 2) and heart_beat/call_out removal (real steps
    // 5-6) run here, since `object` cannot depend on `net`/`scheduler`
    // (both already depend on `object`); everything else --
    // variable-zeroing (step 1), shadow-chain handling (step 3),
    // living-name removal (step 4), and the re-init-then-create()
    // sequence (step 9) -- runs inside VM::reloadObject()/
    // ObjectManager::reloadObject() (see its own header comment for the
    // full citation). Running the socket/scheduler cleanup *before*
    // calling into ObjectManager here, rather than real code's own
    // interleaved order, has no observable effect: neither touches
    // object variables, the shadow chain, or living-name state, and
    // both still finish well before create() runs either way. Two real
    // steps have no equivalent and are skipped entirely (see
    // ObjectManager::reloadObject()'s own comment for why): the real
    // light-system total-light decrement (no light system exists at
    // all) and the real euid-from-uid reset (this driver has only a
    // single privs_ field, no separate uid/euid pair).
    t.registerEfun("reload_object", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            throw LpcRuntimeError("reload_object: expected an object argument");
        }
        auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        if (!ob) return Value{};

        SocketRegistry::closeAllOwnedBy(ob);
        if (vm.scheduler()) {
            vm.scheduler()->setHeartbeatInterval(ob, 0);
            vm.scheduler()->removeAllCallOutsForObject(ob);
        } else {
            ob->setHeartbeatInterval(0);
        }

        // Same close-referencing-sockets callback destruct() now passes
        // (see its own registration comment) -- real reload_object()'s
        // own shadow cascade destructs any object that was shadowing ob
        // via the exact same real destruct_object(), which unconditionally
        // closes referencing sockets for whatever it is actually
        // destructing, not just for a bare destruct() call specifically.
        vm.reloadObject(ob, [](const std::shared_ptr<LpcObject>& destructed) {
            SocketRegistry::closeAllOwnedBy(destructed);
            ParserPackage::onObjectDestroyed(destructed);
        });
        return Value{};
    });

    // int remove_interactive(object ob) -- real packages/contrib.c's own
    // f_remove_interactive(): disconnects ob's connection without
    // destructing ob itself -- confirmed directly, the real intended use
    // is exec()ing a fresh connection onto an already-loaded, previously-
    // interactive object for linkdead reconnection, where the object must
    // survive. Returns 0 (a no-op) if ob is destructed or was never
    // interactive; otherwise closes its connection (Connection::close(),
    // the same real disconnect cleanup destruct()'s own connection-
    // closing branch above already reuses) and returns 1.
    t.registerEfun("remove_interactive", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            throw LpcRuntimeError("remove_interactive: expected an object argument");
        }
        auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        if (!ob || ob->isDestructed()) return Value(int64_t{0});
        Connection* conn = InteractiveRegistry::find(ob);
        if (!conn) return Value(int64_t{0});
        conn->close();
        return Value(int64_t{1});
    });

    // object find_object(string path, void|int compile) / object
    // load_object(string path) -- real func_spec.c: "object
    // find_object(string, int default: 0); object load_object
    // find_object(string, int default: 1);" -- the same underlying
    // lookup, just a different default for the second, normally-hidden
    // "compile a miss" argument depending on which name it is called
    // by. A bare find_object(path) therefore only ever looks (see
    // ObjectManager::lookupLoadedObject()'s own comment for why this is
    // a distinct, deliberately non-compiling lookup from VM::findObject()
    // -- call_other()'s own string-target overload, which always
    // compiles a miss, confirmed separately against simulate.c's own
    // find_object() body); load_object(path), or find_object(path, 1),
    // compiles on a miss.
    auto findObjectImpl = [](VM& vm, std::vector<Value>& args, bool compileDefault) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("find_object: expected a string path argument");
        }
        bool compile = compileDefault;
        if (args.size() > 1 && std::holds_alternative<int64_t>(args[1].data)) {
            compile = std::get<int64_t>(args[1].data) != 0;
        }
        const std::string& path = std::get<std::string>(args[0].data);
        auto ob = compile ? vm.findObject(path) : vm.lookupObject(path);
        if (!ob) return Value{};
        return Value(ob);
    };
    t.registerEfun("find_object", [findObjectImpl](VM& vm, std::vector<Value>& args) -> Value {
        return findObjectImpl(vm, args, false);
    });
    t.registerEfun("load_object", [findObjectImpl](VM& vm, std::vector<Value>& args) -> Value {
        return findObjectImpl(vm, args, true);
    });

    // ------------------------------------------------------------------
    // add_action()/enable_commands() command dispatch subsystem.
    // Confirmed against fluffos-2.9-ds2.08/add_action.c directly (not
    // guessed) and against real usage across this mudlib -- see
    // STATUS.md's own "add_action/enable_commands command dispatch"
    // section for the full recon/design writeup and citations.
    // ------------------------------------------------------------------

    // object environment(void | object) -- real func_spec.cpp signature.
    t.registerEfun("environment", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> target;
        if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            target = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else {
            target = vm.currentObject();
        }
        if (!target) return Value{};
        auto env = target->environment().lock();
        if (!env) return Value{};
        return Value(env);
    });

    // void set_environment(object item, object|void env) -- LDMud-only
    // (real func_spec, `void set_environment(object, object|void);`;
    // real f_set_environment(), object.c:5152-5230). ROADMAP.md row
    // 1.7/1.8's own trigger-point investigation: this is the one real
    // primitive secure/master/hooks.c's own moveHook() actually calls
    // to perform the move itself ("This efun is to be used in the
    // H_MOVE_OBJECTx hook, as it does nothing else than moving the item
    // -- no calls to init() or such", object.c:5159-5161) -- without it,
    // hooks.c's own real H_MOVE_OBJECT0 body cannot run verbatim at all,
    // which is why this driver did not have it until this slice
    // (nothing before now ever needed the low-level move primitive
    // separated from move_object()'s own init()-calling legs). Pure
    // reparenting, deliberately not replicating real code's own shadow-
    // object rejection ("Can't move an object that is shadowing.") or
    // recursive-move rejection ("Can't move object inside itself.") --
    // neither is reachable from hooks.c's own real call shape, and this
    // driver's own pre-existing moveObject() has never enforced either
    // one for the FluffOS-style fallback path either, so adding it only
    // here would be a new, uneven restriction rather than a faithful
    // port. A void/omitted env unlinks item with no new environment,
    // matching real "env may be 0".
    t.registerEfun("set_environment", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("set_environment() requires at least 1 argument");
        auto* itemPtr = std::get_if<std::shared_ptr<LpcObject>>(&args[0].data);
        if (!itemPtr || !*itemPtr) throw LpcRuntimeError("Bad argument 1 to set_environment()");
        auto item = *itemPtr;

        std::shared_ptr<LpcObject> dest;
        if (args.size() >= 2 && !args[1].isVoid()) {
            auto* destPtr = std::get_if<std::shared_ptr<LpcObject>>(&args[1].data);
            if (!destPtr || !*destPtr) throw LpcRuntimeError("Bad argument 2 to set_environment()");
            dest = *destPtr;
        }

        // real object.c:5188-5198's own three O_RESET_STATE clears --
        // dest, item ("touch it", the real inline comment), and item's
        // old super, all cleared as part of this same real primitive
        // (confirmed directly, the exact real lines this efun's own
        // header comment already cites for the move itself). See
        // LpcObject::resetState()'s own header comment for what this
        // backs (Scheduler::tickResetsAndCleanup()'s real-vs-virtual
        // reset decision).
        if (dest) dest->setResetState(false);
        if (auto oldEnv = item->environment().lock()) {
            auto& oldInv = oldEnv->inventory();
            oldInv.erase(std::remove(oldInv.begin(), oldInv.end(), item), oldInv.end());
            oldEnv->setResetState(false);
        }
        item->setResetState(false);
        item->setEnvironment(dest);
        if (dest) dest->inventory().push_back(item);
        return Value{};
    });

    // object *all_inventory(object default: this_object()) -- real
    // func_spec.c signature. Confirmed against fluffos-2.9-ds2.08's own
    // array.c f_all_inventory()/all_inventory(): walks the target's
    // direct-children linked list (ob->contains/next_inv there) and
    // returns them as a plain array, in insertion order, no recursion.
    // This driver already tracks the same relationship directly as
    // LpcObject::inventory_ (populated by VM::moveObject(), the same
    // list environment() above already reads the reverse edge of), so
    // no new bookkeeping was needed. Found live needing this: std/
    // clean_up.c's own remove() (all(): "i = sizeof(inv =
    // all_inventory(this_object())); while(i--) if(inv[i]) inv[i]->
    // move(env);"), reached from secure/std/login.c's new_user() when a
    // player declines the "Confirm <name> ... (y/n)" prompt -- __Player
    // was already speculatively created via player_object() before the
    // confirmation (see login.c's own comment on why), so declining has
    // to clean it back up via a real remove() call, which had never
    // been exercised live before this path.
    t.registerEfun("all_inventory", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> target;
        if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            target = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else {
            target = vm.currentObject();
        }
        auto result = std::make_shared<Array>();
        if (target) {
            for (auto& item : target->inventory()) {
                if (item) result->items.push_back(Value(item));
            }
        }
        return Value(result);
    });

    // object *deep_inventory(object default: this_object()) -- same
    // reference source (array.c's deep_inventory_count()/
    // deep_inventory_collect()): all_inventory()'s direct children, plus
    // every one of their own children recursively, depth-first, target
    // itself never included. Found live needing this the same pass as
    // all_inventory() above: std/clean_up.c's own clean_up() (unlike
    // remove(), not yet confirmed reached live, but the same file, same
    // gap category, and trivial to add alongside).
    t.registerEfun("deep_inventory", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> target;
        if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            target = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else {
            target = vm.currentObject();
        }
        auto result = std::make_shared<Array>();
        std::function<void(const std::shared_ptr<LpcObject>&)> collect =
            [&](const std::shared_ptr<LpcObject>& ob) {
                if (!ob) return;
                for (auto& item : ob->inventory()) {
                    if (!item) continue;
                    result->items.push_back(Value(item));
                    collect(item);
                }
            };
        collect(target);
        return Value(result);
    });

    // object first_inventory(object | string default: this_object()) --
    // func_spec.c: "object first_inventory(object | string default:
    // F__THIS_OBJECT);". Confirmed against fluffos-2.9-ds2.08's own
    // simulate.c first_inventory(): a string argument is resolved via
    // find_object() and, if the resolved container is itself hidden and
    // not visible to the caller, treated as not found (real
    // "bad_argument" -- this driver throws instead, matching the
    // existing convention other object-or-string efuns already use, see
    // call_other()'s own citation above); an object argument is used
    // directly with no such check (a real, confirmed asymmetry in the
    // reference source, not an oversight here). The result is the
    // container's first child, skipping forward past any hidden entries
    // the caller cannot see (isVisibleToObserver() above, only real
    // because F_SET_HIDE is defined) -- 0 if the container has none.
    // Real container->contains is a real doubly-linked list with newest-
    // arrival prepended to the front (simulate.c's own move_object():
    // "item->next_inv = dest->contains; dest->contains = item;"); this
    // driver's own LpcObject::inventory_ is a plain vector that
    // VM::moveObject() appends to instead (oldest-arrival first) -- a
    // pre-existing divergence in moveObject() itself, not introduced
    // here, but one this efun is the first to make LPC-observable:
    // first_inventory() on this driver returns the *oldest* occupant,
    // not the most recently arrived one real FluffOS would return.
    t.registerEfun("first_inventory", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> container;
        if (args.empty()) {
            container = vm.currentObject();
        } else if (std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            container = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else if (std::holds_alternative<std::string>(args[0].data)) {
            container = vm.findObject(std::get<std::string>(args[0].data));
            if (container && !isVisibleToObserver(vm, container)) container = nullptr;
        } else {
            throw LpcRuntimeError("first_inventory: expected an object or string first argument");
        }
        if (!container) throw LpcRuntimeError("first_inventory: bad argument 1");
        for (auto& child : container->inventory()) {
            if (isVisibleToObserver(vm, child)) return Value(child);
        }
        return Value{};
    });

    // object next_inventory(object default: this_object()) --
    // func_spec.c: "object next_inventory(object default:
    // F__THIS_OBJECT);". Confirmed against efuns_main.c's own
    // f_next_inventory(): "ob = sp->u.ob->next_inv;" -- the *next
    // sibling* in ob's own environment's inventory list, not "ob's own
    // first child" (that's first_inventory() above; the two efuns walk
    // different edges of the same tree, real FluffOS's own contains/
    // next_inv pair). Same hidden-skip loop as first_inventory() above.
    // No string-argument form in the real signature, unlike
    // first_inventory() -- confirmed directly against func_spec.c, not
    // assumed symmetric. Implemented as an O(n) scan of ob's
    // environment's inventory vector for ob's own position plus one,
    // rather than a stored sibling pointer -- this driver already made
    // the same simplification for environment()/inventory() generally
    // (see LpcObject.hpp's own comment on why a plain vector was chosen
    // over an intrusive linked list), and nothing confirmed live needs
    // better than O(n) here. Same real ordering divergence as
    // first_inventory() above: this driver's vector walks oldest-to-
    // newest arrival order, the reverse of real FluffOS's next_inv
    // chain.
    t.registerEfun("next_inventory", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> ob = !args.empty() &&
                std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)
            ? std::get<std::shared_ptr<LpcObject>>(args[0].data)
            : vm.currentObject();
        if (!ob) return Value{};
        auto env = ob->environment().lock();
        if (!env) return Value{};
        auto& siblings = env->inventory();
        auto it = std::find(siblings.begin(), siblings.end(), ob);
        if (it == siblings.end()) return Value{};
        for (++it; it != siblings.end(); ++it) {
            if (isVisibleToObserver(vm, *it)) return Value(*it);
        }
        return Value{};
    });

    // object present(object | string, void | object) -- func_spec.c:
    // "object present(object | string, void | object);". Confirmed
    // against fluffos-2.9-ds2.08's own simulate.c object_present()/
    // object_present2():
    //  - object form: with an explicit container, true only when the
    //    given object's environment is exactly that container; with no
    //    container, also true when the given object is a *sibling* of
    //    current_object() (same environment).
    //  - string form: searches the container's direct inventory (current
    //    object's own inventory when none given) for an item whose
    //    id(str) apply returns truthy, matching this mudlib's own
    //    std/Object.c id() convention (every present()-checked object
    //    defines it) and CLAUDE.md's own documented idiom ("reset()
    //    with present(\"id\", this_object()) checks"). With no explicit
    //    container, real present() also falls back to searching the
    //    calling object's own environment's inventory (a sibling
    //    search) when the direct search misses -- reproduced here via
    //    the same searchIn() helper.
    // Not implemented: the numbered-suffix form ("sword 2", real
    // object_present2()'s count-skip logic) -- not confirmed needed by
    // any call site reached live yet, all real usage found so far is a
    // plain unnumbered id string. A missing id() function on a
    // candidate is not an error (VM::callFunction() already returns a
    // falsy monostate for that, same as a real failed apply()), so it
    // is silently skipped rather than treated as a match.
    // Found live blocking domains/ChiTown/areas/chitown_start.c's own
    // reset() -- the very first starting room a fresh character reaches
    // after finish_creation() -- via exactly the present("id", this_object())
    // pattern CLAUDE.md's rule 11 documents.
    t.registerEfun("present", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty()) {
            throw LpcRuntimeError("present: expected (object|string, void|object) arguments");
        }
        bool explicitContainer = args.size() > 1 &&
            std::holds_alternative<std::shared_ptr<LpcObject>>(args[1].data);
        std::shared_ptr<LpcObject> container =
            explicitContainer ? std::get<std::shared_ptr<LpcObject>>(args[1].data) : vm.currentObject();
        if (!container) return Value{};

        if (std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            auto target = std::get<std::shared_ptr<LpcObject>>(args[0].data);
            if (!target) return Value{};
            auto env = target->environment().lock();
            if (env == container) return Value(target);
            if (!explicitContainer) {
                auto containerEnv = container->environment().lock();
                if (containerEnv && env == containerEnv) return Value(target);
            }
            return Value{};
        }

        if (!std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("present: expected (object|string, void|object) arguments");
        }
        const std::string& idStr = std::get<std::string>(args[0].data);
        auto searchIn = [&](const std::shared_ptr<LpcObject>& c) -> std::shared_ptr<LpcObject> {
            if (!c) return nullptr;
            for (auto& item : c->inventory()) {
                if (!item) continue;
                if (isTruthy(vm.callFunction(item, "id", {Value(idStr)}))) return item;
            }
            return nullptr;
        };
        if (auto found = searchIn(container)) return Value(found);
        if (!explicitContainer) {
            if (auto found = searchIn(container->environment().lock())) return Value(found);
        }
        return Value{};
    });

    // void move_object(object | string dest) -- moves current_object.
    // The string-path overload resolves the same way real move_object()
    // does (find_object(), which auto-compiles on a miss -- confirmed
    // against simulate.c's own find_object() body, see the find_object
    // efun's own comment just above), matching std/Object.c's own
    // move()'s "if(!(ob = find_object(dest))) ..." fallback exactly.
    t.registerEfun("move_object", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("move_object: expected a destination argument");
        auto item = vm.currentObject();
        if (!item) return Value{};
        std::shared_ptr<LpcObject> dest;
        if (std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            dest = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else if (std::holds_alternative<std::string>(args[0].data)) {
            dest = vm.findObject(std::get<std::string>(args[0].data));
        }
        if (!dest) throw LpcRuntimeError("move_object: destination not found");
        vm.moveObject(item, dest);
        return Value{};
    });

    // void enable_commands() / void disable_commands() -- always act on
    // current_object (real semantics: neither takes an argument).
    t.registerEfun("enable_commands", [](VM& vm, std::vector<Value>&) -> Value {
        if (auto ob = vm.currentObject()) ob->setCommandsEnabled(true);
        return Value{};
    });
    t.registerEfun("disable_commands", [](VM& vm, std::vector<Value>&) -> Value {
        if (auto ob = vm.currentObject()) ob->setCommandsEnabled(false);
        return Value{};
    });

    // void set_hide(int) -- func_spec.c: "void set_hide(int);". Confirmed
    // directly against fluffos-2.9-ds2.08/efuns_main.c's own f_set_hide():
    // "if (!valid_hide(current_object)) { sp--; return; }" gates the
    // whole call on a master apply first -- this is a real permission
    // check, not a bare flag setter. Uses VM::masterObject() +
    // VM::callFunction() (not VM::applyMaster(), which throws if no
    // master is loaded at all) so a master that simply does not define
    // valid_hide() -- callFunction() already returns a falsy Value{} for
    // an apply the target has no function for, matching real FluffOS's
    // own apply_master_ob() "not defined" == failure convention -- or no
    // master loaded at all (a boot-time-only scenario in a real driver)
    // both correctly decline rather than throw, same as real FluffOS
    // silently doing nothing when valid_hide() rejects the request. Real
    // f_set_hide() also updates two global counters (num_hidden,
    // num_hidden_users) nothing in this driver reads yet -- not
    // replicated, see LpcObject::setHidden()'s own comment for why.
    // Picked from the efun-coverage audit's Tier 1 quick-win list
    // (docs/source-audits/efun-coverage.md): real call sites include
    // std/Object.c's own hide(x) wrapper (never shadowed locally, unlike
    // the set_light false-positive from a prior session) and two direct
    // calls in std/user.c.
    t.registerEfun("set_hide", [](VM& vm, std::vector<Value>& args) -> Value {
        auto ob = vm.currentObject();
        if (!ob) return Value{};
        auto master = vm.masterObject();
        bool permitted = master && isTruthy(vm.callFunction(master, "valid_hide", {Value(ob)}));
        if (!permitted) return Value{};
        ob->setHidden(!args.empty() && isTruthy(args[0]));
        return Value{};
    });

    // void enable_wizard(void) / disable_wizard(void) / int wizardp(object)
    // -- current FluffOS's own real, genuinely new-since-2.9 efuns
    // (confirmed absent from temp/reference/fluffos-2.9-ds2.08 entirely:
    // no enable_wizard/disable_wizard/wizardp anywhere in that tree).
    // Signatures confirmed directly against real current source:
    // src/packages/core/core.spec's own "void enable_wizard(); void
    // disable_wizard(); int wizardp(object);" (all three under the
    // same #ifndef NO_WIZARDS block). Real f_enable_wizard()/
    // f_disable_wizard() (efuns_main.cc), fetched live: both take zero
    // arguments and always target current_object -- "if
    // (current_object->interactive) { current_object->flags |=
    // O_IS_WIZARD; }" / "&= ~O_IS_WIZARD" -- silently doing nothing for
    // a non-interactive caller, not throwing. This driver's own
    // equivalent of "is current_object interactive right now" is
    // InteractiveRegistry membership (the same live check interactive()
    // above already uses, not the sticky everInteractive_ flag
    // userp()/query_once_interactive() use instead -- see LpcObject.hpp's
    // own wasEverInteractive() comment for that distinction). Real
    // f_wizardp() (efuns_main.cc): "i = sp->u.ob->flags & O_IS_WIZARD;
    // put_number(i != 0);" -- a plain flag read on an explicit object
    // argument, no interactive requirement of its own. Real
    // enable_wizard() also grants restricted-ed access and trace()/
    // traceprefix() privilege, and real error_handler() reads this same
    // flag to decide a wizard's own full-trace error message versus
    // DEFAULT_ERROR_MESSAGE for an ordinary player (real doc,
    // docs/efun/mudlib/enable_wizard.md, fetched live) -- none of that
    // consumes the flag here yet, matching LpcObject::isWizard()'s own
    // comment and this session's deliberately narrow scope (the flag
    // itself, not every real behavior it gates).
    t.registerEfun("enable_wizard", [](VM& vm, std::vector<Value>&) -> Value {
        auto ob = vm.currentObject();
        if (!ob) return Value{};
        for (auto& live : InteractiveRegistry::all()) {
            if (live == ob) { ob->setWizard(true); break; }
        }
        return Value{};
    });
    t.registerEfun("disable_wizard", [](VM& vm, std::vector<Value>&) -> Value {
        auto ob = vm.currentObject();
        if (!ob) return Value{};
        for (auto& live : InteractiveRegistry::all()) {
            if (live == ob) { ob->setWizard(false); break; }
        }
        return Value{};
    });
    t.registerEfun("wizardp", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            return Value(int64_t{0});
        }
        auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        if (!ob) return Value(int64_t{0});
        return Value(static_cast<int64_t>(ob->isWizard() ? 1 : 0));
    });

    // int living(object ob default: this_object()) -- func_spec.c: "int
    // living(object default: F__THIS_OBJECT);". Real semantics
    // (add_action.c's f_living(): "if (sp->u.ob->flags & O_ENABLE_COMMANDS)
    // ... *sp = const1 ... else *sp = const0") are exactly whether
    // enable_commands() has been called on the object, nothing more --
    // backed directly by the same commandsEnabled_ flag the
    // enable_commands()/disable_commands() pair above already maintains.
    // Confirmed live needed: std/Object.c's own move() gates
    // move_object() behind "living(this_object()) && living(ob)" to
    // block one living thing from moving directly into another (the
    // "mountable" exception aside).
    // real func_spec.c: "int living(object default: this_object());" --
    // DEFAULT_THIS_OBJECT (efun_defs.c:110) only ever fires when the real
    // *call site* itself omits the argument; real f_living() (add_action.c:
    // 687-695) then unconditionally dereferences whatever single svalue
    // is on the stack as an object, trusting the compiler's own static
    // T_OBJECT arg-type check. This driver's own equivalent of "the call
    // site omitted the argument" is args.empty(), not "args[0] is present
    // but happens not to hold an object" -- those are different real LPC
    // situations (the second is an explicit, on-purpose `living(attackers
    // [i])` where attackers[i]'s *current runtime value* just happens to
    // be a plain int 0, the everyday "no object here" idiom for an
    // `object` typed slot), and conflating them here previously meant
    // `living(<falsy non-object value>)` silently answered for
    // this_object() instead, live-confirmed via TMI-2's own real
    // std/user.c::clean_up_attackers() ("if (attackers[i] == 0 ||
    // !living(attackers[i])) continue;" -- a stale, save/restore-nulled
    // attackers[] entry wrongly reported the *player's own* living()
    // status instead of correctly reporting false for the absent
    // attacker, masking the real bug just above it, see restore_object's
    // own "N" -> Value(int64_t{0}) fix's citation for the root cause).
    t.registerEfun("living", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> target;
        if (args.empty()) {
            target = vm.currentObject();
        } else if (std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            target = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        }
        return Value(int64_t{target && target->commandsEnabled() ? 1 : 0});
    });

    // object shadow(object ob, int flag default: 1) -- Phase 0.6.
    // Confirmed against fluffos-2.9-ds2.08's own f_shadow() and
    // validate_shadowing() (efuns_main.c / interpret.c) directly, not
    // instruct.md's own simplified description.
    //
    // flag == 0 is a pure query, not an attach: returns whoever is
    // currently shadowing ob (real "ob = ob->shadowed"), or 0. This is
    // the ONLY real, live-reachable shape of shadow() in this mudlib
    // (cmds/creator/_scan.c's own "shadow(ob, 0)", confirmed by grep;
    // its one real attach-mode caller is dead code, guarded behind
    // "#if HAS_SHADOWS", a macro never defined anywhere in this mudlib's
    // own config or in the real fluffos-2.9-ds2.08 source it was ported
    // from). There is no separate "query_shadowed()" efun in real
    // FluffOS at all, despite that being a natural-sounding name --
    // shadow(ob, 0) is the real mechanism, confirmed by that same real
    // call site.
    //
    // flag != 0 (attach mode) real validation, all confirmed directly
    // against validate_shadowing(): current_object cannot already be
    // shadowing something ("Already shadowing"), cannot itself already
    // be shadowed ("Can't shadow when shadowed"), cannot be inside an
    // environment ("must not reside inside another object"), ob cannot
    // be the master object, ob cannot itself already be shadowing
    // something ("Can't shadow a shadow"), and finally master's own
    // valid_shadow(ob) apply must approve -- reusing this driver's own
    // already-established "master && isTruthy(callFunction(master,
    // "valid_X", ...))" pattern (see set_hide's identical valid_hide()
    // gate just above). Confirmed this mudlib's own real master.c never
    // defines valid_shadow() at all, so against this exact mudlib,
    // attach-mode shadow() is correctly, always denied by default --
    // matching real semantics precisely, not a driver gap: real
    // MASTER_APPROVED() on an apply the master does not define is
    // exactly as falsy as this driver's own callFunction() returning
    // Value{} for the same case.
    //
    // Not implemented: check_shadow_functions()'s "nomask" conflict
    // check -- this driver's compiler has no nomask modifier concept at
    // all (same category of gap as the class/buffer type gaps already
    // documented elsewhere), so nothing could ever trigger it anyway.
    // On success, the new shadow is inserted at the END of any existing
    // chain on ob (real "while (ob->shadowed) ob = ob->shadowed;" before
    // the assignment), matching real multi-shadow stacking order.
    //
    // ROADMAP row 1.5 (rescoped): real LDMud `shadow()` is a genuinely
    // different efun, not just a renamed one -- confirmed directly
    // against `temp/ldmud/src/func_spec` ("int shadow(object)
    // no_lightweight;", one argument, no flag at all) and
    // `temp/ldmud/src/simulate.c`'s own `f_shadow()`/`validate_shadowing()`
    // (LDMud 3.6.8, this project's vendored clone). Three real
    // divergences from the FluffOS shape just above: (1) always attach
    // mode -- there is no query flag, LDMud's own separate query efun
    // (`query_shadowing()`) is obsolete in this exact 3.6.8 clone
    // (`temp/ldmud/doc/obsolete/query_shadowing`, moved out of
    // `doc/efun` entirely -- so nothing to add for it here); (2) returns
    // int 1/0, never the shadowed object; (3) the master apply is
    // `query_allow_shadow()`, not `valid_shadow()`
    // (`temp/ldmud/doc/master/query_allow_shadow`). One more genuine
    // driver-level asymmetry, confirmed by reading `validate_shadowing()`
    // in both real sources side by side: FluffOS's own version has an
    // explicit "cannot shadow the master object" guard
    // (`fluffos-2.9-ds2.08/interpret.c`'s "ob == master_ob" check,
    // already ported below in the shared FluffOS path); LDMud's
    // `validate_shadowing()` has no such check at the driver level at
    // all -- master-object protection there is purely an *advisory*
    // mudlib convention inside `query_allow_shadow()`'s own
    // implementation ("should deny shadowing on all root objects", its
    // own doc's wording), not a driver mechanism, so nothing to port for
    // the LDMud branch here. Similarly, the "victim-side opt-out"
    // instruct.md previously described as a second driver-enforced
    // layer is not real either: `validate_shadowing()` only ever calls
    // one apply, `query_allow_shadow()`; whether that apply's own LPC
    // body chooses to call something like `victim->prevent_shadow(...)`
    // is entirely up to the master file (and even LDMud's own two doc
    // files disagree on the conventional name -- `doc/efun/shadow` says
    // `query_prevent_shadow()`, `doc/master/query_allow_shadow` says
    // `prevent_shadow()` -- further evidence it is prose convention, not
    // driver-enforced grammar). Gated on `vm.config().dialect() ==
    // "ldmud"` exactly like `replace_program()`'s own dialect branch.
    t.registerEfun("shadow", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            throw LpcRuntimeError("shadow: expected an object as first argument");
        }
        auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        bool ldmud = vm.config().dialect() == "ldmud";
        if (!ob || ob->isDestructed()) return ldmud ? Value(int64_t{0}) : Value{};

        if (ldmud) {
            auto self = vm.currentObject();
            if (!self) return Value(int64_t{0});
            if (ob == self) {
                throw LpcRuntimeError("shadow: Can't shadow self.");
            }
            if (self->shadowing().lock()) {
                throw LpcRuntimeError("shadow: Already shadowing.");
            }
            if (self->shadowedBy().lock()) {
                throw LpcRuntimeError("shadow: Can't shadow when shadowed.");
            }
            if (self->environment().lock()) {
                throw LpcRuntimeError(
                    "shadow: The shadow must not reside inside another object.");
            }
            if (ob->shadowing().lock()) {
                throw LpcRuntimeError("shadow: Can't shadow a shadow.");
            }
            auto master = vm.masterObject();
            bool approved =
                master && isTruthy(vm.callFunction(master, "query_allow_shadow", {Value(ob)}));
            if (!approved) return Value(int64_t{0});
            if (self->isDestructed()) return Value(int64_t{0});

            auto victim = ob;
            while (auto next = victim->shadowedBy().lock()) victim = next;
            self->setShadowing(victim);
            victim->setShadowedBy(self);
            return Value(int64_t{1});
        }

        bool attach = args.size() < 2 || !std::holds_alternative<int64_t>(args[1].data) ||
                      std::get<int64_t>(args[1].data) != 0;
        if (!attach) {
            auto shadow = ob->shadowedBy().lock();
            return shadow ? Value(shadow) : Value{};
        }

        auto self = vm.currentObject();
        if (!self) return Value{};
        if (ob == self) {
            throw LpcRuntimeError("shadow: Can't shadow self");
        }
        if (self->shadowing().lock()) {
            throw LpcRuntimeError("shadow: Already shadowing.");
        }
        if (self->shadowedBy().lock()) {
            throw LpcRuntimeError("shadow: Can't shadow when shadowed.");
        }
        if (self->environment().lock()) {
            throw LpcRuntimeError("shadow: The shadow must not reside inside another object.");
        }
        auto master = vm.masterObject();
        if (ob == master) {
            throw LpcRuntimeError("shadow: cannot shadow the master object.");
        }
        if (ob->shadowing().lock()) {
            throw LpcRuntimeError("shadow: Can't shadow a shadow.");
        }
        bool approved = master && isTruthy(vm.callFunction(master, "valid_shadow", {Value(ob)}));
        if (!approved) return Value{};
        // Real f_shadow(): this second check happens only after
        // validate_shadowing() (the block above) already approved,
        // matching the real source's own two-step order exactly (a
        // master valid_shadow() apply that destructs current_object as
        // a side effect is the only way this can differ from the
        // pre-check above).
        if (self->isDestructed()) return Value{};

        auto victim = ob;
        while (auto next = victim->shadowedBy().lock()) victim = next;
        self->setShadowing(victim);
        victim->setShadowedBy(self);
        return Value(victim);
    });

    // object query_shadowing(object ob default: this_object()) -- the
    // inverse direction of shadow(ob, 0): confirmed against efuns_main.c's
    // own f_query_shadowing() ("ob->shadowing"), returns whoever ob
    // itself is shadowing, or 0. Zero real call sites in this mudlib
    // (confirmed by grep), registered anyway per this row's own task
    // list -- shadow() and query_shadowing() are the real, complete
    // FluffOS pair (func_spec.c: "object shadow(object, int default: 1);
    // object query_shadowing(object);"), not a driver invention.
    t.registerEfun("query_shadowing", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> ob;
        if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else {
            ob = vm.currentObject();
        }
        if (!ob) return Value{};
        auto target = ob->shadowing().lock();
        return target ? Value(target) : Value{};
    });

    // int remove_shadow(void|object ob default: this_object()) -- real
    // packages/contrib.c's own f_remove_shadow(), confirmed directly:
    // splices ob out of whatever shadow chain it is part of (whether ob
    // is itself a shadow, or is being shadowed, or both), the exact same
    // "reconnect its neighbors to each other directly" unlink already
    // implemented for ObjectManager::destructObject()'s own non-cascade
    // shadow-splice branch -- reused here without the destruct, since
    // real remove_shadow() only breaks the link, it never destructs
    // anything. Returns 1 on success, 0 if ob is null or was not part of
    // any shadow relationship at all (real "ob->shadowing == 0 &&
    // ob->shadowed == 0" guard).
    t.registerEfun("remove_shadow", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> ob;
        if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else {
            ob = vm.currentObject();
        }
        auto shadowedBy = ob ? ob->shadowedBy().lock() : nullptr;
        auto shadowing = ob ? ob->shadowing().lock() : nullptr;
        if (!ob || (!shadowedBy && !shadowing)) return Value(int64_t{0});

        if (shadowing) shadowing->setShadowedBy(shadowedBy);
        if (shadowedBy) shadowedBy->setShadowing(shadowing);
        ob->setShadowing(std::weak_ptr<LpcObject>());
        ob->setShadowedBy(std::weak_ptr<LpcObject>());
        return Value(int64_t{1});
    });

    // void unshadow(void) -- ROADMAP row 1.5's LDMud shadow work.
    // Real LDMud-only efun, no FluffOS equivalent at all (grepped the
    // full vendored fluffos-2.9-ds2.08 tree for "unshadow", zero hits).
    // Confirmed against `temp/ldmud/src/simulate.c`'s own `f_unshadow()`
    // (3.6.8): the guard is `current_object.u.ob->flags & O_SHADOW &&
    // shadowing != NULL` -- this only fires when current_object is
    // itself shadowing something. When it fires, it reconnects that
    // victim directly to whoever was shadowing current_object (real
    // "our victim is now shadowed by our shadow"), then clears both of
    // current_object's own links. A real, subtle asymmetry confirmed by
    // reading the C directly, not inferred from the doc's own looser
    // prose ("if the calling object is being shadowed, that is also
    // stopped"): if current_object is ONLY being shadowed by someone
    // else and is not itself shadowing anything, the guard never fires
    // and unshadow() is a genuine no-op -- an object cannot force
    // itself out from under its own shadow this way. No dialect gate on
    // *availability* here (unlike `shadow()`'s branch above, which
    // changes a name both dialects share) -- this table has no existing
    // precedent for withholding an efun's existence by dialect, only
    // for branching an existing shared name's behavior, so this is
    // simply registered like every other efun in the table.
    t.registerEfun("unshadow", [](VM& vm, std::vector<Value>&) -> Value {
        auto self = vm.currentObject();
        if (!self) return Value{};
        auto victim = self->shadowing().lock();
        if (!victim) return Value{};

        auto shadower = self->shadowedBy().lock();
        victim->setShadowedBy(shadower);
        if (shadower) shadower->setShadowing(victim);

        self->setShadowing(std::weak_ptr<LpcObject>());
        self->setShadowedBy(std::weak_ptr<LpcObject>());
        return Value{};
    });

    // void replace_program(string name) -- real replace_program.c's own
    // f_replace_program(): swaps current_object's own program to one of
    // its own inherited programs (a memory-saving optimization in real
    // FluffOS -- many otherwise-identical objects sharing one compiled
    // program instead of each keeping their full own copy; this driver's
    // shared_ptr-managed CompiledProgram already shares structurally, so
    // there is no memory motive here, implemented anyway for the real,
    // observable effect on this object's own future function/variable
    // resolution). Real call sites are genuine and concentrated:
    // es2_mudlib/mudlib/d/*'s own "replace_program(ROOM)"/
    // "replace_program(BANK)" idiom, lightweight domain-area objects
    // that just want a shared base program's exact behavior.
    //
    // Deferred, never applied immediately -- matching real code's own
    // reasoning exactly (replace_program.c's own comment: doing it
    // mid-execution "could result" in volatile data structures were the
    // master consulted for a shadow check right then). See
    // VM::processPendingReplacePrograms() for where the actual swap
    // happens (once per outer driver tick, Scheduler::run()'s own
    // for(;;) loop, matching real remove_destructed_objects()'s own call
    // site in backend.c's while(1) exactly) and searchInheritedProgram()
    // above for how the target ancestor program is found (a name-matched
    // depth-first walk of this object's own inherit tree, reusing
    // CompiledProgram::ancestorBaseOffsets -- already computed once at
    // compile time for a different reason, cross-inheritance object-
    // variable resolution -- for the real var_offset this efun also
    // needs, rather than re-deriving it by hand here).
    //
    // Real guards ported: "current_object == simul_efun_ob" throws
    // (real "replace_program on simul_efun object"); "program to replace
    // the current with has to be inherited" throws when the search finds
    // no match. Real "prog->func_ref" guard (blocks replace_program
    // while a function pointer holds a *direct* reference into
    // current_object's own function table) has no equivalent here: this
    // driver's Closure never holds a direct function-table reference at
    // all, only a bare name re-resolved lazily against its owner object
    // at call time (see Value.hpp's own Closure comment) -- a closure
    // created before the swap that no longer resolves afterward simply
    // throws "undefined function" at its own next call, matching real
    // semantics' intent even though this driver has nothing analogous to
    // guard against up front.
    //
    // ROADMAP row 1.6 (rescoped): real LDMud has no "replaces" directive
    // in `inherit` at all -- grepped `src/prolang.y`'s own
    // `inheritance_qualifier`/`inheritance_modifier` productions, the
    // complete modifier set is `static`/`private`/`public`/`protected`/
    // `nosave`/`nomask`/`deprecated`/`virtual`/`visible`, no `replaces`
    // token anywhere. The real per-dialect divergence lives entirely in
    // this efun's own argument handling instead (`doc/efun/replace_program`
    // + `src/object.c`'s own `v_replace_program()`, LDMud 3.2.9+): LDMud
    // accepts `void replace_program()` with **no** argument and
    // auto-selects the object's sole direct inherit when it has exactly
    // one, throwing "requires argument" when it has more than one and
    // "no inherited program" when it has none. Real FluffOS has no such
    // form at all -- `replace_program.c`'s own `f_replace_program()`
    // unconditionally rejects a non-string arg via `bad_arg(1, ...)`
    // before anything else runs. Gated below on `vm.config().dialect()`
    // (row 1.1) rather than a full `LpcDialect` enum plumb-through, since
    // efun already links Config and a bare string compare is all one
    // call site needs -- no `src/dialect` library dependency added.
    t.registerEfun("replace_program", [](VM& vm, std::vector<Value>& args) -> Value {
        auto ob = vm.currentObject();
        std::string target;
        if (!args.empty() && std::holds_alternative<std::string>(args[0].data)) {
            target = std::get<std::string>(args[0].data);
        } else if (args.empty() && vm.config().dialect() == "ldmud") {
            if (!ob) {
                throw LpcRuntimeError("replace_program called with no current object");
            }
            const auto& inherits = ob->program().inherits;
            if (inherits.empty()) {
                throw LpcRuntimeError("replace_program called with no inherited program");
            }
            if (inherits.size() > 1) {
                throw LpcRuntimeError(
                    "replace_program() requires argument for object with more than one inherit");
            }
            target = inherits[0];
        } else {
            throw LpcRuntimeError("replace_program: expected a string argument");
        }
        if (!ob) {
            throw LpcRuntimeError("replace_program called with no current object");
        }
        if (ob == vm.simulEfunObject()) {
            throw LpcRuntimeError("replace_program on simul_efun object");
        }

        std::string normalizedTarget = ObjectManager::normalizeFilename(target);
        auto newProgram = searchInheritedProgram(ob->program(), normalizedTarget);
        if (!newProgram) {
            throw LpcRuntimeError(
                "replace_program: program to replace the current with has to be inherited");
        }
        auto currentProgram = ob->programPtr();
        auto offsetIt = currentProgram->ancestorBaseOffsets.find(newProgram.get());
        int offset = offsetIt != currentProgram->ancestorBaseOffsets.end() ? offsetIt->second : 0;

        vm.enqueueReplaceProgram(ob, std::move(newProgram), offset, target);
        return Value{};
    });

    // string query_replaced_program(void|object ob default: this_object())
    // -- real packages/contrib.c's own f_query_replaced_program(): the
    // name last passed to a successfully-applied replace_program() call
    // on ob, or int 0 if it was never replaced. Reads
    // LpcObject::replacedProgramName(), only ever set by
    // VM::processPendingReplacePrograms() once a staged swap actually
    // applies -- correctly still 0 for an object with a replace_program()
    // call merely *pending* (not yet applied), matching real semantics:
    // real replaced_program is only ever written by replace_programs()
    // itself, never by f_replace_program() staging the request.
    t.registerEfun("query_replaced_program", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> ob;
        if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else {
            ob = vm.currentObject();
        }
        if (!ob || !ob->replacedProgramName()) return Value(int64_t{0});
        return Value(*ob->replacedProgramName());
    });

    // int replaceable(object ob, void|string *ignore) -- real
    // packages/contrib.c's own f_replaceable(): true iff every function
    // ob's own program defines *locally* (not inherited -- real
    // FUNC_INHERITED/FUNC_NO_CODE filter) is either "create", the
    // compiler's own synthesized initializer (real "__INIT", this
    // driver's own equivalent is CodeGen's synthesized "$objvarinit" --
    // see CodeGen.cpp's own comment), or one of the caller's explicit
    // ignore-list names -- i.e. nothing about ob's own file would
    // actually be lost if replace_program() swapped it away. This
    // driver's own CompiledProgram::functions already *is* exactly
    // "locally defined in this file, with real code" (an inherited
    // function lives in a *different* CompiledProgram's own functions
    // list entirely, only reached by findFunctionInChain()'s own
    // separate walk -- see CompiledProgram's own Bytecode.hpp comment),
    // so no separate inherited/no-code filtering is needed here at all;
    // real code's own filtering is just this driver's existing data
    // shape. Real "obj == simul_efun_ob || prog->func_ref" forces false
    // additionally -- func_ref has no equivalent here for the same
    // reason replace_program()'s own registration comment above already
    // gives (this driver's Closure never holds a direct function-table
    // reference to guard against). Real call site confirmed genuine and
    // paired directly with replace_program() itself: dead-souls' own
    // std/room.c gates its own "replace_program(tmp[0])" behind
    // "if (replaceable(this_object()) && ...)".
    t.registerEfun("replaceable", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            throw LpcRuntimeError("replaceable: expected an object argument");
        }
        auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        if (!ob) return Value(int64_t{0});

        std::vector<std::string> ignore = {"create", "$objvarinit"};
        if (args.size() > 1) {
            if (!std::holds_alternative<std::shared_ptr<Array>>(args[1].data)) {
                throw LpcRuntimeError("replaceable: expected a string array second argument");
            }
            auto extra = std::get<std::shared_ptr<Array>>(args[1].data);
            if (extra) {
                for (auto& item : extra->items) {
                    if (auto* s = std::get_if<std::string>(&item.data)) ignore.push_back(*s);
                }
            }
        }

        bool result = true;
        for (auto& fn : ob->program().functions) {
            if (std::find(ignore.begin(), ignore.end(), fn.name) == ignore.end()) {
                result = false;
                break;
            }
        }
        if (ob == vm.simulEfunObject()) result = false;
        return Value(static_cast<int64_t>(result ? 1 : 0));
    });

    // object snoop(object by, void|object victim) -- Phase 0.13 (snoop
    // family). Confirmed directly against fluffos-2.9-ds2.08's own
    // f_snoop() (efuns_main.c) and new_set_snoop() (comm.c), not the task
    // description that first proposed this row: that description's own
    // "snoop(object target)" / "snoop(object target, object snooper)"
    // naming has the roles backwards from the real efun -- real
    // func_spec.c's signature is "object snoop(object, void | object);",
    // and f_snoop() resolves the *first* argument as "by" (the snooper)
    // in both the 1-arg and 2-arg forms, never as the target.
    //
    // 1-arg form, snoop(by): always a *stop* -- unlinks whatever by is
    // currently snooping, if anything (real new_set_snoop(by, 0), the
    // "if (by->flags & O_SNOOP) { scan all_users, clear snooped_by==by }"
    // branch). This is the real, only way a wizard's own "snoop" command
    // (typically "snoop(this_player())") turns itself back off -- there
    // is no separate "unsnoop()" efun. Returns by itself on success (real
    // f_snoop() leaves that stack slot untouched on this path), 0 only if
    // by is already destructed. Also reached by a literal snoop(by, 0)
    // -- real func_spec.c's "void | object" second parameter accepts the
    // 0 literal as a coerced null object, landing in new_set_snoop()'s
    // own victim-is-NULL branch exactly like the 1-arg form; matched here
    // by only treating args[1] as a real victim when it actually holds a
    // non-null object.
    //
    // 2-arg form, snoop(by, victim): by starts snooping victim.
    // Confirmed real checks, in order: victim must be interactive (real
    // "if (!victim->interactive) error(...)" -- a hard, catchable LPC
    // error, not a silent 0, matched here) and the anti-loop walk (real
    // "tmp = by; while (tmp) { if (tmp == victim) return 0; tmp =
    // tmp->interactive ? tmp->interactive->snooped_by : 0; }" -- walks up
    // the chain of "whoever is currently snooping tmp", denying (return
    // 0, no throw) if victim ever appears in it; this is what stops both
    // a direct self-snoop and a multi-hop snoop cycle in one check). On
    // approval, any previous snoop *by* was running and whoever was
    // previously snooping *victim* are both unlinked first, matching real
    // new_set_snoop()'s own two "terminate previous" scans, then the new
    // link is made. Returns victim on success, 0 on denial.
    //
    // Deliberately NOT gated on any master()->valid_snoop() apply under
    // FluffOS, unlike shadow()'s own valid_shadow() gate just above --
    // confirmed exhaustively against this exact reference build:
    // applies.h defines APPLY_VALID_SHADOW (36) but has no
    // APPLY_VALID_SNOOP entry at all, new_set_snoop() itself never calls
    // apply()/master_ob for permission, and func_spec.c's own snoop()
    // signature carries no such hook either. This is a real, load-bearing
    // difference from shadow(), not an oversight in either direction:
    // real FluffOS leaves snoop authorization entirely to the mudlib
    // layer (a wizard-only command wrapping this efun with its own check
    // before ever calling it), and this driver matches that by adding no
    // invented gate to the FluffOS path below.
    //
    // ROADMAP row 1.16 (LDMud master apply name table): real LDMud
    // `snoop()` genuinely diverges, confirmed by reading
    // `temp/ldmud/src/comm.c`'s own `v_snoop()`/`set_snoop()` in full
    // (3.6.8), not just `doc/efun/snoop`'s own prose (which turned out
    // stale on the return type -- see below). Three real differences
    // from the FluffOS shape just above: (1) `master->valid_snoop(by,
    // victim-or-0)` is called for **both** forms, start and stop, not
    // just start (`set_snoop()`'s own comment: "The function calls
    // master->valid_snoop() to test if the snoop is allowed", called
    // unconditionally before anything else); (2) return type is plain
    // `int`, never the object -- confirmed against `temp/ldmud/src/
    // func_spec`'s own `"int snoop(object, void|object);"` declaration
    // and `v_snoop()`'s own `put_number(sp, i)`, which genuinely
    // contradicts `doc/efun/snoop`'s own stale "object snoop(...)" /
    // "Return <snoopee> on success" SYNOPSIS text -- the doc is wrong,
    // the C is authoritative; (3) a non-interactive victim on the start
    // form is a normal `0` return (`set_snoop()`'s own "if
    // (!(O_SET_INTERACTIVE(on, you)) || on->closing) return 0;"), never
    // a thrown error the way the FluffOS path above does. `set_snoop()`
    // also distinguishes a snoop-loop denial (`return -1`) from every
    // other failure (`return 0`) -- this driver's existing anti-loop walk
    // below already detects the identical condition (this driver's
    // snooping()/snoopedBy() model a single mutual link per object, the
    // same graph shape LDMud's own snoop_on/snoop_by pair represents,
    // just traversed from the opposite end -- "is victim already
    // (transitively) snooping by" is the same fact as LDMud's own "is by
    // already (transitively) being snooped via victim's own chain"),
    // reused here rather than re-derived, just tagged `-1` under this
    // branch instead of the FluffOS path's plain denial. `query_snoop()`
    // itself is obsolete as an LPC-visible efun in this exact 3.6.8
    // clone (`temp/ldmud/doc/obsolete/query_snoop`) -- its real
    // replacement, `interactive_info(ob, II_SNOOP_*)`, is a materially
    // different, much larger efun this driver has no equivalent of at
    // all, out of scope for this pass; nothing changed for
    // `query_snoop()`/`query_snooping()` here.
    t.registerEfun("snoop", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            throw LpcRuntimeError("snoop: expected an object as first argument");
        }
        auto by = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        bool ldmud = vm.config().dialect() == "ldmud";
        if (!by || by->isDestructed()) return ldmud ? Value(int64_t{0}) : Value{};

        bool hasVictim = args.size() > 1 &&
                          std::holds_alternative<std::shared_ptr<LpcObject>>(args[1].data) &&
                          std::get<std::shared_ptr<LpcObject>>(args[1].data);
        auto victim = hasVictim ? std::get<std::shared_ptr<LpcObject>>(args[1].data) : nullptr;

        if (ldmud) {
            auto master = vm.masterObject();
            Value victimArg = hasVictim ? Value(victim) : Value(int64_t{0});
            bool approved =
                master && isTruthy(vm.callFunction(master, "valid_snoop", {Value(by), victimArg}));
            if (!approved) return Value(int64_t{0});
            if (by->isDestructed()) return Value(int64_t{0});

            if (!hasVictim) {
                bool hadSnoop = false;
                if (auto prev = by->snooping().lock()) {
                    prev->setSnoopedBy(std::weak_ptr<LpcObject>());
                    hadSnoop = true;
                }
                by->setSnooping(std::weak_ptr<LpcObject>());
                return Value(int64_t{hadSnoop ? 1 : 0});
            }

            if (victim->isDestructed()) return Value(int64_t{0});
            if (!InteractiveRegistry::find(victim)) return Value(int64_t{0});

            auto tmp = by;
            while (tmp) {
                if (tmp == victim) return Value(int64_t{-1});
                tmp = tmp->snoopedBy().lock();
            }

            if (auto prevVictim = by->snooping().lock()) {
                prevVictim->setSnoopedBy(std::weak_ptr<LpcObject>());
            }
            if (auto prevSnooper = victim->snoopedBy().lock()) {
                prevSnooper->setSnooping(std::weak_ptr<LpcObject>());
            }
            victim->setSnoopedBy(by);
            by->setSnooping(victim);
            return Value(int64_t{1});
        }

        if (!hasVictim) {
            if (auto prev = by->snooping().lock()) {
                prev->setSnoopedBy(std::weak_ptr<LpcObject>());
            }
            by->setSnooping(std::weak_ptr<LpcObject>());
            return Value(by);
        }

        if (victim->isDestructed()) return Value{};
        if (!InteractiveRegistry::find(victim)) {
            throw LpcRuntimeError("snoop: Second argument of snoop() is not interactive!");
        }

        auto tmp = by;
        while (tmp) {
            if (tmp == victim) return Value{};
            tmp = tmp->snoopedBy().lock();
        }

        if (auto prevVictim = by->snooping().lock()) {
            prevVictim->setSnoopedBy(std::weak_ptr<LpcObject>());
        }
        if (auto prevSnooper = victim->snoopedBy().lock()) {
            prevSnooper->setSnooping(std::weak_ptr<LpcObject>());
        }
        victim->setSnoopedBy(by);
        by->setSnooping(victim);
        return Value(victim);
    });

    // object query_snoop(object ob) -- real comm.c's own query_snoop():
    // "if (!ob->interactive) return 0; return ob->interactive->snooped_by;"
    // -- who is currently snooping ob, or 0 if ob is not itself
    // interactive (or nobody is snooping it). Mandatory argument, no
    // default, matching real func_spec.c's "object query_snoop(object);"
    // (DEFAULT_NONE) exactly -- unlike query_shadowing() just above,
    // which does default to this_object().
    t.registerEfun("query_snoop", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            throw LpcRuntimeError("query_snoop: expected an object argument");
        }
        auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        if (!ob || !InteractiveRegistry::find(ob)) return Value{};
        auto snooper = ob->snoopedBy().lock();
        return snooper ? Value(snooper) : Value{};
    });

    // object query_snooping(object ob) -- real comm.c's own
    // query_snooping(): "if (!(ob->flags & O_SNOOP)) return 0;" then
    // scans all_users for whichever entry's own snooped_by points back at
    // ob. Unlike query_snoop(), real code does not require ob itself to
    // be interactive -- any object, connected or not, can be the "by" of
    // a snoop() call (new_set_snoop() never checks by->interactive
    // either), so this driver's own snooping_ field (set purely by
    // snoop() regardless of ob's own connection state) is read directly,
    // with no InteractiveRegistry check to match.
    t.registerEfun("query_snooping", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            throw LpcRuntimeError("query_snooping: expected an object argument");
        }
        auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        if (!ob) return Value{};
        auto target = ob->snooping().lock();
        return target ? Value(target) : Value{};
    });

    // void add_action(string fun, string | string* cmd, void | int flag)
    // -- registers fun (a function on current_object) onto
    // command_giver's action table under cmd (or once per element, for
    // the array form -- confirmed real usage: cmds/skills/_mist.c's own
    // "add_action(\"checkdest\", ({ \"go\", \"enter\" }))", and
    // add_action.c's own f_add_action() array-handling loop). Only the
    // string-function-name form is implemented, not the real signature's
    // "string | function" alternative -- confirmed by grepping every
    // "add_action((:" call site in this mudlib: there are none, every
    // real call passes a bare function name.
    //
    // real add_action() also requires current_object to be "near"
    // command_giver (add_action.c's own check: current_object *is*
    // command_giver, or is in its inventory, or shares its environment,
    // or *is* its environment) -- implemented the same way, silently
    // declining rather than erroring on a mismatch ("No need for an
    // error, they know what they did wrong," the reference source's own
    // comment).
    t.registerEfun("add_action", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2) throw LpcRuntimeError("add_action: expected (function, verb) arguments");
        if (!std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("add_action: only a string function name is supported");
        }
        const std::string& fn = std::get<std::string>(args[0].data);

        auto ob = vm.currentObject();
        auto giver = resolveCommandGiver(vm);
        if (!ob || !giver) return Value{};
        bool near = (ob == giver) ||
            (ob->environment().lock() == giver) ||
            (giver->environment().lock() == ob) ||
            (ob->environment().lock() == giver->environment().lock() && ob->environment().lock());
        if (!near) return Value{};

        int flag = 0;
        if (args.size() > 2 && std::holds_alternative<int64_t>(args[2].data)) {
            flag = static_cast<int>(std::get<int64_t>(args[2].data)) & 3;
        }

        std::vector<std::string> verbs;
        if (std::holds_alternative<std::string>(args[1].data)) {
            verbs.push_back(std::get<std::string>(args[1].data));
        } else if (auto* arr = std::get_if<std::shared_ptr<Array>>(&args[1].data)) {
            if (*arr) {
                for (const auto& v : (*arr)->items) {
                    if (std::holds_alternative<std::string>(v.data)) {
                        verbs.push_back(std::get<std::string>(v.data));
                    }
                }
            }
        } else {
            throw LpcRuntimeError("add_action: expected a string or string array verb argument");
        }

        for (auto& verb : verbs) {
            LpcObject::ActionEntry entry;
            entry.verb = verb;
            entry.functionName = fn;
            entry.owner = ob;
            entry.flag = flag;
            giver->addAction(std::move(entry));
        }
        return Value{};
    });

    // int remove_action(string act, string verb) -- real signature takes
    // no object argument; it always targets command_giver (or
    // current_object if no command_giver is set, add_action.c's own
    // fallback), removing current_object's own registration of it.
    t.registerEfun("remove_action", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("remove_action: expected (string act, string verb) arguments");
        }
        auto giver = resolveCommandGiver(vm);
        if (!giver) giver = vm.currentObject();
        auto ob = vm.currentObject();
        if (!giver || !ob) return Value(static_cast<int64_t>(0));
        bool removed = giver->removeAction(ob, std::get<std::string>(args[0].data),
                                            std::get<std::string>(args[1].data));
        return Value(static_cast<int64_t>(removed ? 1 : 0));
    });

    // mixed *commands(void) -- real array.c's own commands(ob) helper
    // (called by f_commands() as "commands(current_object)", confirmed
    // directly): current_object's own action table (the exact same
    // LpcObject::actions() add_action()/remove_action() already use),
    // one 4-element array per entry: ({verb, flags, owner, function_name}).
    // Confirmed live-reachable: std/user.c's own local_commands()
    // ("return commands();"). The unrelated "query_actions" this
    // driver's own instruct.md previously claimed was "already done" is
    // not a real efun in this reference build at all (zero hits in
    // efun_defs.c) -- corrected, see instruct.md's own Tier 2 note.
    t.registerEfun("commands", [](VM& vm, std::vector<Value>&) -> Value {
        auto ob = vm.currentObject();
        auto result = std::make_shared<Array>();
        if (!ob) return Value(result);
        for (auto& entry : ob->actions()) {
            auto tuple = std::make_shared<Array>();
            tuple->items.push_back(Value(entry.verb));
            tuple->items.push_back(Value(static_cast<int64_t>(entry.flag)));
            auto owner = entry.owner.lock();
            tuple->items.push_back(owner && !owner->isDestructed() ? Value(owner) : Value(static_cast<int64_t>(0)));
            tuple->items.push_back(Value(entry.functionName));
            result->items.push_back(Value(tuple));
        }
        return Value(result);
    });

    // void parse_init() -- real packages/parser.c's f_parse_init():
    // allocates current_object's own pinfo (real "if
    // (current_object->pinfo) return;" -- idempotent, a second call is
    // a silent no-op) so parse_add_rule()/parse_sentence()/
    // parse_my_rules() will accept it afterward. This slice only ports
    // the "has parse_init() been called" bit (LpcObject::hasParseInfo(),
    // see its own comment for what real parse_info_t's fuller shape is
    // not yet ported) -- there is nothing else for this efun to do
    // until the noun-phrase-resolution slice needs the rest of that
    // struct.
    t.registerEfun("parse_init", [](VM& vm, std::vector<Value>&) -> Value {
        auto ob = vm.currentObject();
        if (ob) ob->setHasParseInfo(true);
        return Value{};
    });

    // void parse_add_rule(string verb, string rule) -- real
    // packages/parser.c's f_parse_add_rule(). See ParserPackage.hpp for
    // what is and is not ported from real interrogate_master()/
    // make_rule() at this slice.
    t.registerEfun("parse_add_rule", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("parse_add_rule: expected (string verb, string rule) arguments");
        }
        auto ob = vm.currentObject();
        if (!ob || !ob->hasParseInfo()) {
            throw LpcRuntimeError(
                "parse_add_rule: object is not known by the parser (call parse_init() first)");
        }

        ParserPackage::addRule(std::get<std::string>(args[0].data), std::get<std::string>(args[1].data), ob,
                                fetchParserLiterals(vm));

        // real "handler->pinfo->flags |= PI_VERB_HANDLER; ... ret =
        // apply(LIVINGS_ARE_REMOTE, handler, 0, ORIGIN_DRIVER); if
        // (!IS_ZERO(ret)) handler->pinfo->flags |= PI_REMOTE_LIVINGS;"
        // -- not ported in the first parse_* slice (deferred as
        // "purely for sentence-matching, not observable by
        // add_rule/dump/remove"), now real and worth doing correctly
        // since this slice adds the PI_* flag storage this needed all
        // along. Real code has no O_DESTRUCTED guard after this
        // specific apply (unlike parse_refresh()'s own copy of the same
        // call, which does check) -- if livings_are_remote() destructs
        // `ob` itself, real code would already be touching a freed
        // pinfo; this driver's own LpcObject is never actually freed on
        // destruct (see LpcObject::isDestructed()'s own comment), so
        // setting a flag on it here afterward is harmless either way,
        // and porting the missing guard would just be inventing
        // stricter behavior than real code actually has.
        ob->setParseInfoFlags(ob->parseInfoFlags() | ParserInfoFlag::VerbHandler);
        Value remoteRet = vm.callFunction(ob, "livings_are_remote", {});
        if (isTruthy(remoteRet)) {
            ob->setParseInfoFlags(ob->parseInfoFlags() | ParserInfoFlag::RemoteLivings);
        }
        return Value{};
    });

    // void parse_refresh() -- real packages/parser.c's f_parse_refresh():
    // informs the parser that current_object's own cached noun/adjective/
    // plural id data (ParserPackage::interrogateObject(), row 0.13a item
    // 8 piece 1) may be stale and should be recomputed the next time
    // load_objects() actually needs it. Real semantics ported in full:
    // the master_ob special case -- real "master_state &= ~MS_HAS_USERS;
    // if (!master_ob->pinfo) return;", unconditionally invalidating the
    // cached master()->users() list (ParserPackage::
    // invalidateMasterUsersCache(), item 8 piece 2) BEFORE the early
    // return, not after -- the "not known by the parser" guard; the
    // PI_SETUP/PI_REFRESH flag dance (real "pi->flags &= PI_VERB_HANDLER;
    // pi->flags |= PI_REFRESH;" when PI_SETUP was set, or just
    // "pi->flags &= PI_VERB_HANDLER;" otherwise); and the same real
    // PI_VERB_HANDLER -> livings_are_remote() -> PI_REMOTE_LIVINGS
    // re-check parse_add_rule() above now also does, this time with real
    // code's own O_DESTRUCTED guard (isDestructed() here) after the
    // apply, matching the one real difference between these two real
    // call sites exactly.
    t.registerEfun("parse_refresh", [](VM& vm, std::vector<Value>&) -> Value {
        auto ob = vm.currentObject();
        if (!ob) return Value{};

        if (ob == vm.masterObject()) {
            ParserPackage::invalidateMasterUsersCache();
            if (!ob->hasParseInfo()) return Value{};
        }

        if (!ob->hasParseInfo()) {
            throw LpcRuntimeError("parse_refresh: object is not known by the parser (call parse_init() first)");
        }

        int flags = ob->parseInfoFlags();
        if (flags & ParserInfoFlag::Setup) {
            flags &= ParserInfoFlag::VerbHandler;
            flags |= ParserInfoFlag::Refresh;
        } else {
            flags &= ParserInfoFlag::VerbHandler;
        }
        ob->setParseInfoFlags(flags);

        if (flags & ParserInfoFlag::VerbHandler) {
            Value remoteRet = vm.callFunction(ob, "livings_are_remote", {});
            if (ob->isDestructed()) return Value{};
            if (isTruthy(remoteRet)) {
                ob->setParseInfoFlags(ob->parseInfoFlags() | ParserInfoFlag::RemoteLivings);
            }
        }
        return Value{};
    });

    // void parse_add_synonym(string new_verb, string old_verb, void |
    // string rule) -- real packages/parser.c's f_parse_add_synonym().
    // Unlike parse_add_rule(), real code never checks or sets
    // current_object->pinfo here at all (confirmed directly -- neither
    // form touches it), so this does not gate on hasParseInfo() either;
    // the only object-identity check that matters is the 3-arg form's
    // own "rule owned by a different object" ownership guard inside
    // ParserPackage::addSynonym() itself, against whichever object
    // originally registered the rule being copied.
    t.registerEfun("parse_add_synonym", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError(
                "parse_add_synonym: expected (string new_verb, string old_verb, void|string rule) arguments");
        }
        std::string rule;
        if (args.size() > 2) {
            if (!std::holds_alternative<std::string>(args[2].data)) {
                throw LpcRuntimeError("parse_add_synonym: rule argument, if given, must be a string");
            }
            rule = std::get<std::string>(args[2].data);
        }
        auto ob = vm.currentObject();
        ParserPackage::addSynonym(std::get<std::string>(args[0].data), std::get<std::string>(args[1].data), rule,
                                   ob, fetchParserLiterals(vm));
        return Value{};
    });

    // void parse_remove(string verb) -- real packages/parser.c's
    // f_parse_remove(). Silent no-op for an unknown verb or an object
    // that never registered a rule under it, matching real code.
    t.registerEfun("parse_remove", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("parse_remove: expected a string verb argument");
        }
        auto ob = vm.currentObject();
        if (ob) ParserPackage::removeRules(std::get<std::string>(args[0].data), ob);
        return Value{};
    });

    // string parse_dump() -- real packages/parser.c's f_parse_dump():
    // the entire real/synonym verb table plus every rule string
    // registered under it, one human-readable block of text. See
    // ParserPackage::dump()'s own comment for the two deliberate,
    // documented differences from real output (verb iteration order;
    // a destructed handler's own text).
    t.registerEfun("parse_dump", [](VM&, std::vector<Value>&) -> Value { return Value(ParserPackage::dump()); });

    // mixed parse_sentence(string, void|int, void|object*, void|mapping)
    // -- real packages/parser.c's f_parse_sentence(). The third argument
    // (real `parse_env`, an explicit object-array override for
    // loadObjects()'s own candidate universe) is real as of 2026-08-19 --
    // see ParserPackage::parseSentence()'s own header comment. The fourth
    // (`nicks`, a lazy word-to-object nickname mapping, real
    // `add_nicknames()`/`expand_node()`) is real as of 2026-08-20, same
    // comment.
    t.registerEfun("parse_sentence", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("parse_sentence: expected a string sentence argument");
        }
        bool debugFlag = false;
        if (args.size() > 1) {
            if (auto* n = std::get_if<int64_t>(&args[1].data)) debugFlag = (*n != 0);
        }
        const Value* envArray = nullptr;
        if (args.size() > 2 && std::holds_alternative<std::shared_ptr<Array>>(args[2].data)) {
            envArray = &args[2];
        }
        const Value* nicks = nullptr;
        if (args.size() > 3 && std::holds_alternative<std::shared_ptr<Mapping>>(args[3].data)) {
            nicks = &args[3];
        }
        auto ob = vm.currentObject();
        return ParserPackage::parseSentence(vm, ob, std::get<std::string>(args[0].data), debugFlag, envArray, nicks);
    });

    // mixed parse_my_rules(object user, string sentence, void|int
    // do_the_call) -- real packages/parser.c's f_parse_my_rules(): the
    // same matching engine as parse_sentence() above, restricted to rule
    // nodes registered by the calling object (real current_object) and
    // matched against `user` explicitly rather than this_player(). See
    // ParserPackage::parseMyRules()'s own header comment for the real
    // "flag defaults to false, not true, when the third argument is
    // omitted" derivation and the real, still-live recursive-call guard
    // (distinct from parse_sentence()'s own, which real source has
    // commented out).
    t.registerEfun("parse_my_rules", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError(
                "parse_my_rules: expected (object user, string sentence, void|int do_the_call) arguments");
        }
        auto user = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        bool doTheCallFlag = false;
        if (args.size() > 2) {
            if (auto* n = std::get_if<int64_t>(&args[2].data)) doTheCallFlag = (*n != 0);
        }
        auto restrictedHandler = vm.currentObject();
        return ParserPackage::parseMyRules(vm, user, restrictedHandler, std::get<std::string>(args[1].data),
                                            doTheCallFlag);
    });

    // string query_verb() -- the full typed first word of the line
    // currently being dispatched (real semantics, even for a
    // V_SHORT/V_NOSPACE partial match -- see VM::dispatchCommand()).
    // Returns 0 (real LPC's "no current command" result), not "", when
    // nothing is being dispatched -- confirmed against real query_verb()
    // returning 0 outside of command context, not an empty string, which
    // matters for code that tests "if(query_verb())" rather than
    // comparing against a specific string.
    t.registerEfun("query_verb", [](VM& vm, std::vector<Value>&) -> Value {
        std::string verb = vm.currentVerb();
        if (verb.empty()) return Value{};
        return Value(verb);
    });

    // void notify_fail(string | function) -- real add_action.c's own
    // f_notify_fail(): confirmed against the real spec directly
    // (func_spec.c: "void notify_fail(string | function);") rather than
    // assumed to be string-only. Sets a pending "command not understood"
    // message on command_giver's own connection (real interactive_t::
    // default_err_message, this driver's own Connection::
    // pendingNotifyFail_) -- it does NOT print anything itself. The
    // message is only ever shown if the *entire* rest of dispatch for
    // this input line ends with no add_action handler claiming it (real
    // notify_no_command(), fired from user_parser()'s own dispatch loop
    // only after every matching sentence either declined or none
    // matched at all -- see Server::dispatchLine()'s own wiring for
    // exactly where this driver mirrors that). A handler tried *after*
    // this one that DOES claim the command (returns truthy) leaves
    // whatever was set here unconsulted and it is simply overwritten or
    // discarded on the next line -- matching real semantics precisely,
    // since notify_no_command() only ever runs on the "nothing claimed
    // it" path, confirmed by reading user_parser()'s own dispatch loop
    // directly (a truthy return is an immediate "return 1", skipping
    // notify_no_command() entirely). No separate "clear first" step is
    // needed the way real f_notify_fail() calling clear_notify() does --
    // a plain Value assignment already releases whatever was set before.
    // No-op (not an error) when there is no interactive command_giver,
    // matching real f_notify_fail()'s own "if (command_giver &&
    // command_giver->interactive)" guard.
    t.registerEfun("notify_fail", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() ||
            (!std::holds_alternative<std::string>(args[0].data) &&
             !std::holds_alternative<std::shared_ptr<Closure>>(args[0].data))) {
            throw LpcRuntimeError("notify_fail: expected a string or function argument");
        }
        auto giver = resolveCommandGiver(vm);
        if (!giver) return Value{};
        if (Connection* conn = InteractiveRegistry::find(giver)) {
            conn->setPendingNotifyFail(args[0]);
        }
        return Value{};
    });

    // mixed query_notify_fail() -- real packages/contrib.c's own
    // f_query_notify_fail(): "if (command_giver && command_giver->
    // interactive)", the same guard notify_fail() above uses, then
    // returns whatever is currently sitting in default_err_message (this
    // driver's own pendingNotifyFail_) without consuming it -- a plain
    // peek, not the one-shot take notify_no_command()'s own dispatch
    // path uses (Server::dispatchLine() -> Connection::
    // takePendingNotifyFail(), unaffected by this efun since it never
    // runs). Returns 0 (real push_number(0)) when there is no
    // interactive command_giver or nothing pending.
    t.registerEfun("query_notify_fail", [](VM& vm, std::vector<Value>&) -> Value {
        auto giver = resolveCommandGiver(vm);
        if (!giver) return Value(int64_t{0});
        if (Connection* conn = InteractiveRegistry::find(giver)) {
            if (const auto& pending = conn->peekPendingNotifyFail(); pending.has_value()) {
                return *pending;
            }
        }
        return Value(int64_t{0});
    });

    // int exec(object new_ob, object old_ob) -- real replace_interactive()
    // (efuns_main.c's f_exec(): "replace_interactive((sp-1)->u.ob,
    // sp->u.ob)"): rebinds the interactive connection currently driving
    // old_ob over to new_ob instead. Confirmed the missing piece for a
    // real login to ever actually reach the created player object:
    // secure/std/login.c's own account-creation flow calls
    // "exec(__Player, this_object())" once the new character exists, and
    // without this efun the connection stays bound to the login object
    // forever, so the player object's own create()/setup() runs but the
    // player's own typed input never reaches it. Implemented via
    // Connection::attach(), which already does exactly this rebind (and
    // the matching InteractiveRegistry update) for the one real
    // connection this driver tracks per socket -- old_ob is not looked
    // up independently, it is assumed to be whichever connection is
    // currently driving this very call (OutputContext::current()), which
    // matches every real call site in this mudlib (exec() is always
    // called by old_ob itself, mid-command, never by an unrelated third
    // object). Any pending input_to() registration on the old object is
    // dropped rather than carried over -- real login.c's own flow always
    // finishes its own input_to chain before calling exec(), and a
    // handler scoped to the login object's own dialogue should not fire
    // against the new player's first typed line.
    t.registerEfun("exec", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            return Value(static_cast<int64_t>(0));
        }
        auto newOb = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        if (!newOb) return Value(static_cast<int64_t>(0));
        auto* conn = OutputContext::current();
        if (!conn) return Value(static_cast<int64_t>(0));
        conn->takePendingInputTo();
        conn->attach(newOb);
        return Value(static_cast<int64_t>(1));
    });

    // object this_player(void | int) -- real command_giver, falling back
    // to the connection currently driving the call when no command is
    // actively being dispatched (e.g. code running from create()/setup()
    // during login, before this_player() has ever been set by a real
    // dispatched command) -- the same OutputContext::current() fallback
    // message() already uses for the analogous "which connection is this
    // running for" question. Only the default (this_player(0)) form is
    // implemented; the this_interactive()/this_user() aliases
    // (this_player(1)) are not, since nothing on this driver's confirmed
    // path calls this_player() with an argument.
    t.registerEfun("this_player", [](VM& vm, std::vector<Value>&) -> Value {
        if (auto giver = resolveCommandGiver(vm)) return Value(giver);
        return Value{};
    });

    // string query_privs(object default: this_object()) / void
    // set_privs(object, int | string) -- real object_t::privs (see
    // LpcObject.hpp's own comment). Surfaced live compiling/running
    // std/living.c and std/money.c, both of which call query_privs()
    // unconditionally on log-relevant lines (not gated behind any
    // feature this driver's own boot/login path could otherwise skip).
    // set_privs()'s second argument clears privs back to unset for any
    // non-string value (real f_set_privs(): "if (!(sp->type == T_STRING))
    // ob->privs = NULL"), matching the T_NUMBER-vs-T_STRING branch
    // exactly rather than just treating a falsy second argument as
    // "clear".
    t.registerEfun("query_privs", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> target;
        if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            target = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else {
            target = vm.currentObject();
        }
        if (!target || !target->privs().has_value()) return Value{};
        return Value(*target->privs());
    });
    t.registerEfun("set_privs", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            throw LpcRuntimeError("set_privs: expected an object first argument");
        }
        auto target = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        if (!target) return Value{};
        if (args.size() > 1 && std::holds_alternative<std::string>(args[1].data)) {
            target->setPrivs(std::get<std::string>(args[1].data));
        } else {
            target->setPrivs(std::nullopt);
        }
        return Value{};
    });

    // The FluffOS uid / euid trust efuns (ROADMAP row 3.1). Real source:
    // temp/reference/fluffos-2.9-ds2.08/packages/uids.c (the four f_*
    // bodies) and packages/uids_spec.c (the signatures). The per-object
    // uid_/euid_ state they read and write is assigned at boot
    // (master->uid = get_root_uid()) and per load/clone
    // (give_uid_to_object()) by ObjectManager, see
    // include/amlp/security/UidModel.hpp. Heavily used: about 529
    // seteuid(), 441 getuid(), 354 geteuid(), 49 export_uid() call-site
    // lines across the vendored corpora, overwhelmingly the "void
    // create() { seteuid(getuid()); }" idiom that today throws
    // "undefined function or efun: seteuid".

    // string getuid(object default: F__THIS_OBJECT) -- real f_getuid():
    // returns ob->uid->name. Real's uid is never NULL for a loaded
    // object; this driver returns 0 when the uid model is inactive (no
    // get_root_uid() in the master) or the object predates it.
    t.registerEfun("getuid", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> ob;
        if (!args.empty() && !std::holds_alternative<std::monostate>(args[0].data)) {
            if (!std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
                throw LpcRuntimeError("getuid: argument must be an object");
            }
            ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else {
            ob = vm.currentObject();
        }
        if (ob && ob->uid().has_value()) return Value(*ob->uid());
        return Value(int64_t{0});
    });

    // string geteuid(function | object default: F__THIS_OBJECT) -- real
    // f_geteuid(): an object -> ob->euid->name or int 0 if euid is NULL;
    // a function pointer -> its owner's euid->name, or 0 if the owner is
    // gone or has no euid.
    t.registerEfun("geteuid", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> ob;
        if (!args.empty() && !std::holds_alternative<std::monostate>(args[0].data)) {
            if (auto* fp = std::get_if<std::shared_ptr<Closure>>(&args[0].data)) {
                ob = (*fp) ? (*fp)->owner.lock() : nullptr;
            } else if (std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
                ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
            } else {
                throw LpcRuntimeError("geteuid: argument must be an object or a function");
            }
        } else {
            ob = vm.currentObject();
        }
        if (ob && ob->euid().has_value()) return Value(*ob->euid());
        return Value(int64_t{0});
    });

    // int seteuid(string | int) -- real f_seteuid(). A nonzero int is
    // real's "bad_arg(1, F_SETEUID)". int 0 sets current_object->euid =
    // NULL and returns 1. A string asks master->valid_seteuid(
    // this_object(), str): real MASTER_APPROVED() (master.h:7) treats an
    // *undefined* apply (apply_master_ob returns -1) as approved and only
    // an explicit integer 0 as denial, so a master with no
    // valid_seteuid() lets every seteuid() through. On approval
    // current_object->euid = str and return 1; on denial return 0 and
    // leave euid unchanged.
    t.registerEfun("seteuid", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("seteuid: expected 1 argument");
        auto caller = vm.currentObject();
        if (!caller) throw LpcRuntimeError("seteuid: no current object");
        if (auto* n = std::get_if<int64_t>(&args[0].data)) {
            if (*n != 0) {
                throw LpcRuntimeError("seteuid: bad argument 1 (a nonzero int)");
            }
            caller->setEuid(std::nullopt);
            return Value(int64_t{1});
        }
        auto* s = std::get_if<std::string>(&args[0].data);
        if (!s) throw LpcRuntimeError("seteuid: argument must be a string or 0");

        bool approved = true;
        auto master = vm.masterObject();
        if (master && vm.functionExists(master, "valid_seteuid")) {
            Value ret = vm.applyMaster("valid_seteuid", {Value(caller), Value(*s)});
            if (auto* rn = std::get_if<int64_t>(&ret.data)) {
                approved = (*rn != 0);
            } else if (std::holds_alternative<std::monostate>(ret.data)) {
                // real: a void LPC return is T_NUMBER 0, i.e. denial.
                approved = false;
            } else {
                // real MASTER_APPROVED: a non-number, non-null return is
                // approved.
                approved = true;
            }
        }
        if (!approved) return Value(int64_t{0});
        caller->setEuid(*s);
        return Value(int64_t{1});
    });

    // int export_uid(object) -- real f_export_uid(): errors "Illegal to
    // export uid 0" if the caller's euid is NULL; returns 0 if the
    // target already has an euid; otherwise sets target->uid =
    // current_object->euid and returns 1. Grants the target the caller's
    // effective identity as its permanent owner (login.c's own "so that
    // login.c can export uid to us" pattern).
    t.registerEfun("export_uid", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            throw LpcRuntimeError("export_uid: expected an object first argument");
        }
        auto target = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        if (!target) return Value(int64_t{0});
        auto caller = vm.currentObject();
        if (!caller || !caller->euid().has_value()) {
            throw LpcRuntimeError("export_uid: Illegal to export uid 0");
        }
        if (target->euid().has_value()) return Value(int64_t{0});
        target->setUid(*caller->euid());
        return Value(int64_t{1});
    });

    // void set_living_name(string) -- real add_action.c's own
    // f_set_living_name(), which calls the internal set_living_name(ob,
    // str) that assigns object_t::living_name and registers ob in the
    // real hashed_living[] table. Now backed by a real
    // LivingNameRegistry (see its own header comment for the full
    // find_player()/find_living() design) instead of just storing the
    // name with no lookup table -- surfaced live: std/user.c's own
    // setup() calling set_living_name(query_name()) unconditionally, and
    // std/monster.c's own create() doing the same for every NPC.
    t.registerEfun("set_living_name", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("set_living_name: expected a string argument");
        }
        if (auto ob = vm.currentObject()) {
            const std::string& name = std::get<std::string>(args[0].data);
            ob->setLivingName(name);
            LivingNameRegistry::set(ob, name);
        }
        return Value{};
    });

    // void set_heart_beat(int to) / int query_heart_beat(object default:
    // this_object()) -- real backend.c signature: "to" is not a bare
    // on/off flag, it is a per-object heartbeat-cycle interval (real
    // default HEARTBEAT_INTERVAL is 2 real seconds per cycle, see
    // Scheduler::kHeartbeatCycle) -- confirmed live-needed: std/germ.c's
    // own set_heart_beat(5) relies on a slower cadence than every
    // object's own default 1. Now wired all the way through to a live
    // Scheduler (Scheduler::setHeartbeatInterval()), which mirrors real
    // set_heart_beat()'s exact four branches (disable/fresh-enable/
    // update/reject-negative-update) -- see its own comment. Surfaced
    // live: std/user.c's own setup() calling set_heart_beat(1)
    // unconditionally. query_heart_beat() returns the real configured
    // interval (backend.c's own query_heart_beat(object_t*) returns
    // heart_beats[index].time_to_heart_beat, not a bare 1), backed
    // directly by LpcObject::heartbeatInterval().
    t.registerEfun("set_heart_beat", [](VM& vm, std::vector<Value>& args) -> Value {
        int64_t to = (!args.empty() && std::holds_alternative<int64_t>(args[0].data))
            ? std::get<int64_t>(args[0].data) : 0;
        if (auto ob = vm.currentObject()) {
            if (vm.scheduler()) {
                vm.scheduler()->setHeartbeatInterval(ob, to);
            } else {
                ob->setHeartbeatInterval(static_cast<int>(to));
            }
        }
        return Value{};
    });
    t.registerEfun("query_heart_beat", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> target;
        if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            target = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else {
            target = vm.currentObject();
        }
        return Value(static_cast<int64_t>(target ? target->heartbeatInterval() : 0));
    });

    // object *heart_beats() -- real packages/contrib.c's own
    // f_heart_beats(): "push_refed_array(get_heart_beats());", every
    // object with set_heart_beat() currently enabled (backend.c's own
    // heart_beats[] array). Backed by Scheduler::pendingHeartbeats(), a
    // new read-only accessor added alongside pendingCallOuts() (same
    // "plain accessor, filter lives in the efun's own loop" reasoning
    // call_out_info()'s own registration above already established),
    // skipping any entry whose owner has since been destructed.
    t.registerEfun("heart_beats", [](VM& vm, std::vector<Value>&) -> Value {
        auto result = std::make_shared<Array>();
        if (!vm.scheduler()) return Value(result);
        for (const auto& entry : vm.scheduler()->pendingHeartbeats()) {
            auto ob = entry.target.lock();
            if (!ob || ob->isDestructed()) continue;
            result->items.emplace_back(ob);
        }
        return Value(result);
    });

    // int time() -- seconds since the Unix epoch, same as the real efun.
    t.registerEfun("time", [](VM&, std::vector<Value>&) -> Value {
        return Value(static_cast<int64_t>(std::time(nullptr)));
    });

    // int real_time() -- real packages/contrib.c's own f_real_time():
    // "push_number(time(NULL))", the exact same body as time() just
    // above under a separately-coded efun, not an F_ALIAS_FLAG pair --
    // F_TIME and F_REAL_TIME are two distinct entries in efun_defs.c
    // that just happen to do the same thing, confirmed directly.
    t.registerEfun("real_time", [](VM&, std::vector<Value>&) -> Value {
        return Value(static_cast<int64_t>(std::time(nullptr)));
    });

    // int time_ns() -- current FluffOS's own real, genuinely new-since-
    // 2.9 efun (confirmed absent from temp/reference/fluffos-2.9-ds2.08:
    // no time_ns/perf_counter_ns anywhere in that tree at all), confirmed
    // directly against current real source rather than guessed:
    // src/packages/core/core.spec:373 declares "int time_ns();", no
    // arguments; src/packages/core/time.cc's own f_time_ns() (guarded
    // "#ifdef F_TIME_NS", this driver has no equivalent compile switch so
    // it is simply always registered): "auto now =
    // std::chrono::system_clock::now(); push_number(duration_cast<
    // nanoseconds>(now.time_since_epoch()).count());" -- wall-clock
    // nanoseconds since the Unix epoch, the real testsuite's own
    // testsuite/single/tests/efuns/time_ns.lpc confirming this
    // epoch-relative shape directly: "ASSERT(x > 1685382080000000);", a
    // real epoch-nanosecond lower bound (~June 2023), not a
    // no-fixed-epoch monotonic counter.
    t.registerEfun("time_ns", [](VM&, std::vector<Value>&) -> Value {
        auto now = std::chrono::system_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
        return Value(static_cast<int64_t>(ns.count()));
    });

    // int perf_counter_ns() -- current FluffOS's own real sibling of
    // time_ns() above, same confirmed-absent-from-2.9 status.
    // src/packages/core/core.spec:371 declares "int perf_counter_ns();"
    // (that spec file's own comment, "return highest resolution clock in
    // platform dependent unit", is stale against the real implementation
    // below, which always converts to nanoseconds regardless of
    // platform, matching the function's own "_ns" name and the real
    // testsuite's own direct int-to-int comparison of two calls). Real
    // f_perf_counter_ns() (time.cc, non-Windows branch -- this driver has
    // no Windows target, so only that branch is real scope here): "auto
    // now = std::chrono::high_resolution_clock::now();
    // push_number(duration_cast<nanoseconds>(now.time_since_epoch())
    // .count());" -- ported verbatim, including the real (if slightly
    // loose) choice of std::chrono::high_resolution_clock rather than
    // std::chrono::steady_clock: real testsuite/single/tests/efuns/
    // perf_counter_ns.lpc only ever asserts monotonicity between two
    // successive calls ("ASSERT(b >= a);"), never an absolute
    // epoch-relative bound the way time_ns()'s own test does, matching
    // real high_resolution_clock's own implementation-defined (not
    // guaranteed-steady) nature -- deliberately not "improved" to
    // steady_clock here, since that would diverge from what real
    // FluffOS's own shipped code actually does.
    t.registerEfun("perf_counter_ns", [](VM&, std::vector<Value>&) -> Value {
        auto now = std::chrono::high_resolution_clock::now();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
        return Value(static_cast<int64_t>(ns.count()));
    });

    // string ctime(int|void clock) -- real efun wraps C's own ctime(),
    // including its trailing newline; clock defaults to time() (real
    // func_spec.c: "string ctime(int|void);", efuns_main.c's own
    // f_ctime() calls time(0) when no argument was given).
    t.registerEfun("ctime", [](VM&, std::vector<Value>& args) -> Value {
        std::time_t clock = std::time(nullptr);
        if (!args.empty() && std::holds_alternative<int64_t>(args[0].data)) {
            clock = static_cast<std::time_t>(std::get<int64_t>(args[0].data));
        }
        char* s = std::ctime(&clock);
        return Value(std::string(s ? s : ""));
    });

    // mixed *localtime(int clock) -- confirmed against
    // fluffos-2.9-ds2.08/efuns_port.c's own f_localtime() and
    // include/localtime.h's LT_* index constants: an 11-element array
    // ({sec, min, hour, mday, mon, year, wday, yday, gmtoff, zone,
    // isdst}), year already +1900'd (real f_localtime() does "tm->tm_year
    // + 1900" itself, not a raw tm_year). gmtoff/zone come from
    // struct tm's own tm_gmtoff/tm_zone fields directly (glibc
    // extensions available on this Linux target) rather than the real
    // source's own tangle of platform-specific #ifdef branches (sequent/
    // BSD42/hpux/etc) for the exact same two values on other platforms --
    // functionally identical output on the platform this driver actually
    // builds on, not a different algorithm. Zero real call sites in this
    // mudlib (confirmed by grep; the only hits are doc files and the
    // LT_* constant header), implemented anyway per this row's own Tier 1
    // list.
    t.registerEfun("localtime", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("localtime: expected an int argument");
        }
        std::time_t clock = static_cast<std::time_t>(std::get<int64_t>(args[0].data));
        std::tm tmVal{};
        if (!::localtime_r(&clock, &tmVal)) {
            throw LpcRuntimeError("localtime: localtime_r failed");
        }
        auto result = std::make_shared<Array>();
        result->items.emplace_back(static_cast<int64_t>(tmVal.tm_sec));
        result->items.emplace_back(static_cast<int64_t>(tmVal.tm_min));
        result->items.emplace_back(static_cast<int64_t>(tmVal.tm_hour));
        result->items.emplace_back(static_cast<int64_t>(tmVal.tm_mday));
        result->items.emplace_back(static_cast<int64_t>(tmVal.tm_mon));
        result->items.emplace_back(static_cast<int64_t>(tmVal.tm_year + 1900));
        result->items.emplace_back(static_cast<int64_t>(tmVal.tm_wday));
        result->items.emplace_back(static_cast<int64_t>(tmVal.tm_yday));
        result->items.emplace_back(static_cast<int64_t>(tmVal.tm_gmtoff));
        result->items.emplace_back(std::string(tmVal.tm_zone ? tmVal.tm_zone : ""));
        result->items.emplace_back(static_cast<int64_t>(tmVal.tm_isdst));
        return Value(result);
    });

    // string zonetime(string tz, int clock) and
    // int is_daylight_savings_time(string tz, int clock)
    // -- packages/contrib.c's own f_zonetime() / f_is_daylight_savings_time(),
    // John Viega's 1996 timezone-conversion efuns ("efuns for doing time
    // zone conversions. Much friendlier than doing all the lookup tables
    // in LPC"). Both still present verbatim in current FluffOS
    // (temp/fluffos/src/packages/contrib/contrib.cc), same declared
    // signatures in both trees: `string zonetime(string, int);
    // int is_daylight_savings_time(string, int);`. Neither was registered
    // here before (grep of EfunTable.cpp); the rest of contrib.spec is
    // either already done or already scoped as deferred (rows 2.36 etc).
    //
    // Both work by pointing libc at the named zone: set the TZ
    // environment variable, call tzset(), do the conversion, then restore
    // the previous TZ and tzset() again. Real set_timezone() /
    // reset_timezone() (contrib.c) do this with putenv() and a static
    // buffer; this driver uses setenv()/unsetenv() for the same effect
    // without aliasing a static buffer into the environment. The 2.9
    // source calls ctime()/localtime(); current FluffOS calls
    // ctime_r()/localtime_r(), matched here.
    //
    //   - zonetime(tz, clock): ctime of clock computed in zone tz, with
    //     the trailing newline stripped (real: "retv[len-1] = '\0'").
    //     ctime's fixed "Www Mmm dd hh:mm:ss yyyy" form (two spaces
    //     before a single-digit day), e.g. zonetime("UTC", 1000000000)
    //     == "Sun Sep  9 01:46:40 2001". Real f_zonetime() (current
    //     clone) raises "bad argument to zonetime." when ctime_r returns
    //     null; matched.
    //   - is_daylight_savings_time(tz, clock): localtime of clock in zone
    //     tz, returns (tm_isdst > 0) as 0 or 1. Current FluffOS clamps a
    //     negative clock to 0 and returns -1 if localtime_r fails; both
    //     matched.
    //
    // Zero real mudlib call sites (grepped every vendored corpus under
    // temp/: the only hits are FluffOS's own testsuite). FluffOS-surface
    // parity, the same basis as sha1() and the matrix.spec slices; both
    // are independently verifiable against hand-computed libc output with
    // no live-instance dependency. TZ is a process-global, and this
    // driver runs efuns synchronously on one thread (same assumption real
    // FluffOS makes), so the set/restore pair is not racing anything.
    auto withTimezone = [](const std::string& tz, const std::function<void()>& body) {
        const char* prev = ::getenv("TZ");
        const bool hadPrev = prev != nullptr;
        const std::string saved = hadPrev ? std::string(prev) : std::string();
        ::setenv("TZ", tz.c_str(), 1);
        ::tzset();
        body();
        if (hadPrev) {
            ::setenv("TZ", saved.c_str(), 1);
        } else {
            ::unsetenv("TZ");
        }
        ::tzset();
    };

    t.registerEfun("zonetime", [withTimezone](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 ||
            !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<int64_t>(args[1].data)) {
            throw LpcRuntimeError("zonetime: expected (string tz, int clock)");
        }
        const std::string& tz = std::get<std::string>(args[0].data);
        std::time_t clock = static_cast<std::time_t>(std::get<int64_t>(args[1].data));
        std::string out;
        withTimezone(tz, [&] {
            char buf[64] = {};
            char* s = ::ctime_r(&clock, buf);
            if (s) {
                out = s;
                if (!out.empty() && out.back() == '\n') out.pop_back();
            }
        });
        if (out.empty()) {
            throw LpcRuntimeError("bad argument to zonetime.");
        }
        return Value(out);
    });

    t.registerEfun("is_daylight_savings_time", [withTimezone](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 ||
            !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<int64_t>(args[1].data)) {
            throw LpcRuntimeError("is_daylight_savings_time: expected (string tz, int clock)");
        }
        const std::string& tz = std::get<std::string>(args[0].data);
        std::time_t clock = static_cast<std::time_t>(std::get<int64_t>(args[1].data));
        if (clock < 0) clock = 0;
        int64_t result = 0;
        withTimezone(tz, [&] {
            std::tm tmVal{};
            if (::localtime_r(&clock, &tmVal)) {
                result = (tmVal.tm_isdst > 0) ? 1 : 0;
            } else {
                result = -1;
            }
        });
        return Value(result);
    });

    // mapping rusage() -- confirmed against efuns_port.c's own
    // F_RUSAGE/RUSAGE branch (the one this Linux target actually
    // compiles, not the WIN32/GET_PROCESS_STATS alternatives): a
    // getrusage(RUSAGE_SELF, ...) call, keys named for the real
    // rus.ru_* struct fields, utime/stime converted from
    // seconds+microseconds to whole milliseconds exactly as the real
    // source does ("tv_sec * 1000 + tv_usec / 1000"). maxrss on Linux
    // is resident-set-size in kilobytes read from /proc/self/statm
    // (the real source's own Linux-specific branch, confirmed live
    // reachable on this platform, not the "sun" getpagesize() branch
    // next to it) since Linux's own getrusage() never actually fills
    // ru_maxrss on the kernels this real code was written against.
    // ru_ixrss/idrss/isrss/nswap are always 0 on Linux in both real
    // FluffOS and here (the kernel never populates them; not a driver
    // gap, matching real rus.ru_ixrss etc's own always-0 value here too).
    t.registerEfun("rusage", [](VM&, std::vector<Value>&) -> Value {
        auto result = std::make_shared<Mapping>();
        struct ::rusage rus{};
        if (::getrusage(RUSAGE_SELF, &rus) != 0) {
            return Value(result);
        }
        int64_t maxrss = 0;
        std::ifstream statm("/proc/self/statm");
        if (statm) {
            int64_t pages = 0, resident = 0;
            statm >> pages >> resident;
            maxrss = resident * (::sysconf(_SC_PAGESIZE) / 1024);
        }
        auto add = [&](const char* key, int64_t v) {
            result->entries.emplace_back(Value(std::string(key)), Value(v));
        };
        add("utime", static_cast<int64_t>(rus.ru_utime.tv_sec) * 1000 + rus.ru_utime.tv_usec / 1000);
        add("stime", static_cast<int64_t>(rus.ru_stime.tv_sec) * 1000 + rus.ru_stime.tv_usec / 1000);
        add("maxrss", maxrss);
        add("ixrss", static_cast<int64_t>(rus.ru_ixrss));
        add("idrss", static_cast<int64_t>(rus.ru_idrss));
        add("isrss", static_cast<int64_t>(rus.ru_isrss));
        add("minflt", static_cast<int64_t>(rus.ru_minflt));
        add("majflt", static_cast<int64_t>(rus.ru_majflt));
        add("nswap", static_cast<int64_t>(rus.ru_nswap));
        add("inblock", static_cast<int64_t>(rus.ru_inblock));
        add("oublock", static_cast<int64_t>(rus.ru_oublock));
        add("msgsnd", static_cast<int64_t>(rus.ru_msgsnd));
        add("msgrcv", static_cast<int64_t>(rus.ru_msgrcv));
        add("nsignals", static_cast<int64_t>(rus.ru_nsignals));
        add("nvcsw", static_cast<int64_t>(rus.ru_nvcsw));
        add("nivcsw", static_cast<int64_t>(rus.ru_nivcsw));
        return Value(result);
    });

    // int random(int n) -- confirmed against real efuns_main.c's
    // f_random(): "if (sp->u.number <= 0) { sp->u.number = 0; return; }
    // sp->u.number = random_number(sp->u.number);" -- a uniform int in
    // [0, n-1], or plain 0 for n <= 0 (not an error). func_spec.c: "int
    // random(int);", one required int argument. Found live: domains/
    // Praxis/setter.c's own roll_d6() (Palladium 3d6 attribute rolling,
    // "total += random(6) + 1"), the first efun call chargen's roll step
    // actually makes.
    t.registerEfun("random", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("random: expected an int argument");
        }
        int64_t n = std::get<int64_t>(args[0].data);
        if (n <= 0) return Value(int64_t{0});
        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int64_t> dist(0, n - 1);
        return Value(dist(rng));
    });

    // int secure_random(int n) -- current FluffOS's own real, genuinely
    // new-since-2.9 efun (confirmed absent from
    // temp/reference/fluffos-2.9-ds2.08: no secure_random anywhere in
    // that tree at all), a real, distinct security gap this driver's own
    // random() above does not fill: random() seeds a std::mt19937 once
    // from std::random_device and draws every subsequent number from
    // that same seeded, non-cryptographic PRNG, exactly matching real
    // random_number()'s own equivalent shape (src/base/internal/port.cc)
    // -- fine for gameplay randomness, not for anything security-
    // sensitive (session tokens, reset codes), since a predictable-once-
    // seeded PRNG is not the same guarantee as a real CSPRNG.
    //
    // Signature confirmed directly against real current source, not
    // guessed: src/packages/core/core.spec:66 declares
    // "int secure_random(int);", one argument. Real
    // secure_random_number() (src/base/internal/port.cc:32-44, called by
    // f_secure_random(), efuns_main.cc): "static std::random_device rd(
    // "/dev/urandom"); ... std::uniform_int_distribution<int64_t>
    // dist(0, n - 1); return dist(rd);" -- on Linux/OSX (this driver's
    // own only real target; the file's own Windows branch is out of
    // scope here), std::random_device is used DIRECTLY as the
    // distribution's own engine, drawing fresh entropy from /dev/urandom
    // on every single call, not seeding a separate deterministic PRNG
    // the way random_number() does -- ported verbatim below, including
    // the exact "/dev/urandom" token libstdc++ recognizes (this driver
    // already builds against libstdc++, confirmed by this same
    // environment's own toolchain). n <= 0 returns 0, the same guard
    // real secure_random_number() has (an UB guard against
    // uniform_int_distribution's own "requires a <= b" precondition,
    // confirmed by that function's own comment). Real doc
    // (docs/efun/numbers/secure_random.md, fetched live): "Return a
    // cryptographically secure random number from the range
    // [0 .. (n-1)] (inclusive). On Linux & OSX, this function explicitly
    // use randomness from /dev/urandom." Deliberately NOT built on this
    // driver's own already-linked libcrypto/OpenSSL dependency
    // (RAND_bytes()) despite that being the more obvious-looking choice
    // given hash()'s own precedent: confirmed directly against real
    // source that real FluffOS itself never uses OpenSSL for this at
    // all, only std::random_device -- matching the real implementation
    // exactly took priority over introducing a different (arguably
    // equally valid) mechanism the real driver does not actually use.
    t.registerEfun("secure_random", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("secure_random: expected an int argument");
        }
        int64_t n = std::get<int64_t>(args[0].data);
        if (n <= 0) return Value(int64_t{0});
        static std::random_device rd("/dev/urandom");
        std::uniform_int_distribution<int64_t> dist(0, n - 1);
        return Value(dist(rd));
    });

    // int uptime() -- real efuns_main.c's f_uptime(): "push_number(
    // current_time - boot_time)", seconds since the driver's own boot.
    // bootTime is captured here, at registerCoreEfuns() time (this
    // driver's own real boot, called once from main.cpp before the
    // server starts accepting connections), not lazily on first call --
    // matching real boot_time's own "set once at startup" semantics
    // rather than approximating boot as "whenever uptime() first ran".
    {
        const std::time_t bootTime = std::time(nullptr);
        t.registerEfun("uptime", [bootTime](VM&, std::vector<Value>&) -> Value {
            return Value(static_cast<int64_t>(std::time(nullptr) - bootTime));
        });
    }

    // mixed *sys_network_ports(void) -- current FluffOS's own real,
    // genuinely new-since-2.9 efun (confirmed absent from
    // temp/reference/fluffos-2.9-ds2.08 entirely). Signature confirmed
    // directly against real current source: src/packages/core/core.spec's
    // own "mixed *sys_network_ports();". Real f_sys_network_ports()
    // (src/packages/core/sys.cc): one sub-array per configured, active
    // listening port, real shape "({ (int) external_port_#, (string)
    // type, (int) port, (int) tls })" -- external_port_# is the port's
    // real 1-indexed config-table slot (i+1 across a fixed 5-slot real
    // table), type is "telnet" or "websocket" (port_kind_name()), tls is
    // 1 only when that specific port has a real cert/key pair
    // configured. This driver has exactly one real listening port
    // (Config::port(), etc/driver.cfg's own "port:" directive), no
    // multi-port config support at all (a real, separate, larger gap
    // this row does not attempt to close), and no TLS support (row
    // 2.13, not started) -- so the honest, real port for this build is
    // "({ 1, \"telnet\", <configured port>, 0 })", a single-element
    // outer array, matching the real shape exactly for the one real
    // port this driver actually has rather than fabricating additional
    // slots or a websocket/tls entry this driver cannot back.
    t.registerEfun("sys_network_ports", [](VM& vm, std::vector<Value>&) -> Value {
        auto portInfo = std::make_shared<Array>();
        portInfo->items.emplace_back(int64_t{1});
        portInfo->items.emplace_back(std::string("telnet"));
        portInfo->items.emplace_back(static_cast<int64_t>(vm.config().port()));
        portInfo->items.emplace_back(int64_t{0});

        auto result = std::make_shared<Array>();
        result->items.emplace_back(portInfo);
        return Value(result);
    });

    // void debug_message(string str) -- real main.c's own debug_message()
    // C primitive (varargs, "%s"-style) appends to the driver's own debug
    // log file (LOG_DIR/debug.log by default); the LPC-facing f_debug_message()
    // dispatch wrapper itself has no body anywhere in this vendored source
    // tree (confirmed by grep; only its efun_defs.c table entry exists),
    // but its signature (1 required T_STRING arg, TYPE_NOVALUE return) and
    // the underlying primitive's own well-documented purpose leave little
    // real ambiguity: write the string somewhere the operator can see it.
    // This driver has no configured log-directory facility of its own yet
    // (Config has no logDir()-equivalent key), so this writes to stderr
    // rather than inventing new config surface for a single low-stakes
    // diagnostic efun -- a real, documented simplification, not a silent
    // no-op the way a stub would be.
    t.registerEfun("debug_message", [](VM&, std::vector<Value>& args) -> Value {
        if (!args.empty() && std::holds_alternative<std::string>(args[0].data)) {
            std::cerr << std::get<std::string>(args[0].data);
        }
        return Value{};
    });

    // string in_edit(object ob default: this_object()) -- real
    // efuns_main.c's f_in_edit(): returns the filename currently being
    // edited if ob is in an ed() session, else int 0. This driver has no
    // ed() efun at all (not in this batch either -- a full stateful,
    // input_to()-driven multi-line text editor, out of scope for a
    // self-contained efun body), so no object can ever be "in edit" here
    // -- always 0 is the real, correct answer for this driver's current
    // capabilities, not a placeholder.
    t.registerEfun("in_edit", [](VM&, std::vector<Value>&) -> Value {
        return Value(static_cast<int64_t>(0));
    });

    // int in_input(object ob default: this_object()) -- real
    // efuns_main.c's f_in_input(): "ob->interactive && ob->interactive->input_to"
    // -- true only for a currently-connected object with a pending
    // input_to() handler registered. Confirmed against this driver's own
    // Connection::hasPendingInputTo() (net/instruct.md's own file
    // description already documents this exact state).
    t.registerEfun("in_input", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> ob;
        if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else {
            ob = vm.currentObject();
        }
        Connection* conn = ob ? InteractiveRegistry::find(ob) : nullptr;
        return Value(static_cast<int64_t>(conn && conn->hasPendingInputTo() ? 1 : 0));
    });

    // int userp(object ob) / int query_once_interactive(object ob) --
    // real aliases (func_spec.c: "int userp(object); int
    // query_once_interactive userp(object);") for "has ob ever been
    // interactive" (O_ONCE_INTERACTIVE, set once and never cleared,
    // unlike interactive()'s own "is it interactive *right now*"
    // O_ONLINE-style check). This driver previously had no
    // O_ONCE_INTERACTIVE equivalent and approximated it as "is it
    // interactive right now" (an InteractiveRegistry scan, identical to
    // interactive()'s own check) -- confirmed live-reachable and wrong,
    // not just theoretical: real mudlib content calls userp() 61 times
    // across combat, chat, mail, trading, and admin commands, heavily in
    // the exact "userp(target) && !interactive(target)" or
    // "ob->is_player() && !interactive(ob)" shape (e.g.
    // cmds/mortal/_psi.c, cmds/skills/_backstab.c/_fireball.c/_bolt.c) --
    // "is this a player object, currently offline" -- which the old
    // approximation could never satisfy at all, since it made userp()
    // and interactive() identical. The instant any connected player goes
    // link-dead (an entirely ordinary event), every one of those checks
    // would have silently misclassified their still-present character as
    // not a player. Fixed via LpcObject::wasEverInteractive() (see its
    // own comment): a real sticky flag, set once by Connection::attach()
    // the first time an object is ever bound to a connection (covering
    // both a login shell and a later exec() rebind onto the real player
    // object, matching secure/std/login.c's own documented understanding
    // of this exact efun), never cleared on disconnect.
    auto userpImpl = [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            return Value(int64_t{0});
        }
        auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        if (!ob) return Value(int64_t{0});
        return Value(static_cast<int64_t>(ob->wasEverInteractive() ? 1 : 0));
    };
    t.registerEfun("userp", userpImpl);
    t.registerEfun("query_once_interactive", userpImpl);

    // string crypt(string str, string|int salt) -- real efuns_port.c's
    // f_crypt(): if salt is a string of length >= 2, use it directly;
    // otherwise generate a random 8-character salt from the same
    // charset the reference driver uses, then hash via the system's
    // own crypt(3) (this driver links -lcrypt for it -- see
    // CMakeLists.txt). Confirmed live: secure/std/login.c's own
    // confirm_password() calls "crypt(str2, 0)" to hash a new account's
    // chosen password before saving it.
    t.registerEfun("crypt", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("crypt: expected a string argument");
        }
        const std::string& str = std::get<std::string>(args[0].data);
        std::string salt;
        if (args.size() > 1 && std::holds_alternative<std::string>(args[1].data) &&
            std::get<std::string>(args[1].data).size() >= 2) {
            salt = std::get<std::string>(args[1].data);
        } else {
            static const char choice[] =
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ./";
            static const int choiceLen = static_cast<int>(sizeof(choice) - 1);
            static std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<int> dist(0, choiceLen - 1);
            for (int i = 0; i < 8; ++i) salt += choice[static_cast<size_t>(dist(rng))];
        }
        char* result = ::crypt(str.c_str(), salt.c_str());
        if (!result) throw LpcRuntimeError("crypt: system crypt() failed");
        return Value(std::string(result));
    });

    // string oldcrypt(string str, string|int salt) -- real
    // packages/contrib.c's own f_oldcrypt(): the same system crypt(3)
    // call as crypt() above, but always forced to the classic two-
    // character DES salt (confirmed directly: "salt[0] = sp->u.string[0];
    // salt[1] = sp->u.string[1];" only ever reads the first two salt
    // characters, even when a longer one is supplied, and generates only
    // two random characters otherwise -- unlike crypt()'s own real
    // 8-character generated salt, which is long enough for the system
    // crypt(3) to pick a modern hash scheme on its own).
    t.registerEfun("oldcrypt", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("oldcrypt: expected a string argument");
        }
        const std::string& str = std::get<std::string>(args[0].data);
        std::string salt;
        if (args.size() > 1 && std::holds_alternative<std::string>(args[1].data) &&
            std::get<std::string>(args[1].data).size() >= 2) {
            salt = std::get<std::string>(args[1].data).substr(0, 2);
        } else {
            static const char choice[] =
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ./";
            static const int choiceLen = static_cast<int>(sizeof(choice) - 1);
            static std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<int> dist(0, choiceLen - 1);
            for (int i = 0; i < 2; ++i) salt += choice[static_cast<size_t>(dist(rng))];
        }
        char* result = ::crypt(str.c_str(), salt.c_str());
        if (!result) throw LpcRuntimeError("oldcrypt: system crypt() failed");
        return Value(std::string(result));
    });

    // string hash(string algo, string str) -- real docs/efun/strings/
    // hash.md's own f_hash(): computes a cryptographic digest of str
    // using the named algorithm via OpenSSL's EVP interface (this
    // driver links -lcrypto for it -- see CMakeLists.txt), returning
    // the digest as a lowercase hex string. Real driver requires
    // PACKAGE_CRYPTO at compile time; this driver has no equivalent
    // switch, hash() is simply always registered. Algorithm names are
    // matched case-insensitively, confirmed directly against a real
    // EVP_get_digestbyname() probe in this environment (both "sha256"
    // and "SHA256" resolve to the same digest). Real doc's own
    // compatibility notes are confirmed live too, not assumed: md5/
    // sha1/sha224/sha256/sha384/sha512/sha3-224/sha3-256/sha3-384/
    // sha3-512/blake2s256/blake2b512/sm3/ripemd160 all compute
    // correctly against this build's OpenSSL 3.5 (md5("Something")
    // confirmed byte-for-byte against the real doc's own example,
    // "73f9977556584a369800e775b48f3dbe"); md2/mdc2 are genuinely
    // absent (removed upstream in OpenSSL 3.x, matching the real doc's
    // own note) and md4 resolves a name but fails at digest-init time
    // (moved to OpenSSL 3.x's "legacy" provider, not loaded by default
    // in this build) -- both distinguished below from a truly unknown
    // algorithm name, since the underlying cause differs even though a
    // real driver built the same way would hit the same two gaps.
    t.registerEfun("hash", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("hash() requires (string algo, string str)");
        }
        std::string algo = std::get<std::string>(args[0].data);
        std::transform(algo.begin(), algo.end(), algo.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::string& data = std::get<std::string>(args[1].data);

        const EVP_MD* md = EVP_get_digestbyname(algo.c_str());
        if (!md) {
            throw LpcRuntimeError("hash() unknown hash type: " + algo);
        }
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int digestLen = 0;
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) throw LpcRuntimeError("hash(): failed to allocate digest context");
        bool ok = EVP_DigestInit_ex(ctx, md, nullptr) != 0 &&
                  EVP_DigestUpdate(ctx, data.data(), data.size()) != 0 &&
                  EVP_DigestFinal_ex(ctx, digest, &digestLen) != 0;
        EVP_MD_CTX_free(ctx);
        if (!ok) {
            throw LpcRuntimeError("hash() algorithm not available in this build: " + algo);
        }

        static const char hexChars[] = "0123456789abcdef";
        std::string result;
        result.reserve(digestLen * 2);
        for (unsigned int i = 0; i < digestLen; ++i) {
            result.push_back(hexChars[digest[i] >> 4]);
            result.push_back(hexChars[digest[i] & 0xF]);
        }
        return Value(result);
    });

    // string sha1(string str) -- real src/packages/sha1/sha1.spec's own
    // "string sha1(string|buffer);" plus docs/efun/strings/sha1.md's own
    // f_sha1(): the SHA-1 digest of str, returned as a lowercase hex
    // string. Confirmed absent from the vendored 2.9 ds2.08 reference
    // entirely -- that tree has no crypto or sha1 package at all
    // (temp/reference/fluffos-2.9-ds2.08/packages/ stops at async/
    // compress/contrib/db/develop/dwlib/external/math/matrix/
    // mudlib_stats/parser/sockets/uids) -- so this is genuinely new
    // current-FluffOS surface, the same category as hash() (row 2.16)
    // right above. Real f_sha1() (temp/fluffos/src/packages/sha1/sha1.cc)
    // hand-rolls the SHA-1 block function inline; this driver computes
    // the identical digest via OpenSSL's EVP interface instead, reusing
    // the exact EVP_DigestInit/Update/Final shape and the -lcrypto
    // dependency hash() already established. SHA-1's output is fixed by
    // FIPS 180, so the hand-rolled and EVP paths are byte-for-byte
    // identical for every input by construction -- unlike secure_random()
    // (row 2.24), where the real entropy-source mechanism was itself
    // observable and had to be ported verbatim; here there is nothing
    // observable to diverge on. Real doc's own worked example,
    // sha1("something") == "1af17e73721dbe0c40011b82ed4bb1a7dbe3ce29",
    // was cross-checked byte-for-byte against the system sha1sum before
    // this code was written, not derived from this driver's own output.
    // Real signature is string|buffer; this driver has no buffer value
    // type (rows 2.33/2.42), so only the string form is implemented, and
    // a non-string argument throws rather than being silently
    // mishandled, matching this codebase's own established precedent
    // (explode_reversible()'s empty delimiter, an unsupported sscanf/
    // sprintf format, member_array()'s unsupported 4th argument). The
    // real doc itself notes "The hash(algo, str) external function can
    // handle SHA-1 and more" -- sha1() is the convenience spelling of
    // hash("sha1", str), and this driver's two paths agree (regression
    // test below).
    t.registerEfun("sha1", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("sha1() requires a string argument");
        }
        const std::string& data = std::get<std::string>(args[0].data);

        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int digestLen = 0;
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) throw LpcRuntimeError("sha1(): failed to allocate digest context");
        bool ok = EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr) != 0 &&
                  EVP_DigestUpdate(ctx, data.data(), data.size()) != 0 &&
                  EVP_DigestFinal_ex(ctx, digest, &digestLen) != 0;
        EVP_MD_CTX_free(ctx);
        if (!ok) {
            throw LpcRuntimeError("sha1(): digest computation failed");
        }

        static const char hexChars[] = "0123456789abcdef";
        std::string result;
        result.reserve(digestLen * 2);
        for (unsigned int i = 0; i < digestLen; ++i) {
            result.push_back(hexChars[digest[i] >> 4]);
            result.push_back(hexChars[digest[i] & 0xF]);
        }
        return Value(result);
    });

    // mixed copy(mixed val) -- deep-copies an array or mapping (breaking
    // aliasing with the original); every other value kind in this
    // driver's Value model (int, float, string, object reference,
    // closure) is already copied by plain C++ value/shared_ptr-handle
    // semantics wherever a Value is copied, so copy() is the identity
    // for those, matching real LPC's own "only arrays/mappings actually
    // alias" behavior.
    t.registerEfun("copy", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) return Value{};
        if (auto* arr = std::get_if<std::shared_ptr<Array>>(&args[0].data)) {
            auto result = std::make_shared<Array>();
            if (*arr) result->items = (*arr)->items;
            return Value(result);
        }
        if (auto* map = std::get_if<std::shared_ptr<Mapping>>(&args[0].data)) {
            auto result = std::make_shared<Mapping>();
            if (*map) {
                result->entries = (*map)->entries;
                result->width = (*map)->width;
                result->extraColumns = (*map)->extraColumns;
            }
            return Value(result);
        }
        // Real copy() of a buffer allocates a fresh buffer with the same
        // bytes (a distinct value, unequal by identity to the original),
        // not a shared reference (row 2.33a).
        if (auto* buf = std::get_if<std::shared_ptr<Buffer>>(&args[0].data)) {
            auto result = std::make_shared<Buffer>();
            if (*buf) result->bytes = (*buf)->bytes;
            return Value(result);
        }
        return args[0];
    });

    // int refs(mixed val) -- real packages/develop.c's own f_refs():
    // returns the shared reference count of val, minus 1 to compensate
    // for being passed as refs()'s own argument (confirmed directly:
    // "put_number(r - 1);"). Approximated here via this driver's own
    // std::shared_ptr::use_count() for every reference-counted Value kind
    // (array, mapping, object, closure) -- real refs() also counts
    // interned/shared strings (T_STRING with STRING_COUNTED), which this
    // driver has no equivalent for (plain std::string, never interned or
    // shared between variables), so a string argument always returns 0,
    // matching real refs()'s own non-counted-string branch exactly.
    // Ints/floats: 0, matching real refs()'s own default case for every
    // value kind with no ref count at all.
    t.registerEfun("refs", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(int64_t{0});
        if (auto* arr = std::get_if<std::shared_ptr<Array>>(&args[0].data)) {
            return Value(static_cast<int64_t>(*arr ? arr->use_count() - 1 : 0));
        }
        if (auto* map = std::get_if<std::shared_ptr<Mapping>>(&args[0].data)) {
            return Value(static_cast<int64_t>(*map ? map->use_count() - 1 : 0));
        }
        if (auto* ob = std::get_if<std::shared_ptr<LpcObject>>(&args[0].data)) {
            return Value(static_cast<int64_t>(*ob ? ob->use_count() - 1 : 0));
        }
        if (auto* fn = std::get_if<std::shared_ptr<Closure>>(&args[0].data)) {
            return Value(static_cast<int64_t>(*fn ? fn->use_count() - 1 : 0));
        }
        return Value(int64_t{0});
    });

    // int member_array(mixed needle, string|mixed* haystack, void|int
    // start) -- efuns_main.c's f_member_array(): first matching index,
    // or -1. The optional 4th "search backwards" flag argument is not
    // implemented (nothing in this mudlib's boot/login/account path
    // uses it -- confirmed by grep, matching this codebase's existing
    // convention of throwing rather than silently mishandling an
    // unsupported shape).
    t.registerEfun("member_array", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2) {
            throw LpcRuntimeError("member_array: expected (needle, haystack, void|int start) arguments");
        }
        int64_t start = 0;
        if (args.size() > 2 && std::holds_alternative<int64_t>(args[2].data)) {
            start = std::get<int64_t>(args[2].data);
        }
        if (start < 0) {
            throw LpcRuntimeError("member_array: start index must be non-negative");
        }

        if (auto* haystackStr = std::get_if<std::string>(&args[1].data)) {
            if (!std::holds_alternative<int64_t>(args[0].data)) {
                throw LpcRuntimeError("member_array: needle must be an int char code when searching a string");
            }
            int64_t code = std::get<int64_t>(args[0].data);
            for (size_t i = static_cast<size_t>(start); i < haystackStr->size(); ++i) {
                if (static_cast<unsigned char>((*haystackStr)[i]) == code) {
                    return Value(static_cast<int64_t>(i));
                }
            }
            return Value(int64_t{-1});
        }

        if (auto* haystackArr = std::get_if<std::shared_ptr<Array>>(&args[1].data)) {
            if (!*haystackArr) return Value(int64_t{-1});
            const auto& items = (*haystackArr)->items;
            for (size_t i = static_cast<size_t>(start); i < items.size(); ++i) {
                if (valuesEqual(items[i], args[0])) {
                    return Value(static_cast<int64_t>(i));
                }
            }
            return Value(int64_t{-1});
        }

        throw LpcRuntimeError("member_array: haystack must be a string or an array");
    });

    // int interactive(object ob) -- true if ob is currently bound to a
    // live connection. Real FluffOS checks "ob->interactive != 0"
    // (efuns_main.c's f_interactive()); this driver's InteractiveRegistry
    // (net/InteractiveRegistry.hpp) is exactly that same membership,
    // populated by Connection::attach()/close().
    t.registerEfun("interactive", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            return Value(int64_t{0});
        }
        auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        if (!ob) return Value(int64_t{0});
        for (auto& live : InteractiveRegistry::all()) {
            if (live == ob) return Value(int64_t{1});
        }
        return Value(int64_t{0});
    });

    // int query_idle(object ob) -- real func_spec.c's own "int
    // query_idle(object);". Real comm.c's own f_query_idle():
    // "if (!ob->interactive) error(...); return current_time -
    // ob->interactive->last_time;" -- an interactive-only efun, unlike
    // userp()/interactive() which both quietly return 0 for a
    // non-interactive argument. Confirmed real and required (not
    // approximated as "always 0" for non-interactive) by this mudlib's
    // own real call sites: cmds/mortal/_who.c and _idle.c both call it
    // unguarded on this_player(), and std/user.c's own heart_beat() gates
    // its auto-idle-logout on it every heartbeat cycle for every
    // connected player -- reachable on every single tick now that
    // heart_beat() is real (see "Real call_out()/heart_beat() scheduler"
    // in STATUS.md), not just a rarely-hit command.
    t.registerEfun("query_idle", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            throw LpcRuntimeError("query_idle: expected an object argument");
        }
        auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        if (!ob) {
            throw LpcRuntimeError("query_idle: ob is not interactive");
        }
        Connection* conn = InteractiveRegistry::find(ob);
        if (!conn) {
            throw LpcRuntimeError("query_idle: ob is not interactive");
        }
        int64_t idle = static_cast<int64_t>(std::time(nullptr)) -
                        static_cast<int64_t>(conn->lastActivityTime());
        return Value(idle);
    });

    // int query_screen_width(object ob) / int query_screen_height(object
    // ob) -- Phase 0.8. Not real FluffOS efuns: confirmed by grepping
    // func_spec.c/efun_defs.c/applies_table.c directly, zero hits. Real
    // FluffOS's own actual NAWS mechanism is push-based, not pull-based:
    // the driver calls a "window_size(width, height)" apply
    // (applies.h's own APPLY_WINDOW_SIZE) on the connection's bound
    // object every time a NAWS subnegotiation arrives, confirmed against
    // comm.c directly ("apply(APPLY_WINDOW_SIZE, ip->ob, 2,
    // ORIGIN_DRIVER)"); it is up to the mudlib's own window_size()
    // handler to store the values if it wants them queryable later. That
    // apply is deliberately not implemented here -- these two efuns are
    // a driver-added pull-based convenience matching this row's own task
    // list, reading the same Connection-level terminalWidth_/Height_
    // fields the real NAWS subnegotiation parser already fills, not a
    // port of the real apply-based mechanism. Same query-by-object
    // pattern as query_idle() just above, not query_ip_number()'s
    // current-connection-only shape, since window size is inherently a
    // property of a specific player's own connection.
    t.registerEfun("query_screen_width", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            throw LpcRuntimeError("query_screen_width: expected an object argument");
        }
        auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        Connection* conn = ob ? InteractiveRegistry::find(ob) : nullptr;
        if (!conn) throw LpcRuntimeError("query_screen_width: ob is not interactive");
        return Value(static_cast<int64_t>(conn->terminalWidth()));
    });
    t.registerEfun("query_screen_height", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            throw LpcRuntimeError("query_screen_height: expected an object argument");
        }
        auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        Connection* conn = ob ? InteractiveRegistry::find(ob) : nullptr;
        if (!conn) throw LpcRuntimeError("query_screen_height: ob is not interactive");
        return Value(static_cast<int64_t>(conn->terminalHeight()));
    });

    // void request_term_size() -- real comm.c's own f_request_term_size():
    // "add_binary_message(command_giver, telnet_do_naws, ...)", a bare IAC
    // DO NAWS with no reply expected inline (the client answers later with
    // its own SB NAWS subnegotiation, already fully handled by
    // handleSubnegotiation()/query_screen_width()/query_screen_height()
    // just above -- this is the one missing half, the proactive request a
    // client that has not already volunteered WILL NAWS unprompted needs).
    // No-op (not an error) when there is no interactive command_giver,
    // matching real f_request_term_size()'s implicit
    // "command_giver->interactive" write target. Unlike
    // query_screen_width/height above, this one *is* a real efun name
    // (efun_defs.c: F_REQUEST_TERM_SIZE), not a driver-added convenience.
    t.registerEfun("request_term_size", [](VM& vm, std::vector<Value>&) -> Value {
        auto giver = resolveCommandGiver(vm);
        if (!giver) return Value{};
        if (Connection* conn = InteractiveRegistry::find(giver)) {
            conn->requestWindowSize();
        }
        return Value{};
    });

    // void request_term_type() -- real comm.c's own f_request_term_type():
    // "add_binary_message(command_giver, telnet_term_query, ...)", a bare
    // IAC SB TTYPE SEND IAC SE. Row 3.4's own first slice: this, together
    // with terminal_type() (the real APPLY_TERMINAL_TYPE, fired from
    // Server::handleConnection()) and start_request_term_type() just below,
    // is the *entire* real driver-level MTTS mechanism -- confirmed by
    // reading comm.c directly, not assumed: there is no round-counting
    // state, no comparison against a previous answer, and no "MTTS <n>"
    // bitmask table anywhere in the real driver. The actual Mud Terminal
    // Type Standard multi-round convention (ask again via this efun,
    // compare to the last terminal_type() value, stop once it repeats or a
    // third round yields "MTTS <bitmask>") is genuinely mudlib-side in real
    // FluffOS, not a driver gap -- so it is deliberately not built as a
    // driver efun here (see this row's own ROADMAP.md cell for the
    // rescoping this finding drove). Same silent no-interactive-command-
    // giver no-op as request_term_size() just above, matching real
    // f_request_term_type()'s own implicit "command_giver->interactive"
    // write target.
    t.registerEfun("request_term_type", [](VM& vm, std::vector<Value>&) -> Value {
        auto giver = resolveCommandGiver(vm);
        if (!giver) return Value{};
        if (Connection* conn = InteractiveRegistry::find(giver)) {
            conn->requestTerminalType();
        }
        return Value{};
    });

    // void start_request_term_type() -- real comm.c's own
    // f_start_request_term_type(): "add_binary_message(command_giver,
    // telnet_do_ttype, ...)", a bare IAC DO TTYPE. Distinct from
    // request_term_type() above exactly as real code keeps them two
    // separate efuns: this kicks off negotiation itself (real new_user()
    // already does so unprompted for every telnet connection --
    // Server::onNewConnection() mirrors that directly -- so the ordinary
    // case never needs this efun at all); request_term_type() asks for the
    // next value once negotiation is already under way.
    t.registerEfun("start_request_term_type", [](VM& vm, std::vector<Value>&) -> Value {
        auto giver = resolveCommandGiver(vm);
        if (!giver) return Value{};
        if (Connection* conn = InteractiveRegistry::find(giver)) {
            conn->startRequestTerminalType();
        }
        return Value{};
    });

    // string query_terminal_type(object ob) -- not a real FluffOS efun,
    // confirmed by grepping func_spec.c/efun_defs.c/applies_table.c
    // directly, zero hits, exactly like query_screen_width()/
    // query_screen_height() above (see their own header comment): real
    // FluffOS's own terminal-type delivery is push-based only
    // (terminal_type(), fired from Server::handleConnection() the moment
    // an SB TTYPE IS response is parsed). A driver-added pull-based
    // convenience reading the same Connection::terminalType() field that
    // real mechanism already fills, empty string until the first response
    // arrives. Same query-by-object pattern as query_screen_width/height,
    // not query_ip_number()'s current-connection-only shape.
    t.registerEfun("query_terminal_type", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            throw LpcRuntimeError("query_terminal_type: expected an object argument");
        }
        auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        Connection* conn = ob ? InteractiveRegistry::find(ob) : nullptr;
        if (!conn) throw LpcRuntimeError("query_terminal_type: ob is not interactive");
        return Value(conn->terminalType());
    });

    // string terminal_colour(string str, mapping colours, void|int
    // max_colors, void|int indent) -- Phase 0.8's own item 4 (net/
    // instruct.md). Real signature confirmed against efun_defs.c ground
    // truth ("terminal_colour",F_TERMINAL_COLOUR,...,T_STRING,T_MAPPING,
    // T_NUMBER,T_NUMBER,...,2,4 min/max args) since neither func_spec.c's
    // text nor any .c file in the vendored tree carries an actual
    // f_terminal_colour() body -- another real gap in this specific
    // archived copy, same situation as debug_info/base_name/pluralize.
    // net/instruct.md's own citation for this item ("comm.c's telnet_neg()")
    // is the same already-disproven function name from row 0.8's NAWS
    // item, and doesn't actually describe terminal_colour() at all --
    // %^ colour markup is not a telnet/RFC concern, it is a mudlib-level
    // string convention.
    //
    // Not guessed at despite the missing driver-side reference: this
    // exact mudlib has real, load-bearing, live code implementing the
    // identical operation twice -- daemon/terminal.c's own no_colours()
    // ("explode(str, \"%^\"); if (term_info[\"unknown\"][bits[i]])
    // bits[i] = \"\";") and std/user.c's own message() colourizing step
    // ("explode(msg, \"%^\"); if (term_info[words[i]]) words[i] =
    // term_info[words[i]];"). Both split on the literal two-character
    // delimiter "%^" (not a paired "%^TOKEN%^" wrapper parse), then for
    // each resulting segment: if it is a truthy key in the caller-
    // supplied colour mapping, substitute (or, for the "no colour"
    // case, strip to empty); otherwise the segment is left exactly as
    // it was, matching real plain text passing through untouched.
    // Implemented as one unified version of both real functions,
    // parameterized on max_colors exactly as the real signature implies:
    // max_colors > 0 substitutes a recognized token with the mapping's
    // own string value (user.c's own real behavior); max_colors <= 0
    // (or omitted -- default 1, "on", since a caller who bothered to
    // pass a real colours mapping is not typically asking for it to be
    // discarded) strips a recognized token to nothing (no_colours()'s
    // own real behavior).
    //
    // "Truthy" here follows this driver's own established isTruthy()
    // (an empty string counts as falsy), not real LPC's own "any string
    // reference, even empty, is truthy" rule -- a genuine, pre-existing
    // divergence in this driver's own Value semantics (confirmed
    // directly in Value.cpp, not new to this efun), so a colour mapping
    // whose own values are empty strings (real terminal.c's own
    // "unknown" variant, used for clients with no colour support) would
    // leave recognized tokens as literal un-stripped text here rather
    // than genuinely stripping them the way real no_colours() does.
    // Flagged rather than silently diverging unnoticed; not fixed, since
    // isTruthy()'s own string-emptiness rule is used everywhere else in
    // this driver and changing it is far outside this row's own scope.
    // indent (the 4th argument) is accepted for signature compatibility
    // only, not implemented -- no reference for its real line-wrapping
    // behavior exists anywhere in the vendored tree, and nothing
    // confirmed live in this mudlib calls terminal_colour() at all (zero
    // real call sites found by grep; only the %^ markup convention
    // itself is real and load-bearing, always processed by this
    // mudlib's own inline code, never by calling this efun).
    t.registerEfun("terminal_colour", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::shared_ptr<Mapping>>(args[1].data)) {
            throw LpcRuntimeError("terminal_colour: expected (string, mapping, void|int, void|int)");
        }
        const std::string& str = std::get<std::string>(args[0].data);
        auto colours = std::get<std::shared_ptr<Mapping>>(args[1].data);
        int64_t maxColors = 1;
        if (args.size() > 2 && std::holds_alternative<int64_t>(args[2].data)) {
            maxColors = std::get<int64_t>(args[2].data);
        }

        std::vector<std::string> parts;
        size_t pos = 0;
        for (;;) {
            size_t next = str.find("%^", pos);
            if (next == std::string::npos) {
                parts.push_back(str.substr(pos));
                break;
            }
            parts.push_back(str.substr(pos, next - pos));
            pos = next + 2;
        }

        auto lookup = [&](const std::string& key) -> const Value* {
            if (!colours) return nullptr;
            for (const auto& entry : colours->entries) {
                if (std::holds_alternative<std::string>(entry.first.data) &&
                    std::get<std::string>(entry.first.data) == key) {
                    return &entry.second;
                }
            }
            return nullptr;
        };

        std::string result;
        for (const auto& part : parts) {
            const Value* found = lookup(part);
            if (found && isTruthy(*found)) {
                if (maxColors > 0 && std::holds_alternative<std::string>(found->data)) {
                    result += std::get<std::string>(found->data);
                }
                // else: no colour support, or a non-string mapping value
                // -- strip the recognized token to nothing.
            } else {
                result += part;
            }
        }
        return Value(result);
    });

    // string query_ip_number(void|object ob) -- comm.c's real
    // query_ip_number(): "inet_ntoa(ob->interactive->addr.sin_addr)",
    // defaulting to command_giver when ob is omitted. This driver has
    // no separate "command_giver" concept from "the connection driving
    // the current call" (OutputContext::current(), the same stand-in
    // used throughout this driver's other connection-scoped efuns --
    // receive(), input_to()), so the object argument is accepted for
    // signature compatibility but not actually used to look up a
    // *different* connection's address; only the current one's own
    // peer address is queried, via getpeername() on its fd.
    t.registerEfun("query_ip_number", [](VM&, std::vector<Value>&) -> Value {
        Connection* conn = OutputContext::current();
        if (!conn) return Value{};
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        if (::getpeername(conn->fd(), reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
            return Value{};
        }
        char buf[INET_ADDRSTRLEN];
        if (!::inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf))) {
            return Value{};
        }
        return Value(std::string(buf));
    });

    // string query_ip_name(void|object ob) -- real comm.c's own
    // query_ip_name() does a reverse-DNS lookup of the peer address,
    // falling back to the numeric IP when hostname resolution is
    // unavailable/disabled (real FluffOS itself gates this behind a
    // config option and has the same numeric fallback). This driver
    // does no DNS resolution of its own at all (a blocking reverse
    // lookup inline in the connection-handling loop would stall every
    // other connection during it) -- always takes that same fallback,
    // returning the numeric IP string, matching query_ip_number()'s own
    // "only the current connection, via OutputContext::current()"
    // simplification.
    t.registerEfun("query_ip_name", [](VM&, std::vector<Value>&) -> Value {
        Connection* conn = OutputContext::current();
        if (!conn) return Value{};
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        if (::getpeername(conn->fd(), reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
            return Value{};
        }
        char buf[INET_ADDRSTRLEN];
        if (!::inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf))) {
            return Value{};
        }
        return Value(std::string(buf));
    });

    // int query_ip_port(void|object ob default: command_giver) -- real
    // packages/contrib.c's own query_ip_port(): the local port number
    // ob's connection is using. This driver has exactly one listening
    // port (Config::port(), Server::listen()'s own single accept loop --
    // confirmed by grep, no multi-port SocketRegistry exists), so unlike
    // query_ip_number()/query_ip_name() above (which fall back to "only
    // the current connection" since a real per-connection lookup would
    // need one), this one supports the real explicit ob argument
    // properly: any currently-interactive object's real answer is always
    // the single configured port, not something that needs tracking per
    // Connection. Returns 0 if ob (or, with no argument, command_giver)
    // is not currently interactive, matching real query_ip_port(0)'s own
    // "!ob->interactive" branch.
    t.registerEfun("query_ip_port", [](VM& vm, std::vector<Value>& args) -> Value {
        std::shared_ptr<LpcObject> ob;
        if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        } else {
            ob = resolveCommandGiver(vm);
        }
        if (!ob || !InteractiveRegistry::find(ob)) return Value(int64_t{0});
        return Value(static_cast<int64_t>(vm.config().port()));
    });

    // socket_* efun family (ROADMAP row 0.10). Signatures, mode/state
    // values, error codes, and callback argument conventions verified
    // directly against fluffos-2.9-ds2.08/socket_efuns.c and
    // socket_efuns.h (the real C implementation, not net/instruct.md's
    // own proposed design for this row -- see below for what instruct.md
    // got wrong). Actual socket() family syscalls and state-machine logic
    // live in SocketRegistry (mirroring how Connection, not EfunTable,
    // owns telnet parsing); these registrations are thin arg-validation
    // wrappers, matching every other net-layer efun in this file.
    //
    // net/instruct.md's own "What to build" list for this row (net/
    // instruct.md's own text) does not match the real efun family in
    // three ways, each corrected here rather than followed blindly, same
    // as every previous row this pattern has shown up in:
    // 1. It lists a `socket_read(int handle) -> mixed` efun. No such
    //    efun exists anywhere in efun_defs.c (grepped directly) -- real
    //    FluffOS sockets are purely callback-driven; incoming data always
    //    arrives via read_callback firing asynchronously (see
    //    Server::pollSockets()), never via a synchronous read call. Not
    //    implemented, because it is not real.
    // 2. Its proposed `socket_create(int type, string callback)` two-arg
    //    shape and `socket_bind(int handle, int port)` shape both drop a
    //    real optional third argument each actually has (void|string
    //    close_callback; void|string addr) -- confirmed against
    //    efun_defs.c's own F_SOCKET_CREATE (2-3 args) / F_SOCKET_BIND
    //    (2-3 args). Implemented with the real 3-arg signatures.
    // 3. Its efun list omits socket_listen and socket_accept entirely
    //    (despite citing them nowhere) while real FluffOS's socket family
    //    cannot open a listening/accepting server socket without both --
    //    confirmed real via efun_defs.c's own F_SOCKET_LISTEN/
    //    F_SOCKET_ACCEPT entries and lib/daemon/network.c's own live
    //    (non-doc) socket_create()/socket_bind() call site. Implemented,
    //    since "basics" cannot mean client-only.
    //
    // Not implemented, and flagged here rather than left silently absent:
    // MUD mode (arbitrary-LPC-value socket_write, needs real wire framing
    // this driver has no equivalent for -- see LpcSocket.hpp), the two
    // BINARY modes (no buffer type in this driver's Value at all, the
    // same pre-existing gap noted on to_int()'s own T_BUFFER case), and
    // socket_release()/socket_acquire() (PACKAGE_SOCKETS-gated object-to-
    // object socket transfer, out of "basics" scope, not cited by
    // net/instruct.md either).

    // int socket_create(int mode, string|function read_callback, void|string|function close_callback)
    t.registerEfun("socket_create", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("socket_create: expected (int, string|function, void|string|function)");
        }
        int mode = static_cast<int>(std::get<int64_t>(args[0].data));
        Value closeCb = args.size() > 2 ? args[2] : Value{};
        int result = SocketRegistry::create(mode, args[1], closeCb, vm.currentObject());
        return Value(static_cast<int64_t>(result));
    });

    // int socket_bind(int fd, int port, void|string addr)
    t.registerEfun("socket_bind", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<int64_t>(args[0].data) ||
            !std::holds_alternative<int64_t>(args[1].data)) {
            throw LpcRuntimeError("socket_bind: expected (int, int, void|string)");
        }
        int handle = static_cast<int>(std::get<int64_t>(args[0].data));
        int port = static_cast<int>(std::get<int64_t>(args[1].data));
        bool hasAddr = args.size() > 2 && std::holds_alternative<std::string>(args[2].data);
        std::string addr = hasAddr ? std::get<std::string>(args[2].data) : std::string();
        int result = SocketRegistry::bind(handle, port, addr, hasAddr, vm.currentObject());
        return Value(static_cast<int64_t>(result));
    });

    // int socket_listen(int fd, string|function connect_callback)
    t.registerEfun("socket_listen", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("socket_listen: expected (int, string|function)");
        }
        int handle = static_cast<int>(std::get<int64_t>(args[0].data));
        int result = SocketRegistry::listen(handle, args[1], vm.currentObject());
        return Value(static_cast<int64_t>(result));
    });

    // int socket_accept(int fd, string|function read_callback, string|function write_callback)
    t.registerEfun("socket_accept", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 3 || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("socket_accept: expected (int, string|function, string|function)");
        }
        int handle = static_cast<int>(std::get<int64_t>(args[0].data));
        int result = SocketRegistry::accept(handle, args[1], args[2], vm.currentObject());
        return Value(static_cast<int64_t>(result));
    });

    // int socket_connect(int fd, string address, string|function read_callback, string|function write_callback)
    t.registerEfun("socket_connect", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 4 || !std::holds_alternative<int64_t>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("socket_connect: expected (int, string, string|function, string|function)");
        }
        int handle = static_cast<int>(std::get<int64_t>(args[0].data));
        const std::string& address = std::get<std::string>(args[1].data);
        int result = SocketRegistry::connect(handle, address, args[2], args[3], vm.currentObject());
        return Value(static_cast<int64_t>(result));
    });

    // int socket_write(int fd, mixed message, void|string address)
    t.registerEfun("socket_write", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("socket_write: expected (int, mixed, void|string)");
        }
        int handle = static_cast<int>(std::get<int64_t>(args[0].data));
        bool hasAddress = args.size() > 2 && std::holds_alternative<std::string>(args[2].data);
        std::string address = hasAddress ? std::get<std::string>(args[2].data) : std::string();
        int result = SocketRegistry::write(handle, args[1], address, hasAddress, vm.currentObject());
        return Value(static_cast<int64_t>(result));
    });

    // int socket_close(int fd)
    // Directly closes out two of row 0.13's own efun-table-growth batch
    // (efun/instruct.md's own status table lists both socket_close and
    // socket_write as "belongs to row 0.10's LpcSocket/SocketRegistry
    // subsystem, not a standalone efun") -- both fall out of this row's
    // own implementation as-is, not as separately scoped extra work.
    t.registerEfun("socket_close", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("socket_close: expected (int)");
        }
        int handle = static_cast<int>(std::get<int64_t>(args[0].data));
        int result = SocketRegistry::close(handle, vm.currentObject());
        return Value(static_cast<int64_t>(result));
    });

    // int socket_release(int fd, object ob, string|function callback)
    // -- real socket_efuns.c's own socket_release(): hands the fd off to
    // ob, which must complete the handoff by calling socket_acquire()
    // itself, synchronously, from inside callback (real code's own
    // "call the callback, then re-check whether S_RELEASE is still set"
    // shape -- see SocketRegistry::beginRelease()/isReleased()/
    // cancelRelease()'s own comments for the real per-step citation,
    // this registration only owns the callback-firing step none of
    // those three can do themselves, no VM access at that layer).
    // Previously filed as "Tier 3, out of basics scope" (instruct.md);
    // re-investigated this session given real, if modest, cross-corpus
    // demand (lima's own socket.c/old_socket.c, dead-souls' own
    // i3router/imc2server socket-handoff daemons) and found this
    // driver's own existing SocketRegistry/LpcSocket infrastructure
    // (owner tracking, string|function callback storage, real error
    // codes) already fits the real mechanism directly -- no new
    // architecture needed after all, just two new fields and two new
    // methods, corrected in STATUS.md/instruct.md rather than left
    // standing.
    t.registerEfun("socket_release", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 3 || !std::holds_alternative<int64_t>(args[0].data) ||
            !std::holds_alternative<std::shared_ptr<LpcObject>>(args[1].data)) {
            throw LpcRuntimeError("socket_release: expected (int, object, string|function)");
        }
        int handle = static_cast<int>(std::get<int64_t>(args[0].data));
        auto ob = std::get<std::shared_ptr<LpcObject>>(args[1].data);
        Value callback = args[2];

        int rc = SocketRegistry::beginRelease(handle, ob, vm.currentObject());
        if (rc != SocketErr::Success) return Value(static_cast<int64_t>(rc));

        // Real "safe_call_function_pointer(callback->u.fp, 2) :
        // safe_apply(callback->u.string, ob, 2, ORIGIN_INTERNAL)" --
        // mirrors Server.cpp's own fireSocketCallback() dispatch exactly
        // (same real function/string split, same real ORIGIN_INTERNAL),
        // duplicated narrowly here rather than shared across the two
        // files since fireSocketCallback lives in Server.cpp's own
        // anonymous namespace and this is the only call site outside it
        // that needs the identical dispatch.
        std::vector<Value> callbackArgs{Value(static_cast<int64_t>(handle)), Value(ob)};
        if (auto* closure = std::get_if<std::shared_ptr<Closure>>(&callback.data)) {
            if (*closure) {
                try {
                    vm.callClosure(*closure, callbackArgs);
                } catch (const std::exception&) {
                    // Real safe_call_function_pointer(): an error inside
                    // the callback is caught and does not abort
                    // socket_release() itself.
                }
            }
        } else if (auto* name = std::get_if<std::string>(&callback.data)) {
            if (ob) {
                try {
                    vm.callFunction(ob, *name, callbackArgs, Origin::Internal);
                } catch (const std::exception&) {
                }
            }
        }

        if (SocketRegistry::isReleased(handle)) {
            SocketRegistry::cancelRelease(handle);
            return Value(static_cast<int64_t>(SocketErr::ESockNotRlsd));
        }
        return Value(static_cast<int64_t>(SocketErr::Success));
    });

    // int socket_acquire(int fd, string|function read_callback,
    //                     string|function write_callback,
    //                     string|function close_callback)
    // -- real socket_efuns.c's own socket_acquire(), the completing half
    // of socket_release() above; see SocketRegistry::acquire()'s own
    // comment for the real per-step citation.
    t.registerEfun("socket_acquire", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 4 || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError(
                "socket_acquire: expected (int, string|function, string|function, string|function)");
        }
        int handle = static_cast<int>(std::get<int64_t>(args[0].data));
        int result = SocketRegistry::acquire(handle, args[1], args[2], args[3], vm.currentObject());
        return Value(static_cast<int64_t>(result));
    });

    // string socket_error(int error)
    t.registerEfun("socket_error", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("socket_error: expected (int)");
        }
        int error = static_cast<int>(std::get<int64_t>(args[0].data));
        return Value(SocketRegistry::errorString(error));
    });

    // mixed *socket_status(void|int fd)
    t.registerEfun("socket_status", [](VM&, std::vector<Value>& args) -> Value {
        if (!args.empty() && std::holds_alternative<int64_t>(args[0].data)) {
            int handle = static_cast<int>(std::get<int64_t>(args[0].data));
            auto sock = SocketRegistry::find(handle);
            if (!sock) return Value(std::make_shared<Array>());
            return SocketRegistry::statusOne(*sock);
        }
        auto result = std::make_shared<Array>();
        for (auto& sock : SocketRegistry::all()) {
            result->items.push_back(SocketRegistry::statusOne(*sock));
        }
        return Value(result);
    });

    // string socket_address(int|object handle, void|int local) -- real
    // packages/sockets.c's own f_socket_address(), confirmed directly:
    // an object argument must be currently interactive (returns 0/falsy
    // otherwise), and returns that connection's own real peer "addr
    // port" string (real code reads straight off the interactive_t's own
    // stored sockaddr_in, the same value getpeername() gives here). An
    // int argument is a socket handle, and `local` (default 0) selects
    // which of the two addresses SocketRegistry already tracks per
    // LpcSocket: 0 = remote/peer (real "r_addr"), 1 = local/bound (real
    // "l_addr") -- confirmed against real get_socket_address(fd, addr,
    // port, local)'s own "(local ? &l_addr : &r_addr)" ternary.
    t.registerEfun("socket_address", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("socket_address: expected (int|object, void|int)");
        bool local = args.size() > 1 && std::holds_alternative<int64_t>(args[1].data) &&
                     std::get<int64_t>(args[1].data) != 0;

        if (auto* obPtr = std::get_if<std::shared_ptr<LpcObject>>(&args[0].data)) {
            Connection* conn = *obPtr ? InteractiveRegistry::find(*obPtr) : nullptr;
            if (!conn) return Value(static_cast<int64_t>(0));
            sockaddr_in addr{};
            socklen_t len = sizeof(addr);
            if (::getpeername(conn->fd(), reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
                return Value(static_cast<int64_t>(0));
            }
            char buf[INET_ADDRSTRLEN];
            if (!::inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf))) {
                return Value(static_cast<int64_t>(0));
            }
            return Value(std::string(buf) + " " + std::to_string(ntohs(addr.sin_port)));
        }

        if (std::holds_alternative<int64_t>(args[0].data)) {
            int handle = static_cast<int>(std::get<int64_t>(args[0].data));
            auto sock = SocketRegistry::find(handle);
            if (!sock) return Value(std::string());
            const std::string& addr = local ? sock->localAddr : sock->remoteAddr;
            int port = local ? sock->localPort : sock->remotePort;
            return Value(addr + " " + std::to_string(port));
        }

        throw LpcRuntimeError("socket_address: expected (int|object, void|int)");
    });

    // string query_host_name() -- real efuns_main.c's own
    // f_query_host_name(): the machine's own real system hostname
    // (gethostname(), not this mudlib's configured mud_name()), 0 if
    // unavailable. Genuinely real but marginal: its only hit in
    // mudlib/nightmare3_fluffos_v2/lib/ is buried inside
    // secure/include/network.h's own (UDP intermud) START_MSG macro,
    // not a direct call site.
    t.registerEfun("query_host_name", [](VM&, std::vector<Value>&) -> Value {
        char buf[256];
        if (::gethostname(buf, sizeof(buf)) != 0) return Value(static_cast<int64_t>(0));
        buf[sizeof(buf) - 1] = '\0';
        return Value(std::string(buf));
    });

    // void flush_messages(object ob) -- real comm.c's own
    // flush_message()/add_message() buffering: forces any output queued
    // for `ob`'s own connection out immediately rather than waiting for
    // the current backend cycle's own automatic flush. This driver's
    // Connection::send() already writes synchronously (a direct blocking
    // ::write() loop, no output-buffering layer sitting in front of it
    // at all -- confirmed directly in Connection.cpp), so the real
    // *observable* effect ("nothing is left unsent") is already this
    // driver's default for every write(), unconditionally. Implemented
    // as a no-op beyond confirming `ob` is actually interactive (real
    // flush_message() silently does nothing for a non-interactive
    // object too), not a fabricated buffering mechanism. Confirmed
    // real, live-reachable: secure/SimulEfun/misc.c's own tc()
    // ("tell_object(dude, ...); if(dude) flush_messages(dude);").
    t.registerEfun("flush_messages", [](VM&, std::vector<Value>& args) -> Value {
        if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
            (void)(ob ? InteractiveRegistry::find(ob) : nullptr);
        }
        return Value{};
    });

    // void set_author(string name) -- real packages/mudlib_stats.c's own
    // f_set_author()/set_author(): tags current_object's own
    // memory/object-count accounting under the given author name in
    // real FluffOS's PACKAGE_MUDLIB_STATS system (mudlib_stats_t's own
    // `objects` counter, ob->stats.author). No other observable effect
    // anywhere in the real source -- confirmed directly, the only real
    // consumer is author_stats()/domain_stats(), themselves already
    // excluded from this table (architecture mismatch: this driver has
    // no per-author memory/object-count tracking model at all, same
    // category as mud_status/cache_stats). Since nothing in this driver
    // could ever observe what set_author() recorded either way, a no-op
    // is not an approximation of real behavior here, it is behaviorally
    // complete -- same reasoning flush_messages() above already
    // establishes for its own already-default real effect. Zero genuine
    // calls to the *core efun* anywhere across all six corpora once
    // checked directly -- the one raw grep hit (lima's own
    // std/book.c) is a same-named local function *definition* (a
    // book object's own "who wrote this" property setter, a same-name
    // shadow, not a call to this efun at all), a real trap this
    // ranking's own tight-but-not-context-aware `\bname\(` matching
    // cannot itself distinguish from a genuine call. Implemented anyway,
    // same bar 0-call-site items like named_livings/query_replaced_program
    // were included under previously: real, self-contained, and its own
    // no-op is provably complete rather than a guess.
    t.registerEfun("set_author", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("set_author: expected a string argument");
        }
        return Value{};
    });

    // object function_owner(function fp) -- real packages/contrib.c's
    // own f_function_owner(): the object that owned fp at the moment it
    // was created (real funptr_hdr_t::owner), via real interpret.h's own
    // put_unrefed_object() macro -- confirmed directly rather than
    // assumed: "if (!(x) || (x)->flags & O_DESTRUCTED) *sp = const0u;
    // else ...", a real int 0 for a null *or destructed* owner, not the
    // object reference regardless (an earlier draft of this comment
    // assumed no O_DESTRUCTED filter existed; re-read and corrected
    // before this landed, not caught only by the test that was written
    // against the wrong assumption first). This driver's own
    // Closure::owner is already real FluffOS's own hdr.owner field,
    // weak_ptr rather than a full reference -- lock() covers the "gone
    // entirely" half of that real check on its own (fails once the
    // owner's last real reference is actually gone), isDestructed()
    // covers the other half explicitly (a still-referenced-but-
    // destructed owner locks successfully but must still read back as 0
    // here, matching real semantics exactly).
    t.registerEfun("function_owner", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<Closure>>(args[0].data)) {
            throw LpcRuntimeError("function_owner: expected a function argument");
        }
        auto closure = std::get<std::shared_ptr<Closure>>(args[0].data);
        if (!closure) return Value(int64_t{0});
        auto owner = closure->owner.lock();
        // Real put_unrefed_object() (interpret.h): "if (!(x) || (x)->flags
        // & O_DESTRUCTED) *sp = const0u" -- a real int 0, not void, for a
        // null or destructed owner (this driver's own isDestructed() is
        // covered too: a still-referenced-but-destructed owner still
        // locks successfully but must still read back as 0 here).
        if (!owner || owner->isDestructed()) return Value(int64_t{0});
        return Value(owner);
    });

    // int num_classes(object ob) -- real packages/contrib.c's own
    // f_num_classes(): "sp->u.ob->prog->num_classes", how many LPC
    // `class` struct declarations ob's own compiled program defines.
    // This driver's compiler has never implemented LPC class
    // declarations at all (no TYPE_CLASS value kind -- the same gap
    // assemble_class/disassemble_class/fetch_class_member/
    // store_class_member above are all excluded for), so this is not a
    // guess or an approximation: every object this driver can possibly
    // compile has exactly zero class declarations, unconditionally and
    // certainly, not merely by default.
    t.registerEfun("num_classes", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
            throw LpcRuntimeError("num_classes: expected an object argument");
        }
        return Value(int64_t{0});
    });

    // object *users() -- every object currently bound to a live
    // connection (real array.c's f_users(), backed by all_users[]).
    t.registerEfun("users", [](VM&, std::vector<Value>&) -> Value {
        auto result = std::make_shared<Array>();
        for (auto& ob : InteractiveRegistry::all()) {
            result->items.emplace_back(ob);
        }
        return Value(result);
    });

    // object *objects(void|string|function filter) -- real array.c's
    // f_objects(), confirmed directly: every loaded object (blueprints
    // and clones alike), optionally narrowed by calling filter(candidate)
    // -- a Closure is called directly, a string name is applied on
    // current_object with the candidate as its one argument (real
    // "apply(func, current_object, 1, ORIGIN_EFUN)"). Real semantics
    // distinguish two very different failure modes, both matched here:
    // the callback returning a plain falsy T_NUMBER 0 just excludes that
    // one candidate, while the callback failing outright (function
    // missing, or throwing) aborts the whole call to an empty array
    // (real "if (!v ...) { ... push_refed_array(&the_null_array); }"),
    // not merely a single exclusion.
    //
    // Needed a live-object registry that did not exist before this
    // batch (InteractiveRegistry only ever covered connected players) --
    // see LiveObjectRegistry.hpp's own comment and
    // src/efun/instruct.md's corrected Tier 1 table.
    // object *get_garbage() -- real packages/contrib.c's own real
    // f_get_garbage() (F_GET_GARBAGE, on in this exact bundled
    // fluffos-2.23-ds03 build, confirmed via packages/contrib_spec.c's
    // own "object *get_garbage();"). Every live object matching real
    // garbage_check()'s own exact four-part condition (contrib.c:2320-
    // 2327), ported directly rather than approximated: a real clone
    // (O_CLONE, this driver's own isClone()), with no environment (real
    // "!ob->super"), not shadowing another object (real
    // "!ob->shadowing"), and exactly one real reference left (real
    // "ob->ref == 1" -- nothing beyond the object table itself still
    // holds it).
    //
    // "ob->ref == 1" has no direct C++ equivalent field, approximated
    // via shared_ptr::use_count(): LiveObjectRegistry's own internal
    // table stores weak_ptr, not shared_ptr (LiveObjectRegistry.cpp),
    // so it contributes zero strong references of its own; all()
    // returns a fresh std::vector<shared_ptr<LpcObject>> by value, one
    // new strong reference per live object, moved (not copied) into it
    // from each lock()'d weak_ptr. So the one structural reference this
    // call itself holds is this loop's own copy, and "no other real
    // LPC-visible reference exists" (real ref==1) is observed here as
    // use_count() == 1 exactly, not 2 -- confirmed by reading both
    // LiveObjectRegistry::add() (weak_ptr push_back) and ::all() (lock()
    // + std::move into the result) directly rather than assumed.
    //
    // This condition is real code, correctly ported, but confirmed live
    // (not assumed) to be effectively always-empty in this driver, for
    // an architectural reason worth naming plainly rather than silently:
    // real FluffOS's own object table is itself a strong holder, so
    // "ref==1" describes a real object that keeps existing, genuinely
    // orphaned but still resident, until something later finds and
    // destructs it. LiveObjectRegistry deliberately holds only weak_ptr
    // (its own header comment: needed so ordinary C++ RAII, not a
    // manual sweep, frees an object once nothing else references it),
    // so the instant an AMLP object would satisfy real "ref==1" here,
    // its one remaining strong reference is the assignment/local-store
    // that is about to overwrite or drop it -- the object is freed by
    // that same statement, before any later get_garbage() call could
    // ever observe it as still alive. Not a bug to fix: this driver's
    // own memory model (auto-collect on last-reference-drop) and real
    // LPC's own model (persist until explicit destruct()) are genuinely
    // different lifecycle contracts, and get_garbage() is exactly the
    // one place that difference becomes LPC-observable. The other three
    // conditions (isClone/environment/shadowing) are real and do filter
    // real, still-referenced clones correctly either way, tested below.
    //
    // The real max_array_size truncation (contrib.c:2335-2336) is not
    // replicated: this driver has no config-backed __MAX_ARRAY_SIZE__
    // yet and no corpus evidence the real cap is ever actually reached
    // at boot -- a known simplification, named rather than silently
    // dropped.
    //
    // Found live continuing the same Dead Souls 3.8.2 boot attempt:
    // secure/sefun/sefun.c's own real call_out() wrapper calls this
    // unconditionally on every real call_out(), needed for master.c's
    // own create() to complete at all.
    t.registerEfun("get_garbage", [](VM&, std::vector<Value>&) -> Value {
        auto result = std::make_shared<Array>();
        for (auto& ob : LiveObjectRegistry::all()) {
            if (!ob) continue;
            if (ob.use_count() != 1) continue;
            if (!ob->isClone()) continue;
            if (!ob->environment().expired()) continue;
            if (!ob->shadowing().expired()) continue;
            result->items.emplace_back(ob);
        }
        return Value(result);
    });

    t.registerEfun("objects", [](VM& vm, std::vector<Value>& args) -> Value {
        auto all = LiveObjectRegistry::all();
        if (args.empty() || std::holds_alternative<std::monostate>(args[0].data)) {
            auto result = std::make_shared<Array>();
            for (auto& ob : all) result->items.emplace_back(ob);
            return Value(result);
        }
        auto* closurePtr = std::get_if<std::shared_ptr<Closure>>(&args[0].data);
        std::string funcName;
        bool isClosure = closurePtr && *closurePtr;
        if (!isClosure) {
            if (!std::holds_alternative<std::string>(args[0].data)) {
                throw LpcRuntimeError("objects: filter must be a string or function");
            }
            funcName = std::get<std::string>(args[0].data);
        }
        auto current = vm.currentObject();
        // Real "!v" (apply()/call_function_pointer() itself failing to
        // find/run the callback at all) aborts the *whole* call to an
        // empty array, not just a per-candidate exclusion. This driver's
        // own VM::callFunction() cannot signal that distinctly from "the
        // callback genuinely returned void" -- unlike real apply(), a
        // missing function silently returns Value{} rather than
        // throwing (the same established convention every other
        // driver-invoked apply in this codebase already relies on, e.g.
        // a missing logon()/heart_beat()) -- so the string-name form
        // checks functionExists() explicitly up front instead of trying
        // to catch a call that was never going to throw. The closure
        // form does not need this: VM::callClosure() already throws for
        // a closure whose bare name resolves to nothing (its own
        // documented contract), which the catch below still handles.
        if (!isClosure && (!current || !vm.functionExists(current, funcName))) {
            return Value(std::make_shared<Array>());
        }
        auto result = std::make_shared<Array>();
        for (auto& ob : all) {
            Value verdict;
            try {
                if (isClosure) {
                    verdict = vm.callClosure(*closurePtr, {Value(ob)});
                } else {
                    verdict = vm.callFunction(current, funcName, {Value(ob)});
                }
            } catch (const std::exception&) {
                return Value(std::make_shared<Array>());
            }
            bool excluded = std::holds_alternative<int64_t>(verdict.data) &&
                             std::get<int64_t>(verdict.data) == 0;
            if (!excluded) result->items.emplace_back(ob);
        }
        return Value(result);
    });

    // object *livings() -- real array.c's own livings(): every loaded
    // object with O_ENABLE_COMMANDS set (real livings_filter(): "return
    // (ob->flags & O_ENABLE_COMMANDS);"), which is exactly
    // LpcObject::commandsEnabled() in this driver, confirmed against
    // enable_commands()/disable_commands()'s own registration.
    //
    // Bare livings() calls from mudlib code never reach this: this
    // mudlib's own secure/SimulEfun/SimulEfun.c defines a real simul_efun
    // of the same name ("object *livings() { return efun::livings() -
    // (efun::livings() - objects()); }"), and this driver's tiered call
    // resolution (local -> inherited -> simul_efun -> core efun) means
    // that simul_efun always wins for a bare call, the same "unreachable
    // core registration" situation as tell_object/say/shout/translate/
    // event. This one is different in one respect, though: that simul_efun
    // body itself calls efun::livings() explicitly (this driver's real,
    // already-working "efun::name(...)" escape hatch -- Lexer.cpp/
    // Parser.cpp), so the core registration below is genuinely
    // load-bearing, just reached only from inside that one file.
    t.registerEfun("livings", [](VM&, std::vector<Value>&) -> Value {
        auto result = std::make_shared<Array>();
        for (auto& ob : LiveObjectRegistry::all()) {
            if (ob->commandsEnabled()) result->items.emplace_back(ob);
        }
        return Value(result);
    });

    // object *named_livings() -- real packages/contrib.c's own
    // f_named_livings(): unlike livings() above (every O_ENABLE_COMMANDS
    // object, found by walking the whole object list), this one walks
    // only hashed_living[] -- objects that actually called
    // set_living_name(), i.e. LivingNameRegistry -- which is why the
    // real driver's own doc comment calls it "substantially faster."
    // Also applies real object_visible()'s O_HIDDEN/valid_hide() gate
    // (confirmed in the real source right alongside O_ENABLE_COMMANDS,
    // both under the same "#ifdef F_SET_HIDE" as everywhere else this
    // driver already applies isVisibleToObserver()), which
    // LivingNameRegistry::allWithCommandsEnabled() itself cannot check
    // without VM access -- applied here instead. Zero real call sites in
    // this mudlib (this efun's own name is never called directly
    // anywhere in mudlib/nightmare3/lib), implemented anyway as a small,
    // self-contained completion of the already-real LivingNameRegistry
    // rather than skipped for a missing-infra reason.
    t.registerEfun("named_livings", [](VM& vm, std::vector<Value>&) -> Value {
        auto result = std::make_shared<Array>();
        for (auto& ob : LivingNameRegistry::allWithCommandsEnabled()) {
            if (isVisibleToObserver(vm, ob)) result->items.emplace_back(ob);
        }
        return Value(result);
    });

    // int reclaim_objects() -- real reclaim.c's own reclaim_objects():
    // walks every live object's own variables, recursing into arrays/
    // mappings/closure-bound-args, rewriting any reference to a now-
    // destructed object to plain int 0 and returning how many were
    // found (see reclaimSweepValue()'s own comment for the exact
    // recursion shape, including its real map_delete()-not-rekey
    // handling of a destructed mapping *key*). Real reclaim_objects()
    // also calls reclaim_call_outs() first, a separate internal pass
    // that eagerly drops pending call_outs targeting a destructed
    // object and does not itself contribute to the returned count
    // (confirmed directly: that count is a call_out.c-local static,
    // reclaim.c's own `cleaned` is unrelated) -- not ported here since
    // this driver's Scheduler already reaches the same observable end
    // state lazily instead (a call_out to a destructed target is
    // already silently skipped at firing time rather than proactively
    // removed early, confirmed by the existing
    // testCallOutSkipsDestructedTargetSilently coverage), so there is
    // nothing left to eagerly clean there. This driver's own
    // shared_ptr-managed object lifetime means nothing here is actually
    // needed to reclaim memory (a destructed object with no remaining
    // references is already freed automatically) -- implemented anyway
    // for the real, observable LPC-level effect: a variable/array
    // element/mapping value still holding a stale reference reads back
    // as int 0 proactively, without waiting for this driver's own
    // lazy coerceIfDestructed() to be triggered by an actual read.
    t.registerEfun("reclaim_objects", [](VM&, std::vector<Value>&) -> Value {
        int64_t cleaned = 0;
        for (auto& ob : LiveObjectRegistry::all()) {
            for (auto& var : ob->variables()) {
                cleaned += reclaimSweepValue(var, 0);
            }
        }
        return Value(cleaned);
    });

    // object find_player(string name) / object find_living(string name)
    // -- real add_action.c: both efuns are the exact same
    // find_living_object(str, user) function, differing only in the
    // "user" flag (find_player passes 1, find_living passes 0), which
    // gates on real O_ONCE_INTERACTIVE -- not "currently connected".
    // Previously this driver approximated find_player() by walking
    // InteractiveRegistry (== users(), currently-connected-only) and
    // asking each object its own query_name() -- silently wrong in two
    // ways once actually checked against the real mechanism: (1) real
    // O_ONCE_INTERACTIVE is sticky (LpcObject::wasEverInteractive(), the
    // same flag userp()/query_once_interactive() already use), so a
    // player who went link-dead but is still present in the world
    // should still be findable, and was not; (2) it matched against
    // query_name() rather than the real living_name set by
    // set_living_name(), which happen to be the same string for a
    // player in this mudlib but are not guaranteed to be, and are not
    // the same at all for most NPCs. find_living() itself was not
    // implemented as an efun at all -- confirmed real usage of 18+ call
    // sites across cmds/mortal/_whisper.c, daemon/mail_d.c,
    // cmds/mortal/_psi.c, several admin commands
    // (_scan.c/_trans.c/_wizheal.c/_teleport.c), and several
    // NPC-targeting object files under domains/Praxis/ -- every one of
    // those previously threw "undefined function or efun: find_living".
    // Both now backed by LivingNameRegistry (see its own header comment
    // for the full design), matching real find_living_object() exactly:
    // a name match that also has O_ENABLE_COMMANDS, plus
    // O_ONCE_INTERACTIVE for find_player() specifically.
    auto findLivingImpl = [](bool requireOnceInteractive) {
        return [requireOnceInteractive](VM&, std::vector<Value>& args) -> Value {
            if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
                return Value{};
            }
            auto ob = LivingNameRegistry::find(std::get<std::string>(args[0].data), requireOnceInteractive);
            if (!ob) return Value{};
            return Value(ob);
        };
    };
    t.registerEfun("find_player", findLivingImpl(true));
    t.registerEfun("find_living", findLivingImpl(false));

    // int file_size(string file) -- file.c's real file_size(): -1 if the
    // path does not exist, -2 if it is a directory, else the byte size.
    t.registerEfun("file_size", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("file_size: expected a string path argument");
        }
        std::string path = vm.resolveMudlibPath(std::get<std::string>(args[0].data));
        struct stat st;
        if (::stat(path.c_str(), &st) != 0) return Value(int64_t{-1});
        if (S_ISDIR(st.st_mode)) return Value(int64_t{-2});
        return Value(static_cast<int64_t>(st.st_size));
    });

    // int file_length(string path) -- real packages/contrib.c's own
    // file_length()/f_file_length(): the number of newline-terminated
    // lines in path (a raw '\n' count, confirmed directly against its own
    // "while ((newp = memchr(p + 1, '\n', num))) { ...; ret++; }" scan),
    // not a line count in the "text editor" sense (a final line with no
    // trailing newline is not counted). Same -1/-2 not-found/is-a-
    // directory convention as file_size() above, reusing the identical
    // stat() gate before opening the file.
    t.registerEfun("file_length", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("file_length: expected a string path argument");
        }
        std::string path = vm.resolveMudlibPath(std::get<std::string>(args[0].data));
        struct stat st;
        if (::stat(path.c_str(), &st) != 0) return Value(int64_t{-1});
        if (S_ISDIR(st.st_mode)) return Value(int64_t{-2});
        std::ifstream f(path, std::ios::binary);
        if (!f) return Value(int64_t{-1});
        int64_t lines = std::count(std::istreambuf_iterator<char>(f),
                                    std::istreambuf_iterator<char>(), '\n');
        return Value(lines);
    });

    // mixed *get_dir(string path, int flags default: 0) -- file.c's real
    // get_dir() + encode_stat() (func_spec.c: "mixed *get_dir(string,
    // int default: 0);", efun_defs.c: 2 args, 2nd is T_NUMBER). The
    // directory portion of path is always literal; only the final path
    // component may carry glob wildcards, matched against that
    // directory's own entries. The optional flags argument:
    //   - flags == -1: each entry is a 3-element array
    //     ({ name, size, mtime }), where size is -2 for a directory and
    //     the byte size for a regular file (real encode_stat():
    //     "(st->st_mode & S_IFDIR) ? -2 : st->st_size"), and mtime is
    //     the file's st_mtime.
    //   - any other flags value (0, and every other bit pattern such as
    //     the 0x10 some newer mudlibs pass): each entry is just the name
    //     string. Real encode_stat() only special-cases -1; every other
    //     value falls to its plain-name branch, so a non-(-1) flag is
    //     accepted and behaves exactly like 0, not an error.
    // The result is sorted by name in both forms (real qsort with
    // pstrcmp / parrcmp). A path resolving to a single existing file
    // (no glob, not a directory) yields a 1-element array. An invalid
    // path returns int 0.
    // Found live blocking EVERY typed command, not just one: daemon/
    // command.c's own rehash() (CMD_D's directory-scan cache behind
    // find_cmd()) calls "get_dir(val[i]+\"/_*.c\")" -- a genuine glob,
    // not the bare-directory-or-bare-file shape this efun originally
    // assumed was the only real usage.
    t.registerEfun("get_dir", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("get_dir: expected a string path argument");
        }
        int64_t flags = 0;
        if (args.size() > 1) {
            if (!std::holds_alternative<int64_t>(args[1].data)) {
                throw LpcRuntimeError("get_dir: flags argument must be an int");
            }
            flags = std::get<int64_t>(args[1].data);
        }
        const bool wantStat = (flags == -1);

        auto result = std::make_shared<Array>();
        auto gated = checkValidPath(vm, std::get<std::string>(args[0].data), false, "get_dir");
        if (!gated) return Value(result);
        std::string path = vm.resolveMudlibPath(*gated);

        size_t slash = path.find_last_of('/');
        std::string dirPart = slash == std::string::npos ? "." : path.substr(0, slash);
        std::string lastComponent = slash == std::string::npos ? path : path.substr(slash + 1);
        bool hasWildcard = lastComponent.find_first_of("*?[") != std::string::npos;

        // Build one result entry: a bare name string, or (flags == -1)
        // the ({ name, size, mtime }) triple. `full` is the path to
        // stat for the triple form.
        auto makeEntry = [&](const std::string& name, const std::string& full) -> Value {
            if (!wantStat) return Value(name);
            auto triple = std::make_shared<Array>();
            struct stat est;
            int64_t size = 0, mtime = 0;
            if (::stat(full.c_str(), &est) == 0) {
                size = S_ISDIR(est.st_mode) ? int64_t{-2}
                                            : static_cast<int64_t>(est.st_size);
                mtime = static_cast<int64_t>(est.st_mtime);
            }
            triple->items.emplace_back(name);
            triple->items.emplace_back(size);
            triple->items.emplace_back(mtime);
            return Value(triple);
        };
        auto entryName = [&](const Value& v) -> const std::string& {
            if (auto* s = std::get_if<std::string>(&v.data)) return *s;
            const auto& a = std::get<std::shared_ptr<Array>>(v.data);
            return std::get<std::string>(a->items[0].data);
        };
        auto sortByName = [&]() {
            std::sort(result->items.begin(), result->items.end(),
                      [&](const Value& a, const Value& b) {
                          return entryName(a) < entryName(b);
                      });
        };

        if (!hasWildcard) {
            struct stat st;
            if (::stat(path.c_str(), &st) != 0) return Value(result);
            if (S_ISDIR(st.st_mode)) {
                DIR* dir = ::opendir(path.c_str());
                if (!dir) return Value(result);
                struct dirent* entry;
                while ((entry = ::readdir(dir)) != nullptr) {
                    std::string name = entry->d_name;
                    if (name == "." || name == "..") continue;
                    result->items.push_back(makeEntry(name, path + "/" + name));
                }
                ::closedir(dir);
            } else {
                result->items.push_back(makeEntry(lastComponent, path));
            }
            sortByName();
            return Value(result);
        }

        struct stat dirSt;
        if (::stat(dirPart.c_str(), &dirSt) != 0 || !S_ISDIR(dirSt.st_mode)) return Value(result);
        DIR* dir = ::opendir(dirPart.c_str());
        if (!dir) return Value(result);
        struct dirent* entry;
        while ((entry = ::readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            if (::fnmatch(lastComponent.c_str(), name.c_str(), 0) == 0) {
                result->items.push_back(makeEntry(name, dirPart + "/" + name));
            }
        }
        ::closedir(dir);
        sortByName();
        return Value(result);
    });

    // int rm(string file) -- file.c's real rm()/remove_file(): 1 on
    // success, 0 if the file did not exist or could not be removed.
    t.registerEfun("rm", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("rm: expected a string path argument");
        }
        auto gated = checkValidPath(vm, std::get<std::string>(args[0].data), true, "rm");
        if (!gated) return Value(int64_t{0});
        std::string path = vm.resolveMudlibPath(*gated);
        return Value(static_cast<int64_t>(::remove(path.c_str()) == 0 ? 1 : 0));
    });

    // int mkdir(string dir) -- 1 on success, 0 on failure (real
    // file.c's mkdir() returns the same 1/0 shape; this driver does not
    // replicate its exact errno-based failure text, only the 1/0
    // result every call site actually branches on).
    t.registerEfun("mkdir", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("mkdir: expected a string path argument");
        }
        auto gated = checkValidPath(vm, std::get<std::string>(args[0].data), true, "mkdir");
        if (!gated) return Value(int64_t{0});
        std::string path = vm.resolveMudlibPath(*gated);
        return Value(static_cast<int64_t>(::mkdir(path.c_str(), 0755) == 0 ? 1 : 0));
    });

    // int save_object(string file) / int restore_object(string file) --
    // real FluffOS serializes an object's own non-static, non-nosave
    // variables to a specific on-disk text format (each line "varname
    // value", values written in LPC literal syntax -- see save.c) and
    // restore_object() parses that same format back, matching each line
    // to a same-named variable on the *calling* object (current_object,
    // not some other target). save_object() itself still only ever
    // writes this driver's own simpler recursive, self-delimiting
    // format (see serializeValue()/deserializeValue() just below):
    // nothing else needs to read a file this driver wrote, so there is
    // no reason to pay for real save_svalue()'s escaping/formatting
    // exactly on the write side. restore_object() now reads *both*
    // formats, auto-detected per line (this driver's own tab-delimited
    // format if a tab is found, the real space-delimited LPC-literal
    // format otherwise -- see parseRealSaveValue() above, and the loop
    // below for the actual dispatch) -- so a real, pre-existing
    // FluffOS save file that ships with a real mudlib (e.g.
    // daemon/save/banish.o) now actually loads its real historical
    // data instead of being silently skipped line by line the way it
    // was before (every line looked for a tab that was never there,
    // "no tab separator matches" -- see this file's own STATUS.md
    // history). This driver's own recursive format covers every Value
    // kind this driver has (int, float, string, array, mapping --
    // arbitrarily nested, not just the flat shapes secure/daemon/
    // account_d.c's own account records happen to use (a since-
    // discarded early scratch object, distinct from the real, shipped
    // /single/account_d.c named just below) except object
    // references and closures, which real save_object() cannot
    // serialize either (an object reference saved to disk cannot
    // survive a reboot, and neither real FluffOS nor this driver
    // attempts it). A width > 1 mapping (real LDMud N-column mapping)
    // also throws rather than saving, a bounded stopgap added
    // 2026-08-21 (ROADMAP.md row 1.9's own addendum) once neither this
    // format nor the real FluffOS one below had any way to represent
    // one -- see serializeValue()'s own Mapping branch for the full
    // reasoning; full width-aware serialization remains its own,
    // separately-scoped, larger item, not attempted here.
    // __SAVE_EXTENSION__ (".o") is appended
    // by the *caller* in this mudlib's own code (e.g. account_d.c's
    // account_path()+__SAVE_EXTENSION__), so these two efuns use the
    // path normalized the same way real object.c's save_object() itself
    // normalizes it (confirmed live: daemon/banish.c's own
    // restore_banish() calls "restore_object(SAVE_BANISH)" with no
    // extension at all, relying on the efun to add one -- a real
    // pre-existing daemon/save/banish.o on disk was unreachable through
    // this driver's own restore_object() until this normalization was
    // added, since it only ever tried the literal, extension-less path
    // it was given): strip a trailing ".c" if present, strip a
    // trailing ".o" if *already* present (so this stays idempotent
    // rather than ever producing "name.o.o"), then always append ".o".
    t.registerEfun("save_object", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("save_object: expected a string path argument");
        }
        auto obj = vm.currentObject();
        if (!obj) throw LpcRuntimeError("save_object: no current object to save");

        // Matches real save_object(): it does not create missing parent
        // directories either, so a save into one still fails cleanly
        // (ofstream simply won't open) rather than silently landing
        // somewhere else -- callers that need the directory to exist
        // make it themselves first (account_d.c's own ensure_dirs()).
        auto gated = checkValidPath(vm, normalizeSavePath(std::get<std::string>(args[0].data)), true, "save_object");
        if (!gated) return Value(int64_t{0});
        std::string path = vm.resolveMudlibPath(*gated);
        std::ofstream f(path, std::ios::trunc);
        if (!f) return Value(int64_t{0});

        const auto& names = obj->program().objectVarNames;
        auto& vars = obj->variables();
        for (size_t i = 0; i < names.size() && i < vars.size(); ++i) {
            f << names[i] << '\t';
            serializeValue(f, vars[i]);
            f << '\n';
        }
        return Value(int64_t{1});
    });

    t.registerEfun("restore_object", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("restore_object: expected a string path argument");
        }
        auto obj = vm.currentObject();
        if (!obj) throw LpcRuntimeError("restore_object: no current object to restore into");

        auto gated = checkValidPath(vm, normalizeSavePath(std::get<std::string>(args[0].data)), false, "restore_object");
        if (!gated) return Value(int64_t{0});
        std::string path = vm.resolveMudlibPath(*gated);
        std::ifstream f(path);
        if (!f) return Value(int64_t{0});

        const auto& names = obj->program().objectVarNames;
        auto& vars = obj->variables();

        std::string line;
        while (std::getline(f, line)) {
            // Real restore_object_from_line(): a line starting with '#'
            // is a comment (real save_object() writes one itself, the
            // originating filename -- see the real banish.o example in
            // this project's own mudlib tree) and is always skipped,
            // in either format.
            if (line.empty() || line[0] == '#') continue;

            std::string name;
            size_t valueStart;
            bool realFormat = false;
            size_t tab = line.find('\t');
            if (tab != std::string::npos) {
                name = line.substr(0, tab);
                valueStart = tab + 1;
            } else {
                size_t space = line.find(' ');
                if (space == std::string::npos) continue;
                name = line.substr(0, space);
                valueStart = space + 1;
                realFormat = true;
            }

            size_t slot = names.size();
            for (size_t i = 0; i < names.size(); ++i) {
                if (names[i] == name) { slot = i; break; }
            }
            if (slot >= names.size() || slot >= vars.size()) continue;

            size_t pos = valueStart;
            vars[slot] = realFormat ? parseRealSaveValue(line, pos) : deserializeValue(line, pos);
        }
        return Value(int64_t{1});
    });

    // int dump_state(string file) / int restore_state(string file) --
    // ROADMAP.md row 2.1's own v1 first slice: StateSerializer
    // (src/persist), a whole-*world* counterpart to save_object()/
    // restore_object() just above. Unlike those two, this format can
    // round-trip an object reference or a closure, since it dumps every
    // live object (LiveObjectRegistry::all()) in one pass and resolves
    // references against a shared id table, not one object's variables
    // in isolation -- see StateSerializer.hpp's own header comment for
    // the full derivation. Gated the same way save_object()/
    // restore_object() and every other file efun in this table already
    // are (checkValidPath(), the real valid_write()/valid_read() master
    // apply), not a new, separate security model.
    t.registerEfun("dump_state", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("dump_state: expected a string path argument");
        }
        auto gated = checkValidPath(vm, std::get<std::string>(args[0].data), true, "dump_state");
        if (!gated) return Value(int64_t{0});
        std::string path = vm.resolveMudlibPath(*gated);
        StateSerializer serializer(vm.objectManager());
        return Value(static_cast<int64_t>(serializer.dumpState(path) ? 1 : 0));
    });

    t.registerEfun("restore_state", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("restore_state: expected a string path argument");
        }
        auto gated = checkValidPath(vm, std::get<std::string>(args[0].data), false, "restore_state");
        if (!gated) return Value(int64_t{0});
        std::string path = vm.resolveMudlibPath(*gated);
        StateSerializer serializer(vm.objectManager());
        return Value(static_cast<int64_t>(serializer.restoreState(path) ? 1 : 0));
    });

    // ---- Rifts combat math (phase 1 of the game-logic-mechanics move,
    // 2026-08-08): pure math extracted from the LPC mudlib's own
    // daemon/rifts_combat.c, one function at a time, each verified
    // against the original LPC implementation across a range of real
    // inputs (see test/test_lexer.cpp) before the LPC copy was
    // removed. Orchestration (apply_rifts_damage, combat_round,
    // npc_combat_tick, execute_attack) and the inherited std/living/
    // combat.c stay in LPC; see STATUS.md for the scoping.

    // int pp_combat_bonus(int pp) -- PP attribute combat bonus lookup.
    // Transcribed unchanged from daemon/rifts_combat.c's own private
    // pp_combat_bonus(): a stepped threshold table, no object access.
    static const auto ppCombatBonusCore = [](int64_t pp) -> int64_t {
        if (pp >= 26) return 6;
        if (pp >= 21) return 5;
        if (pp >= 19) return 4;
        if (pp >= 18) return 3;
        if (pp >= 16) return 2;
        if (pp >= 13) return 1;
        return 0;
    };
    t.registerEfun("pp_combat_bonus", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("pp_combat_bonus: expected an int argument");
        }
        return Value(ppCombatBonusCore(std::get<int64_t>(args[0].data)));
    });

    // int ps_damage_bonus(int ps, int supernatural) -- PS attribute
    // damage bonus; supernatural PS doubles the raw bonus. Transcribed
    // unchanged from daemon/rifts_combat.c's own private
    // ps_damage_bonus().
    static const auto psDamageBonusCore = [](int64_t ps, bool supernatural) -> int64_t {
        int64_t bonus;
        if (ps >= 31) bonus = 7;
        else if (ps >= 30) bonus = 6;
        else if (ps >= 26) bonus = 5;
        else if (ps >= 21) bonus = 4;
        else if (ps >= 18) bonus = 3;
        else if (ps >= 16) bonus = 2;
        else bonus = 0;
        return supernatural ? bonus * 2 : bonus;
    };
    t.registerEfun("ps_damage_bonus", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("ps_damage_bonus: expected (int ps, int supernatural)");
        }
        int64_t ps = std::get<int64_t>(args[0].data);
        bool supernatural = std::holds_alternative<int64_t>(args[1].data) &&
                             std::get<int64_t>(args[1].data) != 0;
        return Value(psDamageBonusCore(ps, supernatural));
    });

    // int occ_base_apm(string occ) -- APM base by OCC combat category.
    // Transcribed unchanged from daemon/rifts_combat.c's own private
    // occ_base_apm(): a fixed occ-name -> bonus table, default 2 for
    // an unset/empty/unrecognized occ. Case-sensitive, matching the
    // LPC switch statement's own exact-string-match semantics.
    static const auto occBaseApmCore = [](const std::string& occ) -> int64_t {
        static const std::unordered_map<std::string, int64_t> table = {
            {"cyber-knight", 6}, {"crazy", 6}, {"juicer", 6},
            {"ninja juicer", 6}, {"delphi juicer", 6}, {"hyperion juicer", 6},
            {"tattooed man", 6}, {"tattoo warrior", 6},

            {"master assassin", 5}, {"city rat", 5}, {"forger", 5},
            {"freelance spy", 5}, {"professional thief", 5}, {"smuggler", 5},
            {"iss peacekeeper", 5}, {"iss specter", 5},

            {"headhunter", 4}, {"bounty hunter", 4}, {"cs grunt", 4},
            {"cs dead boy", 4}, {"cs ranger", 4}, {"cs military specialist", 4},
            {"cs samas rpa pilot", 4}, {"cs technical officer", 4},
            {"merc soldier", 4}, {"special forces (merc)", 4},
            {"tribal warrior", 4}, {"wilderness scout", 4}, {"borg", 4},
            {"glitter boy pilot", 4}, {"robot pilot", 4}, {"ntset protector", 4},
            {"knight (europe)", 4}, {"royal knight", 4}, {"pirate (s.a.)", 4},
            {"sailor (s.a.)", 4},

            {"ley line walker", 3}, {"mystic", 3}, {"shifter", 3},
            {"shaman", 3}, {"techno-wizard", 3}, {"ley line rifter", 3},
            {"air warlock", 3}, {"nega-psychic", 3},
        };
        if (occ.empty()) return 2;
        auto it = table.find(occ);
        return it != table.end() ? it->second : 2;
    };
    t.registerEfun("occ_base_apm", [](VM&, std::vector<Value>& args) -> Value {
        std::string occ;
        if (!args.empty() && std::holds_alternative<std::string>(args[0].data)) {
            occ = std::get<std::string>(args[0].data);
        }
        return Value(occBaseApmCore(occ));
    });

    // int roll_weapon_damage_dice(int num, int sides, int bonus) -- pure
    // dice-rolling core split out of daemon/rifts_combat.c's own private
    // roll_rifts_weapon_damage(): rolls num dice of size sides (each via
    // the real random() efun's own [0, n) plus 1 convention, so this
    // reuses the same efun-table random() rather than a second
    // generator), sums them, adds bonus, and floors the total at 1,
    // exactly matching the LPC original's "damage > 0 ? damage : 1".
    // The ammo check/consume and the "clicks empty" message stayed in
    // LPC: they are object-graph orchestration with a player-visible
    // side effect, not math (see STATUS.md). The LPC wrapper alone
    // decides whether a weapon has valid dice (num > 0 && sides > 0)
    // before calling this.
    t.registerEfun("roll_weapon_damage_dice", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 3 ||
            !std::holds_alternative<int64_t>(args[0].data) ||
            !std::holds_alternative<int64_t>(args[1].data) ||
            !std::holds_alternative<int64_t>(args[2].data)) {
            throw LpcRuntimeError("roll_weapon_damage_dice: expected (int num, int sides, int bonus)");
        }
        int64_t num   = std::get<int64_t>(args[0].data);
        int64_t sides = std::get<int64_t>(args[1].data);
        int64_t bonus = std::get<int64_t>(args[2].data);
        int64_t damage = 0;
        for (int64_t j = 0; j < num; ++j) {
            std::vector<Value> sidesArg{ Value(sides) };
            Value roll = EfunTable::instance().call("random", vm, sidesArg);
            damage += std::get<int64_t>(roll.data) + 1;
        }
        damage += bonus;
        return Value(damage > 0 ? damage : int64_t{1});
    });

    // int roll_MdN(int rolls, int sides, int bonus default: 0) --
    // current FluffOS's own real, genuinely new-since-2.9 efun
    // (confirmed absent from temp/reference/fluffos-2.9-ds2.08 entirely:
    // no roll_MdN anywhere in that tree). Found via a systematic sweep
    // of src/packages/dwlib/dwlib.spec against this driver's registered
    // efuns, the same method rows 2.16/2.23-2.30 used. Signature
    // confirmed directly: dwlib.spec's own "int roll_MdN(int, int, int
    // default:0);". Real f_roll_MdN() (src/packages/contrib/contrib.cc,
    // fetched live -- declared in dwlib.spec but actually implemented
    // in the contrib package, confirmed directly rather than assumed
    // from the file split): "if (rolls > 0 && sides > 0) { while
    // (rolls--) { roll += 1 + random_number(sides); } roll += bonus; }"
    // -- rolls dice.sides()-many draws of 1 + random_number(sides) (a
    // uniform [1, sides] die, the same real random_number() this
    // driver's own random() efun already ports faithfully, reused
    // directly below via EfunTable::call() the same way this driver's
    // own pre-existing roll_weapon_damage_dice() helper immediately
    // above already does), sums them, adds bonus -- but ONLY when both
    // rolls and sides are positive; a non-positive rolls or sides
    // returns a real, plain 0, bonus NOT added in that case either,
    // confirmed directly from the real guard's own scope rather than
    // assumed symmetric with the positive case. Unlike this driver's
    // own roll_weapon_damage_dice() neighbor above (an AMLP-invented,
    // damage-specific helper that floors its own result at 1), real
    // roll_MdN() has no such floor -- a real, deliberate difference,
    // not an oversight, since the two are unrelated real functions that
    // merely resemble each other in shape.
    t.registerEfun("roll_MdN", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<int64_t>(args[0].data) ||
            !std::holds_alternative<int64_t>(args[1].data)) {
            throw LpcRuntimeError("roll_MdN: expected (int rolls, int sides, int bonus default: 0)");
        }
        int64_t rolls = std::get<int64_t>(args[0].data);
        int64_t sides = std::get<int64_t>(args[1].data);
        int64_t bonus = 0;
        if (args.size() > 2 && std::holds_alternative<int64_t>(args[2].data)) {
            bonus = std::get<int64_t>(args[2].data);
        }
        int64_t roll = 0;
        if (rolls > 0 && sides > 0) {
            std::vector<Value> sidesArg{ Value(sides) };
            for (int64_t j = 0; j < rolls; ++j) {
                Value draw = EfunTable::instance().call("random", vm, sidesArg);
                roll += std::get<int64_t>(draw.data) + 1;
            }
            roll += bonus;
        }
        return Value(roll);
    });

    // int vowel(int c) -- current FluffOS's own real, genuinely
    // new-since-2.9 efun (confirmed absent from
    // temp/reference/fluffos-2.9-ds2.08 entirely). Found the same sweep
    // as roll_MdN() above. Signature confirmed directly:
    // dwlib.spec's own "int vowel(int);" (the real doc,
    // docs/efun/contrib/vowel.md, fetched live: "test whether a
    // character is a vowel"). Real f_vowel() (dwlib.cc, fetched live):
    // "char const v = (char)sp->u.number; if (v == 'a' || ... || v ==
    // 'U') { sp->u.number = 1; } else { sp->u.number = 0; }" -- a plain
    // ASCII a/e/i/o/u check, both cases, ported verbatim.
    t.registerEfun("vowel", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("vowel: expected an int argument");
        }
        char c = static_cast<char>(std::get<int64_t>(args[0].data));
        bool isVowel = c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ||
                       c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U';
        return Value(static_cast<int64_t>(isVowel ? 1 : 0));
    });

    // string add_a(string str) -- current FluffOS's own real, genuinely
    // new-since-2.9 efun (confirmed absent from
    // temp/reference/fluffos-2.9-ds2.08 entirely). Found the same
    // sweep. Signature confirmed directly: dwlib.spec's own "string
    // add_a(string);". Real f_add_a() (dwlib.cc, fetched live), ported
    // step for step, not approximated: skip leading spaces; an
    // all-spaces (or now-empty) string returns "a " outright; a string
    // already starting with "a "/"an " (case-insensitive) is returned
    // unchanged; otherwise the decision character defaults to the
    // (space-trimmed) string's own first character, with two real
    // special cases before the vowel check runs -- a leading "us"
    // (case-insensitive) redirects the decision character to the third
    // character instead (the letter right after "us"), starting the
    // "an" decision pre-flipped true ("a use"/"a user"/"a usurper" all
    // redirect to a vowel there and flip back to "a "; "an usher"
    // redirects to a consonant there and stays "an "); a leading "hour"
    // (case-insensitive) forces the decision character to 'o'. The
    // decision character is then checked against a/e/i/o/u (both
    // cases): a vowel there flips the "an" state, anything else leaves
    // it alone -- "an " is prepended if the final state is true,
    // otherwise "a ". Every one of the real doc's own worked/implied
    // examples was traced through this exact algorithm by hand before
    // writing the regression tests: "an apple", "a cat", "a user",
    // "a use", "a usurper", "an usher", "an hour" -- all real, all
    // deterministic, zero live-current-FluffOS-instance dependency
    // needed to verify any of them.
    t.registerEfun("add_a", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("add_a: expected a string argument");
        }
        const std::string& raw = std::get<std::string>(args[0].data);
        size_t start = 0;
        while (start < raw.size() && raw[start] == ' ') ++start;
        if (start == raw.size()) return Value(std::string("a "));

        std::string str = raw.substr(start);
        auto startsWithCI = [&str](const char* prefix) -> bool {
            size_t n = std::strlen(prefix);
            if (str.size() < n) return false;
            for (size_t i = 0; i < n; ++i) {
                if (std::tolower(static_cast<unsigned char>(str[i])) !=
                    std::tolower(static_cast<unsigned char>(prefix[i]))) return false;
            }
            return true;
        };
        if (startsWithCI("a ") || startsWithCI("an ")) return Value(str);

        char first = str[0];
        bool an = false;
        if (startsWithCI("us") && str.size() > 2) {
            first = str[2];
            an = true;
        }
        if (startsWithCI("hour")) {
            first = 'o';
        }
        switch (first) {
            case 'a': case 'e': case 'i': case 'o': case 'u':
            case 'A': case 'E': case 'I': case 'O': case 'U':
                an = !an;
                break;
            default:
                break;
        }
        return Value((an ? std::string("an ") : std::string("a ")) + str);
    });

    // string replace_html(string) / string replace_mxp(string) --
    // dwlib.spec's markup-escaping pair. Present identically in both the
    // pinned reference (temp/reference/fluffos-2.9-ds2.08/packages/dwlib.c)
    // and the current clone (temp/fluffos/src/packages/dwlib/dwlib.cc):
    // dwlib.spec's own "string replace_html(string); string
    // replace_mxp(string);", both bodied by a single shared
    // replace_mxp_html(int html, int mxp) helper -- f_replace_html() calls
    // it (1, 0), f_replace_mxp() calls it (0, 1). Grep of EfunTable.cpp
    // confirmed neither was registered; roll_MdN/vowel/add_a from the same
    // spec landed earlier (rows above). Real helper, ported branch for
    // branch from its own switch: '&' -> "&amp;", '<' -> "&lt;", '>' ->
    // "&gt;" unconditionally; '\n' -> the MXP secure-line tag
    // "\e[4z<BR>" (ESC '[' '4' 'z' '<' 'B' 'R' '>', 8 bytes) when mxp,
    // otherwise copied through as a literal newline (real's `goto def`);
    // '"' -> "&quot;" when html, otherwise copied through (real falls
    // straight into `default` -- there is no break on that path); every
    // other byte copied verbatim. So replace_html escapes & < > and ",
    // leaving newlines alone; replace_mxp escapes & < > and rewrites each
    // newline to <BR>, leaving " alone. Matches docs/efun/contrib/
    // replace_html.md / replace_mxp.md word for word.
    //
    // Two named local choices, neither a silent divergence:
    //   - Real caps the result at the driver's max_string_length (its
    //     `dst2 - dst < max_string_length` loop guard). This driver has no
    //     max-string-length config at all (same situation add_a() above is
    //     in, where the real max-length error path is likewise dropped),
    //     so the whole input is always processed.
    //   - Real reads a C string, so an embedded NUL ends the scan
    //     (`while(*src ...)`). This driver stops at the first NUL to
    //     match, rather than escaping the bytes past it, the same choice
    //     string_difference() (row 2.52) made for the same reason.
    {
        auto replaceMxpHtml = [](const std::string& src, bool html, bool mxp) -> std::string {
            std::string out;
            out.reserve(src.size() + 8);
            for (char ch : src) {
                if (ch == '\0') break;
                switch (ch) {
                    case '&': out += "&amp;"; break;
                    case '<': out += "&lt;"; break;
                    case '>': out += "&gt;"; break;
                    case '\n':
                        if (mxp) out += "\x1b[4z<BR>";
                        else out += '\n';
                        break;
                    case '"':
                        if (html) out += "&quot;";
                        else out += '"';
                        break;
                    default: out += ch; break;
                }
            }
            return out;
        };
        t.registerEfun("replace_html", [replaceMxpHtml](VM&, std::vector<Value>& args) -> Value {
            if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
                throw LpcRuntimeError("replace_html: expected a string argument");
            }
            return Value(replaceMxpHtml(std::get<std::string>(args[0].data), true, false));
        });
        t.registerEfun("replace_mxp", [replaceMxpHtml](VM&, std::vector<Value>& args) -> Value {
            if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
                throw LpcRuntimeError("replace_mxp: expected a string argument");
            }
            return Value(replaceMxpHtml(std::get<std::string>(args[0].data), false, true));
        });
    }

    // -------------------------------------------------------------------------
    // Phase 0.13 efun growth batch - to_float, typeof, rename, rmdir, math
    // -------------------------------------------------------------------------

    // float to_float(string | float | int) -- real efuns_main.c's own
    // f__to_float() (func_spec.c alias: "float to_float _to_float(...)").
    // int → (double) cast; float passes through; string parsed via sscanf
    // "%lf", returning 0.0 for an unparseable string (real f__to_float()
    // does exactly that, no strtod, no error). Confirmed 13 real call sites
    // in the mudlib (grep across lib/), all of the form `to_float(some_int)`
    // to feed a float computation. No buffer case (this driver has no buffer
    // type -- see Value.hpp) -- real FluffOS's T_BUFFER arm is a no-op here.
    t.registerEfun("to_float", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(0.0);
        const Value& v = args[0];
        if (std::holds_alternative<double>(v.data))
            return Value(std::get<double>(v.data));
        if (std::holds_alternative<int64_t>(v.data))
            return Value(static_cast<double>(std::get<int64_t>(v.data)));
        if (std::holds_alternative<std::string>(v.data)) {
            double result = 0.0;
            std::sscanf(std::get<std::string>(v.data).c_str(), "%lf", &result);
            return Value(result);
        }
        throw LpcRuntimeError("Bad argument 1 to to_float()");
    });

    // string typeof(mixed) -- real efuns_main.c's f_typeof(): calls
    // type_name(sp->type) whose result string for each T_* type is:
    //   T_NUMBER   → "int"    T_STRING   → "string"  T_ARRAY    → "array"
    //   T_OBJECT   → "object" T_MAPPING  → "mapping" T_FUNCTION → "function"
    //   T_REAL     → "float"
    // monostate (undefined/void) maps to "int" because real FluffOS treats
    // undefined as the integer 0 at the type-name level (T_NUMBER, subtype
    // T_UNDEFINED -- type_names[0] is "int" for T_NUMBER).
    t.registerEfun("typeof", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) return Value(std::string("int"));
        const Value& v = args[0];
        if (std::holds_alternative<int64_t>(v.data))       return Value(std::string("int"));
        if (std::holds_alternative<double>(v.data))         return Value(std::string("float"));
        if (std::holds_alternative<std::string>(v.data))    return Value(std::string("string"));
        if (std::holds_alternative<std::shared_ptr<LpcObject>>(v.data))
            return Value(std::string("object"));
        if (std::holds_alternative<std::shared_ptr<Array>>(v.data))
            return Value(std::string("array"));
        if (std::holds_alternative<std::shared_ptr<Mapping>>(v.data))
            return Value(std::string("mapping"));
        if (std::holds_alternative<std::shared_ptr<Closure>>(v.data))
            return Value(std::string("function"));
        // T_BUFFER -> "buffer" in real interpret.c's type_names[] (row
        // 2.33a).
        if (std::holds_alternative<std::shared_ptr<Buffer>>(v.data))
            return Value(std::string("buffer"));
        // monostate (void/undefined)
        return Value(std::string("int"));
    });

    // int rename(string from, string to) -- real efuns_main.c's f_rename():
    // delegates to do_rename(from, to, F_RENAME) which calls the C library
    // rename(2). Returns 0 on success, 1 on failure in the real driver
    // (do_rename() returns 0 for "ok, renamed" -- opposite of most file
    // efuns, but confirmed by reading do_rename() directly). Confirmed 10
    // real call sites in the mudlib. Both paths are resolved against mudlib
    // root, matching real check_valid_path() wrapping both args.
    t.registerEfun("rename", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 ||
            !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("rename: expected two string arguments");
        }
        // Real doc/master/valid_write's own list names both
        // "rename_from"/"rename_to" as real func strings this one real
        // efun gates with -- two separate checks, one per path, not one
        // shared "rename" check.
        auto gatedFrom = checkValidPath(vm, std::get<std::string>(args[0].data), true, "rename_from");
        if (!gatedFrom) return Value(int64_t{1}); // real do_rename() failure value
        auto gatedTo = checkValidPath(vm, std::get<std::string>(args[1].data), true, "rename_to");
        if (!gatedTo) return Value(int64_t{1});
        std::string from = vm.resolveMudlibPath(*gatedFrom);
        std::string to   = vm.resolveMudlibPath(*gatedTo);
        // Real do_rename() returns 0 on success, 1 on failure.
        return Value(static_cast<int64_t>(::rename(from.c_str(), to.c_str()) == 0 ? 0 : 1));
    });

    // int rmdir(string dir) -- real efuns_main.c's f_rmdir(): checks the
    // path, calls rmdir(2), pushes 1 on success or 0 on failure. Confirmed
    // 4 real call sites in the mudlib (domains/Praxis/ and others).
    t.registerEfun("rmdir", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("rmdir: expected a string path argument");
        }
        auto gated = checkValidPath(vm, std::get<std::string>(args[0].data), true, "rmdir");
        if (!gated) return Value(int64_t{0});
        std::string path = vm.resolveMudlibPath(*gated);
        return Value(static_cast<int64_t>(::rmdir(path.c_str()) == 0 ? 1 : 0));
    });

    // mixed stat(string path, void|int flags) -- confirmed against
    // fluffos-2.9-ds2.08/efuns_main.c's f_stat() and the real doc/efun/
    // stat page: a regular file returns ({size, mtime, load_time}), a
    // directory (or anything else) falls straight through to
    // get_dir()'s own behavior with the same flags argument. This
    // driver has no per-object "when was this loaded" tracking (see the
    // Known Stubs list's existing query_ip_name() precedent for the
    // same kind of honest, documented gap), so the third element is
    // always 0 rather than a real load timestamp. The directory
    // fallback is implemented by calling this driver's own already-
    // registered get_dir() efun directly (same flags-not-implemented
    // restriction it already has), not a separate reimplementation.
    // Confirmed real, live-reachable: std/user/editor.c's own
    // "stat(__FileName, -1)[1]" (both call sites resolve to a real
    // file, taking the regular-file branch; the -1 flag only matters if
    // get_dir()'s own fallback is reached).
    t.registerEfun("stat", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("stat: expected a string path argument");
        }
        std::string path = vm.resolveMudlibPath(std::get<std::string>(args[0].data));
        struct stat st;
        if (::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            auto result = std::make_shared<Array>();
            result->items.emplace_back(static_cast<int64_t>(st.st_size));
            result->items.emplace_back(static_cast<int64_t>(st.st_mtime));
            result->items.emplace_back(int64_t{0});
            return Value(result);
        }
        std::vector<Value> dirArgs{args[0]};
        if (args.size() > 1) dirArgs.push_back(args[1]);
        return EfunTable::instance().call("get_dir", vm, dirArgs);
    });

    // string read_bytes(string file, void|int start, void|int len) --
    // confirmed against fluffos-2.9-ds2.08/file.c's own read_bytes():
    // start defaults to 0, a negative start counts back from the end of
    // the file, len 0 (or omitted) means "the rest of the file", and it
    // returns plain int 0 (not an error) for a missing file or a start
    // past the end. Confirmed real, live-reachable: secure/SimulEfun/
    // misc.c's own "read_bytes(file, diff, 1024)".
    t.registerEfun("read_bytes", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("read_bytes: expected a string filename argument");
        }
        auto gated = checkValidPath(vm, std::get<std::string>(args[0].data), false, "read_bytes");
        if (!gated) return Value(int64_t{0});
        std::string path = vm.resolveMudlibPath(*gated);
        int64_t start = 0, len = 0;
        if (args.size() > 1 && std::holds_alternative<int64_t>(args[1].data)) {
            start = std::get<int64_t>(args[1].data);
        }
        if (args.size() > 2 && std::holds_alternative<int64_t>(args[2].data)) {
            len = std::get<int64_t>(args[2].data);
        }
        if (len < 0) return Value(int64_t{0});
        std::ifstream f(path, std::ios::binary | std::ios::ate);
        if (!f) return Value(int64_t{0});
        auto size = static_cast<int64_t>(f.tellg());
        if (start < 0) start = size + start;
        if (start < 0 || start >= size) return Value(int64_t{0});
        if (len == 0) len = size;
        if (start + len > size) len = size - start;
        f.seekg(start);
        std::string data(static_cast<size_t>(len), '\0');
        f.read(&data[0], len);
        auto actuallyRead = f.gcount();
        if (actuallyRead <= 0) return Value(int64_t{0});
        data.resize(static_cast<size_t>(actuallyRead));
        return Value(data);
    });

    // int write_bytes(string file, int start, string str) -- confirmed
    // against fluffos-2.9-ds2.08/file.c's own write_bytes(): opens for
    // update (creating the file if it does not exist), a negative start
    // counts back from the end, an out-of-range start fails, writes str
    // at that byte offset (overwriting, not inserting), returns 1 on
    // success and 0 on failure -- the ordinary file-efun convention,
    // NOT rename()'s inverted one, matching real write_bytes() directly
    // rather than assuming it shares do_rename()'s convention the way
    // link() below genuinely does. Only the plain-string form of the
    // real "int | buffer | string" third argument is implemented; this
    // driver has no buffer type (same documented gap as to_int()).
    t.registerEfun("write_bytes", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 3 ||
            !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<int64_t>(args[1].data) ||
            !std::holds_alternative<std::string>(args[2].data)) {
            throw LpcRuntimeError("write_bytes: expected (string, int, string)");
        }
        auto gated = checkValidPath(vm, std::get<std::string>(args[0].data), true, "write_bytes");
        if (!gated) return Value(int64_t{0});
        std::string path = vm.resolveMudlibPath(*gated);
        int64_t start = std::get<int64_t>(args[1].data);
        const std::string& data = std::get<std::string>(args[2].data);

        std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!f) {
            // File does not exist yet -- create it, matching real
            // write_bytes()'s own fopen(file, "wb") fallback.
            std::ofstream create(path, std::ios::binary);
            if (!create) return Value(int64_t{0});
            create.close();
            f.open(path, std::ios::binary | std::ios::in | std::ios::out);
            if (!f) return Value(int64_t{0});
        }
        f.seekg(0, std::ios::end);
        int64_t size = static_cast<int64_t>(f.tellg());
        if (start < 0) start = size + start;
        if (start < 0 || start > size) return Value(int64_t{0});
        f.seekp(start);
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        return Value(static_cast<int64_t>(f.good() ? 1 : 0));
    });

    // int link(string from, string to) -- confirmed against
    // fluffos-2.9-ds2.08/efuns_main.c's f_link(): routes through the
    // exact same do_rename()-family function rename() above already
    // uses, just with the F_LINK flag instead of F_RENAME, so it shares
    // that function's own inverted return convention (0 on success, 1
    // on failure), not the ordinary file-efun convention write_bytes()
    // above uses. Zero real call sites in this mudlib (confirmed by
    // grep; the only hits are doc files), implemented anyway per this
    // row's own Tier 1 list.
    t.registerEfun("link", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 ||
            !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("link: expected two string arguments");
        }
        std::string from = vm.resolveMudlibPath(std::get<std::string>(args[0].data));
        std::string to   = vm.resolveMudlibPath(std::get<std::string>(args[1].data));
        return Value(static_cast<int64_t>(::link(from.c_str(), to.c_str()) == 0 ? 0 : 1));
    });

    // -------------------------------------------------------------------------
    // Math package (packages/math_spec.c / packages/math.c) - all confirmed
    // against the FluffOS reference source directly. Each takes a float;
    // the real implementations operate on sp->u.real in-place and do not
    // do any int-promotion. This driver promotes an int arg to float at the
    // call site (a plain cast), matching real LPC's implicit numeric coercion
    // rather than throwing on an int argument -- call sites in the mudlib
    // freely pass integers to sqrt() and friends.
    // -------------------------------------------------------------------------
    auto asFloat = [](const Value& v) -> double {
        if (std::holds_alternative<double>(v.data))   return std::get<double>(v.data);
        if (std::holds_alternative<int64_t>(v.data))  return static_cast<double>(std::get<int64_t>(v.data));
        throw LpcRuntimeError("math efun: argument must be numeric");
    };

    // float cos(float) / sin(float) / tan(float) -- packages/math.c f_cos(),
    // f_sin(), f_tan(). Real f_tan() has a comment noting it could blow up
    // at x = Pi/2 + N*Pi but does not guard it -- we match that.
    t.registerEfun("cos", [asFloat](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("cos: expected a numeric argument");
        return Value(std::cos(asFloat(args[0])));
    });
    t.registerEfun("sin", [asFloat](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("sin: expected a numeric argument");
        return Value(std::sin(asFloat(args[0])));
    });
    t.registerEfun("tan", [asFloat](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("tan: expected a numeric argument");
        return Value(std::tan(asFloat(args[0])));
    });

    // float asin(float) / acos(float) / atan(float) -- packages/math.c.
    // f_asin() and f_acos() throw if |x| > 1.0; f_atan() has no range check.
    t.registerEfun("asin", [asFloat](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("asin: expected a numeric argument");
        double x = asFloat(args[0]);
        if (x < -1.0) throw LpcRuntimeError("math: asin(x) with (x < -1.0)");
        if (x >  1.0) throw LpcRuntimeError("math: asin(x) with (x > 1.0)");
        return Value(std::asin(x));
    });
    t.registerEfun("acos", [asFloat](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("acos: expected a numeric argument");
        double x = asFloat(args[0]);
        if (x < -1.0) throw LpcRuntimeError("math: acos(x) with (x < -1.0)");
        if (x >  1.0) throw LpcRuntimeError("math: acos(x) with (x > 1.0)");
        return Value(std::acos(x));
    });
    t.registerEfun("atan", [asFloat](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("atan: expected a numeric argument");
        return Value(std::atan(asFloat(args[0])));
    });

    // float sqrt(float) -- packages/math.c f_sqrt(): throws if x < 0.0.
    t.registerEfun("sqrt", [asFloat](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("sqrt: expected a numeric argument");
        double x = asFloat(args[0]);
        if (x < 0.0) throw LpcRuntimeError("math: sqrt(x) with (x < 0.0)");
        return Value(std::sqrt(x));
    });

    // float log(float) -- packages/math.c f_log(): natural logarithm,
    // throws if x <= 0.0.
    t.registerEfun("log", [asFloat](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("log: expected a numeric argument");
        double x = asFloat(args[0]);
        if (x <= 0.0) throw LpcRuntimeError("math: log(x) with (x <= 0.0)");
        return Value(std::log(x));
    });

    // float log10(float) -- packages/math_spec.c declares it; f_log10() is
    // the base-10 variant. Same guard as log() for domain.
    t.registerEfun("log10", [asFloat](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("log10: expected a numeric argument");
        double x = asFloat(args[0]);
        if (x <= 0.0) throw LpcRuntimeError("math: log10(x) with (x <= 0.0)");
        return Value(std::log10(x));
    });

    // float log2(float|int x) -- current FluffOS's own real, genuinely
    // new-since-2.9 efun (confirmed absent from
    // temp/reference/fluffos-2.9-ds2.08: no log2/f_log2 anywhere in that
    // tree at all -- only log()/log10() existed there). Signature and
    // semantics confirmed directly against real current source, not
    // guessed: src/packages/math/math.spec's own "float log2(float|int);",
    // and src/packages/math/math.cc's own f_log2(): int-or-float
    // argument (an int promoted to float first), "error("math: log2(x)
    // with (x <= 0.0)\n");" on a non-positive argument, otherwise real
    // C library log2(). Same domain-guard shape this driver's own
    // log()/log10() right above already use, ported the same way.
    t.registerEfun("log2", [asFloat](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("log2: expected a numeric argument");
        double x = asFloat(args[0]);
        if (x <= 0.0) throw LpcRuntimeError("math: log2(x) with (x <= 0.0)");
        return Value(std::log2(x));
    });

    // float pow(float, float) -- packages/math_spec.c.
    t.registerEfun("pow", [asFloat](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2) throw LpcRuntimeError("pow: expected two numeric arguments");
        return Value(std::pow(asFloat(args[0]), asFloat(args[1])));
    });

    // float exp(float) -- packages/math.c f_exp(): e^x.
    t.registerEfun("exp", [asFloat](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("exp: expected a numeric argument");
        return Value(std::exp(asFloat(args[0])));
    });

    // float floor(float) / float ceil(float) -- packages/math.c f_floor(),
    // f_ceil(). Each operates on sp->u.real in place (T_REAL input only in
    // real FluffOS); this driver promotes int to float the same way as the
    // other math efuns.
    t.registerEfun("floor", [asFloat](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("floor: expected a numeric argument");
        return Value(std::floor(asFloat(args[0])));
    });
    t.registerEfun("ceil", [asFloat](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("ceil: expected a numeric argument");
        return Value(std::ceil(asFloat(args[0])));
    });

    // float round(float f) -- current FluffOS's own real, genuinely
    // new-since-2.9 efun (confirmed absent from
    // temp/reference/fluffos-2.9-ds2.08: no round/f_round anywhere in
    // that tree at all). Signature and semantics confirmed directly
    // against real current source: src/packages/math/math.spec's own
    // "float round(float);" (float-only there, unlike log2()'s
    // float|int; this driver promotes int to float here too, matching
    // its own already-established floor()/ceil() precedent right above,
    // not a real-signature deviation but the same local leniency
    // convention). Real f_round() (src/packages/math/math.cc):
    // "sp->u.real = round(sp->u.real);" -- plain C library round(),
    // round-half-away-from-zero (round(2.5) == 3.0, round(-2.5) ==
    // -3.0), no domain restriction, no error case.
    t.registerEfun("round", [asFloat](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("round: expected a numeric argument");
        return Value(std::round(asFloat(args[0])));
    });

    // -------------------------------------------------------------------------
    // Vector math efuns: norm(), dotprod(), distance(), angle()
    // (packages/math/math.spec + math.cc). Genuinely new-since-2.9:
    // temp/reference/fluffos-2.9-ds2.08/packages/math_spec.c stops at
    // ceil() and has no norm/dotprod/distance/angle anywhere in that tree;
    // the current locally-vendored clone (temp/fluffos/src/packages/math/)
    // adds them, its own math.cc header comment reading "Added norm,
    // dotprod, distance, angle, log2." Same category as row 2.46's sha1(),
    // the log2()/round() row above, and rows 2.16/2.24 -- real current-
    // FluffOS surface the 2.9 reference never carried, not an old gap.
    //
    // Signatures (math.spec, current clone):
    //   float norm(int *|float *);
    //   float dotprod(int *|float *, int *|float *);
    //   float distance(int *|float *, int *|float *);
    //   float angle(int *|float *, int *|float *);
    //
    // Semantics confirmed from math.cc's own norm()/vector_op()/f_norm()/
    // f_dotprod()/f_distance()/f_angle():
    //   - Each element is read as int (T_NUMBER) or float (T_REAL) and
    //     promoted to double; any other element type is an error. Real
    //     f_norm() says "norm: invalid argument 1."; vector_op (used by the
    //     other three) reports "<efun>: invalid arg N." with N being 1 for
    //     the first vector or 2 for the second, and it inspects the second
    //     operand's element before the first. f_angle() runs the dotprod
    //     pass first, so a non-numeric element there surfaces as
    //     "angle: invalid arg N." (the norm-failure texts
    //     "angle: invalid argument 1./2." are unreachable once every
    //     element is known numeric).
    //   - norm(a)      = sqrt(sum a_i^2). An empty array gives sqrt(0.0).
    //   - dotprod(a,b) = sum a_i*b_i. distance(a,b) = sqrt(sum (b_i-a_i)^2).
    //     Both require equal lengths, else "<efun>: cannot take the
    //     <dotprod|distance> of vectors of different sizes."
    //   - angle(a,b)   = acos(dotprod(a,b) / (norm(a) * norm(b))); its size
    //     mismatch reads "angle: cannot calculate the angle between vectors
    //     of different sizes."
    // Real FluffOS operates on the driver stack in place but none of these
    // four mutate their array arguments (unlike the matrix package); this
    // driver reads the std::shared_ptr<Array> arguments and returns a fresh
    // float Value.
    //
    // Corpus call-site frequency, checked before implementing: grepped
    // every vendored corpus under temp/ (core-lib, dead-souls, es2_mudlib,
    // lima, nightmare3, reference-lpc-mud-library, wiz_tools, lil) plus the
    // bundled mudlib/ for norm( / dotprod( / distance( / angle(: zero real
    // LPC call sites. Motivation is FluffOS-surface parity, the same
    // honestly-named basis as rows 2.16/2.24/2.46/2.47/2.48/2.49/2.50;
    // vector norms and dot products are independently verifiable against
    // hand-computed values with no live-instance dependency.
    // -------------------------------------------------------------------------
    auto vectorArg = [](const Value& v, const std::string& errmsg) -> std::vector<double> {
        if (!std::holds_alternative<std::shared_ptr<Array>>(v.data)) {
            throw LpcRuntimeError(errmsg);
        }
        auto arr = std::get<std::shared_ptr<Array>>(v.data);
        std::vector<double> out;
        if (arr) {
            out.reserve(arr->items.size());
            for (const auto& el : arr->items) {
                if (std::holds_alternative<double>(el.data)) {
                    out.push_back(std::get<double>(el.data));
                } else if (std::holds_alternative<int64_t>(el.data)) {
                    out.push_back(static_cast<double>(std::get<int64_t>(el.data)));
                } else {
                    throw LpcRuntimeError(errmsg);
                }
            }
        }
        return out;
    };

    t.registerEfun("norm", [vectorArg](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("norm: expected an array argument");
        std::vector<double> a = vectorArg(args[0], "norm: invalid argument 1.");
        double total = 0.0;
        for (double x : a) total += x * x;
        return Value(std::sqrt(total));
    });

    auto vecPair = [vectorArg](std::vector<Value>& args, const char* efun,
                               const char* sizemsg)
            -> std::pair<std::vector<double>, std::vector<double>> {
        if (args.size() < 2) {
            throw LpcRuntimeError(std::string(efun) + ": expected two array arguments");
        }
        std::vector<double> a = vectorArg(args[0], std::string(efun) + ": invalid arg 1.");
        std::vector<double> b = vectorArg(args[1], std::string(efun) + ": invalid arg 2.");
        if (a.size() != b.size()) throw LpcRuntimeError(sizemsg);
        return {std::move(a), std::move(b)};
    };

    t.registerEfun("dotprod", [vecPair](VM&, std::vector<Value>& args) -> Value {
        auto p = vecPair(args, "dotprod",
            "dotprod: cannot take the dotprod of vectors of different sizes.");
        double total = 0.0;
        for (std::size_t i = 0; i < p.first.size(); ++i) total += p.first[i] * p.second[i];
        return Value(total);
    });

    t.registerEfun("distance", [vecPair](VM&, std::vector<Value>& args) -> Value {
        auto p = vecPair(args, "distance",
            "distance: cannot take the distance of vectors of different sizes.");
        double total = 0.0;
        for (std::size_t i = 0; i < p.first.size(); ++i) {
            double d = p.second[i] - p.first[i];
            total += d * d;
        }
        return Value(std::sqrt(total));
    });

    t.registerEfun("angle", [vecPair](VM&, std::vector<Value>& args) -> Value {
        auto p = vecPair(args, "angle",
            "angle: cannot calculate the angle between vectors of different sizes.");
        double dot = 0.0, na = 0.0, nb = 0.0;
        for (std::size_t i = 0; i < p.first.size(); ++i) {
            dot += p.first[i] * p.second[i];
            na  += p.first[i] * p.first[i];
            nb  += p.second[i] * p.second[i];
        }
        return Value(std::acos(dot / (std::sqrt(na) * std::sqrt(nb))));
    });

    // -------------------------------------------------------------------------
    // Matrix package (packages/matrix_spec.c / matrix.c) - first slice only:
    // id_matrix(), translate(), scale(). This is an old, always-present gap,
    // not new-since-2.9: temp/reference/fluffos-2.9-ds2.08/packages/ carries
    // matrix.c/matrix.h/matrix_spec.c already, and the current locally-
    // vendored clone (temp/fluffos/src/packages/matrix/) has the identical
    // math. Row 2.45 had bucketed all 8 matrix.spec names as deferred; this
    // slice carves out the three that need nothing new (no dependency, no
    // buffer type, no scheduler wiring, no security surface) and are
    // independently verifiable by hand-computed known matrices.
    //
    // Signatures (matrix_spec.c, both trees, identical):
    //   float *id_matrix();
    //   float *translate(float *, float, float, float);
    //   float *scale(float *, float, float, float);
    //
    // Semantics confirmed from f_id_matrix()/f_translate()/f_scale() plus
    // translate_matrix()/scale_matrix()/mult_matrix() in
    // temp/reference/fluffos-2.9-ds2.08/packages/matrix.c, and the in-place
    // contract from the current clone's own docs
    // (temp/fluffos/docs/efun/general/{id_matrix,translate,scale}.md):
    //
    //   - id_matrix() returns a fresh 16-element float array holding the 4x4
    //     row-major identity ({1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}).
    //   - translate(m,x,y,z) computes m = m * T, where T is the identity
    //     with elements 12,13,14 set to x,y,z. scale(m,x,y,z) computes
    //     m = m * S, where S is diag(x,y,z,1). mult_matrix is the plain
    //     row-major product m[4r+c] = sum_k a[4r+k] * b[4k+c].
    //   - The passed matrix array is mutated IN PLACE (its 16 elements
    //     overwritten) and that same array is the return value, not a copy.
    //     This driver returns the identical std::shared_ptr<Array> it was
    //     handed, so LPC aliasing matches real FluffOS exactly.
    //
    // Matrix-argument validation follows the current clone, which added an
    // explicit "matrix transform requires a 16-element array." /
    // "...float array." error() guard that the 2.9 tree lacked (2.9 read 16
    // slots unconditionally). This driver has tagged Values, so validating
    // is the correct port rather than an over-read.
    //
    // Real f_translate()/f_scale() bad_arg() only on a non-T_REAL 3rd or 4th
    // argument (the 2nd, x, is read unchecked in 2.9). This driver instead
    // coerces all three numerically via the same asFloat() its math package
    // uses, matching this codebase's own established int-to-float leniency
    // rather than reproducing a union-misread quirk.
    //
    // Deferred to later slices, named: rotate_x()/rotate_y()/rotate_z()
    // (add RADIANS_PER_DEGREE plus trig), and lookat_rotate()/
    // lookat_rotate2() (add the Vector helpers normalize_array/
    // cross_product/points_to_array; lookat_rotate2 additionally needs the
    // min_arg > 4 support the 2.9 spec's own comment says the compiler
    // lacked, still #if 0 there, only live in the current clone).
    //
    // Corpus call-site frequency, checked before implementing: grepped every
    // vendored corpus under temp/ (core-lib, dead-souls, es2_mudlib, lima,
    // nightmare3, reference-lpc-mud-library, this project's own bundled
    // mudlib/, wiz_tools, lil) for id_matrix / translate( / scale( /
    // rotate_x / rotate_y / rotate_z / lookat_rotate: zero real LPC call
    // sites (the translate(/scale( hits are all unrelated -- NPC language
    // translation, the word "scale" in prose). Motivation is FluffOS-
    // surface parity specifically, the same honestly-named basis as rows
    // 2.16/2.24/2.25/2.46; matrix math is independently verifiable against
    // hand-computable known results with no live-instance dependency.
    // -------------------------------------------------------------------------
    auto matrixArg16 = [](const Value& v, const char* efun) -> std::shared_ptr<Array> {
        if (!std::holds_alternative<std::shared_ptr<Array>>(v.data)) {
            throw LpcRuntimeError(std::string(efun) +
                                  ": first argument must be a 16-element float array");
        }
        auto arr = std::get<std::shared_ptr<Array>>(v.data);
        if (!arr || arr->items.size() < 16) {
            throw LpcRuntimeError("matrix transform requires a 16-element array.");
        }
        for (const auto& el : arr->items) {
            if (!std::holds_alternative<double>(el.data)) {
                throw LpcRuntimeError("matrix transform requires a 16-element float array.");
            }
        }
        return arr;
    };

    // m[4r+c] = sum_k a[4r+k] * b[4k+c] -- matrix.c's mult_matrix(), the
    // plain row-major 4x4 product, expanded here the same way.
    auto multMatrix = [](const double* a, const double* b, double* out) {
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) {
                out[4 * r + c] = a[4 * r + 0] * b[0 * 4 + c] +
                                 a[4 * r + 1] * b[1 * 4 + c] +
                                 a[4 * r + 2] * b[2 * 4 + c] +
                                 a[4 * r + 3] * b[3 * 4 + c];
            }
        }
    };

    t.registerEfun("id_matrix", [](VM&, std::vector<Value>& args) -> Value {
        (void)args;
        static const double ident[16] = {1., 0., 0., 0., 0., 1., 0., 0.,
                                         0., 0., 1., 0., 0., 0., 0., 1.};
        auto arr = std::make_shared<Array>();
        arr->items.reserve(16);
        for (double d : ident) arr->items.emplace_back(Value(d));
        return Value(arr);
    });

    t.registerEfun("translate", [asFloat, matrixArg16, multMatrix](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 4) {
            throw LpcRuntimeError("translate: expected (float *matrix, float x, float y, float z)");
        }
        auto m = matrixArg16(args[0], "translate");
        double x = asFloat(args[1]), y = asFloat(args[2]), z = asFloat(args[3]);
        double cur[16];
        for (int i = 0; i < 16; ++i) cur[i] = std::get<double>(m->items[i].data);
        const double trans[16] = {1., 0., 0., 0., 0., 1., 0., 0.,
                                  0., 0., 1., 0., x,  y,  z,  1.};
        double res[16];
        multMatrix(cur, trans, res);
        for (int i = 0; i < 16; ++i) m->items[i] = Value(res[i]);
        return Value(m);
    });

    t.registerEfun("scale", [asFloat, matrixArg16, multMatrix](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 4) {
            throw LpcRuntimeError("scale: expected (float *matrix, float x, float y, float z)");
        }
        auto m = matrixArg16(args[0], "scale");
        double x = asFloat(args[1]), y = asFloat(args[2]), z = asFloat(args[3]);
        double cur[16];
        for (int i = 0; i < 16; ++i) cur[i] = std::get<double>(m->items[i].data);
        const double scaling[16] = {x,  0., 0., 0., 0., y,  0., 0.,
                                    0., 0., z,  0., 0., 0., 0., 1.};
        double res[16];
        multMatrix(cur, scaling, res);
        for (int i = 0; i < 16; ++i) m->items[i] = Value(res[i]);
        return Value(m);
    });

    // float *rotate_x(float *matrix, float degrees) / rotate_y / rotate_z --
    // matrix.spec slice 2. Row 2.47 landed the id_matrix()/translate()/
    // scale() first slice and named this trio as the next deferred one.
    // Signatures (packages/matrix_spec.c, both the vendored 2.9 ds2.08
    // reference and the current clone, identical):
    //   float *rotate_x(float *, float);
    //   float *rotate_y(float *, float);
    //   float *rotate_z(float *, float);
    //
    // Semantics from temp/reference/fluffos-2.9-ds2.08/packages/matrix.c's
    // own f_rotate_x()/f_rotate_y()/f_rotate_z() plus rotate_x_matrix()/
    // rotate_y_matrix()/rotate_z_matrix() (re-checked against the current
    // clone temp/fluffos/src/packages/matrix/matrix.cc -- identical math):
    //
    //   - The angle is in DEGREES, converted to radians with
    //     RADIANS_PER_DEGREE. matrix.h's own literal for that constant is
    //     0.01745329252 (a truncation of pi/180), used verbatim below
    //     rather than M_PI/180.0 so this driver's output matches the real
    //     package byte-for-byte, not just to 1e-9.
    //   - matrix = matrix * R, where R is the standard row-major rotation
    //     about the named axis:
    //       Rx = [1,0,0,0, 0,c,s,0, 0,-s,c,0, 0,0,0,1]
    //       Ry = [c,0,-s,0, 0,1,0,0, s,0,c,0, 0,0,0,1]
    //       Rz = [c,s,0,0, -s,c,0,0, 0,0,1,0, 0,0,0,1]
    //     with c = cos(rad), s = sin(rad). mult_matrix is the same plain
    //     row-major product translate()/scale() above already use.
    //   - The passed array is mutated IN PLACE and that same array is the
    //     return value, not a copy (docs/efun/general/rotate_x.md:
    //     "modified IN PLACE ... that same array is left on the stack as
    //     the return value"). This driver returns the identical
    //     std::shared_ptr<Array> it was handed.
    //
    // Same 16-float-array validation guard the current clone added (the
    // 2.9 tree read 16 slots unconditionally), reused here via
    // matrixArg16(). Real f_rotate_x() reads the angle via (sp--)->u.real
    // with no type check; this driver coerces via asFloat() like the rest
    // of its math efuns, matching this codebase's own int-to-float
    // leniency rather than reproducing a union misread.
    //
    // Corpus call-site frequency was already checked when row 2.47 landed
    // (grepped every vendored corpus under temp/ for rotate_x/rotate_y/
    // rotate_z alongside id_matrix/translate/scale: zero real LPC call
    // sites). Motivation is FluffOS-surface parity, the same honestly-
    // named basis as rows 2.16/2.24/2.25/2.46/2.47; rotation matrices are
    // independently verifiable against hand-computed values (0 and 90
    // degrees) with no live-instance dependency.
    auto applyRotation = [asFloat, matrixArg16, multMatrix](
            std::vector<Value>& args, const char* efun, char axis) -> Value {
        if (args.size() < 2) {
            throw LpcRuntimeError(std::string(efun) +
                                  ": expected (float *matrix, float degrees)");
        }
        auto m = matrixArg16(args[0], efun);
        double a = asFloat(args[1]) * 0.01745329252;  // RADIANS_PER_DEGREE
        double c = std::cos(a), s = std::sin(a);
        double cur[16];
        for (int i = 0; i < 16; ++i) cur[i] = std::get<double>(m->items[i].data);
        double rot[16] = {1., 0., 0., 0., 0., 1., 0., 0.,
                          0., 0., 1., 0., 0., 0., 0., 1.};
        if (axis == 'x') {
            rot[5] = c;  rot[6] = s;  rot[9] = -s;  rot[10] = c;
        } else if (axis == 'y') {
            rot[0] = c;  rot[2] = -s;  rot[8] = s;  rot[10] = c;
        } else {  // 'z'
            rot[0] = c;  rot[1] = s;  rot[4] = -s;  rot[5] = c;
        }
        double res[16];
        multMatrix(cur, rot, res);
        for (int i = 0; i < 16; ++i) m->items[i] = Value(res[i]);
        return Value(m);
    };

    t.registerEfun("rotate_x", [applyRotation](VM&, std::vector<Value>& args) -> Value {
        return applyRotation(args, "rotate_x", 'x');
    });
    t.registerEfun("rotate_y", [applyRotation](VM&, std::vector<Value>& args) -> Value {
        return applyRotation(args, "rotate_y", 'y');
    });
    t.registerEfun("rotate_z", [applyRotation](VM&, std::vector<Value>& args) -> Value {
        return applyRotation(args, "rotate_z", 'z');
    });

    // float *lookat_rotate(float *matrix, float x, float y, float z) and
    // float *lookat_rotate2(float *matrix, float ex, float ey, float ez,
    //                       float lx, float ly, float lz)
    // -- matrix.spec final slice. Row 2.48 landed rotate_x/y/z and named
    // these two as the last deferred pair.
    //
    // Signatures: matrix_spec.c in the vendored 2.9 ds2.08 reference
    // declares `float *lookat_rotate(float *, float, float, float);` live
    // and `float *lookat_rotate2(float *, float, float, float, float,
    // float, float);` inside a `#if 0` block, with the comment "for this
    // efun to work again, the compiler needs support for min_arg > 4 ... a
    // limit of 4 args was imposed" on FluffOS's own spec-driven efun
    // argument type-checker. That limit does NOT exist in this driver:
    // there is no .spec/efun_defs.c pipeline here, no compile-time efun
    // signature table at all (CodeGen.cpp deliberately does not link the
    // efun table, to avoid a link cycle), and CallEfun passes an
    // arbitrary-length std::vector<Value> straight through to the lambda,
    // which validates its own arguments -- pcre_assoc() right below is
    // already a registered 5-argument efun reading args[4]. So both names
    // ship together here; the 2.9 `#if 0` reason is a FluffOS-compiler
    // artifact with no analog in this architecture.
    //
    // Semantics from temp/reference/fluffos-2.9-ds2.08/packages/matrix.c's
    // own lookat_rotate()/lookat_rotate2() (the core functions, both
    // compiled unconditionally; only the f_lookat_rotate2() stack glue was
    // ever `#if 0`), re-checked against the current clone
    // temp/fluffos/src/packages/matrix/matrix.cc -- identical math:
    //
    //   - Vector helpers (matrix.c file-static): points_to_array(v,pa,pb)
    //     sets v = pa - pb componentwise; cross_product(v,va,vb) sets
    //     v = va x vb; normalize_array(v) divides v by |v| but ONLY when
    //     |v| != 0 (the real `if (m)` guard -- a zero-length vector is
    //     left untouched, not turned into NaNs).
    //   - lookat_rotate(T, x,y,z): look point lp = (x,y,z); eye point
    //     ep = (T[12],T[13],T[14]); the up-reference u0 = (T[0],T[4],T[8]).
    //     All three are read from the input matrix BEFORE it is
    //     overwritten.
    //   - lookat_rotate2(ex,ey,ez, lx,ly,lz): ep = (ex,ey,ez),
    //     lp = (lx,ly,lz), u0 = (0,1,0) fixed; the input matrix contents
    //     are not read at all.
    //   - Then, identically for both: N = normalize(lp - ep);
    //     V = normalize(N x u0); U = normalize(V x N); and the result
    //     matrix is
    //       [ U.x V.x N.x 0
    //         U.y V.y N.y 0
    //         U.z V.z N.z 0
    //         U.ep V.ep N.ep 1 ]
    //     where U.ep is the dot product U . ep, etc. (matrix.c has two
    //     `#if 0` alternatives for row 3 -- the raw ep and the negated
    //     dot -- the live code is the positive dot product used here).
    //   - The passed array is overwritten in place with those 16 values
    //     and returned, same aliasing contract as translate()/scale()/
    //     rotate_x() above (f_lookat_rotate does `sp -= 3`, leaving the
    //     matrix array on the stack as the return value).
    //
    // Local conventions carried over from slices 1 and 2, not new: the
    // matrixArg16 guard (2.9 read 16 slots unconditionally; f_lookat_rotate
    // bad_arg's only on args 3 and 4) and asFloat() coercion of the
    // coordinates (this codebase's established int-to-float leniency
    // rather than a union misread). Corpus call-site frequency was checked
    // when row 2.47 landed: zero real LPC call sites for lookat_rotate in
    // any vendored corpus under temp/. Motivation is FluffOS-surface
    // parity, the same basis as rows 2.16/2.24/2.25/2.46/2.47/2.48; the
    // result is independently verifiable against hand-computed viewing
    // matrices with no live-instance dependency.
    auto lookatRotate = [matrixArg16, asFloat](
            std::vector<Value>& args, const char* efun, bool variant2) -> Value {
        const size_t need = variant2 ? 7 : 4;
        if (args.size() < need) {
            throw LpcRuntimeError(std::string(efun) + (variant2
                ? ": expected (float *matrix, float ex, float ey, float ez, float lx, float ly, float lz)"
                : ": expected (float *matrix, float x, float y, float z)"));
        }
        auto m = matrixArg16(args[0], efun);

        double ep[3], lp[3], u0[3];
        if (variant2) {
            ep[0] = asFloat(args[1]); ep[1] = asFloat(args[2]); ep[2] = asFloat(args[3]);
            lp[0] = asFloat(args[4]); lp[1] = asFloat(args[5]); lp[2] = asFloat(args[6]);
            u0[0] = 0.0; u0[1] = 1.0; u0[2] = 0.0;
        } else {
            lp[0] = asFloat(args[1]); lp[1] = asFloat(args[2]); lp[2] = asFloat(args[3]);
            double T[16];
            for (int i = 0; i < 16; ++i) T[i] = std::get<double>(m->items[i].data);
            ep[0] = T[12]; ep[1] = T[13]; ep[2] = T[14];
            u0[0] = T[0];  u0[1] = T[4];  u0[2] = T[8];
        }

        auto normalize = [](double* v) {
            double mag = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (mag != 0.0) { v[0] /= mag; v[1] /= mag; v[2] /= mag; }
        };
        auto cross = [](double* out, const double* a, const double* b) {
            out[0] = a[1] * b[2] - a[2] * b[1];
            out[1] = a[2] * b[0] - a[0] * b[2];
            out[2] = a[0] * b[1] - a[1] * b[0];
        };

        double N[3] = {lp[0] - ep[0], lp[1] - ep[1], lp[2] - ep[2]};
        normalize(N);
        double U[3] = {u0[0], u0[1], u0[2]};
        double V[3];
        cross(V, N, U);
        normalize(V);
        cross(U, V, N);
        normalize(U);

        double M[16];
        M[0] = U[0];  M[1] = V[0];  M[2] = N[0];  M[3] = 0.0;
        M[4] = U[1];  M[5] = V[1];  M[6] = N[1];  M[7] = 0.0;
        M[8] = U[2];  M[9] = V[2];  M[10] = N[2]; M[11] = 0.0;
        M[12] = U[0] * ep[0] + U[1] * ep[1] + U[2] * ep[2];
        M[13] = V[0] * ep[0] + V[1] * ep[1] + V[2] * ep[2];
        M[14] = N[0] * ep[0] + N[1] * ep[1] + N[2] * ep[2];
        M[15] = 1.0;

        for (int i = 0; i < 16; ++i) m->items[i] = Value(M[i]);
        return Value(m);
    };

    t.registerEfun("lookat_rotate", [lookatRotate](VM&, std::vector<Value>& args) -> Value {
        return lookatRotate(args, "lookat_rotate", false);
    });
    t.registerEfun("lookat_rotate2", [lookatRotate](VM&, std::vector<Value>& args) -> Value {
        return lookatRotate(args, "lookat_rotate2", true);
    });

    // mixed abs(int | float) -- packages/contrib.c f_abs(): negates negative
    // numbers in-place; preserves the exact input type (int in → int out,
    // float in → float out), not a float-returning function.
    t.registerEfun("abs", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("abs: expected a numeric argument");
        const Value& v = args[0];
        if (std::holds_alternative<int64_t>(v.data)) {
            int64_t n = std::get<int64_t>(v.data);
            return Value(n < 0 ? -n : n);
        }
        if (std::holds_alternative<double>(v.data)) {
            double d = std::get<double>(v.data);
            return Value(d < 0.0 ? -d : d);
        }
        throw LpcRuntimeError("abs: argument must be int or float");
    });

    // mixed max(mixed *, void|int flag) -- packages/contrib.c f_max():
    // takes an array of ints/floats/strings, returns the largest element.
    // With flag != 0 (second argument), returns the *index* of the max
    // element instead of the value itself, matching real f_max()'s own
    // "if (st_num_arg == 2 && sp->u.number != 0) push_number(max_index)"
    // branch. Confirmed against contrib.c directly.
    t.registerEfun("max", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<Array>>(args[0].data))
            throw LpcRuntimeError("max: expected an array as first argument");
        const auto& arr = std::get<std::shared_ptr<Array>>(args[0].data)->items;
        if (arr.empty()) throw LpcRuntimeError("max: can't find max of an empty array");

        bool returnIndex = args.size() >= 2 &&
            std::holds_alternative<int64_t>(args[1].data) &&
            std::get<int64_t>(args[1].data) != 0;

        size_t maxIdx = 0;
        for (size_t i = 1; i < arr.size(); ++i) {
            const Value& cur = arr[i];
            const Value& best = arr[maxIdx];
            bool curGreater = false;
            if (std::holds_alternative<int64_t>(cur.data) && std::holds_alternative<int64_t>(best.data))
                curGreater = std::get<int64_t>(cur.data) > std::get<int64_t>(best.data);
            else if (std::holds_alternative<double>(cur.data) && std::holds_alternative<double>(best.data))
                curGreater = std::get<double>(cur.data) > std::get<double>(best.data);
            else if (std::holds_alternative<int64_t>(cur.data) && std::holds_alternative<double>(best.data))
                curGreater = static_cast<double>(std::get<int64_t>(cur.data)) > std::get<double>(best.data);
            else if (std::holds_alternative<double>(cur.data) && std::holds_alternative<int64_t>(best.data))
                curGreater = std::get<double>(cur.data) > static_cast<double>(std::get<int64_t>(best.data));
            else if (std::holds_alternative<std::string>(cur.data) && std::holds_alternative<std::string>(best.data))
                curGreater = std::get<std::string>(cur.data) > std::get<std::string>(best.data);
            else
                throw LpcRuntimeError("max: array must consist of ints, floats, or strings of the same type");
            if (curGreater) maxIdx = i;
        }
        if (returnIndex) return Value(static_cast<int64_t>(maxIdx));
        return arr[maxIdx];
    });

    // mixed min(mixed *, void|int flag) -- packages/contrib.c f_min():
    // mirror of max(), returns smallest element (or its index when flag!=0).
    t.registerEfun("min", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<Array>>(args[0].data))
            throw LpcRuntimeError("min: expected an array as first argument");
        const auto& arr = std::get<std::shared_ptr<Array>>(args[0].data)->items;
        if (arr.empty()) throw LpcRuntimeError("min: can't find min of an empty array");

        bool returnIndex = args.size() >= 2 &&
            std::holds_alternative<int64_t>(args[1].data) &&
            std::get<int64_t>(args[1].data) != 0;

        size_t minIdx = 0;
        for (size_t i = 1; i < arr.size(); ++i) {
            const Value& cur = arr[i];
            const Value& best = arr[minIdx];
            bool curSmaller = false;
            if (std::holds_alternative<int64_t>(cur.data) && std::holds_alternative<int64_t>(best.data))
                curSmaller = std::get<int64_t>(cur.data) < std::get<int64_t>(best.data);
            else if (std::holds_alternative<double>(cur.data) && std::holds_alternative<double>(best.data))
                curSmaller = std::get<double>(cur.data) < std::get<double>(best.data);
            else if (std::holds_alternative<int64_t>(cur.data) && std::holds_alternative<double>(best.data))
                curSmaller = static_cast<double>(std::get<int64_t>(cur.data)) < std::get<double>(best.data);
            else if (std::holds_alternative<double>(cur.data) && std::holds_alternative<int64_t>(best.data))
                curSmaller = std::get<double>(cur.data) < static_cast<double>(std::get<int64_t>(best.data));
            else if (std::holds_alternative<std::string>(cur.data) && std::holds_alternative<std::string>(best.data))
                curSmaller = std::get<std::string>(cur.data) < std::get<std::string>(best.data);
            else
                throw LpcRuntimeError("min: array must consist of ints, floats, or strings of the same type");
            if (curSmaller) minIdx = i;
        }
        if (returnIndex) return Value(static_cast<int64_t>(minIdx));
        return arr[minIdx];
    });

    // mixed regexp(string | string *, string pattern, void | int flag)
    // -- Phase 0 row 0.11. Confirmed directly against func_spec.c's own
    // signature and efuns_main.c's f_regexp(): a single-string subject
    // returns plain int 1/0 (match_single_regexp() -- a 3rd argument is
    // an error in this form, "3rd argument illegal for regexp(string,
    // string)", real error text this driver reproduces verbatim); an
    // array-of-strings subject returns the *matching* elements
    // themselves (match_regexp()), not a bool array -- this is a much
    // broader efun than "1 if matches, 0 if not" alone (the simplified
    // description this row's own task prompt gives, which only covers
    // the single-string form).
    //
    // flag is only meaningful for the array form: bit 0 (flag&1)
    // interleaves each matching element's original 1-based index right
    // after it in the result (string, index, string, index, ...,
    // confirmed by hand-tracing match_regexp()'s own backward-filling
    // loop against a concrete example -- string first, index second,
    // ascending original order); bit 1 (flag&2) inverts the selection
    // to non-matching elements instead ("match = !(flag & 2)" in the
    // real source). A non-string array element is never selected
    // either way, matching match_regexp()'s own "!(type == T_STRING)"
    // short-circuit landing on the same res[size] = 0 branch regardless
    // of invert.
    t.registerEfun("regexp", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("regexp: expected a pattern string as the second argument");
        }
        const std::string& pattern = std::get<std::string>(args[1].data);

        if (std::holds_alternative<std::string>(args[0].data)) {
            if (args.size() > 2) {
                throw LpcRuntimeError("3rd argument illegal for regexp(string, string)");
            }
            const std::string& subject = std::get<std::string>(args[0].data);
            auto code = compileRegex(pattern);
            size_t s = 0, e = 0;
            bool matched = regexFindNext(code.get(), subject, 0, s, e);
            return Value(static_cast<int64_t>(matched ? 1 : 0));
        }

        if (!std::holds_alternative<std::shared_ptr<Array>>(args[0].data)) {
            throw LpcRuntimeError("regexp: expected a string or an array of strings as the first argument");
        }
        auto arr = std::get<std::shared_ptr<Array>>(args[0].data);
        int64_t flag = 0;
        if (args.size() > 2) {
            if (!std::holds_alternative<int64_t>(args[2].data)) {
                throw LpcRuntimeError("Bad argument 3 to regexp()");
            }
            flag = std::get<int64_t>(args[2].data);
        }
        bool invert = (flag & 2) != 0;
        bool withIndex = (flag & 1) != 0;

        auto code = compileRegex(pattern);
        auto result = std::make_shared<Array>();
        if (arr) {
            for (size_t i = 0; i < arr->items.size(); ++i) {
                const Value& item = arr->items[i];
                if (!std::holds_alternative<std::string>(item.data)) continue;
                const std::string& line = std::get<std::string>(item.data);
                size_t s = 0, e = 0;
                bool matched = regexFindNext(code.get(), line, 0, s, e);
                if (matched == invert) continue;
                result->items.push_back(item);
                if (withIndex) result->items.push_back(Value(static_cast<int64_t>(i + 1)));
            }
        }
        return Value(result);
    });

    // string *regexplode(string str, string pattern) -- explodes str
    // around every match of pattern, keeping the matched substrings in
    // the result: an array alternating [text, match, text, match, ...,
    // text], always one more text segment than there are matches (an
    // unmatched string returns a single-element array holding the
    // whole input, mirroring explode()'s own no-op-on-no-match shape).
    // Checked directly against the vendored fluffos-2.9-ds2.08 source
    // before implementing, not assumed: this is NOT a real FluffOS 2.9
    // efun -- grepped the entire tree (func_spec.c, efun_defs.c,
    // efunctions.h, opc.h, array.c, regexp.c) and the only hit anywhere
    // is a comment inside implode()'s own loop-safety code ("The
    // following is from regexplode, to prevent i guess infinite loops
    // on \"\" patterns - Randor 5/29/94"), i.e. a MudOS-lineage efun
    // implode() once borrowed a guard from, not a function this driver
    // ever had a real spec for. Implemented anyway because this row's
    // own instruct.md/prompt.md explicitly ask for it and the shape is
    // a genuine, common LP-family convenience (LDMud and older MudOS
    // trees have had it); the empty-match loop guard below is the
    // exact same kludge reg_assoc()'s real implementation documents at
    // that same 5/29/94 comment, reused here for the same reason.
    t.registerEfun("regexplode", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("regexplode: expected (string, string pattern)");
        }
        const std::string& str = std::get<std::string>(args[0].data);
        const std::string& pattern = std::get<std::string>(args[1].data);

        auto code = compileRegex(pattern);
        auto result = std::make_shared<Array>();

        size_t cursor = 0, scan = 0;
        while (scan < str.size()) {
            size_t s = 0, e = 0;
            if (!regexFindNext(code.get(), str, scan, s, e)) break;
            result->items.push_back(Value(str.substr(cursor, s - cursor)));
            result->items.push_back(Value(str.substr(s, e - s)));
            cursor = e;
            scan = e;
            if (s == e) {
                // Zero-length match (e.g. a pattern like "x*" matching
                // the empty string) would otherwise loop forever at the
                // same offset -- same guard reg_assoc() below uses.
                if (scan >= str.size()) break;
                ++scan;
            }
        }
        result->items.push_back(Value(str.substr(cursor)));
        return Value(result);
    });

    // mixed *reg_assoc(string str, string *patterns, mixed *tokens,
    // mixed | void default) -- Phase 0 row 0.11. Confirmed directly
    // against array.c's own reg_assoc(), including its own worked
    // example in a comment right above the function
    // (reg_assoc("testhahatest", ({"haha","te"}), ({2,3}), 4) ==
    // ({({"","te","st","haha","","te","st"}), ({4,3,4,2,4,3,4})})),
    // reproduced verbatim as this efun's own regression test below.
    // Scans str left to right; at each position, tries every pattern
    // and keeps whichever produces the earliest-starting match (ties
    // go to the lower pattern index, via strict "<", matching the real
    // "if (!laststart || currstart < laststart)" comparison exactly --
    // real regexp.c's own immediate break on a zero-offset match is
    // just a shortcut for the same outcome, not a different rule).
    // Returns two same-length arrays: alternating [text-before-match,
    // matched-substring, text-before-next-match, ...,
    // trailing-text] and, in the same positions, [default, that
    // match's own token, default, ..., default] -- one more text/
    // default slot than there are matches, exactly mirroring
    // regexplode()'s own shape above (this is in fact the real,
    // original regexplode() this driver's own regexplode() above is
    // named after, per that function's own "from regexplode" comment).
    // Zero patterns is a real, explicit special case in the source
    // (the "else / Default match" branch): returns the whole string
    // unmatched, paired with a single default token.
    auto regAssocImpl = [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 3 ||
            !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::shared_ptr<Array>>(args[1].data) ||
            !std::holds_alternative<std::shared_ptr<Array>>(args[2].data)) {
            throw LpcRuntimeError("reg_assoc: expected (string, string *patterns, mixed *tokens, mixed|void default)");
        }
        const std::string& str = std::get<std::string>(args[0].data);
        auto patternsArr = std::get<std::shared_ptr<Array>>(args[1].data);
        auto tokensArr = std::get<std::shared_ptr<Array>>(args[2].data);
        Value defaultVal = args.size() > 3 ? args[3] : Value(int64_t{0});

        const size_t size = patternsArr ? patternsArr->items.size() : 0;
        if (size != (tokensArr ? tokensArr->items.size() : 0)) {
            throw LpcRuntimeError("Pattern and token array sizes must be identical.");
        }

        auto textResult = std::make_shared<Array>();
        auto tokenResult = std::make_shared<Array>();

        if (size == 0) {
            textResult->items.push_back(Value(str));
            tokenResult->items.push_back(defaultVal);
        } else {
            std::vector<Pcre2CodePtr> codes;
            codes.reserve(size);
            for (size_t i = 0; i < size; ++i) {
                if (!std::holds_alternative<std::string>(patternsArr->items[i].data)) {
                    throw LpcRuntimeError("Non-string found in pattern array.");
                }
                codes.push_back(compileRegex(std::get<std::string>(patternsArr->items[i].data)));
            }

            struct Match { size_t begin, end, tokIdx; };
            std::vector<Match> matches;
            size_t scan = 0;
            while (scan < str.size()) {
                size_t bestStart = std::string::npos, bestEnd = 0, bestIdx = 0;
                for (size_t i = 0; i < size; ++i) {
                    size_t s = 0, e = 0;
                    if (!regexFindNext(codes[i].get(), str, scan, s, e)) continue;
                    if (bestStart == std::string::npos || s < bestStart) {
                        bestStart = s; bestEnd = e; bestIdx = i;
                    }
                }
                if (bestStart == std::string::npos) break;
                matches.push_back({bestStart, bestEnd, bestIdx});
                scan = bestEnd;
                if (bestStart == bestEnd) {
                    if (scan >= str.size()) break;
                    ++scan;
                }
            }

            size_t cursor = 0;
            for (const auto& m : matches) {
                textResult->items.push_back(Value(str.substr(cursor, m.begin - cursor)));
                tokenResult->items.push_back(defaultVal);
                textResult->items.push_back(Value(str.substr(m.begin, m.end - m.begin)));
                tokenResult->items.push_back(tokensArr->items[m.tokIdx]);
                cursor = m.end;
            }
            textResult->items.push_back(Value(str.substr(cursor)));
            tokenResult->items.push_back(defaultVal);
        }

        auto outer = std::make_shared<Array>();
        outer->items.push_back(Value(textResult));
        outer->items.push_back(Value(tokenResult));
        return Value(outer);
    };
    t.registerEfun("reg_assoc", regAssocImpl);
    // "regexp_assoc" -- alias per this row's own instruct.md. Not a
    // real FluffOS 2.9 name either (same grep sweep as regexplode()
    // above found nothing), added for the same reason: the task's own
    // spec explicitly calls for it as a second name for reg_assoc.
    t.registerEfun("regexp_assoc", regAssocImpl);

    // pcre_match()/pcre_assoc() -- Phase 2 row 2.12 ("Full PCRE regexp
    // suite -- already started in Phase 0 - extend"). Real scope
    // confirmed from source before writing any code, the same
    // discipline row 2.15 used: grepped the entire pinned
    // temp/reference/fluffos-2.9-ds2.08 tree (func_spec.c, efun_defs.c,
    // and a plain "pcre" text search across every file in it) and found
    // zero hits anywhere -- that pinned 2.9-ds2.08 driver has no PCRE
    // package at all, only regexp()/reg_assoc() on its own bundled
    // Henry Spencer engine (the same finding the comment atop this
    // block already documents). This row's own real evidence is a
    // *different* real, separately-vendored FluffOS source tree,
    // temp/fluffos (a modern/master checkout, not the pinned
    // fluffos-2.9-ds2.08 patchlevel): src/packages/pcre/pcre.spec,
    // pcre.cc, and src/include/pcre_flags.h, a real package that only
    // exists in later FluffOS history, after it moved off the Henry
    // Spencer engine onto real PCRE. Documented explicitly rather than
    // silently treated as if it were 2.9-ds2.08 evidence, matching row
    // 2.15's own precedent of naming exactly which real vendored source
    // a citation comes from when it isn't the primary pinned tree.
    //
    // pcre_match()/pcre_assoc() are real, current FluffOS efuns
    // documented (pcre.cc's own header comment, docs/efun/pcre/
    // pcre_match.md, docs/efun/pcre/pcre_assoc.md, all confirmed
    // directly) as drop-in PCRE-backed analogs of regexp()/reg_assoc():
    // "analog with regexp efun for backwards compatibility reasons but
    // utilizing the PCRE library" -- i.e. same selection/tokenizing
    // contract as the existing regexp()/reg_assoc() above, plus an
    // extra optional "pcre_flags" bitmask argument regexp()/reg_assoc()
    // never had. Real call-site evidence confirmed in the vendored lima
    // corpus (lib/daemons/xterm256_d.c, lib/std/modules/m_frame.c):
    // pcre_assoc() and pcre_match() only, zero real call sites anywhere
    // in any vendored corpus (core-lib, dead-souls, es2_mudlib, lima,
    // nightmare3, reference-lpc-mud-library) for the real package's
    // other six efuns (pcre_version/pcre_match_all/pcre_extract/
    // pcre_replace/pcre_replace_callback/pcre_cache) -- those six are
    // out of scope this slice, the same bounded-to-real-evidence
    // discipline row 2.15 used for db_affected_rows/db_insert_id/
    // db_coldefs.
    //
    // pcre_flags bit values and their compile/exec-option mapping are
    // cited directly from src/include/pcre_flags.h and pcre.cc's own
    // compute_compile_options()/compute_exec_options() (confirmed
    // directly, not guessed): PCRE_I/PCRE_M/PCRE_S/PCRE_U/PCRE_X are
    // compile-time options (PCRE_CASELESS/MULTILINE/DOTALL/UNGREEDY/
    // EXTENDED); PCRE_A (anchored) is exec-time only. Real
    // compute_compile_options() also always ORs in PCRE_UTF8
    // unconditionally regardless of flags -- ported as PCRE2's own
    // PCRE2_UTF (PCRE2's renamed equivalent; PCRE2 has no PCRE2_UTF8
    // constant, confirmed against the linked pcre2.h directly) -- a
    // real, deliberate divergence from regexp()/reg_assoc()/
    // regexplode() above, which stay byte-oriented (compileOptions 0)
    // exactly as Phase 0 row 0.11 built and tested them; only these two
    // new efuns get PCRE2_UTF, matching what real pcre_match()/
    // pcre_assoc() actually do that real regexp()/reg_assoc() don't.
    constexpr int64_t kPcreFlagI = 1LL << 16;  // PCRE_I / PCRE_CASELESS
    constexpr int64_t kPcreFlagM = 1LL << 17;  // PCRE_M / PCRE_MULTILINE
    constexpr int64_t kPcreFlagS = 1LL << 18;  // PCRE_S / PCRE_DOTALL
    constexpr int64_t kPcreFlagU = 1LL << 19;  // PCRE_U / PCRE_UNGREEDY
    constexpr int64_t kPcreFlagX = 1LL << 20;  // PCRE_X / PCRE_EXTENDED
    constexpr int64_t kPcreFlagA = 1LL << 21;  // PCRE_A / PCRE_ANCHORED (exec-time)

    auto pcreCompileOptions = [](int64_t flags) -> uint32_t {
        uint32_t opts = PCRE2_UTF;
        if (flags & kPcreFlagI) opts |= PCRE2_CASELESS;
        if (flags & kPcreFlagM) opts |= PCRE2_MULTILINE;
        if (flags & kPcreFlagS) opts |= PCRE2_DOTALL;
        if (flags & kPcreFlagU) opts |= PCRE2_UNGREEDY;
        if (flags & kPcreFlagX) opts |= PCRE2_EXTENDED;
        return opts;
    };
    auto pcreMatchOptions = [](int64_t flags) -> uint32_t {
        return (flags & kPcreFlagA) ? PCRE2_ANCHORED : 0u;
    };

    // mixed pcre_match(string | string *, string pattern, void | int
    // flag, void | int pcre_flags) -- string form returns plain int
    // 1/0 exactly like regexp(string, string); the 3rd argument in that
    // form IS pcre_flags directly (confirmed against pcre_match.md's
    // own "For string input, the 3rd argument is treated as
    // pcre_flags" and f_pcre_match()'s own real argument parsing), no
    // 4th argument exists in the string form. Array form is identical
    // to regexp(string*, string, flag)'s own selection contract above
    // (flag&2 inverts to non-matching elements, flag&1 interleaves each
    // matching element's original 1-based index -- confirmed identical
    // against pcre_match()'s own real "match = !(flag & 2)" plus the
    // "flag &= 1" index-doubling below it), with pcre_flags as the
    // optional 4th argument. Real f_pcre_match() computes is_string
    // from a stack slot that used to be wrong whenever a 3rd argument
    // was present (a real, now-fixed driver bug pcre_match.lpc's own
    // testsuite regression-tests directly) -- moot here since this
    // driver's own calling convention passes args as a plain vector
    // (args[0] is always the real 1st argument regardless of arg
    // count), so that bug class cannot occur in the first place, not
    // something to reproduce.
    t.registerEfun("pcre_match", [pcreCompileOptions, pcreMatchOptions](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("pcre_match: expected a pattern string as the second argument");
        }
        const std::string& pattern = std::get<std::string>(args[1].data);

        if (std::holds_alternative<std::string>(args[0].data)) {
            if (args.size() > 3) {
                throw LpcRuntimeError("4th argument illegal for pcre_match(string, string)");
            }
            int64_t pcreFlags = 0;
            if (args.size() > 2) {
                if (!std::holds_alternative<int64_t>(args[2].data)) {
                    throw LpcRuntimeError("Bad argument 3 to pcre_match()");
                }
                pcreFlags = std::get<int64_t>(args[2].data);
            }
            const std::string& subject = std::get<std::string>(args[0].data);
            auto code = compileRegex(pattern, pcreCompileOptions(pcreFlags));
            size_t s = 0, e = 0;
            bool matched = regexFindNext(code.get(), subject, 0, s, e, pcreMatchOptions(pcreFlags));
            return Value(static_cast<int64_t>(matched ? 1 : 0));
        }

        if (!std::holds_alternative<std::shared_ptr<Array>>(args[0].data)) {
            throw LpcRuntimeError("pcre_match: expected a string or an array of strings as the first argument");
        }
        auto arr = std::get<std::shared_ptr<Array>>(args[0].data);
        int64_t flag = 0;
        if (args.size() > 2) {
            if (!std::holds_alternative<int64_t>(args[2].data)) {
                throw LpcRuntimeError("Bad argument 3 to pcre_match()");
            }
            flag = std::get<int64_t>(args[2].data);
        }
        int64_t pcreFlags = 0;
        if (args.size() > 3) {
            if (!std::holds_alternative<int64_t>(args[3].data)) {
                throw LpcRuntimeError("Bad argument 4 to pcre_match()");
            }
            pcreFlags = std::get<int64_t>(args[3].data);
        }
        bool invert = (flag & 2) != 0;
        bool withIndex = (flag & 1) != 0;

        auto code = compileRegex(pattern, pcreCompileOptions(pcreFlags));
        uint32_t matchOpts = pcreMatchOptions(pcreFlags);
        auto result = std::make_shared<Array>();
        if (arr) {
            for (size_t i = 0; i < arr->items.size(); ++i) {
                const Value& item = arr->items[i];
                if (!std::holds_alternative<std::string>(item.data)) continue;
                const std::string& line = std::get<std::string>(item.data);
                size_t s = 0, e = 0;
                bool matched = regexFindNext(code.get(), line, 0, s, e, matchOpts);
                if (matched == invert) continue;
                result->items.push_back(item);
                if (withIndex) result->items.push_back(Value(static_cast<int64_t>(i + 1)));
            }
        }
        return Value(result);
    });

    // mixed *pcre_assoc(string str, string *patterns, mixed *tokens,
    // mixed | void default, void | int pcre_flags) -- identical
    // selection/tokenizing contract to reg_assoc() above (pcre.cc's own
    // pcre_assoc() is a direct, acknowledged copy: "This is mostly
    // copy/paste from reg_assoc", confirmed directly), plus the same
    // pcre_flags bitmask applied uniformly to every pattern in the
    // array (real rgpp[i]->compile_flags = compute_compile_options(
    // pcre_flags) inside the same per-pattern compile loop for every
    // i, not a per-pattern flag).
    t.registerEfun("pcre_assoc", [pcreCompileOptions, pcreMatchOptions](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 3 ||
            !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::shared_ptr<Array>>(args[1].data) ||
            !std::holds_alternative<std::shared_ptr<Array>>(args[2].data)) {
            throw LpcRuntimeError("pcre_assoc: expected (string, string *patterns, mixed *tokens, mixed|void default, void|int pcre_flags)");
        }
        const std::string& str = std::get<std::string>(args[0].data);
        auto patternsArr = std::get<std::shared_ptr<Array>>(args[1].data);
        auto tokensArr = std::get<std::shared_ptr<Array>>(args[2].data);
        Value defaultVal = args.size() > 3 ? args[3] : Value(int64_t{0});
        int64_t pcreFlags = 0;
        if (args.size() > 4) {
            if (!std::holds_alternative<int64_t>(args[4].data)) {
                throw LpcRuntimeError("Bad argument 5 to pcre_assoc()");
            }
            pcreFlags = std::get<int64_t>(args[4].data);
        }
        uint32_t compileOpts = pcreCompileOptions(pcreFlags);
        uint32_t matchOpts = pcreMatchOptions(pcreFlags);

        const size_t size = patternsArr ? patternsArr->items.size() : 0;
        if (size != (tokensArr ? tokensArr->items.size() : 0)) {
            throw LpcRuntimeError("Pattern and token array sizes must be identical.");
        }

        auto textResult = std::make_shared<Array>();
        auto tokenResult = std::make_shared<Array>();

        if (size == 0) {
            textResult->items.push_back(Value(str));
            tokenResult->items.push_back(defaultVal);
        } else {
            std::vector<Pcre2CodePtr> codes;
            codes.reserve(size);
            for (size_t i = 0; i < size; ++i) {
                if (!std::holds_alternative<std::string>(patternsArr->items[i].data)) {
                    throw LpcRuntimeError("Non-string found in pattern array.");
                }
                codes.push_back(compileRegex(std::get<std::string>(patternsArr->items[i].data), compileOpts));
            }

            struct Match { size_t begin, end, tokIdx; };
            std::vector<Match> matches;
            size_t scan = 0;
            while (scan < str.size()) {
                size_t bestStart = std::string::npos, bestEnd = 0, bestIdx = 0;
                for (size_t i = 0; i < size; ++i) {
                    size_t s = 0, e = 0;
                    if (!regexFindNext(codes[i].get(), str, scan, s, e, matchOpts)) continue;
                    if (bestStart == std::string::npos || s < bestStart) {
                        bestStart = s; bestEnd = e; bestIdx = i;
                    }
                }
                if (bestStart == std::string::npos) break;
                matches.push_back({bestStart, bestEnd, bestIdx});
                scan = bestEnd;
                if (bestStart == bestEnd) {
                    if (scan >= str.size()) break;
                    ++scan;
                }
            }

            size_t cursor = 0;
            for (const auto& m : matches) {
                textResult->items.push_back(Value(str.substr(cursor, m.begin - cursor)));
                tokenResult->items.push_back(defaultVal);
                textResult->items.push_back(Value(str.substr(m.begin, m.end - m.begin)));
                tokenResult->items.push_back(tokensArr->items[m.tokIdx]);
                cursor = m.end;
            }
            textResult->items.push_back(Value(str.substr(cursor)));
            tokenResult->items.push_back(defaultVal);
        }

        auto outer = std::make_shared<Array>();
        outer->items.push_back(Value(textResult));
        outer->items.push_back(Value(tokenResult));
        return Value(outer);
    });

    // -------------------------------------------------------------------------
    // pcre.spec read-side efuns: pcre_version(), pcre_extract(),
    // pcre_match_all(). Row 2.12 built pcre_match()/pcre_assoc() from the
    // separately-vendored current-FluffOS tree (temp/fluffos/src/packages/
    // pcre/, a package the pinned 2.9 ds2.08 reference never had at all --
    // whole-tree "pcre" grep of temp/reference/fluffos-2.9-ds2.08 is still
    // empty) and named the other six pcre.spec names as out of scope that
    // slice. This slice takes the three that only read match data (no LPC
    // callback trampoline, no replacement-array plumbing): the same
    // continue-the-named-deferral pattern the matrix.spec slices (rows
    // 2.47-2.49) used. pcre_replace()/pcre_replace_callback()/pcre_cache()
    // stay deferred (replace has an unusual "one replacement string per
    // capture group, counts must match" contract plus non-overlapping
    // copy logic, the callback form calls back into LPC per match, and the
    // cache is an internal-structure introspection this driver organizes
    // differently).
    //
    // Signatures from temp/fluffos/src/packages/pcre/pcre.spec:
    //   string pcre_version(void);
    //   string *pcre_extract(string, string, void | int, void | int);
    //   mixed pcre_match_all(string, string, void | int);
    //
    // Semantics confirmed from pcre.cc's own f_pcre_version() /
    // f_pcre_extract() + pcre_get_substrings() / f_pcre_match_all() +
    // pcre_match_all():
    //
    //   - pcre_version(): real pushes the linked engine's version string
    //     (real: pcre_version(), PCRE1). This driver links PCRE2, so it
    //     returns PCRE2's version via pcre2_config(PCRE2_CONFIG_VERSION),
    //     a string like "10.42 2022-12-11". A named engine substitution,
    //     the same kind row 2.12 already made wrapping PCRE2 in place of
    //     the real 2.9 Henry Spencer engine, and row 2.16's hash() made
    //     using OpenSSL EVP -- not a silent divergence.
    //
    //   - pcre_extract(subject, pattern, [include_names], [pcre_flags]):
    //     match pattern against subject once. No match -> empty array
    //     (real: the_null_array). On a match, return an array of the
    //     captured substrings for groups 1..N where N = rc - 1 and rc is
    //     pcre2_match()'s return (one more than the highest-numbered group
    //     that was set); group 0 (the whole match) is NOT included
    //     (pcre_get_substrings() fills ret->item[i-1] from i = 1). A group
    //     that did not participate yields "" (real reads ovector[-1]
    //     slots as a zero-length span). If include_names (the optional 3rd
    //     argument, any nonzero int) is set, a mapping of {named-group
    //     name: that group's captured value} is appended as the last array
    //     element, covering only named groups whose number is in 1..N and
    //     which participated (pcre_get_substrings()'s own name-table
    //     walk). The optional 4th argument is a pcre_flags bitmask, same
    //     PCRE_I/M/S/U/X/A set as pcre_match()/pcre_assoc() above.
    //
    //   - pcre_match_all(subject, pattern, [pcre_flags]): return an array
    //     with one element per non-overlapping match, each element itself
    //     an array [whole-match, group1, ..., group_{rc-1}] (rc elements,
    //     group 0 first this time). Iterates with the standard
    //     empty-match idiom (pcre.cc's own pcre_match_all()): after a
    //     zero-length match, retry at the same offset with
    //     NOTEMPTY_ATSTART | ANCHORED, and if that fails advance one UTF-8
    //     character; otherwise resume at ovector[1].
    //
    // Corpus call-site frequency, checked before implementing: grepped
    // every vendored corpus under temp/ (core-lib, dead-souls, es2_mudlib,
    // lima, nightmare3, reference-lpc-mud-library, wiz_tools, lil) plus the
    // bundled mudlib/ for pcre_version( / pcre_extract( / pcre_match_all(:
    // zero real LPC call sites (row 2.12 already recorded the same for
    // these three). Motivation is FluffOS-surface parity, the same
    // honestly-named basis as rows 2.12/2.16/2.24/2.46/2.51/2.52; regex
    // capture extraction is independently verifiable against hand-written
    // patterns with no live-instance dependency.
    // -------------------------------------------------------------------------

    // Runs one pcre2_match at byteOffset. On a match, fills `groups` with
    // the substring for capture groups 0..(rc-1) (group 0 is the whole
    // match; a non-participating group yields ""), sets mStart/mEnd to the
    // whole-match byte span, and returns the pair count rc (>= 1). Returns
    // 0 on PCRE2_ERROR_NOMATCH; any other PCRE2 error throws.
    auto pcreMatchGroups = [](pcre2_code* code, const std::string& subject,
                              size_t byteOffset, uint32_t matchOpts,
                              std::vector<std::string>& groups,
                              size_t& mStart, size_t& mEnd) -> int {
        groups.clear();
        if (byteOffset > subject.size()) return 0;
        pcre2_match_data* md = pcre2_match_data_create_from_pattern(code, nullptr);
        int rc = pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(subject.data()),
                             static_cast<PCRE2_SIZE>(subject.size()),
                             static_cast<PCRE2_SIZE>(byteOffset), matchOpts, md, nullptr);
        if (rc < 0) {
            pcre2_match_data_free(md);
            if (rc == PCRE2_ERROR_NOMATCH) return 0;
            throw LpcRuntimeError("pcre: match error");
        }
        // rc == 0 means the ovector was too small; impossible with match
        // data sized from the pattern, but fall back to its real count.
        uint32_t pairs = (rc == 0) ? pcre2_get_ovector_count(md)
                                   : static_cast<uint32_t>(rc);
        PCRE2_SIZE* ov = pcre2_get_ovector_pointer(md);
        mStart = static_cast<size_t>(ov[0]);
        mEnd = static_cast<size_t>(ov[1]);
        for (uint32_t i = 0; i < pairs; ++i) {
            PCRE2_SIZE s = ov[2 * i], e = ov[2 * i + 1];
            if (s == PCRE2_UNSET || e == PCRE2_UNSET || e < s) {
                groups.emplace_back();
            } else {
                groups.emplace_back(subject.substr(s, e - s));
            }
        }
        pcre2_match_data_free(md);
        return static_cast<int>(pairs);
    };

    t.registerEfun("pcre_version", [](VM&, std::vector<Value>&) -> Value {
        char buf[64] = {0};
        int n = pcre2_config(PCRE2_CONFIG_VERSION, buf);
        if (n <= 0) return Value(std::string("unknown"));
        return Value(std::string(buf));
    });

    t.registerEfun("pcre_extract", [pcreCompileOptions, pcreMatchOptions, pcreMatchGroups](
            VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 ||
            !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("pcre_extract: expected (string subject, string pattern, void|int include_names, void|int pcre_flags)");
        }
        const std::string& subject = std::get<std::string>(args[0].data);
        const std::string& pattern = std::get<std::string>(args[1].data);

        bool includeNames = false;
        if (args.size() > 2) {
            if (!std::holds_alternative<int64_t>(args[2].data)) {
                throw LpcRuntimeError("Bad argument 3 to pcre_extract()");
            }
            includeNames = std::get<int64_t>(args[2].data) != 0;
        }
        int64_t pcreFlags = 0;
        if (args.size() > 3) {
            if (!std::holds_alternative<int64_t>(args[3].data)) {
                throw LpcRuntimeError("Bad argument 4 to pcre_extract()");
            }
            pcreFlags = std::get<int64_t>(args[3].data);
        }

        auto code = compileRegex(pattern, pcreCompileOptions(pcreFlags));
        std::vector<std::string> groups;
        size_t ms = 0, me = 0;
        int rc = pcreMatchGroups(code.get(), subject, 0, pcreMatchOptions(pcreFlags),
                                 groups, ms, me);

        auto result = std::make_shared<Array>();
        if (rc <= 0) return Value(result);  // no match -> empty array

        // Groups 1..(rc-1); group 0 (the whole match) is not included.
        for (size_t i = 1; i < groups.size(); ++i) {
            result->items.push_back(Value(groups[i]));
        }

        if (includeNames && !result->items.empty()) {
            auto names = std::make_shared<Mapping>();
            uint32_t nameCount = 0, nameEntrySize = 0;
            PCRE2_SPTR nameTable = nullptr;
            pcre2_pattern_info(code.get(), PCRE2_INFO_NAMECOUNT, &nameCount);
            pcre2_pattern_info(code.get(), PCRE2_INFO_NAMEENTRYSIZE, &nameEntrySize);
            pcre2_pattern_info(code.get(), PCRE2_INFO_NAMETABLE, &nameTable);
            for (uint32_t k = 0; k < nameCount; ++k) {
                PCRE2_SPTR entry = nameTable + k * nameEntrySize;
                int groupNum = (entry[0] << 8) | entry[1];
                if (groupNum <= 0 || static_cast<size_t>(groupNum) >= groups.size()) {
                    continue;  // whole match, or a group past what rc reported
                }
                // Real pcre_get_substrings() skips a named group that did
                // not participate (ovector slot < 0). This driver's groups
                // vector collapses "did not participate" and "matched
                // empty" both to "", so a non-participating named group is
                // mapped to "" here rather than omitted -- a narrow,
                // named fidelity gap that only shows for optional named
                // groups, never for a plain (?<name>...) that matched.
                std::string name(reinterpret_cast<const char*>(entry + 2));
                names->entries.emplace_back(Value(name),
                                            Value(groups[static_cast<size_t>(groupNum)]));
            }
            result->items.push_back(Value(names));
        }
        return Value(result);
    });

    t.registerEfun("pcre_match_all", [pcreCompileOptions, pcreMatchOptions, pcreMatchGroups](
            VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 ||
            !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("pcre_match_all: expected (string subject, string pattern, void|int pcre_flags)");
        }
        const std::string& subject = std::get<std::string>(args[0].data);
        const std::string& pattern = std::get<std::string>(args[1].data);
        int64_t pcreFlags = 0;
        if (args.size() > 2) {
            if (!std::holds_alternative<int64_t>(args[2].data)) {
                throw LpcRuntimeError("Bad argument 3 to pcre_match_all()");
            }
            pcreFlags = std::get<int64_t>(args[2].data);
        }

        auto code = compileRegex(pattern, pcreCompileOptions(pcreFlags));
        uint32_t baseOpts = pcreMatchOptions(pcreFlags);

        auto outer = std::make_shared<Array>();
        size_t offset = 0;
        uint32_t retryOpts = 0;
        // Real pcre_match_all()'s own loop guard is "offset < s_length"
        // (strictly), so a trailing zero-length match at end-of-string is
        // not reported and an empty subject yields an empty array.
        while (offset < subject.size()) {
            std::vector<std::string> groups;
            size_t ms = 0, me = 0;
            int rc = pcreMatchGroups(code.get(), subject, offset, baseOpts | retryOpts,
                                     groups, ms, me);
            if (rc <= 0) {
                if (retryOpts == 0) break;
                // Previous match was empty and nothing non-empty matches
                // anchored here: step over one UTF-8 character and rescan.
                ++offset;
                while (offset < subject.size() &&
                       (static_cast<unsigned char>(subject[offset]) & 0xC0) == 0x80) {
                    ++offset;
                }
                retryOpts = 0;
                continue;
            }
            auto match = std::make_shared<Array>();
            for (const auto& g : groups) match->items.push_back(Value(g));
            outer->items.push_back(Value(match));

            if (me == ms) {
                retryOpts = PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED;
                offset = me;  // unchanged; the retry runs at this same spot
            } else {
                retryOpts = 0;
                offset = me;
            }
        }
        return Value(outer);
    });

    // string pcre_replace(string subject, string pattern, string
    // *replacements, void | int pcre_flags) -- packages/pcre/pcre.spec.
    // The remaining self-contained pcre.spec name after row 2.53's
    // read-side trio (pcre_replace_callback() still deferred: it calls
    // back into LPC once per match; pcre_cache() still deferred:
    // internal-structure introspection).
    //
    // This is NOT an ordinary regex substitution. Semantics from pcre.cc's
    // own f_pcre_replace() + pcre_get_replace(): match the pattern against
    // the subject ONCE, then rebuild the subject with each SELECTED
    // capture group replaced by the correspondingly-indexed element of the
    // replacements array (group i by replacements[i-1]). A group is
    // "selected" only when it starts at or after the end of the last
    // selected group, so a nested or overlapping inner group is left
    // alone; a non-participating group is likewise skipped (real reads its
    // ovector slot as -1, which is < prev). Text between selected groups,
    // and the prefix/suffix around them, is copied through verbatim.
    //
    // Real edge cases, all reproduced:
    //   - No match: return the subject unchanged (real f_pcre_replace()
    //     pop_2_elems() and returns, leaving the subject on the stack).
    //   - rc == 1 (pattern has no capture groups): return the subject
    //     unchanged (real's own "if (run->rc == 1)" early return).
    //   - (rc - 1) != sizeof(replacements): error "Number of captured
    //     substrings and replacements do not match, %d vs %d.".
    //   - A non-string element in replacements: error "Bad argument 3 to
    //     pcre_replace(): replacement array must contain only strings.".
    //   - A non-array 3rd argument: error "Bad argument 3 to
    //     pcre_replace()".
    // The optional 4th argument is the same pcre_flags bitmask
    // (PCRE_I/M/S/U/X/A) as pcre_match()/pcre_extract() above.
    //
    // One narrow named divergence from real: real f_pcre_replace()
    // initializes its running gate from ovector[2] (group 1's start)
    // directly, so a pattern whose very first group is optional and did
    // not participate makes real copy a bogus (size_t)(-1)-derived prefix
    // length (then clamped). This driver instead treats a non-
    // participating group-1 start as 0 for the prefix and gate, so the
    // pathological input produces a sane result rather than a clamped
    // one. Every case where group 1 actually participated is identical.
    //
    // Corpus call-site frequency, checked before implementing: grepped
    // every vendored corpus under temp/ (core-lib, dead-souls, es2_mudlib,
    // lima, nightmare3, reference-lpc-mud-library, wiz_tools, lil) plus the
    // bundled mudlib/ for pcre_replace(: zero real LPC call sites (rows
    // 2.12 and 2.53 recorded the same). Motivation is FluffOS-surface
    // parity, the same honestly-named basis as rows 2.12/2.16/2.51/2.53;
    // the group-selection rule is independently verifiable against
    // hand-written patterns with no live-instance dependency.
    t.registerEfun("pcre_replace", [pcreCompileOptions, pcreMatchOptions](
            VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 3 ||
            !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("pcre_replace: expected (string subject, string pattern, string *replacements, void|int pcre_flags)");
        }
        if (!std::holds_alternative<std::shared_ptr<Array>>(args[2].data)) {
            throw LpcRuntimeError("Bad argument 3 to pcre_replace()");
        }
        const std::string& subject = std::get<std::string>(args[0].data);
        const std::string& pattern = std::get<std::string>(args[1].data);
        auto replacements = std::get<std::shared_ptr<Array>>(args[2].data);
        int64_t pcreFlags = 0;
        if (args.size() > 3) {
            if (!std::holds_alternative<int64_t>(args[3].data)) {
                throw LpcRuntimeError("Bad argument 4 to pcre_replace()");
            }
            pcreFlags = std::get<int64_t>(args[3].data);
        }

        auto code = compileRegex(pattern, pcreCompileOptions(pcreFlags));
        pcre2_match_data* md = pcre2_match_data_create_from_pattern(code.get(), nullptr);
        int rc = pcre2_match(code.get(), reinterpret_cast<PCRE2_SPTR>(subject.data()),
                             static_cast<PCRE2_SIZE>(subject.size()), 0,
                             pcreMatchOptions(pcreFlags), md, nullptr);
        if (rc < 0) {
            pcre2_match_data_free(md);
            if (rc == PCRE2_ERROR_NOMATCH) return Value(subject);  // no match -> unchanged
            throw LpcRuntimeError("pcre_replace: match error");
        }
        uint32_t pairs = (rc == 0) ? pcre2_get_ovector_count(md)
                                   : static_cast<uint32_t>(rc);
        PCRE2_SIZE* ov = pcre2_get_ovector_pointer(md);

        if (pairs <= 1) {  // pattern captured nothing -> subject unchanged
            pcre2_match_data_free(md);
            return Value(subject);
        }
        const size_t groupCount = pairs - 1;
        const size_t repCount = replacements ? replacements->items.size() : 0;
        if (groupCount != repCount) {
            pcre2_match_data_free(md);
            throw LpcRuntimeError(
                "Number of captured substrings and replacements do not match, " +
                std::to_string(groupCount) + " vs " + std::to_string(repCount) + ".");
        }
        for (const auto& el : replacements->items) {
            if (!std::holds_alternative<std::string>(el.data)) {
                pcre2_match_data_free(md);
                throw LpcRuntimeError(
                    "Bad argument 3 to pcre_replace(): replacement array must contain only strings.");
            }
        }

        // Rebuild the subject, substituting each selected group.
        size_t firstStart = (ov[2] == PCRE2_UNSET) ? 0 : static_cast<size_t>(ov[2]);
        std::string out = subject.substr(0, firstStart);
        size_t gate = firstStart;      // next selected group must start >= this
        size_t lastEnd = firstStart;   // end of the last selected group
        for (size_t i = 1; i <= groupCount; ++i) {
            PCRE2_SIZE gs = ov[2 * i], ge = ov[2 * i + 1];
            if (gs == PCRE2_UNSET || ge == PCRE2_UNSET) continue;  // did not participate
            size_t gstart = static_cast<size_t>(gs), gend = static_cast<size_t>(ge);
            if (gstart < gate) continue;  // nested / overlapping
            if (gstart > lastEnd) out += subject.substr(lastEnd, gstart - lastEnd);
            out += std::get<std::string>(replacements->items[i - 1].data);
            lastEnd = gend;
            gate = gend;
        }
        if (lastEnd < subject.size()) out += subject.substr(lastEnd);

        pcre2_match_data_free(md);
        return Value(out);
    });

    // -------------------------------------------------------------------------
    // Phase 0.13 efun growth batch (post-restructure) - test_bit/set_bit/
    // clear_bit, crc32, cp, inherits, get_config, query_load_average, say,
    // save_variable/restore_variable. Found this round by diffing this
    // repo's own bundled Lil starter mudlib's real efun conformance suite
    // (mudlib/single/tests/efuns/*.c -- each filename names the one real
    // efun that file exercises) against EfunTable's currently registered
    // names, then checked each surviving name against func_spec.c/
    // efuns_main.c directly before implementing -- the extraction that
    // produced this repo dropped the old nightmare3_fluffos_v2 mudlib this
    // row's prior batches ranked against (see this row's own
    // instruct.md), so this is the first batch ranked against Lil's own
    // content instead.
    // -------------------------------------------------------------------------

    // string set_bit(string str, int bit) / string clear_bit(string str,
    // int bit) / int test_bit(string str, int bit) -- real efuns_main.c's
    // own f_set_bit()/f_clear_bit()/f_test_bit(), confirmed directly: each
    // packed character holds 6 bits (not 8), addressed as "ind = bit / 6;
    // bit %= 6", and the on-disk byte range is always ' ' (0x20) through
    // '_' (0x20 + 0x3f, 0x5f) -- a byte outside that range is flagged as
    // corrupt ("Illegal bit pattern") rather than silently masked. This
    // driver has no MAX_BITS config value of its own (real rc.c's own
    // runtime-configured __MAX_BITFIELD_BITS__); a fixed, generous
    // ceiling is used purely to reject clearly-bogus huge indices the way
    // every real caller expects some bound to exist, not to replicate any
    // specific real-world configured default.
    constexpr int64_t kMaxBits = 1000000;

    t.registerEfun("set_bit", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<int64_t>(args[1].data)) {
            throw LpcRuntimeError("set_bit: expected (string, int) arguments");
        }
        std::string str = std::get<std::string>(args[0].data);
        int64_t bitNum = std::get<int64_t>(args[1].data);
        if (bitNum > kMaxBits) {
            throw LpcRuntimeError("set_bit() bit requested exceeds maximum bits");
        }
        if (bitNum < 0) {
            throw LpcRuntimeError("Bad argument 2 (negative) to set_bit().");
        }
        size_t ind = static_cast<size_t>(bitNum / 6);
        int bit = static_cast<int>(bitNum % 6);
        if (ind >= str.size()) str.resize(ind + 1, ' ');
        unsigned char ch = static_cast<unsigned char>(str[ind]);
        if (ch > 0x3f + ' ' || ch < ' ') {
            throw LpcRuntimeError("Illegal bit pattern in set_bit character " + std::to_string(ind));
        }
        str[ind] = static_cast<char>(((ch - ' ') | (1 << bit)) + ' ');
        return Value(str);
    });

    t.registerEfun("clear_bit", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<int64_t>(args[1].data)) {
            throw LpcRuntimeError("clear_bit: expected (string, int) arguments");
        }
        std::string str = std::get<std::string>(args[0].data);
        int64_t bitNum = std::get<int64_t>(args[1].data);
        if (bitNum > kMaxBits) {
            throw LpcRuntimeError("clear_bit() bit requested exceeds maximum bits");
        }
        if (bitNum < 0) {
            throw LpcRuntimeError("Bad argument 2 (negative) to clear_bit().");
        }
        size_t ind = static_cast<size_t>(bitNum / 6);
        int bit = static_cast<int>(bitNum % 6);
        // Real f_clear_bit(): an index past the current length returns
        // the first argument completely unmodified, not an error and not
        // a resize -- clearing a bit that was never set is a no-op.
        if (ind >= str.size()) return Value(str);
        unsigned char ch = static_cast<unsigned char>(str[ind]);
        if (ch > 0x3f + ' ' || ch < ' ') {
            throw LpcRuntimeError("Illegal bit pattern in clear_bit character " + std::to_string(ind));
        }
        str[ind] = static_cast<char>(((ch - ' ') & ~(1 << bit)) + ' ');
        return Value(str);
    });

    t.registerEfun("test_bit", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<int64_t>(args[1].data)) {
            throw LpcRuntimeError("test_bit: expected (string, int) arguments");
        }
        const std::string& str = std::get<std::string>(args[0].data);
        int64_t bitNum = std::get<int64_t>(args[1].data);
        // Real f_test_bit(): the out-of-range-length check runs *before*
        // the negative check, confirmed by reading it directly -- a
        // negative index past a short/empty string returns 0, not an
        // error, matching that real ordering exactly.
        if (bitNum / 6 >= static_cast<int64_t>(str.size())) return Value(static_cast<int64_t>(0));
        if (bitNum < 0) {
            throw LpcRuntimeError("Bad argument 2 (negative) to test_bit().");
        }
        unsigned char ch = static_cast<unsigned char>(str[static_cast<size_t>(bitNum / 6)]);
        bool isSet = ((ch - ' ') & (1 << (bitNum % 6))) != 0;
        return Value(static_cast<int64_t>(isSet ? 1 : 0));
    });

    // int next_bit(string str, int start) -- real efuns_main.c's own
    // f_next_bit(), same 6-bit-per-character encoding as set_bit/
    // clear_bit/test_bit above: returns the index of the next *set* bit,
    // or -1 if none remain. Confirmed directly, not assumed from the
    // other three's own shape: the real scan is asymmetric at the
    // boundary -- for start <= 0 it scans from bit 0 *inclusive* (so
    // next_bit(str, 0) can legitimately return 0 if bit 0 is set), but
    // for start > 0 it scans strictly *after* start (real "bit = 0x3f -
    // ((1 << ((start % 6) + 1)) - 1)" masks out bit `start` itself and
    // everything before it in that same character). No real call site in
    // this mudlib -- implemented anyway as the natural fourth member of
    // the real set_bit/clear_bit/test_bit/next_bit family func_spec.c
    // defines together.
    t.registerEfun("next_bit", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<int64_t>(args[1].data)) {
            throw LpcRuntimeError("next_bit: expected (string, int) arguments");
        }
        const std::string& str = std::get<std::string>(args[0].data);
        int64_t start = std::get<int64_t>(args[1].data);
        int64_t len = static_cast<int64_t>(str.size());
        if (len == 0 || start / 6 >= len) return Value(static_cast<int64_t>(-1));
        int64_t begin = (start > 0) ? (start + 1) : 0;
        for (int64_t bit = begin; bit < len * 6; ++bit) {
            unsigned char ch = static_cast<unsigned char>(str[static_cast<size_t>(bit / 6)]);
            if ((ch - ' ') & (1 << (bit % 6))) return Value(bit);
        }
        return Value(static_cast<int64_t>(-1));
    });

    // int crc32(string | buffer) -- real crc32.c's own compute_crc32():
    // standard reflected CRC-32 (polynomial 0xedb88320, confirmed
    // directly against crctab.h's own generated table), seeded to
    // 0xFFFFFFFF, but with NO final complement step -- confirmed directly
    // by reading compute_crc32() itself ("return crc;", no "^ 0xFFFFFFFF"
    // anywhere), a real, deliberate divergence from the textbook/zlib
    // CRC-32 this implementation would otherwise match exactly.
    // crc32("") therefore legitimately equals the raw seed, 0xFFFFFFFF
    // (4294967295), not 0 -- confirmed as a regression test rather than
    // assumed. Real f_crc32() (efuns_main.c) also accepts a buffer
    // ("func_spec.c: int crc32(string OR_BUFFER)"), running the identical
    // CRC over the buffer's raw bytes -- wired here as part of row 2.33a.
    t.registerEfun("crc32", [](VM&, std::vector<Value>& args) -> Value {
        const unsigned char* data = nullptr;
        size_t len = 0;
        std::string strHolder;
        if (!args.empty() && std::holds_alternative<std::string>(args[0].data)) {
            strHolder = std::get<std::string>(args[0].data);
            data = reinterpret_cast<const unsigned char*>(strHolder.data());
            len = strHolder.size();
        } else if (!args.empty()) {
            if (auto* buf = std::get_if<std::shared_ptr<Buffer>>(&args[0].data); buf && *buf) {
                data = (*buf)->bytes.data();
                len = (*buf)->bytes.size();
            } else {
                throw LpcRuntimeError("crc32: expected a string or buffer argument");
            }
        } else {
            throw LpcRuntimeError("crc32: expected a string or buffer argument");
        }
        static uint32_t table[256];
        static bool initialized = false;
        if (!initialized) {
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k) {
                    c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                }
                table[i] = c;
            }
            initialized = true;
        }
        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < len; ++i) {
            crc = table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
        }
        return Value(static_cast<int64_t>(crc));
    });

    // int cp(string from, string to) -- real file.c's own copy_file():
    // reads the whole source file and writes it to the destination,
    // truncating any existing destination content, returning a real,
    // nonzero success indicator. Real copy_file() also returns distinct
    // negative codes for distinct failure kinds and retries into a
    // synthesized filename when "to" names an existing directory -- not
    // implemented here, since every real call site this row's own
    // content exercises only checks truthiness and never copies onto a
    // directory target.
    t.registerEfun("cp", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("cp: expected (string from, string to) arguments");
        }
        std::string from = vm.resolveMudlibPath(std::get<std::string>(args[0].data));
        std::string to = vm.resolveMudlibPath(std::get<std::string>(args[1].data));
        std::ifstream in(from, std::ios::binary);
        if (!in) return Value(static_cast<int64_t>(0));
        std::ofstream out(to, std::ios::binary | std::ios::trunc);
        if (!out) return Value(static_cast<int64_t>(0));
        out << in.rdbuf();
        if (!out) return Value(static_cast<int64_t>(0));
        return Value(static_cast<int64_t>(1));
    });

    // int inherits(string filename, object base default: this_object())
    // -- real efuns_main.c's own f_inherits(): finds/loads the object
    // named by filename, then checks whether base's own program
    // transitively inherits that object's program (confirmed directly,
    // not the "compare two path strings" shape func_spec.c's bare
    // signature might suggest -- real F_INHERITS calls find_object2() on
    // the string argument and does a program-identity walk, base->prog
    // against ob->prog). This driver has no find_object2()-style
    // find-or-load-and-fail-cleanly primitive to reuse here, so this is
    // narrower than that real load-and-compare shape: filename is only
    // ever compared, normalized the same way deep_inherit_list()'s own
    // normalizeInheritPath() already does, against base's own filename
    // and base's own deep inherit chain -- never actually loaded/compiled
    // as a side effect of this boolean query. Confirmed sufficient for
    // this row's own real, tested call sites: a nonexistent filename like
    // "foo" simply never matches either check and correctly returns 0,
    // the same observable result real f_inherits()'s own
    // find_object2()-fails branch produces.
    t.registerEfun("inherits", [resolveInheritTarget, normalizeInheritPath, collectDeepInherits](
                                    VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("inherits: expected a string filename as the first argument");
        }
        std::string candidate = normalizeInheritPath(std::get<std::string>(args[0].data));

        std::vector<Value> baseArgs;
        if (args.size() > 1) baseArgs.push_back(args[1]);
        auto base = resolveInheritTarget(vm, baseArgs);
        if (!base) return Value(static_cast<int64_t>(0));

        if (normalizeInheritPath(base->filename()) == candidate) return Value(static_cast<int64_t>(1));

        std::vector<Value> chain;
        collectDeepInherits(base->program(), collectDeepInherits, chain);
        for (auto& v : chain) {
            if (auto* s = std::get_if<std::string>(&v.data); s && *s == candidate) {
                return Value(static_cast<int64_t>(1));
            }
        }
        return Value(static_cast<int64_t>(0));
    });

    // mixed *functions(object ob default: this_object(), int flag default: 0)
    // -- real packages/contrib_spec.c's own declared signature
    // ("mixed *functions(object, int default: 0);"), body in
    // packages/contrib.c's own f_functions(). flag&1 selects detailed
    // per-function sub-arrays ([name, num_args, return_type,
    // ...arg_types]) over a bare name list; flag&2 restricts the result
    // to ob's own directly-defined functions, omitting everything only
    // reached through inheritance. Real per-argument/return TYPE_*
    // strings come from declared-type metadata this driver's own
    // CompiledProgram never tracks (FunctionEntry only carries
    // name/entryPoint/numArgs/numLocals, no declared-type info at all,
    // confirmed directly against Bytecode.hpp) -- every type slot here
    // is the fixed placeholder "mixed" instead of a real per-declaration
    // type name, the closest honest equivalent to an untyped/mixed
    // declaration, documented rather than silently fabricated. Real
    // f_functions() also hides its own synthesized __INIT-family
    // initializer function from the result (checking
    // APPLY___INIT_SPECIAL_CHAR); this driver's own equivalent is the
    // synthesized "$objvarinit" CodeGen.cpp adds to every program's own
    // function list (see its own comment there), excluded here the same
    // way. The flag&2-clear (include-inherited) case walks
    // inheritedPrograms depth-first, own functions first then each
    // parent in declaration order, first name seen wins -- the same
    // override precedence findFunctionInChain() (this file's own
    // OpCode::Call resolver, VM.cpp) already uses for an ordinary
    // function call, so a function this program itself overrides never
    // shows up twice or under its shadowed parent's own entry.
    t.registerEfun("functions", [resolveInheritTarget](VM& vm, std::vector<Value>& args) -> Value {
        auto ob = resolveInheritTarget(vm, args);
        if (!ob) return Value(std::make_shared<Array>());
        int64_t flag = 0;
        if (args.size() > 1 && std::holds_alternative<int64_t>(args[1].data)) {
            flag = std::get<int64_t>(args[1].data);
        }

        std::vector<const FunctionEntry*> entries;
        if (flag & 2) {
            for (const auto& fn : ob->program().functions) {
                if (fn.name == "$objvarinit") continue;
                entries.push_back(&fn);
            }
        } else {
            std::unordered_set<std::string> seen;
            std::function<void(const CompiledProgram&)> collect =
                [&](const CompiledProgram& prog) {
                    for (const auto& fn : prog.functions) {
                        if (fn.name == "$objvarinit") continue;
                        if (!seen.insert(fn.name).second) continue;
                        entries.push_back(&fn);
                    }
                    for (const auto& parent : prog.inheritedPrograms) {
                        if (parent) collect(*parent);
                    }
                };
            collect(ob->program());
        }

        auto result = std::make_shared<Array>();
        for (const auto* fn : entries) {
            if (flag & 1) {
                auto sub = std::make_shared<Array>();
                sub->items.push_back(Value(fn->name));
                sub->items.push_back(Value(static_cast<int64_t>(fn->numArgs)));
                sub->items.push_back(Value(std::string("mixed")));
                for (int i = 0; i < fn->numArgs; ++i) {
                    sub->items.push_back(Value(std::string("mixed")));
                }
                result->items.push_back(Value(sub));
            } else {
                result->items.push_back(Value(fn->name));
            }
        }
        return Value(result);
    });

    // mixed *variables(object ob default: this_object(), int flag default: 0)
    // -- real packages/contrib_spec.c's own declared signature, body in
    // packages/contrib.c's own f_variables(): recurses ob's own inherit
    // tree first (in inherit-declaration order), then ob's own
    // directly-declared variables last -- the exact same flattened
    // order this driver's own CompiledProgram::objectVarNames already
    // carries end to end (see save_object()/restore_object()'s own
    // established use of that same field, above). flag truthy selects
    // [name, type] pairs over a bare name list; no declared-type
    // metadata exists here either (see functions()'s own comment just
    // above), so type is always the fixed placeholder "mixed".
    t.registerEfun("variables", [resolveInheritTarget](VM& vm, std::vector<Value>& args) -> Value {
        auto ob = resolveInheritTarget(vm, args);
        if (!ob) return Value(std::make_shared<Array>());
        int64_t flag = 0;
        if (args.size() > 1 && std::holds_alternative<int64_t>(args[1].data)) {
            flag = std::get<int64_t>(args[1].data);
        }
        const auto& names = ob->program().objectVarNames;
        auto result = std::make_shared<Array>();
        for (const auto& name : names) {
            if (flag) {
                auto sub = std::make_shared<Array>();
                sub->items.push_back(Value(name));
                sub->items.push_back(Value(std::string("mixed")));
                result->items.push_back(Value(sub));
            } else {
                result->items.push_back(Value(name));
            }
        }
        return Value(result);
    });

    // mixed fetch_variable(string name) -- real packages/contrib_spec.c's
    // own declared signature, body in packages/contrib.c's own
    // f_fetch_variable(): looks up name in current_object's own
    // flattened variable table (real find_global_variable(), this
    // driver's own CompiledProgram::objectVarNames, the same table
    // variables()/save_object()/restore_object() above already use),
    // returning its current value. Real "No variable named '%s'!\n"
    // error text kept, current_object always implicit -- there is no
    // object argument in the real signature, unlike functions()/
    // variables() above.
    t.registerEfun("fetch_variable", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("fetch_variable: expected a string variable name");
        }
        auto ob = vm.currentObject();
        if (!ob) throw LpcRuntimeError("fetch_variable: no current object");
        const auto& names = ob->program().objectVarNames;
        const auto& name = std::get<std::string>(args[0].data);
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                return i < ob->variables().size() ? ob->variables()[i] : Value{};
            }
        }
        throw LpcRuntimeError("No variable named '" + name + "'!");
    });

    // void store_variable(string name, mixed value) -- real
    // f_store_variable(), the write-side counterpart directly above,
    // same current_object-implicit/name-lookup/error-text semantics.
    t.registerEfun("store_variable", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("store_variable: expected (string name, mixed value) arguments");
        }
        auto ob = vm.currentObject();
        if (!ob) throw LpcRuntimeError("store_variable: no current object");
        const auto& names = ob->program().objectVarNames;
        const auto& name = std::get<std::string>(args[0].data);
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                if (i < ob->variables().size()) ob->variables()[i] = args[1];
                return Value{};
            }
        }
        throw LpcRuntimeError("No variable named '" + name + "'!");
    });

    // mixed get_config(int what) -- real rc.c's own get_config_item():
    // dispatches on a large table of compile-time/runtime configuration
    // indices (~50 string and int entries in a real build) this driver
    // has no equivalent registry for. Only index 0 (real __MUD_NAME__,
    // the first string-kind entry -- confirmed via config.h's own
    // "#define MUD_NAME CONFIG_STR(__MUD_NAME__)" plus get_config_item()'s
    // own "num < BASE_CONFIG_INT" string branch, base index 0) is
    // implemented, matching this row's own real, tested call site
    // (get_config(0) == MUD_NAME).
    //
    // Index 23 (real __MAX_EVAL_COST__, Dead Souls 3.8.2's own bundled
    // fluffos-2.23-ds03/secure/include/runtime_config.h: "#define
    // __MAX_EVAL_COST__ CFG_INT(8)", CFG_INT(x) = x + BASE_CONFIG_INT,
    // BASE_CONFIG_INT = BASE_CONFIG_STR(0) + 15 = 15, so 8 + 15 = 23)
    // added the same way: real rc.c's own get_config_item() reads this
    // straight out of config_int[], itself loaded directly from the
    // real config file's own "maximum evaluation cost : %d" line
    // (rc.c's own real scan_config_line() call) -- the exact same real
    // setting this driver's own Config::maxEvalCost() already tracks
    // (etc/driver_*.cfg's own "max_eval_cost:" key), not a fabricated
    // statistic this driver has no source for at all, unlike
    // mud_status()/cache_stats()'s own genuine architecture mismatch.
    // Found live against a real third-party mudlib corpus (Dead Souls
    // 3.8.2's own boot attempt): secure/daemon/master.c's own real
    // create() does "eval_threshold = (get_config(__MAX_EVAL_COST__) /
    // 1000000) + 1;", the required master object failing to load
    // without it.
    //
    // Index 29 (real __MAX_STRING_LENGTH__, this same bundled
    // runtime_config.h's own "#define __MAX_STRING_LENGTH__
    // CFG_INT(14)", so 14 + 15 = 29) added the same way: real rc.c's
    // own get_config_item() reads this from config_int[], loaded from
    // the real config file's own "maximum string length" line, the
    // exact same real value this driver's own Config::maxStringLength()
    // now tracks. Found live continuing the same Dead Souls 3.8.2 boot
    // attempt: secure/sefun/sefun.c's own real read_file() sefun
    // wrapper does "if(sz < 1 || sz >= get_config(__MAX_STRING_LENGTH__))
    // return \"\";", called from this same file's own $objvarinit (a
    // top-level "= read_file(...)" object-variable initializer), so
    // this blocked the simul_efun object from loading at all, before
    // even reaching master.c.
    //
    // Every other index -- including any negative index,
    // real get_config_item()'s own explicit "num < 0" failure branch --
    // still throws a clear error rather than fabricating a value this
    // codebase has no real source for, same as before.
    t.registerEfun("get_config", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("get_config: expected an int argument");
        }
        int64_t what = std::get<int64_t>(args[0].data);
        if (what == 0) return Value(vm.mudName());
        if (what == 23) return Value(static_cast<int64_t>(vm.config().maxEvalCost()));
        if (what == 29) return Value(static_cast<int64_t>(vm.config().maxStringLength()));
        throw LpcRuntimeError("get_config: unsupported or invalid config index");
    });

    // string query_load_average() -- real backend.c's own
    // query_load_av(): "sprintf(buff, \"%.2f cmds/s, %.2f comp
    // lines/s\", load_av, compile_av)", confirmed directly. This driver
    // tracks neither figure as a rolling rate (no per-second sampling
    // window anywhere in Scheduler/Server), so this returns a fixed,
    // honestly-zero string in the real format shape rather than a
    // fabricated live number -- this row's own real, tested call site
    // only checks stringp() on the result, never a specific rate.
    t.registerEfun("query_load_average", [](VM&, std::vector<Value>&) -> Value {
        return Value(std::string("0.00 cmds/s, 0.00 comp lines/s"));
    });

    // void say(string str, void|object|object* exclude) -- real
    // simulate.c's own say()/send_say(): broadcasts str to origin's
    // surrounding object (if eligible), everything else in that
    // surrounding object's own inventory except origin itself, and
    // everything in origin's own inventory -- origin being command_giver
    // if one is active, else current_object (confirmed directly:
    // "if (current_object->flags & O_LISTENER || current_object->
    // interactive) save_command_giver(current_object); else
    // save_command_giver(command_giver); ... origin = command_giver ?
    // command_giver : current_object" -- this driver approximates the
    // O_LISTENER/interactive-current_object special case as "always
    // prefer command_giver, falling back to current_object", since this
    // driver has no O_LISTENER flag of its own and no real call site here
    // needs that finer distinction). Real eligibility to receive the
    // message is "O_LISTENER || interactive" -- approximated here as "has
    // a live Connection via InteractiveRegistry", this driver's own
    // already-established stand-in for that same real check (see
    // message()'s own use of the identical registry above). avoid is
    // never sent to; origin itself is never sent to either -- it can
    // never appear in its own inventory, and the surrounding object's
    // inventory loop explicitly excludes it, matching real send_say()'s
    // own two call sites exactly. Previously excluded from this table
    // entirely because the old nightmare3_fluffos_v2 mudlib this row's
    // prior batches ranked against shadowed it with a simul_efun (see
    // STATUS.md's 2026-08-21 entry) -- that mudlib is gone from this repo
    // after the extraction, and this repo's own bundled Lil calls the
    // bare efun directly and unshadowed (mudlib/single/tests/efuns/
    // talker.c), so the exclusion no longer applies.
    t.registerEfun("say", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("say: expected a string as the first argument");
        }
        const std::string& text = std::get<std::string>(args[0].data);

        std::vector<std::shared_ptr<LpcObject>> avoid;
        if (args.size() > 1) {
            if (auto* ob = std::get_if<std::shared_ptr<LpcObject>>(&args[1].data)) {
                if (*ob) avoid.push_back(*ob);
            } else if (auto* arr = std::get_if<std::shared_ptr<Array>>(&args[1].data)) {
                if (*arr) {
                    for (auto& item : (*arr)->items) {
                        if (auto* o = std::get_if<std::shared_ptr<LpcObject>>(&item.data)) {
                            if (*o) avoid.push_back(*o);
                        }
                    }
                }
            }
        }

        auto isAvoided = [&avoid](const std::shared_ptr<LpcObject>& ob) {
            return std::find(avoid.begin(), avoid.end(), ob) != avoid.end();
        };
        auto sendTo = [&](const std::shared_ptr<LpcObject>& ob) {
            if (!ob || isAvoided(ob)) return;
            if (Connection* conn = InteractiveRegistry::find(ob)) deliverToConnection(vm, conn, text);
        };

        auto origin = resolveCommandGiver(vm);
        if (!origin) origin = vm.currentObject();
        if (!origin) return Value{};

        if (auto env = origin->environment().lock()) {
            sendTo(env);
            for (auto& sib : env->inventory()) {
                if (sib != origin) sendTo(sib);
            }
        }
        for (auto& inv : origin->inventory()) {
            sendTo(inv);
        }
        return Value{};
    });

    // string save_variable(mixed) / mixed restore_variable(string) --
    // real object.c's own save_variable()/restore_variable(), a single-
    // value save to/from the exact same on-disk text format save_object()/
    // restore_object() already use for a whole object's variables (see
    // writeRealSaveValue()/parseRestoreVariableTopLevel() above for the
    // real spec citations). save_variable() writes that real format
    // directly (unlike save_object() above, which still only ever writes
    // this driver's own simpler custom format) because save_variable()'s
    // own contract *is* "return the real save-file text for this value" --
    // there is no other format for it to write.
    t.registerEfun("save_variable", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty()) throw LpcRuntimeError("save_variable: expected one argument");
        std::string out;
        writeRealSaveValue(out, args[0]);
        return Value(out);
    });

    t.registerEfun("restore_variable", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("restore_variable: expected a string argument");
        }
        return parseRestoreVariableTopLevel(std::get<std::string>(args[0].data));
    });

    // -------------------------------------------------------------------------
    // Phase 0.13 efun growth batch, continued: children, set_light, bind.
    // Same method as the prior batch -- diffed EfunTable's registered
    // names against mudlib/single/tests/efuns/*.c, checked each surviving
    // name against func_spec.c/efuns_main.c directly before implementing.
    // Most of the remaining gap is excluded architecture mismatches
    // (mud_status/cache_stats/malloc_status/dumpallobj/opcprof's driver-
    // internal C-struct dumps, allocate_buffer/read_buffer's buffer type,
    // get_char/ed's per-keystroke/multi-line-editor infrastructure, plain
    // false positives, or test fixture files rather than real efun
    // names -- see src/efun/instruct.md's own status table) or, for
    // origin()/snoop()'s own family, real but sizeable enough (per-call
    // origin tagging through every VM call path; a whole snoop-target
    // registry plus Connection-level output duplication) to warrant their
    // own row rather than folding into this batch -- flagged in
    // STATUS.md's own dated entry for this batch rather than silently
    // skipped.
    // -------------------------------------------------------------------------

    // object *children(string filename) -- real otable.c's own children():
    // every currently-loaded object (blueprint or clone alike) whose own
    // filename starts with the given prefix, confirmed directly
    // (basename()-stripped clone-id aside -- this driver has no clone-id
    // suffix concept at all, same pre-existing gap already noted on
    // base_name()'s own comment, so that strip is already a no-op here).
    // Reuses LiveObjectRegistry (already backing objects()/livings()
    // above) rather than a separate table -- the exact same "every
    // currently-loaded object" set real children()'s own object hash
    // table walks. filename is normalized the same way
    // ObjectManager::normalizeFilename() strips a trailing ".c" before
    // storing LpcObject::filename() in the first place, so a real
    // __FILE__-shaped argument (which always carries ".c",
    // ObjectManager.cpp's own predefine comment) matches correctly
    // against the extension-less stored form. Real, narrow scope
    // limitation inherited directly from LiveObjectRegistry itself (see
    // its own header comment): a clone with no live shared_ptr anywhere
    // else in this driver is not enumerated here, unlike real FluffOS's
    // own persistent, refcount-independent object table, where a clone
    // stays listed until explicitly destruct()ed regardless of whether
    // any LPC-level variable still references it. Not a gap specific to
    // this efun -- objects()/livings() above already carry the identical
    // limitation, and this driver's own clone lifetime model (a clone
    // lives only as long as something genuinely holds it, typically an
    // environment/inventory slot) already assumes it everywhere else.
    t.registerEfun("children", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("children: expected a string argument");
        }
        std::string prefix = std::get<std::string>(args[0].data);
        if (prefix.size() >= 2 && prefix.compare(prefix.size() - 2, 2, ".c") == 0) {
            prefix.erase(prefix.size() - 2);
        }
        auto result = std::make_shared<Array>();
        for (auto& ob : LiveObjectRegistry::all()) {
            const std::string& name = ob->filename();
            if (name.size() >= prefix.size() && name.compare(0, prefix.size(), prefix) == 0) {
                result->items.emplace_back(ob);
            }
        }
        return Value(result);
    });

    // int set_light(int n) -- real efuns_main.c's own f_set_light():
    // confirmed directly, not guessed from func_spec.c's bare "/*
    // set_light should die a dark death */ int set_light(int);" comment
    // alone -- adds n to current_object's own total light count, real
    // simulate.c's own add_light() (confirmed directly) propagating that
    // same delta up through every ancestor environment too, then returns
    // the *topmost* ancestor's own resulting total, walking all the way
    // to the root regardless of how many levels up that is. Deprecated in
    // real FluffOS itself, kept here only because this repo's own bundled
    // Lil starter mudlib genuinely calls it
    // (mudlib/single/tests/efuns/light.c's own create()).
    t.registerEfun("set_light", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("set_light: expected an int argument");
        }
        auto ob = vm.currentObject();
        if (!ob) return Value(static_cast<int64_t>(0));
        int delta = static_cast<int>(std::get<int64_t>(args[0].data));
        ob->setTotalLight(ob->totalLight() + delta);
        auto root = ob;
        for (auto env = ob->environment().lock(); env; env = env->environment().lock()) {
            env->setTotalLight(env->totalLight() + delta);
            root = env;
        }
        return Value(static_cast<int64_t>(root->totalLight()));
    });

    // void set_debug_level(int | string) -- real efuns_main.c's own
    // f_set_debug_level(): toggles bits in a driver-internal debug_level
    // bitmask (debug.c) controlling which debug(...) trace categories
    // print to the driver's own console -- this driver has no equivalent
    // category-tagged trace system anywhere (confirmed by grep: no
    // debug()-style call site exists in this codebase at all), so there
    // is nothing real for this efun to toggle. Accepted and silently
    // ignored (matching this codebase's own established convention for
    // set_eval_limit()'s own accumulated-cost model this driver
    // approximates instead of replicating) rather than left as "undefined
    // efun", since this row's own real, tested call site
    // (mudlib/single/tests/efuns/set_debug_level.c) only needs the call
    // to not throw, gated behind a `__DEBUG_MACRO__` this mudlib never
    // defines by default -- confirmed by grep, so that test's own body is
    // an intentional no-op even under real FluffOS.
    t.registerEfun("set_debug_level", [](VM&, std::vector<Value>&) -> Value {
        return Value{};
    });

    // function bind(function fp, object new_owner) -- real efuns_main.c's
    // own f_bind(): rebinds a function pointer's own owner (the object a
    // closure's local/inherited-function and global-variable references
    // resolve against) to a different object, gated behind
    // master()->valid_bind(binder, old_owner, new_owner) -- confirmed
    // directly via f_bind()'s own exact 3-argument
    // push_object(current_object)/push_object(old_owner)/push_object(new_
    // owner) sequence before its "apply_master_ob(APPLY_VALID_BIND, 3)"
    // call, matching this driver's own already-established "master &&
    // isTruthy(callFunction(master, 'valid_X', ...))" gate pattern (see
    // set_hide's identical valid_hide() gate above). Real f_bind() also
    // has two FP_NOT_BINDABLE guards (a closure over local variables, or
    // one referencing its own owner's globals/local functions, cannot be
    // safely rebound) -- not implemented, since this driver's own
    // simplified Closure (Value.hpp's own comment: a bare function name
    // plus already-bound arguments, re-resolved lazily against whatever
    // `owner` currently is at *call* time, not baked at construction) has
    // no equivalent unsafe case: every closure this driver can construct
    // is already safe to rebind by construction, there is nothing for
    // that guard to protect against here. Same-owner rebind is a real
    // no-op, matching f_bind()'s own "if (ob == old_fp->hdr.owner) { no
    // change; }" branch exactly -- returns the identical closure
    // unchanged rather than a new copy.
    t.registerEfun("bind", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::shared_ptr<Closure>>(args[0].data) ||
            !std::holds_alternative<std::shared_ptr<LpcObject>>(args[1].data)) {
            throw LpcRuntimeError("bind: expected (function, object) arguments");
        }
        auto oldClosure = std::get<std::shared_ptr<Closure>>(args[0].data);
        auto newOwner = std::get<std::shared_ptr<LpcObject>>(args[1].data);
        if (!oldClosure) throw LpcRuntimeError("bind: expected a real function pointer");
        auto oldOwner = oldClosure->owner.lock();
        if (oldOwner == newOwner) return Value(oldClosure);

        auto master = vm.masterObject();
        bool permitted = master && isTruthy(vm.callFunction(
            master, "valid_bind", {Value(vm.currentObject()), Value(oldOwner), Value(newOwner)}));
        if (!permitted) {
            throw LpcRuntimeError("Master object denied permission to bind() function pointer.");
        }

        auto rebound = std::make_shared<Closure>();
        rebound->owner = newOwner;
        rebound->functionName = oldClosure->functionName;
        rebound->boundArgs = oldClosure->boundArgs;
        return Value(rebound);
    });

    // -------------------------------------------------------------------------
    // Phase 0.13 efun growth batch, continued (2026-08-22): tell_object,
    // tell_room, shout, this_interactive/this_user, map_mapping,
    // filter_mapping. Lil's own real efun conformance suite
    // (mudlib/single/tests/efuns/*.c) is now exhausted as a ranking
    // source -- diffing it against EfunTable's registered names this
    // batch turned up nothing but already-documented architecture-
    // mismatch exclusions, C-internal helper names that were never real
    // LPC efuns (add_light, break_string), test-fixture files
    // (badshad/goodshad/inh0-2/light/talker/unloaded), names confirmed
    // absent from efun_defs.c's own ground-truth registration table
    // (enable_wizard, query_ed_mode, function_profile, has_errors), and
    // sscanf, which is already real and implemented -- just not through
    // this table, matching real FluffOS's own special-cased lvalue-
    // argument grammar handling (see ROADMAP row 0.2). Re-scoped this
    // batch's ranking source back to real call-site frequency across the
    // whole bundled Lil mudlib (mudlib/, not just tests/efuns/), diffed
    // against efun_defs.c directly, the same methodology used before the
    // conformance-suite pass started.
    //
    // void tell_object(object ob, string str) -- real object.c's own
    // tell_object(): if ob has a live connection, the text is written
    // straight to it (through this driver's existing add_message-
    // equivalent chokepoint, deliverToConnection() -- so tell_object()
    // output is correctly snoop-duplicated too, matching real add_message()
    // being the one function both paths funnel through); otherwise the
    // real driver calls catch_tell(string) on ob (an NPC-to-NPC/NPC-to-
    // player communication channel, real APPLY_CATCH_TELL) instead of
    // writing to a socket that doesn't exist. A destructed or null ob is
    // a silent no-op, matching real "if (!ob || ob->flags&O_DESTRUCTED)"
    // -- real add_message(0, ...) technically still writes to stderr
    // under a debug build define this driver has no equivalent for, not
    // implemented, matching this project's own "no fabricated driver-
    // internal diagnostics" precedent.
    t.registerEfun("tell_object", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("tell_object: expected (object, string) arguments");
        }
        auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
        const std::string& text = std::get<std::string>(args[1].data);
        if (!ob || ob->isDestructed()) return Value{};
        if (Connection* conn = InteractiveRegistry::find(ob)) {
            deliverToConnection(vm, conn, text);
        } else {
            vm.callFunction(ob, "catch_tell", {Value(text)});
        }
        return Value{};
    });

    // void tell_room(object|string room, string|object|int|float msg,
    // void|object|object* avoid) -- real simulate.c's own tell_room():
    // sends to every object in room's *direct* inventory (not recursive)
    // that is currently interactive, skipping anything in avoid (a
    // single object or an array, same shape as say()'s own avoid
    // argument just above). room may be a string path instead of an
    // object reference, resolved the same way call_other()'s own
    // string-target form does (this driver's existing VM::findObject(),
    // which compiles on a miss) -- real find_object() used internally
    // here does not compile on a miss, a minor, documented divergence
    // with no real call site in this mudlib to be wrong against either
    // way. msg is polymorphic in real tell_room() (a legacy quirk, not
    // this driver's invention): a plain string is sent as-is, an object
    // is converted to its own filename string, a number is stringified --
    // matched here for the two shapes this driver can express (string,
    // object); the T_REAL (float) case real func_spec.c also lists has no
    // real call site anywhere in this mudlib and is not implemented.
    // Real code's own additional `!ob->interactive && !(ob->flags &
    // O_LISTENER)` skip and `object_visible(ob)` gate are scoped down to
    // "is ob currently interactive" here -- this driver tracks no
    // O_LISTENER-equivalent flag at all (see shout()'s own comment just
    // below for why that flag turns out to be real-world dead code in a
    // normal, non-NO_ADD_ACTION build anyway) and has no hidden-from-
    // room-broadcast visibility concept separate from set_hide()'s own
    // already-implemented, differently-scoped mechanism.
    t.registerEfun("tell_room", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2) {
            throw LpcRuntimeError("tell_room: expected (object|string, mixed, ...) arguments");
        }
        std::shared_ptr<LpcObject> room;
        if (auto* ob = std::get_if<std::shared_ptr<LpcObject>>(&args[0].data)) {
            room = *ob;
        } else if (auto* path = std::get_if<std::string>(&args[0].data)) {
            room = vm.findObject(*path);
        } else {
            throw LpcRuntimeError("tell_room: expected an object or string first argument");
        }
        if (!room || room->isDestructed()) return Value{};

        std::string text;
        if (auto* s = std::get_if<std::string>(&args[1].data)) {
            text = *s;
        } else if (auto* ob = std::get_if<std::shared_ptr<LpcObject>>(&args[1].data)) {
            text = *ob ? (*ob)->filename() : "0";
        } else if (auto* n = std::get_if<int64_t>(&args[1].data)) {
            text = std::to_string(*n);
        } else {
            throw LpcRuntimeError("tell_room: expected a string, object, or int second argument");
        }

        std::vector<std::shared_ptr<LpcObject>> avoid;
        if (args.size() > 2) {
            if (auto* ob = std::get_if<std::shared_ptr<LpcObject>>(&args[2].data)) {
                if (*ob) avoid.push_back(*ob);
            } else if (auto* arr = std::get_if<std::shared_ptr<Array>>(&args[2].data)) {
                if (*arr) {
                    for (auto& item : (*arr)->items) {
                        if (auto* o = std::get_if<std::shared_ptr<LpcObject>>(&item.data)) {
                            if (*o) avoid.push_back(*o);
                        }
                    }
                }
            }
        }

        for (auto& ob : room->inventory()) {
            if (!ob || std::find(avoid.begin(), avoid.end(), ob) != avoid.end()) continue;
            if (Connection* conn = InteractiveRegistry::find(ob)) {
                deliverToConnection(vm, conn, text);
            }
        }
        return Value{};
    });

    // void shout(string str) -- real simulate.c's own shout_string(),
    // confirmed directly before implementing, not assumed from the
    // efun's general reputation: it walks *every* object in the game
    // (not just interactive ones) and sends to whichever ones have the
    // O_LISTENER flag set, skipping command_giver and anything with no
    // environment. Confirmed real, and confirmed dead in a normal build:
    // O_LISTENER's only setter anywhere in this reference source
    // (simulate.c's own init_object()) is itself gated behind "#ifdef
    // NO_ADD_ACTION", and no efun exists anywhere in func_spec.c to set
    // it from LPC either -- so in the actual build that generated this
    // vendored efun_defs.c (which genuinely has add_action/commands/
    // enable_commands/livings registered, proving NO_ADD_ACTION was NOT
    // active for that generation, regardless of what the currently
    // checked-in options.h happens to say -- see this row's own STATUS.md
    // entry), O_LISTENER can never be set at all, and literal-real
    // shout_string() broadcasts to nobody, ever. This is a real,
    // confirmed architectural dead end in this exact reference
    // configuration, not a misreading -- but this mudlib's own real,
    // live call sites (command/say.c and command/quit.c both literally
    // "#define say(x) shout(x)": the bundled say command's entire
    // implementation *is* a call to this efun) obviously expect it to
    // reach connected players, matching O_LISTENER's own doc comment
    // ("can hear say(), etc", object.h) -- the flag's evident intent, not
    // its dead-code letter. Implemented as "every currently-interactive
    // object except command_giver" to match that intent and keep this
    // mudlib's own say command actually working, a deliberate, documented
    // departure from the literal (but provably unreachable) O_LISTENER
    // condition rather than a faithful reproduction of dead code.
    t.registerEfun("shout", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("shout: expected a string argument");
        }
        const std::string& text = std::get<std::string>(args[0].data);
        auto giver = resolveCommandGiver(vm);
        for (auto& ob : InteractiveRegistry::all()) {
            if (ob == giver) continue;
            if (Connection* conn = InteractiveRegistry::find(ob)) {
                deliverToConnection(vm, conn, text);
            }
        }
        return Value{};
    });

    // object this_interactive() / object this_user() -- real efun_defs.c:
    // both F_THIS_PLAYER | F_ALIAS_FLAG, the exact same code as
    // this_player(1) (confirmed directly, not two separate
    // implementations -- same "alias shares the real efun's code" pattern
    // already established for shallow_inherit_list/inherit_list and
    // map_array/map). Real f_this_player()'s flag==1 branch returns
    // current_interactive, not command_giver -- a real, load-bearing
    // distinction this_player(0)'s own registration above does not
    // capture: command_giver can be reassigned mid-dispatch (real
    // set_this_player(), living.c-style code), while current_interactive
    // stays fixed to whichever connection is actually driving this
    // execution for its whole duration. This driver has no separate
    // current_interactive tracking distinct from the commandGiverStack_
    // this_player(0) reads (see resolveCommandGiver()'s own comment
    // above), so this reads OutputContext::current()'s own bound object
    // directly instead -- bypassing the command-giver stack entirely,
    // the closest available proxy for "the literal connection driving
    // this call, unaffected by any mid-dispatch reassignment". Real,
    // confirmed live call site: mudlib/single/master.c's own
    // error_handler(), "this_interactive() || this_player()".
    auto thisInteractiveImpl = [](VM&, std::vector<Value>&) -> Value {
        if (Connection* conn = OutputContext::current()) {
            if (auto bound = conn->boundObject()) return Value(bound);
        }
        return Value{};
    };
    t.registerEfun("this_interactive", thisInteractiveImpl);
    t.registerEfun("this_user", thisInteractiveImpl);

    // mapping map_mapping(mapping m, string|function f, ...) -- real
    // mapping.c's own map_mapping(): calls f(key, value, ...extra) for
    // every entry and replaces that entry's *value* with the result,
    // keys unchanged (confirmed directly against its own "push key; push
    // value; call; assign result back into the value slot" body, not
    // assumed from map_array's shape alone). Same alias-before-real
    // naming and two call shapes (string-plus-target-object or Closure)
    // as map_array/filter_array above; unlike those, this always returns
    // a copy (real code copies the mapping first when its refcount is
    // shared) rather than mutating the argument in place, which this
    // driver's own copy-on-write-free Value model makes simpler, not
    // harder, to get right: entries are copied up front, then each
    // value is overwritten by the callback's result. Confirmed real,
    // live call site: mudlib/single/simul_efun.c's own use of
    // map_mapping() directly.
    t.registerEfun("map_mapping", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::shared_ptr<Mapping>>(args[0].data)) {
            throw LpcRuntimeError("map_mapping: expected a mapping first argument");
        }
        auto m = std::get<std::shared_ptr<Mapping>>(args[0].data);
        auto result = std::make_shared<Mapping>();
        if (!m) return Value(result);
        result->entries = m->entries;
        result->width = m->width;
        result->extraColumns = m->extraColumns;

        if (auto* closurePtr = std::get_if<std::shared_ptr<Closure>>(&args[1].data)) {
            if (!*closurePtr) return Value(result);
            std::vector<Value> extra(args.begin() + 2, args.end());
            for (auto& entry : result->entries) {
                std::vector<Value> callArgs;
                callArgs.reserve(2 + extra.size());
                callArgs.push_back(entry.first);
                callArgs.push_back(entry.second);
                callArgs.insert(callArgs.end(), extra.begin(), extra.end());
                entry.second = vm.callClosure(*closurePtr, std::move(callArgs));
            }
            return Value(result);
        }

        if (!std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("map_mapping: expected a string or function second argument");
        }
        if (args.size() < 3 || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[2].data)) {
            throw LpcRuntimeError("map_mapping: string function name requires an object third argument");
        }
        const std::string& funcName = std::get<std::string>(args[1].data);
        auto target = std::get<std::shared_ptr<LpcObject>>(args[2].data);
        std::vector<Value> extra(args.begin() + 3, args.end());
        for (auto& entry : result->entries) {
            std::vector<Value> callArgs;
            callArgs.reserve(2 + extra.size());
            callArgs.push_back(entry.first);
            callArgs.push_back(entry.second);
            callArgs.insert(callArgs.end(), extra.begin(), extra.end());
            // Origin::Efun -- real map_mapping() (mapping.c, F_MAP,
            // the same core efun code map_array shares) also dispatches
            // via process_efun_callback()/call_efun_callback(), see
            // map_array's own comment above.
            entry.second = vm.callFunction(target, funcName, std::move(callArgs), Origin::Efun);
        }
        return Value(result);
    });

    // mapping filter_mapping(mapping m, string|function f, ...) -- real
    // mapping.c's own filter_mapping(): same per-entry f(key, value,
    // ...extra) call as map_mapping() just above, but keeps the whole
    // entry (key and value both, unchanged) in a brand-new mapping only
    // when the result is truthy, rather than overwriting the value in
    // place -- confirmed directly against its own "if (!ret->type !=
    // T_NUMBER || ret->u.number) { ...keep... }" body. No real call site
    // in this mudlib (zero hits, confirmed by grep) -- implemented anyway
    // alongside map_mapping() as the same real, complete mapping/string/
    // function-callback triple func_spec.c defines together (map/filter/
    // sort_array's own array-side precedent), not a driver invention.
    t.registerEfun("filter_mapping", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::shared_ptr<Mapping>>(args[0].data)) {
            throw LpcRuntimeError("filter_mapping: expected a mapping first argument");
        }
        auto m = std::get<std::shared_ptr<Mapping>>(args[0].data);
        auto result = std::make_shared<Mapping>();
        if (!m) return Value(result);
        result->width = m->width;

        if (auto* closurePtr = std::get_if<std::shared_ptr<Closure>>(&args[1].data)) {
            if (!*closurePtr) return Value(result);
            std::vector<Value> extra(args.begin() + 2, args.end());
            for (size_t i = 0; i < m->entries.size(); ++i) {
                auto& entry = m->entries[i];
                std::vector<Value> callArgs;
                callArgs.reserve(2 + extra.size());
                callArgs.push_back(entry.first);
                callArgs.push_back(entry.second);
                callArgs.insert(callArgs.end(), extra.begin(), extra.end());
                if (isTruthy(vm.callClosure(*closurePtr, std::move(callArgs)))) {
                    result->entries.push_back(entry);
                    if (!m->extraColumns.empty()) result->extraColumns.push_back(m->extraColumns[i]);
                }
            }
            return Value(result);
        }

        if (!std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("filter_mapping: expected a string or function second argument");
        }
        if (args.size() < 3 || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[2].data)) {
            throw LpcRuntimeError("filter_mapping: string function name requires an object third argument");
        }
        const std::string& funcName = std::get<std::string>(args[1].data);
        auto target = std::get<std::shared_ptr<LpcObject>>(args[2].data);
        std::vector<Value> extra(args.begin() + 3, args.end());
        for (size_t i = 0; i < m->entries.size(); ++i) {
            auto& entry = m->entries[i];
            std::vector<Value> callArgs;
            callArgs.reserve(2 + extra.size());
            callArgs.push_back(entry.first);
            callArgs.push_back(entry.second);
            callArgs.insert(callArgs.end(), extra.begin(), extra.end());
            // Origin::Efun -- real filter_mapping() (mapping.c, F_FILTER,
            // shared with filter_array) also dispatches via
            // process_efun_callback()/call_efun_callback(), see
            // map_array's own comment above.
            if (isTruthy(vm.callFunction(target, funcName, std::move(callArgs), Origin::Efun))) {
                result->entries.push_back(entry);
                if (!m->extraColumns.empty()) result->extraColumns.push_back(m->extraColumns[i]);
            }
        }
        return Value(result);
    });

    // mixed element_of(mixed *arr) -- real packages/contrib.c's own
    // f_element_of(): returns one uniformly random element from arr,
    // throwing "Can't take element from empty array." for an empty one
    // (confirmed directly, not a silent 0 the way some other empty-array
    // efuns in this driver are). Same random-number-generation approach
    // already established for the random() efun above (a local static
    // std::mt19937 -- this driver has no shared/seeded RNG service for
    // efuns to draw from in common, matching random()'s own precedent
    // rather than inventing a new one).
    t.registerEfun("element_of", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<Array>>(args[0].data)) {
            throw LpcRuntimeError("element_of: expected an array argument");
        }
        auto arr = std::get<std::shared_ptr<Array>>(args[0].data);
        if (!arr || arr->items.empty()) {
            throw LpcRuntimeError("Can't take element from empty array.");
        }
        static std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, arr->items.size() - 1);
        return arr->items[dist(rng)];
    });

    // mixed *shuffle(mixed *arr) -- real packages/contrib.c's own
    // f_shuffle()/shuffle(): an in-place Fisher-Yates shuffle (real "for
    // (i = 0; i < args->size; i++) { j = random_number(i + 1); swap(i,
    // j); }", confirmed directly -- the classic inside-out variant, not
    // the more common backwards-scanning one, though both produce a
    // uniform permutation), a no-op for fewer than two elements, and
    // returns the *same* array object mutated in place -- matching this
    // driver's own established array-aliasing semantics (see copy()'s
    // own comment on arrays being shared by reference between variables
    // until explicitly copied), the same by-reference mutation real
    // FluffOS's own array_t reference gives it. A non-array argument
    // returns an empty array rather than throwing, matching real
    // f_shuffle()'s own "push_refed_array(&the_null_array)" fallback.
    t.registerEfun("shuffle", [](VM&, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::shared_ptr<Array>>(args[0].data)) {
            return Value(std::make_shared<Array>());
        }
        auto arr = std::get<std::shared_ptr<Array>>(args[0].data);
        if (!arr || arr->items.size() < 2) return Value(arr ? arr : std::make_shared<Array>());
        static std::mt19937 rng(std::random_device{}());
        for (size_t i = 0; i < arr->items.size(); ++i) {
            std::uniform_int_distribution<size_t> dist(0, i);
            size_t j = dist(rng);
            if (i != j) std::swap(arr->items[i], arr->items[j]);
        }
        return Value(arr);
    });

    // --- db_* (ROADMAP.md row 2.15, scoped 2026-08-21; dialect-gated
    // 2026-08-27, ROADMAP.md row 2.40) --------------------------------
    // Real LDMud's own db_* family (temp/ldmud/src/pkg-mysql.c), not
    // FluffOS's -- see DbRegistry.hpp's own header comment for the full
    // evidence chain (core-lib's own README.md states it targets LDMud;
    // real vendored FluffOS 2.9 has a completely different db_connect/
    // db_exec/db_fetch signature shape and no db_error/db_handles/
    // db_conv_string at all). Backed by SQLite (DbRegistry) rather than
    // a live MySQL server. Every real call site here mirrors
    // pkg-mysql.c's own "check_privilege(name, MY_TRUE, sp)" first --
    // VM::privilegeViolation("mysql", {efun name}) throwing on denial,
    // matching real check_privilege()'s own raise_error==MY_TRUE for
    // every one of these efuns (confirmed directly, every real call site
    // in pkg-mysql.c passes MY_TRUE). db_conv_string is the one real
    // exception -- its own real doc/source has no check_privilege() call
    // at all, so it is not gated here either.
    //
    // Dialect-gated to "ldmud" only, added this session after a full
    // .spec-sweep pass confirmed real current FluffOS's own db.spec/
    // db.cc (temp/fluffos/src/packages/db/) diverges from this family in
    // every real signature: db_connect(host, database, user, type) vs.
    // LDMud's db_connect(database, user, password) -- host is FIRST
    // under real FluffOS, absent entirely under real LDMud; db_exec()
    // returns rows-affected-or-error-string under real FluffOS vs. the
    // handle-on-success/0-on-error real LDMud returns; db_fetch(handle,
    // row) is row-indexed under real FluffOS vs. LDMud's sequential,
    // single-arg db_fetch(handle); db_close() returns a 1/0 success flag
    // under real FluffOS vs. the handle number real LDMud returns; real
    // FluffOS gates on a differently-named valid_database() master apply,
    // not check_privilege(). Every real corpus this project has evidence
    // for was re-checked directly (not assumed): zero real db_* call
    // sites anywhere outside core-lib (dead-souls, es2_mudlib, lima,
    // nightmare3, reference-lpc-mud-library, this project's own bundled
    // mudlib all confirmed clean), and core-lib's own real usage does
    // not merely happen to fit LDMud's shape, it actively depends on it
    // -- secure/simulated-efuns/database.c's own "dbHandle =
    // efun::db_exec(dbHandle, sqlQuery);" idiom only makes sense under
    // real LDMud's own "db_exec returns the handle on success" contract;
    // under real FluffOS's own "returns rows-affected" contract, that
    // exact real line would silently overwrite dbHandle with a row count
    // instead, corrupting every subsequent call on that connection. Zero
    // real evidence anywhere justifies building a second, FluffOS-shaped
    // db_* target -- gating this driver's own single, real, evidence-
    // backed implementation to the one dialect it is actually correct
    // for converts what was previously a silent wrong-shape footgun
    // under "dialect: fluffos" (this driver's own default) into an
    // honest "not implemented for this dialect" gap instead, the same
    // "throw rather than silently misbehave" principle this codebase
    // already applies elsewhere (an unsupported sscanf/sprintf format,
    // an unsupported explode_reversible() delimiter, and so on).
    auto requireLdmudDbDialect = [](VM& vm, const char* efunName) {
        if (vm.config().dialect() != "ldmud") {
            throw LpcRuntimeError(std::string(efunName) +
                "(): not implemented under dialect '" + vm.config().dialect() +
                "' -- this driver's db_* family matches real LDMud's own "
                "pkg-mysql.c signature and return semantics specifically, "
                "confirmed to diverge from real current FluffOS's own "
                "db.c/db.spec in every real db_* argument shape and return "
                "contract, with zero real corpus evidence anywhere for the "
                "FluffOS shape (see ROADMAP.md row 2.40)");
        }
    };

    // int db_connect(string database, string|void user, string|void password)
    t.registerEfun("db_connect", [requireLdmudDbDialect](VM& vm, std::vector<Value>& args) -> Value {
        requireLdmudDbDialect(vm, "db_connect");
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("db_connect: expected a database name string");
        }
        if (!vm.privilegeViolation("mysql", {Value(std::string("db_connect"))})) {
            throw LpcRuntimeError("db_connect(): Privilege violation.");
        }
        const std::string& database = std::get<std::string>(args[0].data);
        std::string user, password;
        bool hasUser = false, hasPassword = false;
        if (args.size() > 1 && std::holds_alternative<std::string>(args[1].data)) {
            user = std::get<std::string>(args[1].data);
            hasUser = true;
        }
        if (args.size() > 2 && std::holds_alternative<std::string>(args[2].data)) {
            password = std::get<std::string>(args[2].data);
            hasPassword = true;
        }
        int handle = DbRegistry::connect(database, user, hasUser, password, hasPassword);
        return Value(static_cast<int64_t>(handle));
    });

    // int db_exec(int handle, string statement)
    t.registerEfun("db_exec", [requireLdmudDbDialect](VM& vm, std::vector<Value>& args) -> Value {
        requireLdmudDbDialect(vm, "db_exec");
        if (args.size() < 2 || !std::holds_alternative<int64_t>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("db_exec: expected (int handle, string statement)");
        }
        if (!vm.privilegeViolation("mysql", {Value(std::string("db_exec"))})) {
            throw LpcRuntimeError("db_exec(): Privilege violation.");
        }
        int handle = static_cast<int>(std::get<int64_t>(args[0].data));
        const std::string& stmt = std::get<std::string>(args[1].data);
        return Value(static_cast<int64_t>(DbRegistry::exec(handle, stmt)));
    });

    // mixed db_fetch(int handle)
    t.registerEfun("db_fetch", [requireLdmudDbDialect](VM& vm, std::vector<Value>& args) -> Value {
        requireLdmudDbDialect(vm, "db_fetch");
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("db_fetch: expected an int handle argument");
        }
        if (!vm.privilegeViolation("mysql", {Value(std::string("db_fetch"))})) {
            throw LpcRuntimeError("db_fetch(): Privilege violation.");
        }
        int handle = static_cast<int>(std::get<int64_t>(args[0].data));
        return DbRegistry::fetch(handle);
    });

    // int db_close(int handle)
    t.registerEfun("db_close", [requireLdmudDbDialect](VM& vm, std::vector<Value>& args) -> Value {
        requireLdmudDbDialect(vm, "db_close");
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("db_close: expected an int handle argument");
        }
        if (!vm.privilegeViolation("mysql", {Value(std::string("db_close"))})) {
            throw LpcRuntimeError("db_close(): Privilege violation.");
        }
        int handle = static_cast<int>(std::get<int64_t>(args[0].data));
        return Value(static_cast<int64_t>(DbRegistry::close(handle)));
    });

    // string|int db_error(int handle)
    t.registerEfun("db_error", [requireLdmudDbDialect](VM& vm, std::vector<Value>& args) -> Value {
        requireLdmudDbDialect(vm, "db_error");
        if (args.empty() || !std::holds_alternative<int64_t>(args[0].data)) {
            throw LpcRuntimeError("db_error: expected an int handle argument");
        }
        if (!vm.privilegeViolation("mysql", {Value(std::string("db_error"))})) {
            throw LpcRuntimeError("db_error(): Privilege violation.");
        }
        int handle = static_cast<int>(std::get<int64_t>(args[0].data));
        return DbRegistry::error(handle);
    });

    // int *db_handles()
    t.registerEfun("db_handles", [requireLdmudDbDialect](VM& vm, std::vector<Value>&) -> Value {
        requireLdmudDbDialect(vm, "db_handles");
        if (!vm.privilegeViolation("mysql", {Value(std::string("db_handles"))})) {
            throw LpcRuntimeError("db_handles(): Privilege violation.");
        }
        auto result = std::make_shared<Array>();
        for (int h : DbRegistry::handles()) result->items.emplace_back(static_cast<int64_t>(h));
        return Value(result);
    });

    // string db_conv_string(string str)
    t.registerEfun("db_conv_string", [requireLdmudDbDialect](VM& vm, std::vector<Value>& args) -> Value {
        requireLdmudDbDialect(vm, "db_conv_string");
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("db_conv_string: expected a string argument");
        }
        return Value(DbRegistry::convString(std::get<std::string>(args[0].data)));
    });
}

} // namespace amlp
