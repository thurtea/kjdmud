#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include "amlp/core/Errors.hpp"

namespace amlp {

class LpcObject;
struct Array;
struct Mapping;
struct Closure;
struct Buffer;

// DGD's real "nil" literal (ROADMAP.md row 1.2/1.3's greenlit slice,
// row 1.10's own minimal real piece). A stateless marker, no payload --
// real DGD's own runtime representation (data.h's "Value nil = { T_NIL,
// TRUE };") carries no data beyond its type tag either. Deliberately
// distinct from std::monostate (this driver's own pre-existing "void"/
// uninitialized/missing-mapping-key encoding, which coerces to a real 0
// in arithmetic and string concatenation -- see VM.cpp's own
// asArithmeticOperand()/formatNumberForConcat() comments): real DGD's
// own "nil.type = (stricttc) ? T_NIL : T_INT;" (data.cpp) shows nil
// only has a genuinely distinct runtime type under strict-typechecking
// mode -- the mode this implementation targets, since that is the only
// mode where nil means anything at all rather than literally being
// integer 0. Only ever constructed when `LpcDialect::DGD` is the active
// dialect (see Lexer::lexIdentOrKeyword()'s own dialect gate on the
// "nil" keyword) -- under FluffOS/LDMud this variant alternative simply
// never gets used.
struct Nil {};

// LDMud's own "'name" symbol literal (ROADMAP.md row 1.2/1.3's own
// "new Value variant member needed" note, and row 1.7/1.8's own
// unbound_lambda() investigation, which is what actually greenlit
// building it: real LDMud's own quoted-code lambda() bodies -- see
// Closure::lambdaBody below -- use a symbol to mean "substitute this
// lambda parameter's own bound value here", distinct from an ordinary
// string. Confirmed as a real, separate runtime value in real LDMud,
// not just lexer trivia: temp/ldmud/src/lex.c:6216-6266 is the real
// lexer rule (see Lexer::lexQuote()'s own comment for the char-literal-
// vs-symbol disambiguation it ports), and interpret.h's own svalue_t
// carries a dedicated T_SYMBOL type tag for it. Only the plain "one
// leading quote, bare identifier" shape is supported here -- real
// LDMud's own multi-quote symbols ("''name", quotes > 1) and the
// separate "'({" quoted-aggregate literal are both unimplemented,
// matching zero confirmed real corpus usage for either.
struct Symbol {
    std::string name;
};

// The buffer alternative is deliberately kept LAST in this variant.
// src/vm/Value.cpp's valuesEqual() is the only place that reads
// data.index() for logic (an "a.index() != b.index()" fast path), so
// appending here keeps every existing alternative at its current index
// and gives Buffer the new highest one. Nothing in the tree does a
// numeric std::get<N>, std::visit, or exhaustive switch over this
// variant (checked directly), so a new trailing member is additive.
using ValueVariant = std::variant<
    std::monostate,
    Nil,
    int64_t,
    double,
    std::string,
    Symbol,
    std::shared_ptr<LpcObject>,
    std::shared_ptr<Array>,
    std::shared_ptr<Mapping>,
    std::shared_ptr<Closure>,
    std::shared_ptr<Buffer>
>;

struct Value {
    ValueVariant data;

    // Real FluffOS's T_UNDEFINED (lpc.h: "#define T_UNDEFINED 0x4 /*
    // undefinedp() returns true */"): a *subtype* flag alongside an
    // ordinary T_NUMBER svalue, not a separate value kind, set only on
    // const0u (main.c: "const0u.type = T_NUMBER; const0u.subtype =
    // T_UNDEFINED; const0u.u.number = 0;") and cleared to 0 by every
    // opcode that produces a fresh number (confirmed by grep across
    // eoperators.c/interpret.c -- every arithmetic/assignment result
    // explicitly resets subtype). This field mirrors that shape
    // directly: a plain bool sibling to `data`, not a new ValueVariant
    // alternative, so isTruthy()/valuesEqual() and every arithmetic
    // opcode keep reading `data` exactly as before and need no changes
    // (see makeUndefinedNumber()'s own comment). Only ever true when
    // `data` holds an int64_t 0 constructed via makeUndefinedNumber();
    // meaningless (and never set) on any other alternative. FluffOS-
    // dialect-only: real LDMud has no equivalent concept at all
    // (confirmed against temp/ldmud/src/svalue.h -- no T_UNDEFINED, no
    // undefinedp()/nullp() efun, no secondary-type slot for it), so
    // callers gate construction on dialect, this field itself carries
    // no dialect awareness.
    bool isUndefined = false;

