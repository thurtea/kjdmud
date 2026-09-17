#include "amlp/vm/VM.hpp"
#include "amlp/object/ObjectManager.hpp"
#include "amlp/object/LpcObject.hpp"
#include "amlp/config/Config.hpp"
#include "amlp/core/Errors.hpp"
#include "amlp/efun/EfunTable.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <utility>

namespace amlp {

// Real efuns_main.c's own origin_name(): "static const char *origins[] =
// { "driver", "local", "call_other", "simul", "internal", "efun",
// "function pointer", "functional" };" indexed by the bit position of
// the real ORIGIN_* flag (origin.h) -- same order here, matching
// Origin's own declaration order in VM.hpp exactly.
const char* originName(Origin origin) {
    switch (origin) {
        case Origin::Driver: return "driver";
        case Origin::Local: return "local";
        case Origin::CallOther: return "call_other";
        case Origin::SimulEfun: return "simul";
        case Origin::Internal: return "internal";
        case Origin::Efun: return "efun";
        case Origin::FunctionPointer: return "function pointer";
        case Origin::Functional: return "functional";
    }
    return "driver";
}

namespace {

// RAII push/pop of VM's originStack_ (real caller_type, saved/restored
// across push_control_stack()/pop_control_stack() -- see VM.hpp's own
// originStack_ comment for the full citation). Used at every real call
// path that pushes a genuine LPC frame, immediately before the run()
// call it wraps, the same "guard object whose constructor pushes and
// destructor pops" shape ObjectFrameGuard/CommandGiverGuard above
// already establish.
class OriginGuard {
public:
    OriginGuard(amlp::VM& vm, amlp::Origin origin) : vm_(vm) { vm_.pushOrigin(origin); }
    ~OriginGuard() { vm_.popOrigin(); }
    OriginGuard(const OriginGuard&) = delete;
    OriginGuard& operator=(const OriginGuard&) = delete;

private:
    amlp::VM& vm_;
};

// Real function.c's own naming convention for a synthesized inline
// lambda's own function-table entry ("$lambda#" + id, CodeGen.cpp's own
// InlineLambdaExpr handling) -- the one case callClosure()'s own tiered
// resolution needs to distinguish from an ordinary named local/inherited
// function, matching real call_function_pointer()'s own FP_FUNCTIONAL
// (anonymous) vs FP_LOCAL (named) split. "$" can never start a real LPC
// identifier (confirmed directly, CodeGen.cpp's own comment), so this
// prefix never collides with a real function name reached the ordinary
// way.
bool isSynthesizedLambdaName(const std::string& name) {
    return name.rfind("$lambda#", 0) == 0;
}

// Real F_LOCAL/F_GLOBAL/F_INDEX (interpret.c): every time a value is
// read out of storage -- a local, an object variable, an array element,
// or a mapping value -- and it turns out to hold a reference to a
// destructed object, the storage itself is rewritten to a real int 0
// right there ("assign_svalue(s, &const0u)") before the read completes,
// not just for this one read: permanently, so every later read of the
// same slot is already a plain 0 with no check needed. This is the
// actual, narrow mechanism behind real LPC's "a destructed object reads
// back as 0" semantics -- confirmed by reading interpret.c directly,
// not guessed: no other opcode (comparison, branch, arithmetic) checks
// O_DESTRUCTED at all (eoperators.c's own f_eq(), for one confirmed
// example, does a raw pointer compare on a T_OBJECT operand with no
// destructed check whatsoever), because by the time a value reaches one
// of those, it has already been coerced here if it needed to be. Array
// range-slicing (array.c's slice_array()) does NOT coerce either,
// confirmed by the same reading -- a destructed element copied into a
// freshly sliced sub-array stays a raw object reference until that new
// array's own element is itself read through one of these same points.
// Wiring this in at exactly PushLocal/PushObjectVar/Index (both the
// array and mapping cases) is therefore not a narrowed-down practical
// subset of real semantics, it is the complete mechanism -- closing the
// "any stale object-typed value silently reads back as 0" gap this
// project's own Known Stubs list had flagged as broader, unfixed scope.
void coerceIfDestructed(Value& v) {
    if (auto* ob = std::get_if<std::shared_ptr<LpcObject>>(&v.data)) {
        if (*ob && (*ob)->isDestructed()) {
            v = Value(static_cast<int64_t>(0));
        }
    }
}

// Pushes obj as the current object for as long as this guard is alive,
// on both of VM's call-tracking stacks -- real FluffOS's
// setup_fake_frame() (interpret.c), which runs unconditionally at the
// top of call_function_pointer() before its type-specific switch (i.e.
// for every closure kind, not just the ones that recurse into more LPC
// bytecode): "previous_ob = current_object; current_object =
// fun->hdr.owner". VM::run() uses this for every LPC function
// activation; VM::callClosure() additionally needs it around a
// closure's own core-efun invocation specifically (see callClosure()'s
// own comment) -- unlike the local/simul_efun-function branches, that
// path does not go through run() at all, so without this guard
// vm.currentObject()/previous_object() would still reflect whichever
// object's run() frame happens to be innermost (whoever called
// evaluate()), not the closure's own owner, breaking any efun that
// looks at "the current object" (save_object() being exactly the one
// that surfaced this live: secure/daemon/account_d.c's own "unguarded((:
// save_object, path :))" was saving master.c's own variables instead of
// account_d.c's -- account_d.c here is a since-discarded early scratch
// mudlib object used only for that live verification, not real vendored
// corpus content and not this driver's own real, shipped /single/
// account_d.c, which calls save_object() directly with no unguarded()
// closure hop at all, see notes/ACCOUNT_LOGIN_PLAN.md and STATUS.md's
// 2026-08-21 entry).
//
// Object-change detection mirrors real FRAME_OB_CHANGE: only push a new
// objectChangeStack_ entry when obj actually differs from the
// immediately enclosing frame, not on every same-object call.
class ObjectFrameGuard {
public:
    ObjectFrameGuard(std::vector<std::shared_ptr<amlp::LpcObject>>& callStack,
                      std::vector<std::shared_ptr<amlp::LpcObject>>& objectChangeStack,
                      const std::shared_ptr<amlp::LpcObject>& obj)
        : callStack_(callStack), objectChangeStack_(objectChangeStack) {
        objectChanged_ = callStack_.empty() || callStack_.back() != obj;
        if (objectChanged_) {
            objectChangeStack_.push_back(callStack_.empty() ? nullptr : callStack_.back());
        }
        callStack_.push_back(obj);
    }
    ~ObjectFrameGuard() {
        callStack_.pop_back();
        if (objectChanged_) objectChangeStack_.pop_back();
    }
    ObjectFrameGuard(const ObjectFrameGuard&) = delete;
    ObjectFrameGuard& operator=(const ObjectFrameGuard&) = delete;

private:
    std::vector<std::shared_ptr<amlp::LpcObject>>& callStack_;
    std::vector<std::shared_ptr<amlp::LpcObject>>& objectChangeStack_;
    bool objectChanged_ = false;
};

// RAII push/pop of VM's commandGiverStack_ (real save_command_giver()/
// restore_command_giver(), add_action.c), used around each leg of
// VM::moveObject()'s init()-calling sequence so a thrown exception (an
// init() body's own runtime error) still pops correctly rather than
// leaving a stale command_giver behind for whatever runs next.
class CommandGiverGuard {
public:
    CommandGiverGuard(amlp::VM& vm, const std::shared_ptr<amlp::LpcObject>& ob) : vm_(vm) {
        vm_.pushCommandGiver(ob);
    }
    ~CommandGiverGuard() { vm_.popCommandGiver(); }
    CommandGiverGuard(const CommandGiverGuard&) = delete;
    CommandGiverGuard& operator=(const CommandGiverGuard&) = delete;

private:
    amlp::VM& vm_;
};

// Real FluffOS's T_UNDEFINED is a *subtype* of T_NUMBER (a number whose
// value already is 0, just tagged specially so undefinedp() can detect
// it) -- not a separate value kind that arithmetic has to special-case.
// This driver's own monostate plays the same "no value" role (a missing
// mapping key, per Index's own comment, or a declared-but-unassigned
// object variable/local before this driver's LpcObject.cpp/VM.cpp own
// fix made those a real 0 directly), so it needs to participate in
// arithmetic exactly like a real 0 too, while remaining distinguishable
// from one via undefinedp()/nullp() specifically. Returns true and sets
// out to 0.0 for monostate, true and the numeric value for int64_t/
// double, false (leaving out untouched) for anything else. Surfaced
// live: std/living.c's own query_stats() doing "stats[stat] + x" where
// stats[stat] is a missing-key monostate for a fresh character whose
// stats mapping has not been rolled yet.
bool asArithmeticOperand(const amlp::Value& v, double& out) {
    if (auto* i = std::get_if<int64_t>(&v.data)) {
        out = static_cast<double>(*i);
        return true;
    }
    if (auto* d = std::get_if<double>(&v.data)) {
        out = *d;
        return true;
    }
    if (std::holds_alternative<std::monostate>(v.data)) {
        out = 0.0;
        return true;
    }
    return false;
}

// Formats an int64_t/double/monostate Value for string+number
// concatenation (OpCode::Add's own "string" +/- int/float branches),
// matching real interpret.c's F_ADD exactly: "%ld" for an int, "%f" for
// a float (C's default six decimal places, not a shortest round-trip
// representation). monostate (this driver's own missing-mapping-key/
// no-value encoding, see asArithmeticOperand's own comment) formats as
// plain "0", the same real-0 treatment asArithmeticOperand already
// gives it for numeric +/-/*. Caller guarantees v actually holds one of
// these three kinds.
std::string formatNumberForConcat(const amlp::Value& v) {
    if (auto* i = std::get_if<int64_t>(&v.data)) {
        return std::to_string(*i);
    }
    if (std::holds_alternative<std::monostate>(v.data)) {
        return "0";
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%f", std::get<double>(v.data));
    return std::string(buf);
}

// Resolves a bare function-call name against a program's own functions
// first, then depth-first against each program it inherits (which may
// itself inherit further -- see Bytecode.hpp's CompiledProgram comment).
// This is the run-time half of OpCode::Call; the compile-time half is
// CodeGen::emitCallExpr(), which never tries to decide locally-vs-
// inherited-vs-efun itself.
struct FunctionLookupResult {
    const CompiledProgram* program = nullptr;
    const FunctionEntry* fn = nullptr;
};

// Phase 2 row 2.9: memoized via CompiledProgram::functionChainCache_ --
// see that field's own long comment in Bytecode.hpp for the real,
// source-confirmed reasoning behind keying this per-program rather than
// per-object, and why no separate invalidation logic is needed anywhere.
// The recursive structure and its result (own functions first, then
// each inheritedPrograms entry depth-first, first match wins) are
// unchanged from before this row -- only the cache check/store wrapping
// each call is new.
FunctionLookupResult findFunctionInChain(const CompiledProgram& program, const std::string& name) {
    auto cached = program.functionChainCache_.find(name);
    if (cached != program.functionChainCache_.end()) {
        return FunctionLookupResult{cached->second.program, cached->second.fn};
    }

    FunctionLookupResult result{};
    for (const auto& fn : program.functions) {
        if (fn.name == name) {
            result = FunctionLookupResult{&program, &fn};
            break;
        }
    }
    if (!result.program) {
        for (const auto& parent : program.inheritedPrograms) {
            if (!parent) continue;
            FunctionLookupResult found = findFunctionInChain(*parent, name);
            if (found.program) {
                result = found;
                break;
            }
        }
    }

    program.functionChainCache_.emplace(
        name, CompiledProgram::FunctionChainCacheEntry{result.program, result.fn});
    return result;
}

// Run-time half of OpCode::CallParent (see Bytecode.hpp's own comment
// and Ast.hpp's CallExpr::parentCall): resolves "::name(...)"/
// "qualifier::name(...)", which must skip *this* program's own
// functions entirely and search only inherited ones, even if this
// program itself defines a same-named function (the entire point of the
// syntax -- e.g. an overridden create() explicitly calling its parent's
// create() too). Bare form (no qualifier) walks every immediate parent
// depth-first via the same findFunctionInChain() the plain Call opcode
// uses, just starting one level down; a qualifier restricts the search
// to the one immediate parent whose own "inherit" path's last path
// component matches it (e.g. "daemon::create()" for
// "inherit \"/std/daemon\";").
std::string pathBasename(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

FunctionLookupResult findParentFunction(const CompiledProgram& program, const std::string& name,
                                         const std::string* qualifier) {
    if (!qualifier) {
        for (const auto& parent : program.inheritedPrograms) {
            if (!parent) continue;
            FunctionLookupResult found = findFunctionInChain(*parent, name);
            if (found.program) return found;
        }
        return FunctionLookupResult{};
    }

    for (size_t i = 0; i < program.inherits.size() && i < program.inheritedPrograms.size(); ++i) {
        if (pathBasename(program.inherits[i]) != *qualifier) continue;
        const auto& parent = program.inheritedPrograms[i];
        if (!parent) continue;
        return findFunctionInChain(*parent, name);
    }
    return FunctionLookupResult{};
}

// Ports FluffOS's inter_sscanf() (interpret.c) for: literal text, "%%",
// "%s", "%d", "%x", "%f", the "%*" skip modifier (matches but does not
// consume an output slot), and "%s" directly adjacent to another
// specifier with no literal text between them (real inter_sscanf's own
// per-specifier lookahead, ported below in adjacentSscanfBoundary()).
// "%(regexp)" is deliberately still not implemented -- this mudlib's
// sscanf() calls never use it (confirmed by grep) -- and throws rather
// than silently mishandling it if some other file ever does.
struct SscanfOutcome {
    int64_t matchCount = 0;
    std::vector<Value> assigned; // one entry per *consumed* (non-skip) slot, in order
};

// Real interpret.c's own per-specifier lookahead for "%s" directly
// followed by another "%<spec>" with no literal text between them: since
// there is nothing literal to delimit where the %s match ends, the driver
// instead scans ahead in the input for where the *next* specifier's own
// pattern would plausibly start, and uses that as the %s boundary. Ported
// directly from inter_sscanf()'s own case-by-case scan loops (interpret.c,
// the "if (*fmt++ == '%')" block after the %s handling), not guessed --
// each case below mirrors one real loop exactly. `spec` is the character
// identifying the *next* specifier (its own optional leading "%*" skip
// flag, if any, has already been looked past by the caller -- it plays no
// part in finding the boundary, only in whether that next specifier's own
// value later gets assigned, which is handled by the normal top-of-loop
// specifier logic once control returns there).
size_t adjacentSscanfBoundary(char spec, const std::string& in0, size_t ip) {
    const size_t inLen = in0.size();
    size_t pos = ip;
    switch (spec) {
        case 'x':
            // Real loop: skip to the next '0', then check whether it
            // starts a real "0x"/"0X" + hex-digit sequence; if not,
            // skip past it and keep looking.
            while (pos < inLen) {
                while (pos < inLen && in0[pos] != '0') ++pos;
                if (pos >= inLen) break;
                char c1 = (pos + 1 < inLen) ? in0[pos + 1] : '\0';
                char c2 = (pos + 2 < inLen) ? in0[pos + 2] : '\0';
                if ((c1 == 'x' || c1 == 'X') &&
                    std::isxdigit(static_cast<unsigned char>(c2))) {
                    break;
                }
                pos += 2;
            }
            return pos < inLen ? pos : inLen;
        case 'd':
            while (pos < inLen && !std::isdigit(static_cast<unsigned char>(in0[pos]))) ++pos;
            return pos;
        case 'f':
            while (pos < inLen) {
                char c = in0[pos];
                if (std::isdigit(static_cast<unsigned char>(c))) break;
                if (c == '.' && pos + 1 < inLen &&
                    std::isdigit(static_cast<unsigned char>(in0[pos + 1]))) {
                    break;
                }
                ++pos;
            }
            return pos;
        case '%':
            while (pos < inLen && in0[pos] != '%') ++pos;
            return pos;
        case 's':
            // Real error text: "Illegal to have 2 adjacent %s's in
            // format string in sscanf()".
            throw LpcRuntimeError("sscanf: illegal to have two adjacent %s specifiers");
        case '(':
            throw NotImplementedError("sscanf: \"%s\" adjacent to \"%(regexp)\"");
        default:
            throw NotImplementedError(
                std::string("sscanf: \"%s\" adjacent to unsupported format specifier '%") +
                spec + "'");
    }
}

SscanfOutcome runSscanf(const std::string& in0, const std::string& fmt0, size_t maxAssigns) {
    SscanfOutcome out;
    size_t ip = 0, fp = 0;
    const size_t inLen = in0.size(), fmtLen = fmt0.size();

    for (;;) {
        // Match literal text up to the next '%' (or end of format).
        while (fp < fmtLen && fmt0[fp] != '%') {
            if (ip >= inLen || in0[ip] != fmt0[fp]) return out;
            ++ip; ++fp;
        }

        if (fp >= fmtLen) {
            // Format exhausted. Any leftover input becomes one final match
            // in the next output slot, if one is still available.
            if (ip < inLen && out.assigned.size() < maxAssigns) {
                out.assigned.emplace_back(Value(in0.substr(ip)));
                ++out.matchCount;
            }
            return out;
        }

        ++fp; // consume '%'
        if (fp < fmtLen && fmt0[fp] == '%') {
            // Literal "%%".
            if (ip >= inLen || in0[ip] != '%') return out;
            ++ip; ++fp;
            ++out.matchCount;
            continue;
        }
        if (fp >= fmtLen) {
            throw LpcRuntimeError("sscanf: format string cannot end in '%'");
        }

        bool skip = (fmt0[fp] == '*');
        if (skip) ++fp;
        if (fp >= fmtLen) {
            throw LpcRuntimeError("sscanf: format string cannot end in '%'");
        }
        char spec = fmt0[fp++];

        if (spec == 'd' || spec == 'x') {
            // Real inter_sscanf(): "case 'x': base = 16; /* fallthrough */
            // case 'd':" -- both go through the same strtol(), only the
            // base differs. std::strtoll(..., 16) accepts both a bare hex
            // digit sequence and an optional leading "0x"/"0X", matching
            // real strtol()'s own base-16 behavior.
            const char* start = in0.c_str() + ip;
            char* endPtr = nullptr;
            long long value = std::strtoll(start, &endPtr, spec == 'x' ? 16 : 10);
            if (endPtr == start) return out; // no digits matched
            ip += static_cast<size_t>(endPtr - start);
            if (!skip) {
                if (out.assigned.size() >= maxAssigns) {
                    throw LpcRuntimeError("sscanf: too few output arguments for format string");
                }
                out.assigned.emplace_back(Value(static_cast<int64_t>(value)));
            }
            ++out.matchCount;
            continue;
        }

        if (spec == 'f') {
            const char* start = in0.c_str() + ip;
            char* endPtr = nullptr;
            double value = std::strtod(start, &endPtr);
            if (endPtr == start) return out; // no float matched
            ip += static_cast<size_t>(endPtr - start);
            if (!skip) {
                if (out.assigned.size() >= maxAssigns) {
                    throw LpcRuntimeError("sscanf: too few output arguments for format string");
                }
                out.assigned.emplace_back(Value(value));
            }
            ++out.matchCount;
            continue;
        }

        if (spec != 's') {
            throw NotImplementedError(
                std::string("sscanf: format specifier '%") + spec +
                "' (only %s, %d, %x, %f, %% are supported)");
        }

        // %s. If the format is now exhausted, the rest of in_string is the
        // match (real inter_sscanf's "we have reached the end of the
        // format string" case).
        if (fp >= fmtLen) {
            if (!skip) {
                if (out.assigned.size() >= maxAssigns) {
                    throw LpcRuntimeError("sscanf: too few output arguments for format string");
                }
                out.assigned.emplace_back(Value(in0.substr(ip)));
            }
            ++out.matchCount;
            return out;
        }

        if (fmt0[fp] == '%') {
            // "%s" directly followed by another "%<spec>" with no literal
            // text in between -- real inter_sscanf()'s own lookahead case.
            // Identify the next specifier's own character (looking past
            // its optional "%*" skip flag, which plays no part in finding
            // the boundary -- see adjacentSscanfBoundary()'s own comment),
            // find where its pattern would start in the remaining input,
            // and use that as the end of this %s match. Deliberately does
            // NOT consume the next specifier here: fp is left pointing at
            // its own leading '%', so the top of this same loop processes
            // it as an entirely ordinary specifier on the next iteration,
            // starting at the boundary just computed -- observably
            // identical to real inter_sscanf() parsing both together
            // inline, without duplicating every specifier's own parsing
            // logic a second time here.
            size_t lookFp = fp + 1;
            if (lookFp < fmtLen && fmt0[lookFp] == '*') ++lookFp;
            char nextSpec = (lookFp < fmtLen) ? fmt0[lookFp] : '\0';
            size_t boundary = adjacentSscanfBoundary(nextSpec, in0, ip);

            if (!skip) {
                if (out.assigned.size() >= maxAssigns) {
                    throw LpcRuntimeError("sscanf: too few output arguments for format string");
                }
                out.assigned.emplace_back(Value(in0.substr(ip, boundary - ip)));
            }
            ip = boundary;
            ++out.matchCount;
            continue;
        }

        // %s terminated by literal text: find where that literal text
        // next occurs in the remaining input, and take everything before
        // it as the match.
        size_t delimStart = fp;
        while (fp < fmtLen && fmt0[fp] != '%') ++fp;
        std::string delim = fmt0.substr(delimStart, fp - delimStart);

        size_t found = in0.find(delim, ip);
        if (found == std::string::npos) return out;

        if (!skip) {
            if (out.assigned.size() >= maxAssigns) {
                throw LpcRuntimeError("sscanf: too few output arguments for format string");
            }
            out.assigned.emplace_back(Value(in0.substr(ip, found - ip)));
        }
        ip = found + delim.size();
        ++out.matchCount;
        // fp already sits at the '%' (or fmtLen) that starts the next
        // segment; the top of the loop picks up from there.
    }
}

} // namespace

VM::VM(ObjectManager& objects, Config& config)
    : objects_(objects), config_(config),
      maxEvalCost_(config.maxEvalCost()) {}

void VM::setMaxEvalCost(int64_t limit) {
    maxEvalCost_ = limit;
}

void VM::enqueueReplaceProgram(std::shared_ptr<LpcObject> ob,
                                std::shared_ptr<CompiledProgram> newProgram, int offset,
                                std::string name) {
    for (auto& pending : pendingReplacePrograms_) {
        if (pending.ob == ob) {
            pending.newProgram = std::move(newProgram);
            pending.offset = offset;
            pending.name = std::move(name);
            return;
        }
    }
    pendingReplacePrograms_.push_back({std::move(ob), std::move(newProgram), offset, std::move(name)});
}

void VM::processPendingReplacePrograms() {
    if (pendingReplacePrograms_.empty()) return;
    std::vector<PendingReplaceProgram> pending = std::move(pendingReplacePrograms_);
    pendingReplacePrograms_.clear();

    for (auto& entry : pending) {
        auto& ob = entry.ob;
        if (!ob || ob->isDestructed() || !entry.newProgram) continue;

        // real "num_fewer = ob->prog->num_variables_total -
        // new_prog->num_variables_total" plus the offset-based
        // variable-array shuffle (replace_program.c's own
        // replace_programs()) -- both of its real branches (offset != 0
        // vs offset == 0) reduce to the same "keep the [offset,
        // offset+newCount) slice, drop the rest" operation once offset
        // is allowed to be 0 (a no-op slice-from-the-front), so this is
        // one unified extraction rather than two separate loops. Falls
        // back to a plain truncation only if the offset is somehow out
        // of range for the object's current variable count (defensive;
        // a genuinely valid ancestor offset from
        // CompiledProgram::ancestorBaseOffsets never hits this).
        size_t newCount = entry.newProgram->objectVarNames.size();
        auto& vars = ob->variables();
        size_t off = entry.offset > 0 ? static_cast<size_t>(entry.offset) : 0;
        if (off + newCount <= vars.size()) {
            std::vector<Value> kept(vars.begin() + static_cast<long>(off),
                                     vars.begin() + static_cast<long>(off + newCount));
            vars = std::move(kept);
        } else {
            vars.resize(newCount);
        }

        // real "if (r_ob->ob->shadowing) { ... splice ob out of the
        // shadow chain ... }" -- only ob's own *outgoing* shadow
        // relationship is stopped, not any shadow relationship pointed
        // *at* ob (real code checks only ob->shadowing, never
        // ob->shadowed, on its own; see replace_program() efun's own
        // registration comment for why that asymmetry is kept exactly
        // as read rather than generalized to an unconditional
        // remove_shadow()-style splice).
        if (auto shadowingTarget = ob->shadowing().lock()) {
            auto myShadower = ob->shadowedBy().lock();
            shadowingTarget->setShadowedBy(myShadower);
            if (myShadower) {
                myShadower->setShadowing(shadowingTarget);
                ob->setShadowedBy(std::weak_ptr<LpcObject>());
            }
            ob->setShadowing(std::weak_ptr<LpcObject>());
        }

        ob->setProgram(std::move(entry.newProgram));

        // real "r_ob->ob->replaced_program = string_copy(r_ob->new_prog->
        // filename, ...)", backing query_replaced_program() -- real
        // add_slash() ensures a leading '/' on the stored name; matched
        // here rather than assuming entry.name already has one, since
        // the mudlib argument to replace_program() itself is not
        // required to include it (this driver's own normalizeFilename()
        // never adds one either).
        std::string stored = entry.name;
        if (stored.empty() || stored.front() != '/') stored.insert(stored.begin(), '/');
        ob->setReplacedProgramName(std::move(stored));
    }
}

Value VM::callFunction(const std::shared_ptr<LpcObject>& obj,
                        const std::string& functionName,
                        std::vector<Value> args,
                        Origin origin) {
    if (!obj) return Value{};

    // real apply()'s own "DEBUG_CHECK(ob->flags & O_DESTRUCTED, ...)"
    // gate (interpret.c): every "call into an object from outside" path
    // goes through this one function -- call_other, applyMaster(),
    // Scheduler's call_out()/heart_beat() firing (via a locked weak_ptr,
    // which can still succeed on a destructed-but-still-referenced
    // object), and moveObject()'s own init() propagation all share it --
    // so a single check here closes the "a destructed object still
    // responds to call_other()/keeps firing heart_beat()" class of bugs
    // everywhere at once, matching real semantics: a destructed target
    // silently does nothing, not an error.
    if (obj->isDestructed()) return Value{};

    // real apply_low()'s own "The function call will swap in the object
    // and also unset its reset status." (interpret.c, right above its own
    // "ob->flags &= ~O_RESET_STATE;", interpret.c:20319) plus "ob->
    // time_of_ref = current_time;" a few lines later (interpret.c:20345)
    // -- every real call into an object from outside marks it "touched",
    // both so Scheduler::tickResetsAndCleanup() does a real (not virtual)
    // reset next time one is due, and so it stays clean_up()-ineligible
    // for a fresh real time_to_cleanup window. Deliberately unconditional
    // here (this method's own single real "call into an object from
    // outside" entry point, per this method's own comment above), matching
    // real apply_low() being called for call_other, heart_beat/call_out
    // firing, and driver-hook dispatch alike -- not narrowed to exclude
    // any of those, the same way real code does not either.
    obj->setResetState(false);
    obj->touchTimeOfRef();

    // Shadow chain (Phase 0.6): real apply_low()'s own two-phase
    // mechanism (interpret.c), confirmed directly before implementing,
    // not assumed from instruct.md's own simplified "call the shadow,
    // check truthy, else fall through" description, which gets the real
    // condition wrong -- it is whether the function is *defined* on a
    // given link of the chain, never the truthiness of what it returns.
    //
    // Phase 1: walk to the outermost still-active shadow. Real "while
    // (ob->shadowed && ob->shadowed != current_object &&
    // !(ob->shadowed->flags & O_DESTRUCTED)) ob = ob->shadowed;" -- the
    // "!= current_object" guard is real and load-bearing: without it, a
    // shadow's own function calling back into its victim (e.g. via
    // call_other to reach the real, unshadowed implementation) would
    // immediately re-enter itself instead, since it IS current_object at
    // that point.
    std::shared_ptr<LpcObject> target = obj;
    {
        auto caller = currentObject();
        auto shadow = target->shadowedBy().lock();
        while (shadow && shadow != caller && !shadow->isDestructed()) {
            target = shadow;
            shadow = target->shadowedBy().lock();
        }
    }

    // Phase 2: search target's own inherit chain (same resolution
    // external entry points always use, see the comment below); if the
    // function is not *defined* there and target itself shadows
    // something further in (a multi-shadow chain), retry one step
    // toward the base victim -- real "goto retry_for_shadow" after the
    // "if (ob->shadowing) { ob = ob->shadowing; ... }" check. This
    // terminates at the original, unshadowed obj once shadowing() is
    // unset, exactly matching real semantics: the base object's own
    // program is always the last one tried.
    //
    // External entry points (call_other, ObjectManager's create() call,
    // applyMaster()) must resolve inherited-but-not-locally-overridden
    // functions the same way a bare in-file call does, or calling an
    // object that only picked up a function via "inherit" (extremely
    // common in this mudlib, e.g. every DAEMON-inheriting command) would
    // silently do nothing instead of running it. Unlike OpCode::Call's
    // resolution, this deliberately does not fall back to the efun table
    // -- call_other("some/object", "sizeof") calling the sizeof() efun on
    // an unrelated object would not be a call_other at all.
    FunctionLookupResult found;
    for (;;) {
        found = findFunctionInChain(target->program(), functionName);
        if (found.program) break;
        auto next = target->shadowing().lock();
        if (!next) break;
        target = next;
    }
    if (!found.program) return Value{};
    // See this method's own VM.hpp doc comment for why origin defaults
    // to Origin::Driver and which real callers pass something else.
    // Applies to whichever function actually gets run above -- shadow
    // resolution changes *which object's own function* runs, never
    // *why* this whole call happened in the first place, so the origin
    // this call was made with is what the target's own frame gets,
    // unconditionally.
    OriginGuard originGuard(*this, origin);
    return run(*found.program, *found.fn, std::move(args), target);
}

Value VM::callFunctionInProgram(const std::shared_ptr<LpcObject>& obj, const CompiledProgram& program,
                                 const std::string& functionName, std::vector<Value> args) {
    if (!obj) return Value{};
    for (const auto& fn : program.functions) {
        if (fn.name == functionName) {
            // Real call___INIT()'s own "caller_type = ORIGIN_DRIVER;"
            // (interpret.c), confirmed directly -- this method's only
            // real caller is ObjectManager::runObjectVarInitializers()'s
            // own per-inherit-level "$objvarinit" dispatch, the exact
            // same real mechanism, so this is hardcoded rather than a
            // parameter: there is no other real call site that would
            // ever need a different origin here.
            OriginGuard originGuard(*this, Origin::Driver);
            return run(program, fn, std::move(args), obj);
        }
    }
    return Value{};
}

bool VM::functionExists(const std::shared_ptr<LpcObject>& obj, const std::string& functionName) const {
    if (!obj || obj->isDestructed()) return false;
    return findFunctionInChain(obj->program(), functionName).program != nullptr;
}

Value VM::applyMaster(const std::string& applyName, std::vector<Value> args) {
    auto master = objects_.masterObject();
    if (!master) {
        throw LpcRuntimeError("applyMaster(" + applyName + "): master object not loaded");
    }
    return callFunction(master, applyName, std::move(args));
}

std::shared_ptr<LpcObject> VM::cloneObject(const std::string& filename) {
    return objects_.cloneObject(filename);
}

void VM::destructObject(const std::shared_ptr<LpcObject>& obj,
                         const std::function<void(const std::shared_ptr<LpcObject>&)>& onDestructed) {
    objects_.destructObject(obj, onDestructed);
}

void VM::reloadObject(const std::shared_ptr<LpcObject>& obj,
                       const std::function<void(const std::shared_ptr<LpcObject>&)>& onDestructed) {
    objects_.reloadObject(obj, onDestructed);
}

std::shared_ptr<LpcObject> VM::masterObject() const {
    return objects_.masterObject();
}

std::shared_ptr<LpcObject> VM::simulEfunObject() const {
    return objects_.simulEfunObject();
}

bool VM::privilegeViolation(const std::string& what, std::vector<Value> args) {
    // real "if (get_current_object() == master_ob) return MY_TRUE; if
    // (get_current_object() == simul_efun_object) return MY_TRUE;"
    // (interpret.c:8552-8553 and identical in the other three real
    // wrappers) -- checked before anything else, no apply at all.
    auto caller = currentObject();
    auto master = masterObject();
    if (caller && (caller == master || caller == simulEfunObject())) return true;

    // real "!svp" branch (interpret.c:8570): no privilege_violation()
    // lfun on the master at all is a hard error, same as a genuine
    // violation, not a silent grant or a silent deny. See this method's
    // own VM.hpp comment for why this is checked explicitly rather than
    // trusting applyMaster()'s own Value{} return, which cannot tell
    // "missing lfun" apart from "lfun returned 0" on its own.
    if (!master || !functionExists(master, "privilege_violation")) {
        throw LpcRuntimeError("privilege violation: " + what);
    }

    std::vector<Value> callArgs;
    callArgs.reserve(args.size() + 2);
    callArgs.emplace_back(what);
    callArgs.emplace_back(caller);
    for (auto& a : args) callArgs.push_back(std::move(a));

    Value result = applyMaster("privilege_violation", std::move(callArgs));

    // real "svp->type != T_NUMBER || svp->u.number < 0" branch
    // (interpret.c:8570): wrong return type or a negative number is also
    // a hard error, exactly like a missing lfun -- only a real T_NUMBER
    // >= 0 return is a legitimate answer.
    if (!std::holds_alternative<int64_t>(result.data)) {
        throw LpcRuntimeError("privilege violation: " + what);
    }
    int64_t n = std::get<int64_t>(result.data);
    if (n < 0) {
        throw LpcRuntimeError("privilege violation: " + what);
    }
    // real "return svp->u.number > 0;" -- exactly 0 is a real, valid
    // "gently denied" answer, not an error.
    return n > 0;
}

std::shared_ptr<LpcObject> VM::findObject(const std::string& filename) const {
    // See VM.hpp's own comment: real find_object() compiles+loads on a
    // miss, which is exactly ObjectManager::loadObject()'s existing
    // cache-by-filename behavior (also used for the master and
    // simul_efun objects at boot).
    return objects_.loadObject(filename);
}

std::shared_ptr<LpcObject> VM::lookupObject(const std::string& filename) const {
    return objects_.lookupLoadedObject(filename);
}

std::shared_ptr<LpcObject> VM::currentObject() const {
    return callStack_.empty() ? nullptr : callStack_.back();
}

std::shared_ptr<LpcObject> VM::previousObject(int idx) const {
    if (idx < 0 || static_cast<size_t>(idx) >= objectChangeStack_.size()) return nullptr;
    return objectChangeStack_[objectChangeStack_.size() - 1 - static_cast<size_t>(idx)];
}

std::vector<std::shared_ptr<LpcObject>> VM::allPreviousObjects() const {
    std::vector<std::shared_ptr<LpcObject>> result;
    for (auto it = objectChangeStack_.rbegin(); it != objectChangeStack_.rend(); ++it) {
        if (*it) result.push_back(*it);
    }
    return result;
}

std::shared_ptr<LpcObject> VM::commandGiver() const {
    return commandGiverStack_.empty() ? nullptr : commandGiverStack_.back();
}

void VM::pushCommandGiver(const std::shared_ptr<LpcObject>& ob) {
    commandGiverStack_.push_back(ob);
}

void VM::popCommandGiver() {
    if (!commandGiverStack_.empty()) commandGiverStack_.pop_back();
}

std::string VM::currentVerb() const {
    return verbStack_.empty() ? std::string() : verbStack_.back();
}

// See VM.hpp's own comment for the overall contract. The lazy-
// resolve-by-name simplification here (versus real FluffOS's
// FP_LOCAL/FP_SIMUL/FP_EFUN classification baked in at the "(: :)"
// literal's own construction time) is safe for this driver's current
// scope specifically because every closure actually reachable in this
// mudlib is built and called within the same short-lived scope --
// e.g. "unguarded((: file_size, p :))" constructs the closure and
// hands it straight to unguarded(), which calls it immediately via
// evaluate(); nothing stores one in an object variable, redefines the
// named function in between, and calls it later expecting the
// original binding to have survived. If that ever changes, this
// comment is the place to revisit it.
Value VM::callClosure(const std::shared_ptr<Closure>& closure, std::vector<Value> extraArgs) {
    if (!closure) {
        throw LpcRuntimeError("evaluate(): not a function value");
    }

    // LDMud unbound_lambda() (ROADMAP.md row 1.7/1.8): real
    // int_call_lambda() (interpret.c:21313) errors "Uncallable closure"
    // (interpret.c:21818-21819) for a still-CLOSURE_UNBOUND_LAMBDA
    // closure called with no bind_ob -- checked here, before the
    // ordinary destructed-owner check just below, because an unbound
    // closure's owner being unset is its normal, expected state (real
    // f_unbound_lambda(): "l->base.ob = const0"), not the "was bound
    // then the object got destructed" case that check exists to catch.
    // See EfunTable.cpp's own bind_lambda() registration for the one
    // real way unboundUntilBound ever flips false.
    if (closure->unboundUntilBound) {
        throw LpcRuntimeError("Uncallable closure");
    }

    auto owner = closure->owner.lock();
    // Previously only checked whether the weak_ptr had actually expired
    // (the owner's last shared_ptr reference dropped) -- a real gap for
    // an owner that was explicitly destruct()ed but is still kept alive
    // by some other reference (e.g. sitting in an array/mapping/another
    // object's variable), which is not at all an unusual thing for
    // destruct() to leave behind. isDestructed() catches that case too.
    if (!owner || owner->isDestructed()) {
        throw LpcRuntimeError("evaluate(): owner of function pointer is destructed");
    }

    std::vector<Value> args;
    args.reserve(closure->boundArgs.size() + extraArgs.size());
    for (const auto& a : closure->boundArgs) args.push_back(a);
    for (auto& a : extraArgs) args.push_back(std::move(a));

    // LDMud unbound_lambda() closure, now bound (ROADMAP.md row 1.7/1.8):
    // real int_call_lambda()'s own CLOSURE_UNBOUND_LAMBDA/CLOSURE_
    // BOUND_LAMBDA cases both fall into running the lambda's own compiled
    // program (interpret.c:21551-21622); this driver never compiles one
    // to bytecode at all (see Value.hpp's Closure::lambdaBody comment),
    // so it is walked directly here instead. Checked after the owner/
    // destructed checks above (an ordinary closure's own owner.lock()
    // never engages this branch since lambdaBody stays void for every
    // other closure kind), and before every tiered-resolution branch
    // below, none of which apply to it.
    if (!closure->lambdaBody.isVoid()) {
        return callUnboundLambdaBody(*closure, std::move(args));
    }

    // "#'efun::name" (Closure::forceEfun) -- real LDMud semantics skip
    // straight to the core efun table, deliberately bypassing this
    // object's own lfun/inherited and simul_efun tiers even if either
    // happens to define a same-named function that would otherwise
    // shadow it (temp/ldmud/doc/LPC/closures's own real citation: "closure
    // to an efun"). Checked before either tiered lookup below, not after,
    // so a same-named lfun/simul_efun never gets a chance to win first.
    if (closure->forceEfun) {
        if (!EfunTable::instance().exists(closure->functionName)) {
            throw LpcRuntimeError("evaluate(): undefined efun: " + closure->functionName);
        }
        ObjectFrameGuard objectFrameGuard(callStack_, objectChangeStack_, owner);
        return EfunTable::instance().call(closure->functionName, *this, args);
    }

    FunctionLookupResult found = findFunctionInChain(owner->program(), closure->functionName);
    if (found.program) {
        // Real call_function_pointer()'s own real split (function.c):
        // "case FP_LOCAL | FP_NOT_BINDABLE: ... caller_type = ORIGIN_LOCAL;"
        // for an ordinary named local/inherited target, vs "case
        // FP_FUNCTIONAL: ... caller_type = ORIGIN_FUNCTIONAL;" for an
        // anonymous inline function -- confirmed directly, not assumed
        // from "it's a closure, so FunctionPointer" (real
        // ORIGIN_FUNCTION_POINTER is only ever the transient fake-frame
        // value setup_fake_frame() sets moments earlier, always
        // overwritten by one of these two before any genuine LPC bytecode
        // actually runs). Distinguished here by real function.c's own
        // synthesized-lambda naming convention (isSynthesizedLambdaName()
        // above), the same "$"-prefixed-name signal CodeGen.cpp already
        // uses to keep these apart at compile time.
        OriginGuard originGuard(
            *this, isSynthesizedLambdaName(found.fn->name) ? Origin::Functional : Origin::Local);
        return run(*found.program, *found.fn, std::move(args), owner);
    }

    auto simulEfun = objects_.simulEfunObject();
    if (simulEfun) {
        FunctionLookupResult simulFound = findFunctionInChain(simulEfun->program(), closure->functionName);
        if (simulFound.program) {
            // Real "case FP_SIMUL: call_simul_efun(...)" (function.c),
            // which itself sets ORIGIN_SIMUL_EFUN via call_direct() --
            // confirmed directly, same real citation OpCode::Call's own
            // tier-3 branch above already uses.
            OriginGuard originGuard(*this, Origin::SimulEfun);
            return run(*simulFound.program, *simulFound.fn, std::move(args), simulEfun);
        }
    }

    if (EfunTable::instance().exists(closure->functionName)) {
        // Unlike the local/simul_efun branches above, calling a core
        // efun does not recurse into run() at all, so without this
        // guard vm.currentObject() would still read whatever object's
        // run() frame is innermost (whoever called evaluate()) instead
        // of this closure's own owner -- see ObjectFrameGuard's own
        // comment, this is exactly the live bug it fixes.
        //
        // Deliberately no OriginGuard here either: real "case FP_EFUN"
        // (function.c) never overrides caller_type before running the
        // raw efun body -- it stays whatever setup_fake_frame() left it
        // as (transiently ORIGIN_FUNCTION_POINTER), a value the efun's
        // own C code never observes since efuns cannot call origin() on
        // themselves. This driver has no fake-frame mechanism to mirror
        // that transient set at all, and nothing could tell the
        // difference either way -- leaving originStack_ untouched here
        // is behaviorally identical to real semantics, just without the
        // unobservable intermediate step.
        ObjectFrameGuard objectFrameGuard(callStack_, objectChangeStack_, owner);
        return EfunTable::instance().call(closure->functionName, *this, args);
    }

    throw LpcRuntimeError("evaluate(): undefined function or efun: " + closure->functionName);
}

// LDMud unbound_lambda() (ROADMAP.md row 1.7/1.8). Binds this closure's
// own declared parameter symbols (lambdaParams, from unbound_lambda()'s
// first argument) positionally to argValues, then evaluates lambdaBody
// (the second argument, real LDMud's own quoted-code call tree) against
// that binding -- real f_unbound_lambda()'s own comment: "The first
// argument is an array describing the arguments (symbols) passed to the
// closure upon evaluation by funcall() or apply(), the second arg forms
// the code of the closure." (closure.c:6907-6909). Extra call-time
// arguments beyond lambdaParams' own count are silently dropped and a
// short call is padded with void, matching this driver's own existing
// "missing/extra args" convention for an ordinary function call (real
// LPC's own default-0-for-missing-arg rule) rather than erroring.
Value VM::callUnboundLambdaBody(const Closure& closure, std::vector<Value> argValues) {
    argValues.resize(closure.lambdaParams.size());
    return evalQuotedLambdaNode(closure.lambdaBody, closure.lambdaParams, argValues);
}

// Walks one node of a real LDMud quoted-code lambda body (closure.c's
// own lambda(), the C function f_unbound_lambda() calls to compile the
// array-of-arrays "LISP-style" quoted code real LPC's lambda()/
// unbound_lambda() efuns take as their body argument -- LDMud doc/LPC/
// closures's own description). Real lambda() supports a genuinely large
// grammar: operator closures as a call's own head (#'+, #'?, ...),
// control-flow closures (#'if, #'while, #'foreach, ...), quoted
// aggregates, symbols referring to the bound object's own global
// variables in addition to the lambda's own declared parameters, and
// more. This driver implements none of that -- only the one real shape
// confirmed live in this mudlib's own corpus, secure/master/hooks.c's
// four unbound_lambda() driver-hook bodies (still unreachable end to end
// without set_driver_hook(), itself still unimplemented -- see
// ROADMAP.md row 1.7/1.8's own note): a plain closure-headed call
// (real LDMud's F_CLOSURE-headed quoted expression, the ordinary case
// lambda() compiles to a straightforward CALL bytecode), each argument
// either a nested call of the same shape or a bare 'name symbol
// referencing one of this same lambda's own declared parameters, and a
// bare literal (int/string/...) standing for itself. Anything past that
// -- an operator/control-flow closure as the head, a symbol that is not
// one of the declared parameters -- honestly errors rather than
// silently misevaluating or guessing at a real LDMud semantic this
// driver does not actually implement.
Value VM::evalQuotedLambdaNode(const Value& node, const std::vector<std::string>& params,
                                const std::vector<Value>& argValues) {
    if (auto* sym = std::get_if<Symbol>(&node.data)) {
        for (size_t i = 0; i < params.size(); ++i) {
            if (params[i] == sym->name) return argValues[i];
        }
        throw LpcRuntimeError(
            "unbound_lambda: symbol '" + sym->name + " is not one of this lambda's own "
            "declared parameters (global-variable symbol references are not supported)");
    }
    if (auto* arr = std::get_if<std::shared_ptr<Array>>(&node.data)) {
        if (!*arr || (*arr)->items.empty()) {
            throw LpcRuntimeError("unbound_lambda: empty quoted-code call expression");
        }
        auto* headClosure = std::get_if<std::shared_ptr<Closure>>(&(*arr)->items[0].data);
        if (!headClosure || !*headClosure) {
            throw LpcRuntimeError(
                "unbound_lambda: quoted-code call expression must start with a closure "
                "(operator/control-flow closures as the call head are not supported)");
        }
        std::vector<Value> callArgs;
        callArgs.reserve((*arr)->items.size() - 1);
        for (size_t i = 1; i < (*arr)->items.size(); ++i) {
            callArgs.push_back(evalQuotedLambdaNode((*arr)->items[i], params, argValues));
        }
        return callClosure(*headClosure, std::move(callArgs));
    }
    // A plain literal (int, string, ...) inside quoted code stands for
    // itself -- real lambda()'s own "constant" node kind.
    return node;
}

std::string VM::resolveMudlibPath(const std::string& lpcPath) const {
    // Real LPC file paths are mudlib-root-relative whether or not the
    // string itself carries a leading '/' -- the same real convention
    // already fixed for inherit targets (ObjectManager::normalizeFilename())
    // and deep_inherit_list()'s own output (EfunTable.cpp), and found
    // live a third time against the same real TMI-2 corpus (row 3.8's
    // boot attempt): adm/daemons/ga_server.c's own real "#define
    // GLOBAL_ALIASES \"adm/etc/global_aliases\"" (no leading slash) fed
    // straight into "read_file(GLOBAL_ALIASES)". Naively concatenating
    // config_.mudlibRoot() + lpcPath without this produced a
    // real, silent, missing-separator path ("...lib" + "adm/etc/..." =
    // "...libadm/etc/..."), so read_file() always returned falsy for a
    // relative-form path -- confirmed live: ga_server's own create()
    // then failed with "explode: expected (string, string) arguments"
    // (explode()'s own first argument was the falsy read_file() result,
    // not a string), cascading into "call_other() couldn't find object"
    // for every command std/user.c's own do_xverb() routes through it.
    std::string normalized = lpcPath;
    if (normalized.empty() || normalized[0] != '/') {
        normalized.insert(normalized.begin(), '/');
    }
    return config_.mudlibRoot() + normalized;
}

const std::string& VM::mudName() const {
    return config_.mudName();
}

// LDMud driver_hook (ROADMAP.md row 1.7/1.8). Range validation matches
// real f_set_driver_hook()'s own "Bad hook number" errorf() exactly
// (simulate.c:5082-5088); the real privilege_violation("set_driver_hook",
// this_object(), what) authorization gate (simulate.c:5091, function
// header comment at 5070 -- confirmed the real call only ever passes the
// hook *number* as extra data, not the value being set, despite
// doc/master/privilege_violation's own "arg2" text; that doc page is
// stale here, same as its now-dead "enable_telnet"/"set_limits" entries)
// is real as of 2026-08-20 (ROADMAP.md row 1.7/1.8's own
// privilege_violation() scoping investigation), wired at the
// EfunTable.cpp call site via VM::privilegeViolation() rather than here,
// matching that helper's own placement next to applyMaster(). Range
// validation (this method) still runs first, exactly like real code's
// own ordering (simulate.c:5082 before :5091). What is still deliberately
// not replicated -- real per-hook type validation against
// hook_type_map[] (prolang.y:195-229), and the special "take ownership
// of an unbound lambda even for a hook whose type map doesn't otherwise
// allow closures, immediately rebinding it to master_ob" case
// (simulate.c:5189-5203) -- real per-hook type mismatches simply
// misbehave at the point of actual dispatch instead of being rejected up
// front (the same permissive-storage precedent m_values() used before
// column validation existed), and the special eager-rebind-to-master
// case is a pure optimization in real LDMud -- H_MOVE_OBJECT0's own real
// trigger (object.c's move_object(), see moveObject()'s own comment
// below) unconditionally rebinds to current_object on every single call
// regardless of what it was bound to at set_driver_hook() time, so
// skipping the eager bind here changes no observable behavior.
// Registered unconditionally in EfunTable.cpp, not gated on dialect,
// matching this table's own established convention (unshadow()'s own
// comment: efun availability is never withheld by dialect here) --
// real FluffOS has no set_driver_hook() efun at all (confirmed by grep,
// zero hits in the vendored fluffos-2.9-ds2.08 tree), so there is no
// real FluffOS behavior this privilege gate could ever conflict with.
Value VM::getDriverHook(int what) const {
    if (what < 0 || what >= kNumDriverHooks) return Value{};
    return driverHooks_[static_cast<size_t>(what)];
}

void VM::setDriverHook(int what, Value arg) {
    if (what < 0 || what >= kNumDriverHooks) {
        throw LpcRuntimeError(
            "Bad hook number: " + std::to_string(what) + ", expected 0.." +
            std::to_string(kNumDriverHooks - 1));
    }
    driverHooks_[static_cast<size_t>(what)] = std::move(arg);
}

// real interpret.h's own "#define call_lambda(lsvp, num_arg)
// int_call_lambda(lsvp, num_arg, true, NULL)" / "#define
// call_lambda_ob(lsvp, num_arg, ob) int_call_lambda(lsvp, num_arg, true,
// ob)" -- both funnel into the same real int_call_lambda(), whose own
// CLOSURE_UNBOUND_LAMBDA case (interpret.c:21551-21561) either uses the
// supplied bind_ob to rebind on the fly (call_lambda_ob's own path,
// real object.c's own determine_uid()/give_uid_to_object(), the real
// mechanism behind H_LOAD_UIDS/H_CLONE_UIDS) or -- for call_lambda's own
// NULL-bind_ob callers like object.c's move_object() -- relies on the
// caller having already mutated the closure's own base.ob field
// directly first (real move_object(): "assign_current_object(&(l->base.
// ob), ...)" for H_MOVE_OBJECT0, "put_ref_object(&(l->base.ob), inter_
// sp[-1].u.ob, ...)" for H_MOVE_OBJECT1). Both real mechanisms have the
// exact same observable effect -- the closure's own home object is
// freshly overwritten immediately before each call -- so this driver
// unifies them into one helper that always mutates owner in place
// (matching real semantics: driver_hook's own stored lambda_t is a
// single shared struct, not copied per call) and then reuses
// callClosure()'s own existing dispatch (tiered resolution or the
// quoted-code lambdaBody walk, whichever this closure actually is) --
// safe to call regardless of whether this closure started out
// unboundUntilBound, since owner is set and unboundUntilBound cleared
// before callClosure() ever sees it.
Value VM::callDriverHookClosure(const std::shared_ptr<Closure>& closure,
                                 const std::shared_ptr<LpcObject>& bindTo,
                                 std::vector<Value> args) {
    if (!closure) {
        throw LpcRuntimeError("driver hook: not a function value");
    }
    closure->owner = bindTo;
    closure->unboundUntilBound = false;
    return callClosure(closure, std::move(args));
}

// See VM.hpp's own comment. Implements two of real setup_new_commands()'s
// (add_action.c) three visitation legs -- the ones this mudlib's own
// confirmed real usage needs (the destination handing its own verbs to
// the mover, and already-present command-enabled occupants exchanging
// init() calls with the mover) -- and skips the third (dest itself being
// command-enabled, i.e. moving into another living object's own
// inventory rather than a room): "rare" per the reference source's own
// comment, and not reachable by anything this mudlib's own confirmed
// boot/movement path does (every real move() call site moves a living
// into a room, never into another living).
//
// Also simplified versus the reference source in one more way, flagged
// rather than silently assumed safe: real setup_new_commands() rechecks
// "if (item->super != dest) return;" after every single apply(), because
// an init() body is free to move `item` again before returning (its own
// comment: "Beware that init() in the room may have moved 'item' !").
// This does not re-check that -- the occupant loop below iterates a
// snapshot of dest's inventory taken before any init() runs, so it is
// safe against the list itself changing size, but an init() that calls
// move_object() on `item` mid-loop will still finish running the rest of
// this function against the *old* dest/item relationship. No real init()
// on this mudlib's confirmed path (Object.c, room/exits.c, room/
// senses.c, living.c's own init_living()) calls move()/move_object() at
// all, so this has not been reachable to verify against real behavior.
void VM::moveObject(const std::shared_ptr<LpcObject>& item, const std::shared_ptr<LpcObject>& dest) {
    // A destructed item/destination is never a valid move -- without
    // this, an already-destructed-but-still-referenced object could be
    // relinked back into a live room's inventory, undoing the unlink
    // ObjectManager::destructObject() just did. Kept ahead of the
    // driver-hook dispatch just below too (real object.c's own
    // move_object() has no equivalent guard of its own -- every real
    // safety check for this lives inside the mudlib-supplied hook
    // closure itself, e.g. hooks.c's own moveHook()'s "objectp(item) &&
    // objectp(destination)" -- but skipping it here would let a hook
    // closure, faithfully ported or not, relink an already-destructed
    // object the same way the no-hook path below is guarded against).
    if (!item || !dest || item == dest || item->isDestructed() || dest->isDestructed()) return;

    // LDMud H_MOVE_OBJECT0/H_MOVE_OBJECT1 (real object.c:3920-3948's own
    // move_object() static function, the shared implementation behind
    // both the move_object() and transfer() efuns): H_MOVE_OBJECT1
    // checked first, bound to item; only if unset does H_MOVE_OBJECT0
    // apply, bound to current_object -- both real, distinct bind
    // targets, not a guess (object.c:3934-3943). Confirmed live
    // (testSetDriverHookH_MOVE_OBJECT0DispatchesThroughRealMoveObjectEfun)
    // that this current_object rebind, while real and faithfully ported
    // here, is not actually observable through real hooks.c's own exact
    // H_MOVE_OBJECT0 body shape: "#'moveHook" inside the quoted-code
    // body is its own separately-compiled CLOSURE_LFUN closure,
    // permanently bound to whichever object it was written in (the
    // master object, in hooks.c's own case) regardless of who calls the
    // wrapping unbound_lambda -- real interpret.c's own CLOSURE_LFUN
    // case sets current_object from *that* closure's own base.ob, not
    // the wrapper's, before running moveHook()'s own body. The rebind
    // still matters for a hook body that runs its own inline code
    // directly instead of dispatching to a separately-bound sub-
    // closure, so it stays here regardless. Real LDMud has no
    // built-in move logic at all past this point ("errorf(\"Don't know
    // how to move objects.\\n\")") -- the hardcoded legs below are this
    // driver's own pre-existing multi-dialect fallback, kept exactly as
    // it was for every dialect (and every LDMud mudlib that has not
    // itself called set_driver_hook()) rather than hard-erroring the
    // way real LDMud would, a deliberate divergence flagged here rather
    // than silently made.
    constexpr int kHMoveObject0 = 0;
    constexpr int kHMoveObject1 = 1;
    // Named locals, not a temporary bound straight into std::get_if --
    // getDriverHook() returns Value by value, and an unnamed temporary's
    // lifetime would end right after the get_if() call that inspects
    // it, leaving the pointer dangling by the time it is dereferenced
    // below.
    Value hook1Val = getDriverHook(kHMoveObject1);
    if (auto* hook1 = std::get_if<std::shared_ptr<Closure>>(&hook1Val.data)) {
        if (*hook1) {
            callDriverHookClosure(*hook1, item, {Value(item), Value(dest)});
            return;
        }
    }
    Value hook0Val = getDriverHook(kHMoveObject0);
    if (auto* hook0 = std::get_if<std::shared_ptr<Closure>>(&hook0Val.data)) {
        if (*hook0) {
            callDriverHookClosure(*hook0, currentObject(), {Value(item), Value(dest)});
            return;
        }
    }

    // real object.c:5188-5198's own three O_RESET_STATE clears (dest,
    // item, item's old super) -- see EfunTable.cpp's own set_environment()
    // registration for the exact same real citation; reproduced here too
    // since this leg is this driver's own hardcoded fallback move (no
    // hook installed), not a call through set_environment() itself.
    dest->setResetState(false);
    if (auto oldEnv = item->environment().lock()) {
        auto& oldInv = oldEnv->inventory();
        oldInv.erase(std::remove(oldInv.begin(), oldInv.end(), item), oldInv.end());
        oldEnv->setResetState(false);
    }
    item->setResetState(false);
    item->setEnvironment(dest);
    dest->inventory().push_back(item);

    // Leg 1: dest's own init() hands dest's actions to item (command_giver
    // = item) -- e.g. a room's exits.c/senses.c registering movement and
    // search verbs onto the player who just walked in.
    if (item->commandsEnabled()) {
        CommandGiverGuard guard(*this, item);
        callFunction(dest, "init", {});
    }

    // Leg 2: every other object already present exchanges init() calls
    // with the mover, in the same order real setup_new_commands() uses
    // (an occupant's init() reaches item first, then item's own init()
    // reaches the occupant) -- matters for which entry ends up more
    // recently added, and therefore checked first at dispatch time.
    // Snapshotting dest's inventory here (not iterating it live) avoids
    // undefined iterator behavior if an init() call below moves anything
    // else in or out of dest.
    std::vector<std::shared_ptr<LpcObject>> occupants = dest->inventory();
    for (auto& ob : occupants) {
        if (ob == item) continue;
        if (ob->commandsEnabled()) {
            CommandGiverGuard guard(*this, ob);
            callFunction(item, "init", {});
        }
        if (item->commandsEnabled()) {
            CommandGiverGuard guard(*this, item);
            callFunction(ob, "init", {});
        }
    }
}

namespace {
// Splits a typed line into its first whitespace-delimited word (the
// verb, real query_verb()'s raw material) and the remainder (the
// argument string every add_action-registered function receives, real
// LPC convention -- whitespace immediately following the verb is
// consumed, not left as a leading space in the argument).
// arg is std::nullopt when there is genuinely nothing after the verb
// (a bare single-word command) -- distinct from a present-but-empty
// string, matching real user_parser()'s own "push_undefined()" branch
// (add_action.c) exactly (see dispatchCommand()'s own comment on why
// this distinction is load-bearing, not cosmetic).
std::pair<std::string, std::optional<std::string>> splitVerbAndArg(const std::string& line) {
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) return {std::string(), std::nullopt};
    size_t verbEnd = line.find_first_of(" \t", start);
    if (verbEnd == std::string::npos) return {line.substr(start), std::nullopt};
    std::string verb = line.substr(start, verbEnd - start);
    size_t argStart = line.find_first_not_of(" \t", verbEnd);
    if (argStart == std::string::npos) return {verb, std::nullopt};
    return {verb, line.substr(argStart)};
}
} // namespace

// See VM.hpp's own comment. real parse_command()/user_parser()
// (add_action.c): walk giver's action table (built incrementally by
// moveObject() above, not rebuilt here -- matches real semantics, the
// table persists across commands until the next move) and call the
// first matching handler that returns truthy, trying further matches if
// one returns falsy.
bool VM::dispatchCommand(const std::shared_ptr<LpcObject>& giver, const std::string& line) {
    if (!giver || giver->isDestructed()) return false;

    // LDMud H_MODIFY_COMMAND (ROADMAP.md row 1.7/1.8's own hooks.c stock-
    // take: real hooks.c's own actual configured value for this hook is
    // a plain mapping of single-letter/two-letter direction abbreviations
    // to their full verb -- "([ \"e\": \"east\", \"w\": \"west\", ... ])"
    // -- picked as this session's real build over the other still-open
    // hook trigger points specifically because it is the one with an
    // everyday, moment-to-moment gameplay impact (players typing "n"
    // instead of "north") rather than object-creation-time bookkeeping.
    // Real trigger point: actions.c's own call_modify_command()
    // (actions.c:514-611), called exactly once from parse_command()
    // (actions.c:792), before verb/arg splitting, with no re-check of
    // the (possibly now-rewritten) line afterward -- real semantics only
    // strip *trailing* spaces first (actions.c:777-785, "for (p = buff +
    // strlen(buff) - 1; ...)", leading whitespace is deliberately left
    // alone), then look the *entire* trimmed line up as one mapping key
    // (find_tabled_str(buff, ...), actions.c:576) -- not just the first
    // word -- meaning a bare "n" matches but "n foo" does not, exactly
    // matching real direction commands' own no-argument shape. Only the
    // real T_MAPPING form is implemented here, matching hooks.c's own
    // actual real usage precisely -- the T_CLOSURE and T_STRING hook
    // forms (a mudlib-supplied rewrite function) and the separate
    // per-interactive-object override (H_MODIFY_COMMAND_FNAME/
    // set_modify_command()) are real but have zero confirmed real corpus
    // usage, honestly left unimplemented rather than guessed at.
    std::string effectiveLine = line;
    {
        constexpr int kHModifyCommand = 9;
        Value hookVal = getDriverHook(kHModifyCommand);
        if (auto* map = std::get_if<std::shared_ptr<Mapping>>(&hookVal.data)) {
            if (*map) {
                size_t end = line.find_last_not_of(' ');
                std::string trimmed = (end == std::string::npos) ? std::string() : line.substr(0, end + 1);
                if (!trimmed.empty()) {
                    for (const auto& entry : (*map)->entries) {
                        if (auto* key = std::get_if<std::string>(&entry.first.data)) {
                            if (*key == trimmed) {
                                if (auto* replacement = std::get_if<std::string>(&entry.second.data)) {
                                    effectiveLine = *replacement;
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    auto [verb, arg] = splitVerbAndArg(effectiveLine);
    if (verb.empty()) return false;

    // Snapshot: a handler is free to call add_action()/remove_action()
    // on itself (this mudlib's own do_sit-style one-shot actions do),
    // which would otherwise mutate giver->actions() out from under a
    // live iteration.
    std::vector<LpcObject::ActionEntry> actions = giver->actions();
    for (const auto& entry : actions) {
        bool matches;
        if (entry.flag == 0) {
            matches = (entry.verb == verb);
        } else {
            // V_SHORT (1) / V_NOSPACE (2): entry.verb only has to be a
            // leading-characters prefix of the typed verb -- real
            // semantics, and an empty entry.verb (living.c's own
            // catch-all "add_action(\"cmd_hook\", \"\", 1)") trivially
            // matches every typed verb, since every string starts with
            // the empty prefix.
            matches = verb.size() >= entry.verb.size() &&
                verb.compare(0, entry.verb.size(), entry.verb) == 0;
        }
        if (!matches) continue;

        auto owner = entry.owner.lock();
        // real: an action whose owner died (or was explicitly
        // destructed but is still referenced elsewhere) is skipped, not
        // an error.
        if (!owner || owner->isDestructed()) continue;

        // real user_parser() (add_action.c): the argument construction is
        // NOT relative to the matched entry's own verb length for either
        // V_SHORT or the plain exact-match case -- only V_NOSPACE (flag
        // 2) reslices relative to entry.verb; both other cases push
        // whatever came after the *typed line's own first word*, or real
        // "push_undefined()" (this driver's Value{} monostate) when there
        // was nothing there at all ("buff[length] == ' '" is false).
        // Confirmed by reading user_parser() directly: a bare one-word
        // command ("look", no trailing text) must reach its handler with
        // an *undefined* argument, never an empty string -- found live
        // root-causing why "look" (and every other bare command) reached
        // cmd_hook()/cmd_look() but still silently declined: cmd_look(str)
        // checks "if(stringp(str))" first, and an empty string passes
        // that check (stringp("") is true), routing into examine_object("")
        // instead of the no-argument "this_player()->
        // describe_current_room(1)" branch -- confirmed the actual reason
        // a real room's own "look" command produced nothing at all,
        // independent of and discovered after the Scheduler work this
        // slice was actually about (see STATUS.md's own account of the
        // live-testing trail that found this).
        Value handlerArg;
        if (entry.flag == 2 && !entry.verb.empty()) {
            // V_NOSPACE: still always a real string (possibly empty),
            // matching real "copy_and_push_string(&buff[strlen(s->verb)])"
            // -- no undefined case for this flag. No real call site in
            // this mudlib uses flag 2 (every real catch-all use found is
            // flag 1), so this remains unverified live the way the flag-1
            // path now is.
            std::string rest = verb.substr(entry.verb.size());
            handlerArg = Value(arg ? (rest.empty() ? *arg : rest + " " + *arg) : rest);
        } else if (arg) {
            handlerArg = Value(*arg);
        }
        // else: handlerArg stays default-constructed Value{} (monostate),
        // matching real push_undefined() for a bare verb with nothing
        // after it.

        // real query_verb() always returns the full typed verb, not the
        // matched prefix -- even for a V_SHORT/V_NOSPACE partial match.
        verbStack_.push_back(verb);
        CommandGiverGuard giverGuard(*this, giver);
        Value result;
        try {
            // Real add_action.c's own user_parser(): "where = (current_object
            // ? ORIGIN_EFUN : ORIGIN_DRIVER);", confirmed directly, with its
            // own comment explaining why -- "If this is called directly
            // from user input, then the origin is the driver and it will
            // be allowed" (reaching even a static/protected handler),
            // whereas a *nested* re-dispatch (this driver's own command()
            // efun, called from within an already-running function, the
            // only real way current_object ends up set at this exact
            // point) is the narrower ORIGIN_EFUN. currentObject() here is
            // exactly real current_object's own truthiness check: null at
            // the true top-level entry (Server::dispatchLine(), no LPC
            // frame active yet), set only when command() called back in.
            Origin origin = currentObject() ? Origin::Efun : Origin::Driver;
            result = callFunction(owner, entry.functionName, {handlerArg}, origin);
        } catch (...) {
            verbStack_.pop_back();
            throw;
        }
        verbStack_.pop_back();

        if (isTruthy(result)) return true;
    }
    return false;
}

Value VM::run(const CompiledProgram& program, const FunctionEntry& fn,
              std::vector<Value> args, const std::shared_ptr<LpcObject>& obj) {
    // Do NOT reset evalCost_ here. Real FluffOS accumulates instruction
    // cost across all nested run()/apply() calls within one top-level
    // dispatch; only Server::dispatchLine() and Scheduler::tick*() reset
    // it, matching process_user_command()/call_heart_beat()/call_call_out()
    // in the reference driver (interpret.c, backend.c, call_out.c).

    // Tracks real FluffOS's current_object for the duration of this one
    // LPC function activation (see VM.hpp's currentObject() comment).
    // RAII rather than an explicit pop before every return: run() has
    // several return points plus exception unwinding (a rethrown
    // LpcRuntimeError with no active catch frame, or EvalCostError,
    // both propagate straight out of the while loop below), and a
    // destructor is the only pop that reliably covers all of them. See
    // ObjectFrameGuard's own comment for the real-semantics citation.
    ObjectFrameGuard objectFrameGuard(callStack_, objectChangeStack_, obj);

    // Base offset to add to this program's own PushObjectVar/
    // StoreObjectVar slot numbers before indexing obj->variables() (see
    // Bytecode.hpp's CompiledProgram::ancestorBaseOffsets comment for the
    // full citation). `program` is whichever specific file's bytecode is
    // executing -- possibly several inherit levels below obj->program(),
    // e.g. a leaf mixin like std/user/nmsh.c running as part of a
    // std/user.c object -- while obj->variables() is always sized to
    // obj->program()'s own fully-flattened total. `program`'s own slot
    // numbers are relative only to its own direct inherit chain (correct
    // when it runs standalone); the fast path (0 offset) covers a
    // function belonging to obj->program() itself, which already uses
    // absolute slots with no adjustment needed.
    int objectVarBase = 0;
    if (&program != &obj->program()) {
        auto offsetIt = obj->program().ancestorBaseOffsets.find(&program);
        if (offsetIt == obj->program().ancestorBaseOffsets.end()) {
            throw LpcRuntimeError(
                "internal error: no object-variable base offset recorded for an inherited program");
        }
        objectVarBase = offsetIt->second;
    }

    // Real int64_t 0 per slot (not monostate -- see LpcObject.cpp's own
    // comment on variables_'s identical initialization for the
    // citation), tagged undefined under FluffOS to match real const0u
    // (main.c/interpret.c's setup_variables()'s own push_undefineds()) --
    // see Value.hpp's own isUndefined/makeUndefinedNumber() comments.
    // Real LDMud has no equivalent, so this stays a plain 0 under every
    // other dialect. The args loop below overwrites whichever slots are
    // actually parameters immediately after anyway.
    Value defaultLocal = (config().dialect() == "fluffos") ? makeUndefinedNumber() : Value(int64_t{0});
    std::vector<Value> locals(fn.numLocals, defaultLocal);
    if (fn.isVarargs && fn.numArgs > 0) {
        // Real interpret.c:1394-1410's own setup_varargs_variables(): the
        // last declared parameter (slot numArgs-1) is always bound to a
        // real array holding every actual argument at or past that
        // position -- a real empty array when the caller supplied too
        // few arguments to reach it (real "arr = &the_null_array;" at
        // :1405), not undefined/0, and a single-element array when
        // exactly one argument lands there (real "n = actual - num_arg +
        // 1", so n=1 when actual == num_arg), not that one value bare.
        size_t restSlot = static_cast<size_t>(fn.numArgs) - 1;
        for (size_t i = 0; i < restSlot && i < args.size() && i < locals.size(); ++i) {
            locals[i] = std::move(args[i]);
        }
        auto restArr = std::make_shared<Array>();
        if (args.size() > restSlot) {
            restArr->items.assign(std::make_move_iterator(args.begin() + static_cast<long>(restSlot)),
                                   std::make_move_iterator(args.end()));
        }
        if (restSlot < locals.size()) {
            locals[restSlot] = Value(restArr);
        }
    } else {
        for (size_t i = 0; i < args.size() && i < locals.size(); ++i) {
            locals[i] = std::move(args[i]);
        }
    }

    std::vector<Value> localStack;
    // Real interpret.c:75/2694's own global num_varargs, kept local to
    // this one VM::run() call instead -- see Bytecode.hpp's
    // OpCode::ExpandVarargs comment for why a member would need explicit
    // save/restore around nested calls that a local does not.
    int64_t pendingVarargsDelta = 0;
    size_t ip = fn.entryPoint;

    // catch(expr) support (see Ast.hpp's CatchExpr and Bytecode.hpp's
    // PushCatchFrame/PopCatchFrame comments). One stack per run() call
    // (i.e. per LPC function invocation), not a VM-wide member: a
    // function with no catch() of its own has an empty stack here and
    // any error simply propagates out of this call via the rethrow
    // below, exactly like today, which is also how a catch() in a
    // *caller* still traps an error thrown deep inside a *callee* that
    // has no catch() of its own -- the callee's own run() call finds its
    // own catchFrames empty, rethrows, and the resulting C++ exception
    // unwinds straight out of that nested run() call (see OpCode::Call
    // below) back into this function's own try/catch, which does have
    // an active frame. .back()/.pop_back() naturally gives innermost-
    // first behavior for catch() nested within one function body too.
    struct CatchFrame {
        size_t resumeIp;
        size_t stackDepth;
    };
    std::vector<CatchFrame> catchFrames;

    while (ip < program.code.size()) {
      try {
        const Instruction& instr = program.code[ip];
        ++evalCost_;
        if (evalCost_ > maxEvalCost_) {
            // Not LpcRuntimeError on purpose -- see EvalCostError's own
            // comment, this must not be catchable by catch().
            throw EvalCostError("eval cost exceeded");
        }

        switch (instr.op) {
            case OpCode::PushConst: {
                if (instr.operand < 0 ||
                    static_cast<size_t>(instr.operand) >= program.stringPool.size()) {
                    throw LpcRuntimeError("PushConst: bad string pool index");
                }
                localStack.emplace_back(Value(program.stringPool[instr.operand]));
                ++ip;
                break;
            }

            case OpCode::PushInt: {
                localStack.emplace_back(Value(static_cast<int64_t>(instr.operand)));
                ++ip;
                break;
            }

            case OpCode::PushFloat: {
                if (instr.operand < 0 ||
                    static_cast<size_t>(instr.operand) >= program.floatPool.size()) {
                    throw LpcRuntimeError("PushFloat: bad float pool index");
                }
                localStack.emplace_back(Value(program.floatPool[instr.operand]));
                ++ip;
                break;
            }

            case OpCode::PushNil: {
                localStack.emplace_back(Value(Nil{}));
                ++ip;
                break;
            }

            case OpCode::PushSymbol: {
                if (instr.operand < 0 ||
                    static_cast<size_t>(instr.operand) >= program.stringPool.size()) {
                    throw LpcRuntimeError("PushSymbol: bad symbol name index");
                }
                localStack.emplace_back(Value(Symbol{program.stringPool[instr.operand]}));
                ++ip;
                break;
            }

            case OpCode::PushLocal: {
                if (instr.operand < 0 || static_cast<size_t>(instr.operand) >= locals.size()) {
                    throw LpcRuntimeError("PushLocal: bad local slot index");
                }
                coerceIfDestructed(locals[instr.operand]);
                localStack.push_back(locals[instr.operand]);
                ++ip;
                break;
            }

            case OpCode::StoreLocal: {
                if (instr.operand < 0 || static_cast<size_t>(instr.operand) >= locals.size()) {
                    throw LpcRuntimeError("StoreLocal: bad local slot index");
                }
                if (localStack.empty()) {
                    throw LpcRuntimeError("StoreLocal: stack underflow");
                }
                locals[instr.operand] = localStack.back();
                localStack.pop_back();
                ++ip;
                break;
            }

            case OpCode::PushObjectVar: {
                auto& vars = obj->variables();
                int64_t slot = static_cast<int64_t>(objectVarBase) + instr.operand;
                if (slot < 0 || static_cast<size_t>(slot) >= vars.size()) {
                    throw LpcRuntimeError("PushObjectVar: bad object variable slot index");
                }
                coerceIfDestructed(vars[static_cast<size_t>(slot)]);
                localStack.push_back(vars[static_cast<size_t>(slot)]);
                ++ip;
                break;
            }

            case OpCode::StoreObjectVar: {
                auto& vars = obj->variables();
                int64_t slot = static_cast<int64_t>(objectVarBase) + instr.operand;
                if (slot < 0 || static_cast<size_t>(slot) >= vars.size()) {
                    throw LpcRuntimeError("StoreObjectVar: bad object variable slot index");
                }
                if (localStack.empty()) {
                    throw LpcRuntimeError("StoreObjectVar: stack underflow");
                }
                vars[static_cast<size_t>(slot)] = localStack.back();
                localStack.pop_back();
                ++ip;
                break;
            }

            case OpCode::Eq:
            case OpCode::Neq: {
                if (localStack.size() < 2) {
                    throw LpcRuntimeError("Eq/Neq: stack underflow");
                }
                Value rhs = localStack.back(); localStack.pop_back();
                Value lhs = localStack.back(); localStack.pop_back();
                bool eq = valuesEqual(lhs, rhs);
                bool result = (instr.op == OpCode::Eq) ? eq : !eq;
                localStack.emplace_back(Value(static_cast<int64_t>(result ? 1 : 0)));
                ++ip;
                break;
            }

            case OpCode::Lt:
            case OpCode::Lte:
            case OpCode::Gt:
            case OpCode::Gte: {
                if (localStack.size() < 2) {
                    throw LpcRuntimeError("comparison: stack underflow");
                }
                Value rhs = localStack.back(); localStack.pop_back();
                Value lhs = localStack.back(); localStack.pop_back();

                // asArithmeticOperand (not a bare int64_t/double check) so
                // monostate -- this driver's own missing-mapping-key/no-
                // value encoding -- compares as a real 0, consistent with
                // the same treatment it already gets in +/-/* (see that
                // function's own comment). Found live: secure/daemon/
                // player.c's own sort_list(), "alpha[\"experience\"] >
                // beta[\"experience\"]" -- a brand new character's own
                // freshly-built mapping entry can have "experience" as a
                // real int (query_exp() itself returns 0, not void), but
                // the general case of comparing a possibly-missing
                // mapping key must not throw where real LPC (which has
                // no such distinction at the value level -- a missing
                // key is simply int 0) would happily compare.
                double lv, rv;
                if (!asArithmeticOperand(lhs, lv)) {
                    throw LpcRuntimeError("comparison: left operand is not numeric");
                }
                if (!asArithmeticOperand(rhs, rv)) {
                    throw LpcRuntimeError("comparison: right operand is not numeric");
                }

                bool result = false;
                switch (instr.op) {
                    case OpCode::Lt:  result = lv < rv; break;
                    case OpCode::Lte: result = lv <= rv; break;
                    case OpCode::Gt:  result = lv > rv; break;
                    case OpCode::Gte: result = lv >= rv; break;
                    default: break;
                }
                localStack.emplace_back(Value(static_cast<int64_t>(result ? 1 : 0)));
                ++ip;
                break;
            }

            case OpCode::Not: {
                if (localStack.empty()) {
                    throw LpcRuntimeError("Not: stack underflow");
                }
                Value v = localStack.back(); localStack.pop_back();
                localStack.emplace_back(Value(static_cast<int64_t>(isTruthy(v) ? 0 : 1)));
                ++ip;
                break;
            }

            case OpCode::Sub:
            case OpCode::Mul:
            case OpCode::Div:
            case OpCode::Mod: {
                if (localStack.size() < 2) {
                    throw LpcRuntimeError("arithmetic: stack underflow");
                }
                Value rhs = localStack.back(); localStack.pop_back();
                Value lhs = localStack.back(); localStack.pop_back();

                // real LPC's "arr1 - arr2": set difference, not numeric
                // subtraction -- every element of arr1 that also occurs
                // anywhere in arr2 (by value equality) is dropped, order
                // and any non-matched duplicates preserved (confirmed
                // against every real LPC driver's documented array "-"
                // operator; grammar.y gives "-" the same F_SUBTRACT
                // opcode regardless of operand type, dispatched on type
                // at runtime the same way this driver's own Add opcode
                // already special-cases string/array/mapping before its
                // shared numeric path). Surfaced live: std/user.c's own
                // register_channels() doing "channels - __RestrictedChannels".
                if (instr.op == OpCode::Sub &&
                    std::holds_alternative<std::shared_ptr<Array>>(lhs.data) &&
                    std::holds_alternative<std::shared_ptr<Array>>(rhs.data)) {
                    auto leftArr = std::get<std::shared_ptr<Array>>(lhs.data);
                    auto rightArr = std::get<std::shared_ptr<Array>>(rhs.data);
                    auto result = std::make_shared<Array>();
                    if (leftArr) {
                        for (const auto& item : leftArr->items) {
                            bool excluded = false;
                            if (rightArr) {
                                for (const auto& other : rightArr->items) {
                                    if (valuesEqual(item, other)) {
                                        excluded = true;
                                        break;
                                    }
                                }
                            }
                            if (!excluded) result->items.push_back(item);
                        }
                    }
                    localStack.emplace_back(Value(result));
                    ++ip;
                    break;
                }

                bool eitherDouble = std::holds_alternative<double>(lhs.data) ||
                                     std::holds_alternative<double>(rhs.data);

                double lv, rv;
                if (!asArithmeticOperand(lhs, lv)) {
                    throw LpcRuntimeError("arithmetic: left operand is not numeric");
                }
                if (!asArithmeticOperand(rhs, rv)) {
                    throw LpcRuntimeError("arithmetic: right operand is not numeric");
                }

                if ((instr.op == OpCode::Div || instr.op == OpCode::Mod) && rv == 0.0) {
                    throw LpcRuntimeError(instr.op == OpCode::Div
                        ? "Div: division by zero"
                        : "Mod: modulo by zero");
                }

                double result = 0.0;
                switch (instr.op) {
                    case OpCode::Sub: result = lv - rv; break;
                    case OpCode::Mul: result = lv * rv; break;
                    case OpCode::Div: result = lv / rv; break;
                    case OpCode::Mod: result = std::fmod(lv, rv); break;
                    default: break;
                }

                if (eitherDouble) {
                    localStack.emplace_back(Value(result));
                } else {
                    localStack.emplace_back(Value(static_cast<int64_t>(result)));
                }
                ++ip;
                break;
            }

            case OpCode::BitAnd: {
                if (localStack.size() < 2) {
                    throw LpcRuntimeError("BitAnd: stack underflow");
                }
                Value rhs = localStack.back(); localStack.pop_back();
                Value lhs = localStack.back(); localStack.pop_back();

                if (std::holds_alternative<std::shared_ptr<Array>>(lhs.data) &&
                    std::holds_alternative<std::shared_ptr<Array>>(rhs.data)) {
                    auto leftArr = std::get<std::shared_ptr<Array>>(lhs.data);
                    auto rightArr = std::get<std::shared_ptr<Array>>(rhs.data);
                    auto result = std::make_shared<Array>();
                    // Set intersection: every element of the left array
                    // that also occurs (by LPC value equality) anywhere in
                    // the right array, preserving the left array's order
                    // and duplicate count. This is a simplified stand-in
                    // for FluffOS's intersect_array() (array.c), which
                    // additionally sorts and de-duplicates its result --
                    // not replicated here since nothing this driver
                    // currently runs depends on that exact ordering, only
                    // on membership (e.g. master.c's
                    // "sizeof(privs & ok)").
                    if (leftArr && rightArr) {
                        for (const auto& item : leftArr->items) {
                            bool found = false;
                            for (const auto& other : rightArr->items) {
                                if (valuesEqual(item, other)) { found = true; break; }
                            }
                            if (found) result->items.push_back(item);
                        }
                    }
                    localStack.emplace_back(Value(result));
                } else if (std::holds_alternative<int64_t>(lhs.data) &&
                           std::holds_alternative<int64_t>(rhs.data)) {
                    int64_t result = std::get<int64_t>(lhs.data) & std::get<int64_t>(rhs.data);
                    localStack.emplace_back(Value(result));
                } else {
                    throw LpcRuntimeError("BitAnd: operands must both be ints or both be arrays");
                }
                ++ip;
                break;
            }

            case OpCode::BitOr:
            case OpCode::BitXor: {
                if (localStack.size() < 2) {
                    throw LpcRuntimeError("BitOr/BitXor: stack underflow");
                }
                Value rhs = localStack.back(); localStack.pop_back();
                Value lhs = localStack.back(); localStack.pop_back();

                if (!std::holds_alternative<int64_t>(lhs.data) ||
                    !std::holds_alternative<int64_t>(rhs.data)) {
                    throw LpcRuntimeError(
                        std::string(instr.op == OpCode::BitOr ? "BitOr" : "BitXor") +
                        ": operands must both be ints (array union is not implemented this slice)");
                }
                int64_t result = (instr.op == OpCode::BitOr)
                    ? (std::get<int64_t>(lhs.data) | std::get<int64_t>(rhs.data))
                    : (std::get<int64_t>(lhs.data) ^ std::get<int64_t>(rhs.data));
                localStack.emplace_back(Value(result));
                ++ip;
                break;
            }

            // Real C-family bitwise left/right shift, int-only (real
            // eoperators.c's own f_lsh()/f_rsh(), real trees.c's own
            // constant-folding case: "case F_LSH: l->v.number <<=
            // r->v.number; break;" -- a plain, unguarded C shift on the
            // real underlying "long", no extra bounds/overflow handling
            // of its own beyond what the host C compiler already does,
            // matched here exactly rather than adding validation real
            // FluffOS itself does not have). Found live against a real
            // third-party mudlib corpus (Dead Souls 3.8.2's own boot
            // attempt): secure/daemon/master.c's own real "((1 << 10) |
            // (1 << 0))" flag-combining idiom -- see Lexer.cpp's own
            // citation for the full corpus count.
            case OpCode::Shl:
            case OpCode::Shr: {
                if (localStack.size() < 2) {
                    throw LpcRuntimeError("Shl/Shr: stack underflow");
                }
                Value rhs = localStack.back(); localStack.pop_back();
                Value lhs = localStack.back(); localStack.pop_back();

                if (!std::holds_alternative<int64_t>(lhs.data) ||
                    !std::holds_alternative<int64_t>(rhs.data)) {
                    throw LpcRuntimeError(
                        std::string(instr.op == OpCode::Shl ? "Shl" : "Shr") +
                        ": operands must both be ints");
                }
                int64_t result = (instr.op == OpCode::Shl)
                    ? (std::get<int64_t>(lhs.data) << std::get<int64_t>(rhs.data))
                    : (std::get<int64_t>(lhs.data) >> std::get<int64_t>(rhs.data));
                localStack.emplace_back(Value(result));
                ++ip;
                break;
            }

            // Real interpret.c's own F_COMPL: "if (sp->type != T_NUMBER)
            // error(\"Bad argument to ~\n\"); sp->u.number =
            // ~sp->u.number;" -- a direct C bitwise complement, int-only.
            case OpCode::BitNot: {
                if (localStack.empty()) {
                    throw LpcRuntimeError("BitNot: stack underflow");
                }
                Value v = localStack.back(); localStack.pop_back();
                if (!std::holds_alternative<int64_t>(v.data)) {
                    throw LpcRuntimeError("Bad argument to ~");
                }
                localStack.emplace_back(Value(~std::get<int64_t>(v.data)));
                ++ip;
                break;
            }

            case OpCode::ForeachKeys: {
                if (localStack.empty()) {
                    throw LpcRuntimeError("ForeachKeys: stack underflow");
                }
                Value v = localStack.back(); localStack.pop_back();
                if (std::holds_alternative<std::shared_ptr<Array>>(v.data)) {
                    localStack.push_back(v);
                } else if (auto* map = std::get_if<std::shared_ptr<Mapping>>(&v.data)) {
                    auto keysArr = std::make_shared<Array>();
                    if (*map) {
                        for (const auto& entry : (*map)->entries) {
                            keysArr->items.push_back(entry.first);
                        }
                    }
                    localStack.emplace_back(Value(keysArr));
                } else {
                    throw LpcRuntimeError("foreach: collection must be an array or mapping");
                }
                ++ip;
                break;
            }

            case OpCode::Jump: {
                if (instr.operand < 0 || static_cast<size_t>(instr.operand) > program.code.size()) {
                    throw LpcRuntimeError("Jump: bad target");
                }
                ip = static_cast<size_t>(instr.operand);
                break;
            }

            case OpCode::JumpIfFalse: {
                if (localStack.empty()) {
                    throw LpcRuntimeError("JumpIfFalse: stack underflow");
                }
                Value cond = localStack.back();
                localStack.pop_back();
                if (!isTruthy(cond)) {
                    if (instr.operand < 0 || static_cast<size_t>(instr.operand) > program.code.size()) {
                        throw LpcRuntimeError("JumpIfFalse: bad target");
                    }
                    ip = static_cast<size_t>(instr.operand);
                } else {
                    ++ip;
                }
                break;
            }

            case OpCode::PushCatchFrame: {
                if (instr.operand < 0 || static_cast<size_t>(instr.operand) > program.code.size()) {
                    throw LpcRuntimeError("PushCatchFrame: bad resume target");
                }
                catchFrames.push_back(CatchFrame{
                    static_cast<size_t>(instr.operand), localStack.size()});
                ++ip;
                break;
            }

            case OpCode::PopCatchFrame: {
                // Only reached on normal completion of the guarded
                // region -- see this opcode's own Bytecode.hpp comment.
                // CodeGen always emits a matching PushCatchFrame before
                // any PopCatchFrame, so an empty stack here would be a
                // codegen bug, not a real runtime condition to recover
                // from.
                if (catchFrames.empty()) {
                    throw LpcRuntimeError("PopCatchFrame: no active catch frame");
                }
                catchFrames.pop_back();
                localStack.emplace_back(Value(static_cast<int64_t>(0)));
                ++ip;
                break;
            }

            // See VM.hpp's timeExpressionStack_ comment and Bytecode.hpp's
            // own TimeExpressionStart/TimeExpressionEnd comment for the
            // full runtime picture.
            case OpCode::TimeExpressionStart: {
                timeExpressionStack_.push_back(std::chrono::steady_clock::now());
                ++ip;
                break;
            }

            case OpCode::TimeExpressionEnd: {
                // CodeGen always emits a matching TimeExpressionStart
                // before any TimeExpressionEnd, so an empty stack here
                // would be a codegen bug, not a real runtime condition.
                if (timeExpressionStack_.empty()) {
                    throw LpcRuntimeError("TimeExpressionEnd: no active time_expression");
                }
                auto start = timeExpressionStack_.back();
                timeExpressionStack_.pop_back();
                auto elapsedUsec = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count();
                localStack.emplace_back(Value(static_cast<int64_t>(elapsedUsec)));
                ++ip;
                break;
            }

            case OpCode::Add: {
                if (localStack.size() < 2) {
                    throw LpcRuntimeError("Add: stack underflow");
                }
                Value rhs = localStack.back(); localStack.pop_back();
                Value lhs = localStack.back(); localStack.pop_back();

                if (std::holds_alternative<std::string>(lhs.data) &&
                    std::holds_alternative<std::string>(rhs.data)) {
                    localStack.emplace_back(
                        Value(std::get<std::string>(lhs.data) + std::get<std::string>(rhs.data)));
                } else if (std::holds_alternative<std::string>(lhs.data) &&
                           (std::holds_alternative<int64_t>(rhs.data) ||
                            std::holds_alternative<double>(rhs.data) ||
                            std::holds_alternative<std::monostate>(rhs.data))) {
                    // "string" + int/float -- confirmed against real
                    // interpret.c's F_ADD (case T_STRING's own nested
                    // T_NUMBER/T_REAL branches): the number is formatted
                    // as text ("%ld"/"%f") and appended, not a type
                    // error. Found live: daemon/terminal.c's own ESC(p)
                    // macro (`sprintf("%c"+(p), 27)`) is called with a
                    // bare int argument in several of its own table
                    // entries (ESC(7), ESC(8)), building a format string
                    // by string+int concatenation exactly like this.
                    // monostate accepted here too, formatting as "0" --
                    // the same missing-mapping-key value asArithmeticOperand
                    // already treats as a real 0 for numeric +/-/*, so a
                    // string+monostate concatenation must agree rather
                    // than throw. Found live: std/money.c's own
                    // query_money(), "return money[str];", called before
                    // any currency has ever been added to a fresh
                    // character's money mapping -- std/user.c's own
                    // setup() logs "... + query_money(\"platinum\") + \" pl,
                    // \" + ...".
                    localStack.emplace_back(Value(
                        std::get<std::string>(lhs.data) + formatNumberForConcat(rhs)));
                } else if ((std::holds_alternative<int64_t>(lhs.data) ||
                            std::holds_alternative<double>(lhs.data) ||
                            std::holds_alternative<std::monostate>(lhs.data)) &&
                           std::holds_alternative<std::string>(rhs.data)) {
                    // int/float + "string" -- the symmetric case (same
                    // interpret.c F_ADD, case T_NUMBER/T_REAL's own
                    // nested T_STRING branch): the number is formatted
                    // and prepended.
                    localStack.emplace_back(Value(
                        formatNumberForConcat(lhs) + std::get<std::string>(rhs.data)));
                } else if (std::holds_alternative<std::shared_ptr<LpcObject>>(lhs.data) &&
                           std::holds_alternative<std::string>(rhs.data)) {
                    // object + "string" -- interpret.c's F_ADD, case
                    // T_STRING's own T_OBJECT branch: the object's own
                    // filename (real obname, "/"-prefixed) is prepended.
                    // This driver's LpcObject::filename() already stores
                    // the leading slash (see file_name() efun's own
                    // comment), so no extra "/" is added here.
                    auto ob = std::get<std::shared_ptr<LpcObject>>(lhs.data);
                    localStack.emplace_back(Value(
                        (ob ? ob->filename() : std::string()) + std::get<std::string>(rhs.data)));
                } else if (std::holds_alternative<std::string>(lhs.data) &&
                           std::holds_alternative<std::shared_ptr<LpcObject>>(rhs.data)) {
                    // "string" + object -- interpret.c's F_ADD, case
                    // T_OBJECT's own T_STRING branch: the object's own
                    // filename is appended.
                    auto ob = std::get<std::shared_ptr<LpcObject>>(rhs.data);
                    localStack.emplace_back(Value(
                        std::get<std::string>(lhs.data) + (ob ? ob->filename() : std::string())));
                } else if (std::holds_alternative<std::shared_ptr<Array>>(lhs.data) &&
                           std::holds_alternative<std::shared_ptr<Array>>(rhs.data)) {
                    auto leftArr = std::get<std::shared_ptr<Array>>(lhs.data);
                    auto rightArr = std::get<std::shared_ptr<Array>>(rhs.data);
                    auto result = std::make_shared<Array>();
                    if (leftArr) {
                        result->items.insert(result->items.end(), leftArr->items.begin(), leftArr->items.end());
                    }
                    if (rightArr) {
                        result->items.insert(result->items.end(), rightArr->items.begin(), rightArr->items.end());
                    }
                    localStack.emplace_back(Value(result));
                } else if (double lv, rv; asArithmeticOperand(lhs, lv) && asArithmeticOperand(rhs, rv)) {
                    bool eitherDouble = std::holds_alternative<double>(lhs.data) ||
                                        std::holds_alternative<double>(rhs.data);
                    if (eitherDouble) {
                        localStack.emplace_back(Value(lv + rv));
                    } else {
                        localStack.emplace_back(Value(static_cast<int64_t>(lv + rv)));
                    }
                } else if (std::holds_alternative<std::shared_ptr<Mapping>>(lhs.data) &&
                           std::holds_alternative<std::shared_ptr<Mapping>>(rhs.data)) {
                    // real LPC "m1 + m2": union of both mappings, keys
                    // from m2 winning on conflict (grammar.y/mapping.c's
                    // own add_mapping() semantics -- confirmed by
                    // reference source, matches every real LPC driver's
                    // documented "+" on two mappings).
                    auto leftMap = std::get<std::shared_ptr<Mapping>>(lhs.data);
                    auto rightMap = std::get<std::shared_ptr<Mapping>>(rhs.data);
                    auto result = std::make_shared<Mapping>();
                    int leftW = leftMap ? leftMap->width : 1;
                    int rightW = rightMap ? rightMap->width : 1;
                    if (leftMap && rightMap && leftW != rightW) {
                        // real doc/LPC/mappings: "Joining mappings is only
                        // possible, if they have the same width"
                        throw LpcRuntimeError("Add: mappings of different width");
                    }
                    if (leftMap) {
                        result->entries = leftMap->entries;
                        result->width = leftMap->width;
                        result->extraColumns = leftMap->extraColumns;
                    } else {
                        result->width = rightW;
                    }
                    if (rightMap) {
                        for (size_t ri = 0; ri < rightMap->entries.size(); ++ri) {
                            const auto& entry = rightMap->entries[ri];
                            bool replaced = false;
                            for (size_t li = 0; li < result->entries.size(); ++li) {
                                if (valuesEqual(result->entries[li].first, entry.first)) {
                                    result->entries[li].second = entry.second;
                                    if (!result->extraColumns.empty()) {
                                        result->extraColumns[li] = rightMap->extraColumns.empty()
                                            ? std::vector<Value>(static_cast<size_t>(result->width - 1))
                                            : rightMap->extraColumns[ri];
                                    }
                                    replaced = true;
                                    break;
                                }
                            }
                            if (!replaced) {
                                result->entries.push_back(entry);
                                if (result->width > 1) {
                                    if (!rightMap->extraColumns.empty()) {
                                        result->extraColumns.push_back(rightMap->extraColumns[ri]);
                                    } else {
                                        result->extraColumns.emplace_back(
                                            static_cast<size_t>(result->width - 1));
                                    }
                                }
                            }
                        }
                    }
                    localStack.emplace_back(Value(result));
                } else {
                    throw LpcRuntimeError(
                        "Add: unsupported operand types (lhs kind " +
                        std::to_string(lhs.data.index()) + ", rhs kind " +
                        std::to_string(rhs.data.index()) + ")");
                }
                ++ip;
                break;
            }

            case OpCode::ExpandVarargs: {
                // See Bytecode.hpp's own OpCode::ExpandVarargs comment
                // for the full real-semantics citation (interpret.c:
                // 2680-2724).
                int64_t n = instr.operand;
                if (n < 0 || static_cast<size_t>(n) >= localStack.size()) {
                    throw LpcRuntimeError("ExpandVarargs: bad stack offset");
                }
                size_t slotIndex = localStack.size() - 1 - static_cast<size_t>(n);
                if (!std::holds_alternative<std::shared_ptr<Array>>(localStack[slotIndex].data)) {
                    throw LpcRuntimeError(
                        "Item being expanded with \"...\" is not an array");
                }
                auto arr = std::get<std::shared_ptr<Array>>(localStack[slotIndex].data);
                size_t arrSize = arr ? arr->items.size() : 0;
                auto slotIt = localStack.begin() + static_cast<long>(slotIndex);
                slotIt = localStack.erase(slotIt);
                if (arrSize > 0) {
                    localStack.insert(slotIt, arr->items.begin(), arr->items.end());
                }
                pendingVarargsDelta += static_cast<int64_t>(arrSize) - 1;
                ++ip;
                break;
            }

            case OpCode::MakeArray: {
                int argc = instr.argCount + static_cast<int32_t>(pendingVarargsDelta);
                pendingVarargsDelta = 0;
                if (argc < 0 || static_cast<size_t>(argc) > localStack.size()) {
                    throw LpcRuntimeError("MakeArray: bad arg count");
                }
                auto arr = std::make_shared<Array>();
                arr->items.assign(localStack.end() - argc, localStack.end());
                localStack.erase(localStack.end() - argc, localStack.end());
                // operand is unused by an ordinary array literal (always
                // 0 there); CodeGen's own NewClassExpr codegen ("new(
                // class Name ...)", ROADMAP.md row 3.10's class scoping
                // report) is the one caller that sets it to 1, real
                // FluffOS's own T_CLASS-vs-T_ARRAY distinction -- see
                // Value.hpp's own isClassInstance comment.
                Value result(arr);
                result.isClassInstance = (instr.operand != 0);
                localStack.emplace_back(std::move(result));
                ++ip;
                break;
            }

            case OpCode::MakeMapping: {
                int entryCount = instr.argCount + static_cast<int32_t>(pendingVarargsDelta);
                pendingVarargsDelta = 0;
                int width = instr.operand;
                if (width < 1) width = 1;
                size_t stride = static_cast<size_t>(width) + 1; // key + values
                if (entryCount < 0 ||
                    stride * static_cast<size_t>(entryCount) > localStack.size()) {
                    throw LpcRuntimeError("MakeMapping: bad arg count");
                }
                size_t total = stride * static_cast<size_t>(entryCount);
                size_t base = localStack.size() - total;
                auto map = std::make_shared<Mapping>();
                map->width = width;
                for (int i = 0; i < entryCount; ++i) {
                    size_t off = base + static_cast<size_t>(i) * stride;
                    Value key = localStack[off];
                    std::vector<Value> values;
                    values.reserve(static_cast<size_t>(width));
                    for (int c = 0; c < width; ++c) {
                        values.push_back(localStack[off + 1 + static_cast<size_t>(c)]);
                    }
                    map->appendEntry(std::move(key), std::move(values));
                }
                localStack.erase(localStack.end() - static_cast<long>(total), localStack.end());
                localStack.emplace_back(Value(map));
                ++ip;
                break;
            }

            case OpCode::Index: {
                bool hasMapColumn = (instr.argCount & 0x4) != 0;
                size_t needed = hasMapColumn ? 3 : 2;
                if (localStack.size() < needed) {
                    throw LpcRuntimeError("Index: stack underflow");
                }
                Value mapColumnVal;
                if (hasMapColumn) {
                    mapColumnVal = localStack.back(); localStack.pop_back();
                }
                Value indexVal = localStack.back(); localStack.pop_back();
                Value targetVal = localStack.back(); localStack.pop_back();
                // See CodeGen.cpp's own comment: argCount is repurposed
                // as a "from the end" flags bitmask for this opcode,
                // bit 0 for the single index here. Bit 2 is the LDMud
                // mapping-column flag (map[key, n]).
                bool indexFromEnd = (instr.argCount & 0x1) != 0;

                if (auto* arr = std::get_if<std::shared_ptr<Array>>(&targetVal.data)) {
                    if (!*arr) {
                        throw LpcRuntimeError("Index: target array is null");
                    }
                    if (!std::holds_alternative<int64_t>(indexVal.data)) {
                        throw LpcRuntimeError("Index: array index must be an integer");
                    }
                    int64_t i = std::get<int64_t>(indexVal.data);
                    // real eoperators.c's f_index()/reverse indexing:
                    // "<N" means index (length - N), computed against
                    // the target's own runtime length.
                    if (indexFromEnd) i = static_cast<int64_t>((*arr)->items.size()) - i;
                    if (i < 0 || static_cast<size_t>(i) >= (*arr)->items.size()) {
                        throw LpcRuntimeError("Index: array index out of bounds");
                    }
                    coerceIfDestructed((*arr)->items[static_cast<size_t>(i)]);
                    localStack.push_back((*arr)->items[static_cast<size_t>(i)]);
                } else if (auto* map = std::get_if<std::shared_ptr<Mapping>>(&targetVal.data)) {
                    if (!*map) {
                        throw LpcRuntimeError("Index: target mapping is null");
                    }
                    bool hit = false;
                    Value found;
                    size_t hitIdx = 0;
                    for (size_t i = 0; i < (*map)->entries.size(); ++i) {
                        if (valuesEqual((*map)->entries[i].first, indexVal)) {
                            hit = true;
                            hitIdx = i;
                            break;
                        }
                    }
                    int col = 0;
                    if (hasMapColumn) {
                        // real push_map_index_value(), interpret.c:6884-6900:
                        // column out of range errors even before the key
                        // lookup; a missing key then returns 0 (const0),
                        // including for col > 0 -- a real doc-vs-code
                        // divergence (doc/LPC/mappings claimed n>0 on a
                        // missing key errors; the C does not).
                        if (!std::holds_alternative<int64_t>(mapColumnVal.data)) {
                            throw LpcRuntimeError("Illegal sub-index type, expected number.");
                        }
                        col = static_cast<int>(std::get<int64_t>(mapColumnVal.data));
                        if (col < 0 || col >= (*map)->width) {
                            throw LpcRuntimeError(
                                "Illegal sub-index " + std::to_string(col) +
                                ", mapping width is " + std::to_string((*map)->width) + ".");
                        }
                    }
                    if (hit) {
                        found = (*map)->getColumn(hitIdx, col);
                        coerceIfDestructed(found);
                        localStack.push_back(found);
                    } else if (hasMapColumn) {
                        // real put_number(sp, 0) -- a genuine int 0, not
                        // this driver's monostate missing-key sentinel
                        // used by ordinary map[key].
                        localStack.push_back(Value(int64_t{0}));
                    } else {
                        localStack.push_back(Value{});
                    }
                } else if (auto* str = std::get_if<std::string>(&targetVal.data)) {
                    if (!std::holds_alternative<int64_t>(indexVal.data)) {
                        throw LpcRuntimeError("Index: string index must be an integer");
                    }
                    int64_t i = std::get<int64_t>(indexVal.data);
                    if (indexFromEnd) i = static_cast<int64_t>(str->size()) - i;
                    // Real interpret.c's own F_INDEX T_STRING case:
                    // "if ((i > SVALUE_STRLEN(sp)) || (i < 0)) error(...)"
                    // -- strictly greater than, not >=, so indexing
                    // exactly at the string's own length (one past the
                    // last real character, the position of its implicit
                    // NUL terminator) is real, defined, non-throwing
                    // behavior that reads back 0, not an error. Found
                    // live against a real third-party mudlib corpus (row
                    // 3.8's TMI-2 boot attempt): the ubiquitous real LPC
                    // idiom "if (lines[i][0] == '#' || lines[i] == \"\")
                    // continue;" (adm/obj/master/groups.c and access.c
                    // both) relies on this exact leniency for an empty
                    // exploded line -- lines[i][0] must read back 0 (a
                    // real string's own std::string::operator[](size())
                    // is already guaranteed by the C++ standard to yield
                    // a null character, so no special-casing is needed
                    // here beyond relaxing the bound itself) rather than
                    // throwing, or the comparison against '#'/'\n' never
                    // even runs and every real mudlib using this idiom
                    // (effectively every one built on classic LPC/MudOS
                    // conventions) throws on its very first blank config
                    // line. This driver's own prior ">=" check threw
                    // exactly there, a real, narrow off-by-one divergence
                    // from real FluffOS, not a deliberate design choice.
                    if (i < 0 || static_cast<size_t>(i) > str->size()) {
                        throw LpcRuntimeError("Index: string index out of bounds");
                    }
                    unsigned char ch = static_cast<unsigned char>((*str)[static_cast<size_t>(i)]);
                    localStack.push_back(Value(static_cast<int64_t>(ch)));
                } else {
                    throw LpcRuntimeError("Index: target is not an array, mapping, or string");
                }
                ++ip;
                break;
            }

            case OpCode::IndexAssign: {
                bool hasMapColumn = (instr.argCount & 0x4) != 0;
                size_t needed = hasMapColumn ? 4 : 3;
                if (localStack.size() < needed) {
                    throw LpcRuntimeError("IndexAssign: stack underflow");
                }
                Value value = localStack.back(); localStack.pop_back();
                Value mapColumnVal;
                if (hasMapColumn) {
                    mapColumnVal = localStack.back(); localStack.pop_back();
                }
                Value indexVal = localStack.back(); localStack.pop_back();
                Value targetVal = localStack.back(); localStack.pop_back();

                if (auto* arr = std::get_if<std::shared_ptr<Array>>(&targetVal.data)) {
                    if (!*arr) {
                        throw LpcRuntimeError("IndexAssign: target array is null");
                    }
                    if (!std::holds_alternative<int64_t>(indexVal.data)) {
                        throw LpcRuntimeError("IndexAssign: array index must be an integer");
                    }
                    int64_t i = std::get<int64_t>(indexVal.data);
                    if (i < 0 || static_cast<size_t>(i) >= (*arr)->items.size()) {
                        throw LpcRuntimeError("IndexAssign: array index out of bounds");
                    }
                    (*arr)->items[static_cast<size_t>(i)] = value;
                } else if (auto* map = std::get_if<std::shared_ptr<Mapping>>(&targetVal.data)) {
                    if (!*map) {
                        throw LpcRuntimeError("IndexAssign: target mapping is null");
                    }
                    bool found = false;
                    size_t foundIdx = 0;
                    for (size_t i = 0; i < (*map)->entries.size(); ++i) {
                        if (valuesEqual((*map)->entries[i].first, indexVal)) {
                            found = true;
                            foundIdx = i;
                            break;
                        }
                    }
                    int col = 0;
                    if (hasMapColumn) {
                        if (!std::holds_alternative<int64_t>(mapColumnVal.data)) {
                            throw LpcRuntimeError("Illegal sub-index type, expected number.");
                        }
                        col = static_cast<int>(std::get<int64_t>(mapColumnVal.data));
                        if (col < 0 || col >= (*map)->width) {
                            throw LpcRuntimeError(
                                "Illegal sub-index " + std::to_string(col) +
                                ", mapping width is " + std::to_string((*map)->width) + ".");
                        }
                    }
                    if (found) {
                        (*map)->setColumn(foundIdx, col, value);
                    } else {
                        // real get_map_lvalue creates a new entry (docs:
                        // missing key with n==0 auto-inserts; the C
                        // assign_mapentry_lvalue path also creates for
                        // n>0 once the column has already been range-
                        // checked against width). Extra columns default
                        // to 0.
                        std::vector<Value> values(static_cast<size_t>((*map)->width), Value(int64_t{0}));
                        values[static_cast<size_t>(col)] = value;
                        (*map)->appendEntry(indexVal, std::move(values));
                    }
                } else {
                    throw LpcRuntimeError("IndexAssign: target is not an array or mapping");
                }
                ++ip;
                break;
            }

            case OpCode::RangeIndex: {
                if (localStack.size() < 3) {
                    throw LpcRuntimeError("RangeIndex: stack underflow");
                }
                Value endVal = localStack.back(); localStack.pop_back();
                Value startVal = localStack.back(); localStack.pop_back();
                Value targetVal = localStack.back(); localStack.pop_back();

                if (!std::holds_alternative<int64_t>(startVal.data)) {
                    throw LpcRuntimeError("RangeIndex: start index must be an integer");
                }
                if (!std::holds_alternative<int64_t>(endVal.data)) {
                    throw LpcRuntimeError("RangeIndex: end index must be an integer");
                }
                int64_t rawStart = std::get<int64_t>(startVal.data);
                int64_t rawEnd = std::get<int64_t>(endVal.data);
                // See CodeGen.cpp's own comment: argCount is repurposed
                // as a "from the end" flags bitmask for this opcode,
                // bit 0 for the start bound, bit 1 for the end bound.
                bool startFromEnd = (instr.argCount & 0x1) != 0;
                bool endFromEnd = (instr.argCount & 0x2) != 0;

                // A literal negative start with no "<" prefix is still
                // rejected exactly as before "from the end" indexing
                // existed (this driver's own pre-existing behavior, not
                // real modern FluffOS's -- real eoperators.c's
                // OLD_RANGE_BEHAVIOR-gated auto-wrap is deprecated
                // there too, "use arr[x..<y]" instead). Only a start
                // that is negative *after* resolving "<N" against the
                // target's own length clamps to 0 instead of throwing
                // (real eoperators.c: "if (from < 0) from = 0;") -- a
                // legitimate outcome for "<N" when N is at least the
                // target's own length, not a caller mistake the way a
                // bare negative literal is.
                if (!startFromEnd && rawStart < 0) {
                    throw LpcRuntimeError("RangeIndex: start index must be non-negative");
                }

                // real eoperators.c's f_range(): "if (code & 0x10) from
                // = len - from;" / "if (code & 0x01) to = len - to;",
                // each resolved against the target's own runtime length.
                auto resolveBounds = [&](int64_t len) {
                    int64_t start = startFromEnd ? (len - rawStart) : rawStart;
                    int64_t end = endFromEnd ? (len - rawEnd) : rawEnd;
                    if (start < 0) start = 0;
                    return std::pair<int64_t, int64_t>(start, end);
                };

                if (auto* str = std::get_if<std::string>(&targetVal.data)) {
                    int64_t len = static_cast<int64_t>(str->size());
                    auto [start, end] = resolveBounds(len);
                    int64_t clampedEnd = std::min(end, len - 1);
                    if (start > clampedEnd) {
                        localStack.push_back(Value(std::string()));
                    } else {
                        localStack.push_back(Value(str->substr(
                            static_cast<size_t>(start),
                            static_cast<size_t>(clampedEnd - start + 1))));
                    }
                } else if (auto* arr = std::get_if<std::shared_ptr<Array>>(&targetVal.data)) {
                    if (!*arr) {
                        throw LpcRuntimeError("RangeIndex: target array is null");
                    }
                    int64_t len = static_cast<int64_t>((*arr)->items.size());
                    auto [start, end] = resolveBounds(len);
                    int64_t clampedEnd = std::min(end, len - 1);
                    auto result = std::make_shared<Array>();
                    for (int64_t i = start; i <= clampedEnd; ++i) {
                        result->items.push_back((*arr)->items[static_cast<size_t>(i)]);
                    }
                    localStack.push_back(Value(result));
                } else {
                    throw LpcRuntimeError("RangeIndex: target is not an array or string");
                }
                ++ip;
                break;
            }

            case OpCode::Call: {
                if (instr.operand < 0 ||
                    static_cast<size_t>(instr.operand) >= program.stringPool.size()) {
                    throw LpcRuntimeError("Call: bad function name index");
                }
                const std::string& funcName = program.stringPool[instr.operand];

                int argc = instr.argCount + static_cast<int32_t>(pendingVarargsDelta);
                pendingVarargsDelta = 0;
                if (argc < 0 || static_cast<size_t>(argc) > localStack.size()) {
                    throw LpcRuntimeError("Call: bad arg count for " + funcName);
                }
                std::vector<Value> callArgs(localStack.end() - argc, localStack.end());
                localStack.erase(localStack.end() - argc, localStack.end());

                // A bare (unqualified) call always resolves against
                // obj->program() -- the object's own top-level, most-
                // derived program -- never against `program` (whichever
                // file's own bytecode happens to be currently executing).
                // This matches real LPC's actual compile-time model, not
                // C++-style non-virtual lexical scoping: FluffOS's own
                // compiler.c define_new_function() flattens an object's
                // entire inherit tree into ONE shared function table, and
                // when a more-derived file redefines a name an ancestor
                // already provided, that redefinition "is... considered
                // to be THE new definition" for the whole object -- every
                // unqualified call to that name resolves through the same
                // table and reaches the override, including calls written
                // inside the ancestor's own source. This is the standard
                // Nightmare/LPC idiom of an ancestor defining a
                // placeholder stub (e.g. std/user/nmsh.c's own
                // "query_name() { return 0; }", one of several such
                // stubs) that a real inheriting file (std/user.c) is
                // meant to override -- confirmed live broken the other
                // way: nmsh.c's own reset_prompt() calling query_name()
                // was reaching nmsh.c's own stub instead of std/user.c's
                // real override. A previous version of this opcode
                // searched `program`'s own lexical scope first and only
                // fell back to obj->program() when nothing was found at
                // all (see std/user/nmsh.c's process_input() calling
                // "query_client", a name nmsh.c neither defines nor
                // inherits, only std/user.c does); searching
                // obj->program() first still finds that case too, since
                // obj->program()'s own depth-first walk necessarily
                // covers every program in its inherit tree, `program`
                // (whatever is currently executing) always among them --
                // so a single top-level-first search is a strict
                // superset of the old two-step logic, not just a
                // different tradeoff.
                //
                // OpCode::CallParent ("::name()"/"qualifier::name()") is
                // deliberately untouched: it is the explicit escape hatch
                // for reaching a specific ancestor's own shadowed
                // definition instead of the override, and must keep
                // searching only the *inherited* programs, skipping
                // `program` itself, exactly as it already does.
                FunctionLookupResult found = findFunctionInChain(obj->program(), funcName);
                if (found.program) {
                    // Real F_CALL_FUNCTION_BY_ADDRESS's own "caller_type =
                    // ORIGIN_LOCAL;" (interpret.c), confirmed directly: a
                    // bare call resolving within the calling object's own
                    // program, local or inherited, is always ORIGIN_LOCAL
                    // -- this is the one real call site the row's only
                    // known security-sensitive origin() check
                    // (secure/daemon/chat.c's own "origin() !=
                    // ORIGIN_LOCAL") actually needs to get right.
                    OriginGuard originGuard(*this, Origin::Local);
                    Value result = run(*found.program, *found.fn, std::move(callArgs), obj);
                    localStack.push_back(std::move(result));
                    ++ip;
                    break;
                }

                // Tier 3: the configured simul_efun object, matching real
                // FluffOS's own resolution order (local/inherited, then
                // simul_efun, then the real efun table -- see lex.c's
                // F_SIMUL_EFUN handling and function.c's
                // call_simul_efun()). Unlike the local/inherited case
                // above, this runs against the simul_efun object's own
                // variables() (via "simulEfun" as the obj argument, not
                // the caller's obj) -- a simul_efun function's object
                // variables belong to the simul_efun object itself, this
                // is not the same "shared flat variable space" situation
                // inherit deliberately sets up.
                auto simulEfun = objects_.simulEfunObject();
                if (simulEfun) {
                    FunctionLookupResult simulFound =
                        findFunctionInChain(simulEfun->program(), funcName);
                    if (simulFound.program) {
                        // Real call_simul_efun()'s own "call_direct(
                        // simul_efun_ob, ..., ORIGIN_SIMUL_EFUN, ...)"
                        // (eoperators.c), confirmed directly.
                        OriginGuard originGuard(*this, Origin::SimulEfun);
                        Value result = run(*simulFound.program, *simulFound.fn,
                                            std::move(callArgs), simulEfun);
                        localStack.push_back(std::move(result));
                        ++ip;
                        break;
                    }
                }

                // Deliberately no OriginGuard here: a bare call resolving
                // to the core efun table never pushes a real LPC frame in
                // real FluffOS either (no push_control_stack() anywhere
                // in an ordinary efun dispatch, confirmed directly), so
                // the origin stays whatever the calling function's own
                // frame already has. The two real exceptions --
                // call_other()/"->" and evaluate()/funcall()/"(*fp)(...)",
                // both compiler-forced through this same efun-table path
                // (CodeGen.cpp's own forceEfun) -- do their own explicit
                // origin tagging entirely inside their own EfunTable.cpp
                // registrations (see call_other's own vm.callFunction(...,
                // Origin::CallOther) and callClosure()'s own tiered
                // resolution respectively), not here: this opcode never
                // needs to know which specific efun name it is about to
                // dispatch.
                if (EfunTable::instance().exists(funcName)) {
                    Value result = EfunTable::instance().call(funcName, *this, callArgs);
                    localStack.push_back(std::move(result));
                } else {
                    throw LpcRuntimeError("undefined function or efun: " + funcName);
                }
                ++ip;
                break;
            }

            case OpCode::CallParent: {
                if (instr.operand < 0 ||
                    static_cast<size_t>(instr.operand) >= program.stringPool.size()) {
                    throw LpcRuntimeError("CallParent: bad function name index");
                }
                const std::string& funcName = program.stringPool[instr.operand];

                if (ip + 1 >= program.code.size() ||
                    program.code[ip + 1].op != OpCode::CallParentQualifierSlot) {
                    throw LpcRuntimeError("CallParent: missing qualifier data instruction");
                }
                int32_t qualifierIdx = program.code[ip + 1].operand;
                std::string qualifierStorage;
                const std::string* qualifier = nullptr;
                if (qualifierIdx >= 0) {
                    if (static_cast<size_t>(qualifierIdx) >= program.stringPool.size()) {
                        throw LpcRuntimeError("CallParent: bad qualifier string index");
                    }
                    qualifierStorage = program.stringPool[qualifierIdx];
                    qualifier = &qualifierStorage;
                }

                int argc = instr.argCount + static_cast<int32_t>(pendingVarargsDelta);
                pendingVarargsDelta = 0;
                if (argc < 0 || static_cast<size_t>(argc) > localStack.size()) {
                    throw LpcRuntimeError("CallParent: bad arg count for " + funcName);
                }
                std::vector<Value> callArgs(localStack.end() - argc, localStack.end());
                localStack.erase(localStack.end() - argc, localStack.end());

                FunctionLookupResult found = findParentFunction(program, funcName, qualifier);
                if (!found.program) {
                    throw LpcRuntimeError(
                        (qualifier ? (*qualifier + "::") : std::string("::")) + funcName +
                        "(): undefined function in inherited program");
                }
                // Real F_CALL_INHERITED's own "caller_type = ORIGIN_LOCAL;"
                // (interpret.c), confirmed directly -- an explicit
                // "::name()"/"qualifier::name()" call reaches an inherited
                // definition the same ORIGIN_LOCAL way a plain same-object
                // bare call does.
                OriginGuard originGuard(*this, Origin::Local);
                Value result = run(*found.program, *found.fn, std::move(callArgs), obj);
                localStack.push_back(std::move(result));
                ip += 2; // past CallParent and its CallParentQualifierSlot data instruction
                break;
            }

            case OpCode::PushClosure: {
                if (instr.operand < 0 ||
                    static_cast<size_t>(instr.operand) >= program.stringPool.size()) {
                    throw LpcRuntimeError("PushClosure: bad function name index");
                }
                const std::string& funcName = program.stringPool[instr.operand];

                int argc = instr.argCount;
                if (argc < 0 || static_cast<size_t>(argc) > localStack.size()) {
                    throw LpcRuntimeError("PushClosure: bad bound-arg count for " + funcName);
                }
                auto closure = std::make_shared<Closure>();
                closure->owner = obj; // weak_ptr from shared_ptr, real current_object at bind time
                closure->functionName = funcName;
                closure->boundArgs.assign(localStack.end() - argc, localStack.end());
                localStack.erase(localStack.end() - argc, localStack.end());

                localStack.push_back(Value(closure));
                ++ip;
                break;
            }

            case OpCode::PushEfunClosure: {
                // Identical construction to PushClosure just above except
                // for the one flag that actually matters -- see
                // Bytecode.hpp's own PushEfunClosure comment and Value.hpp's
                // Closure::forceEfun for why this needs its own opcode
                // rather than an extra branch inside the PushClosure case:
                // same operand/argCount shape, same real precedent as the
                // existing Call/CallEfun split.
                if (instr.operand < 0 ||
                    static_cast<size_t>(instr.operand) >= program.stringPool.size()) {
                    throw LpcRuntimeError("PushEfunClosure: bad function name index");
                }
                const std::string& funcName = program.stringPool[instr.operand];

                int argc = instr.argCount;
                if (argc < 0 || static_cast<size_t>(argc) > localStack.size()) {
                    throw LpcRuntimeError("PushEfunClosure: bad bound-arg count for " + funcName);
                }
                auto closure = std::make_shared<Closure>();
                closure->owner = obj;
                closure->functionName = funcName;
                closure->forceEfun = true;
                closure->boundArgs.assign(localStack.end() - argc, localStack.end());
                localStack.erase(localStack.end() - argc, localStack.end());

                localStack.push_back(Value(closure));
                ++ip;
                break;
            }

            case OpCode::Sscanf: {
                int n = instr.operand;
                if (n < 0) {
                    throw LpcRuntimeError("Sscanf: bad var count");
                }
                if (localStack.size() < 2) {
                    throw LpcRuntimeError("Sscanf: stack underflow");
                }
                Value formatVal = localStack.back(); localStack.pop_back();
                Value targetVal = localStack.back(); localStack.pop_back();

                if (!std::holds_alternative<std::string>(targetVal.data) ||
                    !std::holds_alternative<std::string>(formatVal.data)) {
                    throw LpcRuntimeError("sscanf: first two arguments must be strings");
                }
                if (ip + 1 + static_cast<size_t>(n) > program.code.size()) {
                    throw LpcRuntimeError("Sscanf: truncated var-slot table");
                }

                SscanfOutcome outcome = runSscanf(std::get<std::string>(targetVal.data),
                                                   std::get<std::string>(formatVal.data),
                                                   static_cast<size_t>(n));

                for (size_t i = 0; i < outcome.assigned.size(); ++i) {
                    const Instruction& slotSpec = program.code[ip + 1 + i];
                    bool isObjectVar = slotSpec.argCount != 0;
                    int32_t slot = slotSpec.operand;
                    if (isObjectVar) {
                        auto& vars = obj->variables();
                        if (slot < 0 || static_cast<size_t>(slot) >= vars.size()) {
                            throw LpcRuntimeError("Sscanf: bad object variable slot index");
                        }
                        vars[static_cast<size_t>(slot)] = outcome.assigned[i];
                    } else {
                        if (slot < 0 || static_cast<size_t>(slot) >= locals.size()) {
                            throw LpcRuntimeError("Sscanf: bad local slot index");
                        }
                        locals[static_cast<size_t>(slot)] = outcome.assigned[i];
                    }
                }

                localStack.push_back(Value(outcome.matchCount));
                ip += 1 + static_cast<size_t>(n);
                break;
            }

            case OpCode::CallEfun: {
                if (instr.operand < 0 ||
                    static_cast<size_t>(instr.operand) >= program.stringPool.size()) {
                    throw LpcRuntimeError("CallEfun: bad efun name index");
                }
                const std::string& efunName = program.stringPool[instr.operand];

                int argc = instr.argCount + static_cast<int32_t>(pendingVarargsDelta);
                pendingVarargsDelta = 0;
                if (argc < 0 || static_cast<size_t>(argc) > localStack.size()) {
                    throw LpcRuntimeError("CallEfun: bad arg count for " + efunName);
                }

                std::vector<Value> efunArgs(
                    localStack.end() - argc, localStack.end());
                localStack.erase(localStack.end() - argc, localStack.end());

                Value result = EfunTable::instance().call(efunName, *this, efunArgs);
                localStack.push_back(std::move(result));
                ++ip;
                break;
            }

            case OpCode::Pop: {
                if (!localStack.empty()) localStack.pop_back();
                ++ip;
                break;
            }

            case OpCode::Dup: {
                if (localStack.empty()) {
                    throw LpcRuntimeError("Dup: stack underflow");
                }
                localStack.push_back(localStack.back());
                ++ip;
                break;
            }

            case OpCode::Return: {
                if (localStack.empty()) return Value{};
                return localStack.back();
            }

            case OpCode::Halt:
                return Value{};

            default:
                throw NotImplementedError(
                    "VM::run opcode " + std::to_string(static_cast<int>(instr.op)));
        }
      } catch (const LpcThrownValue& tv) {
        // A real LPC throw(value) -- caught separately from the plain
        // LpcRuntimeError case just below (this handler must come first;
        // LpcThrownValue is-a LpcRuntimeError) specifically so the
        // no-active-catch-frame path never flattens it into a rewrapped
        // string LpcRuntimeError the way an ordinary runtime error is
        // just below. Real throw(value) must reach the *nearest*
        // catch(), anywhere up the call stack, with the exact value
        // still intact -- rewrapping here would silently turn
        // "throw(({\"ERR\", data}))" into a plain string by the time it
        // reached a catch() one function call further up than this
        // one's own (empty) catchFrames.
        if (catchFrames.empty()) {
            throw;
        }

        std::cerr << "[catch] " << obj->filename() << "::" << fn.name
                   << "(): " << tv.what() << "\n";

        CatchFrame frame = catchFrames.back();
        catchFrames.pop_back();
        localStack.resize(frame.stackDepth);
        localStack.push_back(tv.value);
        ip = frame.resumeIp;
      } catch (const LpcRuntimeError& e) {
        // No active catch() anywhere in this call: behave exactly as
        // before catch() existed, propagate to whatever wraps this
        // run() call (a caller's own active catch frame if this was a
        // nested call, or the outermost ObjectManager/Server.cpp safety
        // net if not -- see run()'s own comment above catchFrames).
        // Tagging the file and function here (once, at the innermost
        // frame that had no catch() of its own to absorb it) matches
        // this driver's existing convention of naming the file in every
        // other [object]-prefixed diagnostic -- without it, an uncaught
        // error several calls deep only ever reports its own generic
        // message, not where it actually happened.
        if (catchFrames.empty()) {
            throw LpcRuntimeError(obj->filename() + "::" + fn.name + "(): " + e.what());
        }

        // Log every trapped error to stderr before resuming, unconditionally.
        // Confirmed against real FluffOS's own default build: simulate.c's
        // error_handler() logs a caught error too (debug_message_with_location()
        // + dump_trace()) whenever LOG_CATCHES is defined -- on by default in
        // every shipped local_options.*, including this exact mudlib's own
        // local_options.nm3, whose comment states the reason plainly: "newer
        // libs use catch() a lot, and it's confusing if the errors don't show
        // up in the logs." Without this, a catch() that is working exactly as
        // intended silently hides its own root cause, which is what forced
        // manual instrumentation to root-cause the do_alias() bug earlier this
        // project. No line number: this driver discards line numbers after
        // compilation (only the parser's own compile-time errors carry them),
        // so object filename + function name + message is what's genuinely
        // available, matching what [object]/[net] diagnostics already report
        // elsewhere. Deliberately not also calling master()->error_handler()
        // (real FluffOS's MUDLIB_ERROR_HANDLER path, mapping to the mapping/
        // caught-flag apply this mudlib's own master.c already implements at
        // secure/daemon/master.c:423) -- that is a materially larger feature,
        // flagged as a follow-up rather than implemented speculatively here.
        std::cerr << "[catch] " << obj->filename() << "::" << fn.name
                   << "(): " << e.what() << "\n";

        // Unwind to the innermost still-active catch() (LIFO, matching
        // real FluffOS's own nested do_catch() call stack -- see
        // catchFrames' own comment): discard whatever the guarded
        // expression had partially pushed (real LPC's own stack-pointer
        // restoration on longjmp, confirmed against interpret.c's
        // do_catch()/restore_context()), push the error message as
        // catch(expr)'s result, then resume right after the whole catch
        // expression, exactly where PopCatchFrame's own success path
        // would also have landed.
        CatchFrame frame = catchFrames.back();
        catchFrames.pop_back();
        localStack.resize(frame.stackDepth);
        localStack.emplace_back(Value(std::string(e.what())));
        ip = frame.resumeIp;
      }
    }

    return Value{};
}

// ROADMAP.md row 2.5's own first slice. See VM.hpp's own runAsync()
// comment for why this exists as a genuinely separate coroutine rather
// than a mode flag on run() above, and Bytecode.hpp's own
// OpCode::Suspend/FunctionEntry::isAsync comments for the exact
// contract each piece keeps. Deliberately a small, explicit *subset* of
// run()'s own opcode coverage: real row 2.6 grammar/codegen does not
// exist yet, so nothing in this driver can compile real LPC source into
// a function with isAsync set -- every function this coroutine ever
// actually runs is this row's own hand-built, test-only bytecode (see
// test/test_lexer.cpp's own row 2.5 regression tests), and the opcodes
// those tests exercise are the only ones implemented below. Anything
// else throws a clear, explicit "not yet implemented" error (the same
// NotImplementedError run()'s own default case above already throws
// for a genuinely unhandled opcode) rather than silently misbehaving.
// Extending this to the full opcode set is real row 2.6 scope, not
// attempted here.
//
// Deliberately does NOT push obj onto callStack_/objectChangeStack_ the
// way run()'s own ObjectFrameGuard does, and does NOT push/pop
// originStack_ the way OriginGuard does either: both are single
// VM-wide vectors shared with every ordinary synchronous run() call
// still happening elsewhere (Scheduler ticking a call_out/heartbeat, a
// player command dispatching) -- if a task suspended here left an
// entry on either stack, an unrelated synchronous call resuming
// *between* this coroutine's suspend and its later resume would see a
// stale, wrong currentObject()/origin(), and this coroutine's own
// eventual resume would then pop whatever happens to be on top of that
// shared vector at that later moment, not necessarily the entry it
// pushed -- real, silent stack corruption, not just a wrong-but-
// harmless read. currentObject()/origin() are therefore not reliable
// from inside a running-or-parked async task in this first slice -- a
// real, named gap, not an oversight: row 2.6 will need a genuine
// per-task context (not these shared VM-wide stacks) before an async
// LPC function can safely call an ordinary efun that reads either, and
// building that now, before any real async LPC code exists to need it,
// would be speculative scope this row's own ROADMAP.md note never
// asked for. evalCost_ is the one deliberate exception, shared
// unchanged: it is already a single VM-wide counter reset once per
// top-level dispatch (see its own VM.hpp comment), not a per-call-frame
// stack, so nothing about suspension makes sharing it here any less
// sound than run() already relies on for ordinary nested calls today --
// and sharing it is a hard requirement, not just convenient (see
// Bytecode.hpp's own FunctionEntry comment): a separate async-only
// budget would make `await` a cost-limit escape hatch, exactly what
// ROADMAP.md row 2.5 rules out.
Task<Value> VM::runAsync(const CompiledProgram& program, const FunctionEntry& fn,
                          std::vector<Value> args, const std::shared_ptr<LpcObject>& obj) {
    // Same real const0u default as VM::run() above, same dialect gate --
    // see that site's own comment.
    Value defaultLocalAsync = (config().dialect() == "fluffos") ? makeUndefinedNumber() : Value(int64_t{0});
    std::vector<Value> locals(fn.numLocals, defaultLocalAsync);
    for (size_t i = 0; i < args.size() && i < locals.size(); ++i) {
        locals[i] = std::move(args[i]);
    }
    std::vector<Value> localStack;
    size_t ip = fn.entryPoint;

    while (ip < program.code.size()) {
        const Instruction& instr = program.code[ip];
        ++evalCost_;
        if (evalCost_ > maxEvalCost_) {
            throw EvalCostError("eval cost exceeded");
        }

        switch (instr.op) {
            case OpCode::PushInt: {
                localStack.emplace_back(Value(static_cast<int64_t>(instr.operand)));
                ++ip;
                break;
            }

            case OpCode::PushLocal: {
                if (instr.operand < 0 || static_cast<size_t>(instr.operand) >= locals.size()) {
                    throw LpcRuntimeError("runAsync: PushLocal out of range");
                }
                localStack.push_back(locals[instr.operand]);
                ++ip;
                break;
            }

            case OpCode::StoreLocal: {
                if (instr.operand < 0 || static_cast<size_t>(instr.operand) >= locals.size()) {
                    throw LpcRuntimeError("runAsync: StoreLocal out of range");
                }
                if (localStack.empty()) {
                    throw LpcRuntimeError("runAsync: StoreLocal stack underflow");
                }
                locals[instr.operand] = localStack.back();
                localStack.pop_back();
                ++ip;
                break;
            }

            case OpCode::Add: {
                // Int-only in this first slice -- real Add's full
                // string/float/monostate coercion table (above, this
                // same file) is real row 2.6 codegen scope, not
                // reimplemented here for a hand-built test opcode
                // subset that only ever exercises plain integers.
                if (localStack.size() < 2) {
                    throw LpcRuntimeError("runAsync: Add stack underflow");
                }
                Value rhs = localStack.back(); localStack.pop_back();
                Value lhs = localStack.back(); localStack.pop_back();
                auto* li = std::get_if<int64_t>(&lhs.data);
                auto* ri = std::get_if<int64_t>(&rhs.data);
                if (!li || !ri) {
                    throw LpcRuntimeError("runAsync: Add only supports int+int in this first slice");
                }
                localStack.emplace_back(Value(static_cast<int64_t>(*li + *ri)));
                ++ip;
                break;
            }

            case OpCode::Call: {
                if (instr.operand < 0 ||
                    static_cast<size_t>(instr.operand) >= program.stringPool.size()) {
                    throw LpcRuntimeError("runAsync: Call bad function name index");
                }
                const std::string& funcName = program.stringPool[instr.operand];
                int argc = instr.argCount;
                if (argc < 0 || static_cast<size_t>(argc) > localStack.size()) {
                    throw LpcRuntimeError("runAsync: Call bad arg count for " + funcName);
                }
                std::vector<Value> callArgs(localStack.end() - argc, localStack.end());
                localStack.erase(localStack.end() - argc, localStack.end());

                // Same obj->program()-first resolution run()'s own
                // OpCode::Call uses (see that case's own long comment
                // above) -- this first slice's own hand-built test
                // programs never inherit, so the simul_efun/efun-table
                // tiers that follow it there are not needed here.
                FunctionLookupResult found = findFunctionInChain(obj->program(), funcName);
                if (!found.program) {
                    throw LpcRuntimeError("runAsync: Call unresolved function " + funcName);
                }

                // The exact design decision row 2.5's own scoping
                // session settled on, now real: a callee flagged async
                // is co_await'd (its own suspension, if any, propagates
                // straight up through this call -- the precise "await
                // reached through an intervening plain call" case the
                // old TaskFrame sketch could not have handled); an
                // ordinary callee falls through to the plain, unchanged
                // run(), which cannot suspend and therefore cannot leak
                // a suspension across this coroutine's own frame.
                Value result = found.fn->isAsync
                                   ? co_await runAsync(*found.program, *found.fn, std::move(callArgs), obj)
                                   : run(*found.program, *found.fn, std::move(callArgs), obj);
                localStack.push_back(std::move(result));
                ++ip;
                break;
            }

            case OpCode::Suspend: {
                if (localStack.empty()) {
                    throw LpcRuntimeError("runAsync: Suspend stack underflow");
                }
                Value delayVal = localStack.back(); localStack.pop_back();
                double seconds = 0.0;
                if (auto* i = std::get_if<int64_t>(&delayVal.data)) {
                    seconds = static_cast<double>(*i);
                } else if (auto* d = std::get_if<double>(&delayVal.data)) {
                    seconds = *d;
                } else {
                    throw LpcRuntimeError("runAsync: Suspend delay must be numeric");
                }
                co_await suspendFor(std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(seconds)));
                ++ip;
                break;
            }

            case OpCode::Return: {
                Value result = localStack.empty() ? Value{} : localStack.back();
                co_return result;
            }

            case OpCode::Halt:
                co_return Value{};

            default:
                throw NotImplementedError(
                    "VM::runAsync opcode " + std::to_string(static_cast<int>(instr.op)) +
                    " (row 2.5's first slice implements only PushInt/PushLocal/StoreLocal/"
                    "Add/Call/Suspend/Return/Halt)");
        }
    }

    co_return Value{};
}

// ROADMAP.md row 2.5's own first slice ("Scheduler::run() gains one new
// step..."). See VM.hpp's own resumeReadyAsyncTasks() comment for why
// this lives here (a VM method Scheduler::run() calls, mirroring
// processPendingReplacePrograms() immediately above it in that same
// loop) rather than the Scheduler-side "resumeReadyTasks()" ROADMAP.md's
// own note first sketched, and why that is a real, deliberate
// correction rather than a drift from the design.
void VM::resumeReadyAsyncTasks(std::chrono::steady_clock::time_point now) {
    // Same "collect due entries, then fire each" shape as
    // Scheduler::tickCallOuts() (Scheduler.cpp) -- a task that
    // immediately re-suspends itself on resume (parking a fresh entry
    // in pendingAsyncResumes_) must not corrupt the vector being walked
    // to find what was already due this tick.
    std::vector<std::coroutine_handle<>> due;
    for (auto it = pendingAsyncResumes_.begin(); it != pendingAsyncResumes_.end();) {
        if (it->resumeAt <= now) {
            due.push_back(it->handle);
            it = pendingAsyncResumes_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto handle : due) {
        // Defense in depth, matching tickCallOuts()/tickHeartbeats()'s
        // own per-callback isolation intent: an LPC-level error inside
        // the coroutine body is already captured by Task<Value>'s own
        // promise_type::unhandled_exception() (Task.hpp) rather than
        // escaping resume() itself, so this catch rarely fires in
        // practice today. A fully fire-and-forget top-level task's own
        // captured error is not yet surfaced anywhere once captured
        // that way -- a real, named gap: nothing in this row's own
        // first slice creates such a task (every regression test drives
        // its own Task to completion and reads takeResult() directly),
        // and a real answer for "who owns a fire-and-forget task's
        // outcome" belongs to whichever later row (2.6/2.7) actually
        // gives tasks a caller nobody necessarily awaits.
        try {
            handle.resume();
        } catch (const std::exception& e) {
            std::cerr << "[async task] " << e.what() << "\n";
        }
    }
}

} // namespace amlp
