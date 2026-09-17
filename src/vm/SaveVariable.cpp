#include "kjdmud/vm/SaveVariable.hpp"
#include "kjdmud/core/Errors.hpp"
#include "kjdmud/vm/Value.hpp"

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>
#include <memory>

namespace kjdmud {

// Read-only support. This driver only ever *writes* its own format
// (serializeValue() above); this is purely a fallback reader for
// restore_object() so a genuine, pre-existing FluffOS save file (one
// that shipped with a real mudlib, never touched by this driver) loads
// its real historical data instead of being silently skipped line by
// line (see restore_object's own comment for how a line is dispatched
// to this parser vs. deserializeValue() above). Grounded directly in
// fluffos-2.9-ds2.08/object.c's own save_svalue() (the writer) and
// restore_string()/restore_array()/restore_mapping()/parse_numeric()
// (the readers). Not guessed. Real "class" values ("(/ ... /)") are
// not implemented (this driver has no class/struct type anywhere else
// either). Throws rather than silently mishandling, matching this
// codebase's existing convention for other unimplemented shapes.

// Real restore_string(): reads up to an unescaped '"'. save_svalue()
// itself only ever backslash-escapes '"' and '\\', but the real reader
// is more lenient than its own writer. A backslash before *any*
// character is taken literally, not just those two. So this mirrors
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
// optional '.'-led fractional part. Present only when the source is
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
            // is a skipped element, defaulting to int 0. The trailing
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
            std::vector<std::vector<Value>> pendingExtras;
            int maxWidth = 1;
            while (pos < s.size() && s[pos] != ']') {
                Value key = parseRealSaveValue(s, pos);
                if (pos < s.size() && s[pos] == ':') ++pos;
                Value val = parseRealSaveValue(s, pos);
                std::vector<Value> extra;
                while (pos < s.size() && s[pos] == ';') {
                    ++pos;
                    extra.push_back(parseRealSaveValue(s, pos));
                }
                map->entries.emplace_back(std::move(key), std::move(val));
                pendingExtras.push_back(std::move(extra));
                int w = 1 + static_cast<int>(pendingExtras.back().size());
                if (w > maxWidth) maxWidth = w;
                if (pos < s.size() && s[pos] == ',') ++pos;
            }
            if (pos < s.size()) ++pos; // ']'
            if (pos < s.size() && s[pos] == ')') ++pos;
            map->width = maxWidth;
            if (maxWidth > 1) {
                map->extraColumns.resize(map->entries.size());
                for (size_t i = 0; i < map->entries.size(); ++i) {
                    map->extraColumns[i].assign(static_cast<size_t>(maxWidth - 1),
                                                Value(int64_t{0}));
                    for (size_t c = 0; c < pendingExtras[i].size(); ++c) {
                        map->extraColumns[i][c] = std::move(pendingExtras[i][c]);
                    }
                }
            }
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

// save_variable(mixed)'s own writer. Real object.c's own save_svalue(),
// the exact paired writer for parseRealSaveValue()/parseRealSaveString()
// above (confirmed by reading it directly, not guessed): a string is
// wrapped in '"', with only '"' and '\\' backslash-escaped, and a literal
// embedded '\n' byte is substituted for a literal '\r' byte. Not a
// backslash escape at all, which is why parseRealSaveString() above
// reverses that exact substitution rather than treating '\r' as a
// two-character escape. Arrays/mappings always write a trailing ','
// after *every* element, including the last, matching real
// save_svalue()'s own unconditional per-element ',' inside the loop.
// parseRealSaveValue()'s own array/mapping readers already tolerate
// this leftover trailing comma. Floats use plain "%f" (six decimal
// places, no exponent), the same convention this driver's own %O
// sprintf specifier already uses for T_REAL. Object, closure, and
// buffer match save_svalue's missing cases: write nothing.
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
            for (size_t i = 0; i < (*mv)->entries.size(); ++i) {
                writeRealSaveValue(out, (*mv)->entries[i].first);
                out += ':';
                writeRealSaveValue(out, (*mv)->entries[i].second);
                // Extra columns use ';' like this driver's LDMud mapping
                // literal (FluffOS save_svalue is width-1; ds2.07 object.c:231).
                for (int c = 1; c < (*mv)->width; ++c) {
                    out += ';';
                    if (i < (*mv)->extraColumns.size()) {
                        writeRealSaveValue(out, (*mv)->getColumn(i, c));
                    } else {
                        out += '0';
                    }
                }
                out += ',';
            }
        }
        out += "])";
    } else {
        // No T_OBJECT / T_FUNCTION / T_BUFFER case in save_svalue
        // (ds2.07 object.c:138-252). Write nothing; restore default is 0.
    }
}

// restore_variable(string)'s own top-level string reader. deliberately
// separate from parseRealSaveString() above, not a shared helper, because
// real restore_svalue()'s (object.c) top-level restore_string() enforces
// a real, specific quirk that a nested array/mapping-element string must
// NOT be held to: the closing '"' must be immediately followed by the
// end of the *entire* input buffer, or it is a real ROB_STRING_ERROR.
// Confirmed directly by reading restore_string()'s own two exit paths
// (the plain, no-escape path: "if (*cp--) return ROB_STRING_ERROR;"
// right after the loop that found the closing quote; the escaped path:
// "if ((c == '\\0') || (*cp != '\\0')) return ROB_STRING_ERROR;"). both
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

// restore_variable(string)'s own top-level number reader. real
// parse_numeric() (object.c), confirmed directly: a leading '-' with no
// digit after it is a real failure (ROB_NUMERAL_ERROR), not "-0" or a
// silent 0. parseRealSaveNumber() above has no such check (real
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

// restore_variable(string)'s own top-level dispatcher. real
// restore_svalue() (object.c), confirmed directly: only the T_STRING
// case has any "did this consume the whole buffer" check at all (see
// parseRestoreVariableString()'s own comment). A top-level array,
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


}  // namespace kjdmud