    // Real FluffOS's T_CLASS (ROADMAP.md row 3.10's class scoping
    // report, lpc.h: "#define T_CLASS 0x200") -- a real, distinct
    // top-level svalue type tag in real FluffOS, but its storage is a
    // plain, reused array_t* (class.c's own allocate_class(),
    // interpret.c's own push_class()/push_refed_class(): "sp->type =
    // T_CLASS; sp->u.arr = v;", the identical union field an ordinary
    // T_ARRAY svalue uses). This field mirrors that reuse the same way
    // isUndefined above mirrors real T_UNDEFINED: a plain bool sibling
    // to `data`, not a new ValueVariant alternative, so every ordinary
    // array-oriented efun/opcode (sizeof(), foreach, "+"/"-"
    // concatenation, save_object's own lack of a T_CLASS case, ...)
    // keeps reading `data` exactly as before and needs no changes --
    // matching real FluffOS's own behavior, where an ordinary array
    // opcode does not special-case T_CLASS either. Only ever true when
    // `data` holds a shared_ptr<Array> constructed by a real "new(class
    // Name ...)" construction (CodeGen's own NewClassExpr codegen,
    // OpCode::MakeArray's operand-1 case); meaningless (and never set)
    // on any other alternative or on an ordinary array literal. Exists
    // because classp() (real func_spec.c's own "int classp(mixed);",
    // live in this exact corpus: secure/sefun/identify.c,
    // secure/lib/net/client.c, daemon/intermud.c) needs a real way to
    // tell a class instance apart from a plain array -- real FluffOS
    // itself only needs this same single bool (f_classp(): "sp->type ==
    // T_CLASS", efuns_main.c, no per-class identity check at all), not
    // a per-class name; real member access itself never reads this
    // field either, since member resolution is 100% compile-time (see
    // CodeGen's own resolveClassMemberIndex()/staticClassTypeOf()).
    // FluffOS-dialect-only, same reasoning as isUndefined above.
    bool isClassInstance = false;

    Value() = default;
    template <typename T>
    Value(T v) : data(std::move(v)) {}

    bool isVoid() const { return std::holds_alternative<std::monostate>(data); }
};

bool isTruthy(const Value& v);
bool valuesEqual(const Value& a, const Value& b);

// Real const0u (main.c): a declared-but-not-yet-assigned local/object
// variable under FluffOS. Callers are responsible for the dialect gate
// (real LDMud has no equivalent -- see Value::isUndefined's own
// comment); this just builds the one real shape both VM.cpp's own
// locals-init sites and LpcObject.cpp's object-variable init share, so
// there is exactly one place that constructs it.
inline Value makeUndefinedNumber() {
    Value v(int64_t{0});
    v.isUndefined = true;
    return v;
}

// throw(mixed) -- real FluffOS's own F_THROW (efuns_main.c's f_throw():
// stores the argument into the single global "value to hand back to the
// nearest catch()" (catch_value, interpret.h), then unwinds via the same
// longjmp a runtime error already uses; func_spec.c: "void throw(mixed);"
// -- a real efun, not special grammar the way catch() is). This driver's
// catch() is already implemented as real C++ try/catch bracketing rather
// than setjmp/longjmp (see VM::run()'s own PushCatchFrame/PopCatchFrame
// handling), so throw()'s "carry an arbitrary value to the nearest
// catch()" role is ported the same way: a dedicated exception type
// carrying the real Value, deliberately kept as a subclass of
// LpcRuntimeError rather than changing LpcRuntimeError's own payload
// type -- every existing "throw LpcRuntimeError(...)" call site across
// the compiler/VM/efun table, and every .what() call site that prints
// one, keeps working completely unchanged; only VM::run()'s own
// catch-frame handler needs to tell the two apart (see its own comment).
class LpcThrownValue : public LpcRuntimeError {
public:
    explicit LpcThrownValue(Value v)
        : LpcRuntimeError(describeForWhat(v)), value(std::move(v)) {}

    Value value;

private:
    // Only used if this ever reaches an uncaught-error diagnostic path
    // (the [object]/[net]-prefixed .what() printers) instead of a
    // catch() -- real LPC throw() calls overwhelmingly carry a plain
    // string, matching an ordinary error() message; a non-string thrown
    // value gets a generic placeholder here rather than a full
    // generic Value formatter nothing else in this driver has either.
    static std::string describeForWhat(const Value& v) {
        if (auto* s = std::get_if<std::string>(&v.data)) return *s;
        return "thrown non-string value";
    }
};

struct Array {
    std::vector<Value> items;
};

// Real LPC "buffer" value (MudOS/FluffOS buffer_t, buffer.h: an
// "unsigned short ref; unsigned int size; unsigned char item[size]").
// A fixed-size, zero-initialised, mutable-in-place byte array: the size
// is set once at allocate_buffer() time and never changes (real
// write_buffer() explicitly refuses to write past the end because it
// "can't reallocate the buffer here"). Holds only bytes, never a
// Value, so it can never take part in a reference cycle. Shared by
// std::shared_ptr the same way Array/Mapping/Closure are; real buffers
// are refcounted (buffer_t.ref) and "==" between two buffers is
// pointer identity (eoperators.c f_eq's T_BUFFER case), which
// shared_ptr equality gives directly.
struct Buffer {
    std::vector<unsigned char> bytes;
};

struct Mapping {
    std::vector<std::pair<Value, Value>> entries;
    // Real LDMud mapping->num_values (mapping.c): number of value
    // columns per key, not counting the key. Ordinary FluffOS-style
    // mappings, and every mapping this driver produced before row 1.9's
    // first real width slice, are width 1. Columns 1..width-1 live in
    // extraColumns, kept the same length as entries whenever width > 1
    // (empty whenever width == 1, so every existing column-0 consumer
    // of entries[i].second is unaffected).
    int width = 1;
    std::vector<std::vector<Value>> extraColumns;

    Value getColumn(size_t i, int col) const {
        if (col <= 0) return entries[i].second;
        return extraColumns[i][static_cast<size_t>(col - 1)];
    }
    void setColumn(size_t i, int col, const Value& v) {
        if (col <= 0) entries[i].second = v;
        else extraColumns[i][static_cast<size_t>(col - 1)] = v;
    }
    void appendEntry(Value key, std::vector<Value> values) {
        Value col0 = values.empty() ? Value{} : std::move(values[0]);
        entries.emplace_back(std::move(key), std::move(col0));
        if (width > 1) {
            std::vector<Value> extra(static_cast<size_t>(width - 1), Value(int64_t{0}));
            for (int c = 1; c < width && static_cast<size_t>(c) < values.size(); ++c) {
                extra[static_cast<size_t>(c - 1)] = std::move(values[static_cast<size_t>(c)]);
            }
            extraColumns.push_back(std::move(extra));
        }
    }
    void eraseAt(size_t i) {
        entries.erase(entries.begin() + static_cast<long>(i));
        if (!extraColumns.empty()) {
            extraColumns.erase(extraColumns.begin() + static_cast<long>(i));
        }
    }
};

// A real LPC "function" value -- the "(: name, bound_args... :)" closure
// literal (real FluffOS's funptr_t, see function.h: funptr_hdr_t's
// "owner"/"args" fields plus a type-specific union for FP_EFUN/FP_LOCAL/
// FP_SIMUL/FP_FUNCTIONAL). This driver only implements the bare-
// identifier-name form real usage across this mudlib actually needs
// (confirmed by grepping every "(:" call site, not guessed): an efun,
// local function, or simul_efun referenced by its bare name, with zero
// or more already-bound arguments. Real FluffOS resolves which of
// those four kinds a name is *at construction time* (baked into the
// funptr_t's own type tag via lex.c's identifier classification); this
// driver instead stores just the bare name and re-resolves it lazily at
// *call* time via the same tiered lookup (local/inherited -> simul_efun
// -> efun table) OpCode::Call already uses. This is a deliberate
// simplification, not an oversight: every closure actually reachable in
// this mudlib is constructed and called within the same short-lived
// scope (never persisted across a redefinition or module reload), so
// the two approaches are behaviorally identical for anything this
// driver runs -- see VM::callClosure()'s own comment. The general
// "(: comma_expr :)" inline-lambda form -- which covers what looks
// syntactically like an object-bound closure ("(: obj, \"func\" :)",
// confirmed via grammar.y to actually be a plain comma expression, not
// a dedicated call form -- see Ast.hpp's InlineLambdaExpr) and a bare
// string-constant closure ("(: \"literal\" :)") -- is also implemented,
// but not through this struct's functionName field: CodeGen compiles
// its body to its own synthesized FunctionEntry and stores that
// synthesized name here instead (see CodeGen.cpp's PendingLambda). Only
// $1/$(name)-placeholder inline lambda parameters remain unimplemented:
// nothing on this driver's current boot/login/account-creation path
// uses them (see STATUS.md's closure recon notes for the full count).
struct Closure {
    // The object active when this closure literal was constructed
    // (real funptr_hdr_t::owner, "current_object" at bind time).
    // weak_ptr: a closure outliving its owner's destruction must fail
    // at call time, not keep the object alive artificially, matching
    // real call_function_pointer()'s own "Owner of function pointer is
    // destructed" check.
    std::weak_ptr<LpcObject> owner;
    // The bare name written in the literal.
    std::string functionName;
    // Arguments already bound at construction time ("(: file_size, p
    // :)"'s "p"). Placed *before* any additional call-time arguments
    // when invoked -- confirmed against real FluffOS's own
    // merge_arg_lists() (function.c): bound args are shifted in ahead
    // of whatever is already on the stack from the call site, not
    // appended after.
    std::vector<Value> boundArgs;
    // LDMud's own "#'efun::name" closure-literal prefix ("closure to an
    // efun", temp/ldmud/doc/LPC/closures's own real citation) -- when
    // true, VM::callClosure() skips the normal tiered lfun/inherited ->
    // simul_efun -> efun lookup and resolves functionName against the
    // core efun table only, matching real LDMud semantics exactly:
    // "#'efun::foo" is a closure to *the* efun foo, not whatever
    // lfun/simul_efun of the same name this object might otherwise
    // shadow it with. False for every other closure kind this driver
    // constructs (both "(: name :)" and bare "#'name"), which keep the
    // existing tiered resolution unchanged.
    bool forceEfun = false;

    // LDMud's own unbound_lambda()/bind_lambda() (ROADMAP.md row 1.7/1.8,
    // real closure.c's f_unbound_lambda()/v_bind_lambda()). Real corpus
    // evidence for this: secure/master/hooks.c's own 4 real
    // unbound_lambda() call sites, each handed straight to
    // set_driver_hook() (itself still unimplemented -- see this row's
    // own ROADMAP.md note for the full prerequisite chain this
    // investigation surfaced). A Closure built by unbound_lambda() (see
    // EfunTable.cpp's own registration) has no owner at all yet -- real
    // f_unbound_lambda(): "l->base.ob = const0" (closure.c:6928) -- and
    // carries the real efun's own two arguments verbatim instead of an
    // ordinary functionName: lambdaParams is the declared argument-
    // symbol array (({'item, 'dest}), already resolved to plain names
    // rather than kept as Symbol values -- nothing needs them as
    // anything but a lookup key), lambdaBody is the second argument (the
    // quoted-code call tree, kept as the raw Value exactly as passed,
    // unevaluated until call time -- see VM::evalQuotedLambdaNode()'s
    // own comment for exactly which real quoted-code shapes are walked).
    // unboundUntilBound stays true (real CLOSURE_UNBOUND_LAMBDA) until
    // bind_lambda() flips it false and sets owner (real CLOSURE_
    // BOUND_LAMBDA) -- VM::callClosure() throws real interpret.c's own
    // "Uncallable closure" (interpret.c:21818) for any call attempt
    // while it is still true, exactly matching real int_call_lambda()'s
    // own "no bind_ob and still CLOSURE_UNBOUND_LAMBDA" path. Every
    // other closure kind this driver constructs leaves lambdaBody void
    // and unboundUntilBound false, so callClosure()'s existing tiered-
    // resolution path is completely unaffected.
    bool unboundUntilBound = false;
    std::vector<std::string> lambdaParams;
    Value lambdaBody;
};

} // namespace amlp
