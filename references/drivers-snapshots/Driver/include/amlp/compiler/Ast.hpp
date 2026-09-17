#pragma once
#include <memory>
#include <string>
#include <vector>

namespace amlp {

struct AstNode {
    virtual ~AstNode() = default;
};
using AstPtr = std::unique_ptr<AstNode>;

struct StringLiteral : AstNode {
    std::string value;
};

struct IntLiteral : AstNode {
    int64_t value = 0;
};

struct FloatLiteral : AstNode {
    double value = 0.0;
};

// DGD "nil" literal (ROADMAP.md row 1.2/1.3's own greenlit slice). Real
// DGD (temp/dgd/src/comp/parser.y's own "NIL { $$ = Node::createNil();
// }") -- a stateless marker, no payload, matching the real AST node's
// own shape (Node::createNil() carries only a type tag, no value field
// beyond a constant 0 placeholder).
struct NilLiteral : AstNode {};

// LDMud "'name" symbol literal (ROADMAP.md row 1.7/1.8's own
// unbound_lambda() investigation; see Value.hpp's Symbol comment and
// Lexer::lexQuote() for the real-source citation and lexing detail).
struct SymbolLiteralExpr : AstNode {
    std::string name;
};

struct VarRefExpr : AstNode {
    std::string name;
};

// "And"/"Or" are the short-circuit logical && / || (see emitLogicalExpr);
// "BitAnd" is the plain "&" operator, distinct because it is neither
// short-circuiting nor purely numeric in real LPC -- on two arrays it is
// set intersection, not a bitwise op (confirmed against the FluffOS
// reference driver: eoperators.c's f_and() branches on T_ARRAY before
// falling back to integer "&"). Hit live in secure/daemon/master.c:
// "sizeof(privs & ok)".
// "BitOr"/"BitXor" are the plain "|"/"^" operators, both int-only in
// this driver (unlike BitAnd, real FluffOS's "|" is also array union --
// array.c's f_or() -- but nothing this driver runs yet needs that, only
// the plain-int flags-bitmask shape -- secure/std/login.c's own
// "input_to(\"get_password\", 1 | 2)").
enum class BinOp { Eq, Neq, Lt, Lte, Gt, Gte, Add, Or, And, Sub, Mul, Div, Mod, BitAnd, BitOr, BitXor, Shl, Shr };

struct BinaryExpr : AstNode {
    BinOp op;
    AstPtr left;
    AstPtr right;
};

// grammar.y:1555-1565 comma_expr (CREATE_TWO_VALUES): evaluate left,
// discard it, yield right. Above expr0, not inside it.
struct CommaExpr : AstNode {
    AstPtr left;
    AstPtr right;
};

// BitNot: real LPC/C "~x", one's-complement bitwise NOT (grammar.y:
// "'~' expr0", same "%right L_NOT '~'" precedence tier as unary "!").
// Found live against a real third-party mudlib corpus (row 3.8's TMI-2
// boot attempt): std/user/bitflags.c's own real bitmask-clearing idiom
// ("flags &= ~SOME_FLAG"-shaped code), previously not even lexed --
// Lexer::tokenize() had no case for a bare '~' at all, throwing
// "unrecognized character '~'" outright.
enum class UnaryOp { Not, Neg, BitNot };

struct UnaryExpr : AstNode {
    UnaryOp op = UnaryOp::Not;
    AstPtr operand;
};

struct TernaryExpr : AstNode {
    AstPtr condition;
    AstPtr thenBranch;
    AstPtr elseBranch;
};

struct CallExpr : AstNode {
    std::string callee;
    std::vector<AstPtr> args;
    // Real grammar.y:2488-2496's own "expr_list_node: expr0 |
    // expr0 L_DOT_DOT_DOT" -- a trailing "..." on one call argument
    // spreads a real array into that position at call time (real
    // icode.c's F_EXPAND_VARARGS, see CodeGen::emitSpreadExpansions()'s
    // own citation). Empty when no argument in this call is spread
    // (the overwhelmingly common case), matching this codebase's usual
    // "empty by default" contract for a parallel per-element flag
    // vector; when non-empty it is always exactly args.size() long.
    std::vector<bool> argIsSpread;
    // "efun::name(...)" (grammar.y's "efun_override: L_EFUN L_COLON_COLON
    // identifier"), real LPC's explicit escape hatch straight to the core
    // efun table, skipping local/inherited functions and the simul_efun
    // object entirely -- e.g. secure/SimulEfun/misc.c's own
    // "efun::destruct(ob)", needed there because this file defines its
    // own simul_efun "destruct(object)" wrapper and must still be able to
    // reach the real one. CodeGen::emitCallExpr() emits OpCode::CallEfun
    // instead of the usual tiered OpCode::Call when this is set.
    bool forceEfun = false;

    // "::name(...)" or "qualifier::name(...)" -- an explicit call to an
    // *inherited* definition of a function, bypassing this program's own
    // local definition of the same name even when one exists (grammar.y's
    // function_name production: "L_COLON_COLON identifier" for the bare
    // form, "identifier L_COLON_COLON identifier" for the qualified one
    // -- confirmed live: secure/daemon/account_d.c's own "::create();",
    // calling the daemon.c parent's create() in addition to this file's
    // own override, and secure/daemon/banish.c's "daemon::create();",
    // the same idea naming which ancestor explicitly). Overwhelmingly
    // common across this mudlib (800+ files) since it is the standard
    // way an overridden create()/init() still runs its parent's own
    // setup. An empty parentQualifier means the bare form: search the
    // whole inherited chain depth-first for the nearest match, the same
    // order VM.cpp's findFunctionInChain already walks for a plain call
    // -- just starting one level down, skipping this program's own
    // top-level functions. A non-empty parentQualifier (e.g. "daemon")
    // restricts the search to the inherited program whose own "inherit"
    // path's last path component matches it.
    bool parentCall = false;
    std::string parentQualifier;
};

// The function-name argument is a full expression, not necessarily a
// string literal: real LPC allows "call_other(target, name_var, ...)"
// with the name resolved at run time (confirmed live in the mudlib --
// secure/daemon/master.c's call_other(file, arg), where "arg" is a plain
// variable). The "target->name(...)" syntax is always a literal name
// syntactically (there is no "target->(expr)(...)" form in LPC), so that
// path wraps the identifier in a StringLiteral to fit this same shape.
struct CallOtherExpr : AstNode {
    AstPtr target;
    AstPtr function;
    std::vector<AstPtr> args;
    // Same "..." spread as CallExpr::argIsSpread above (real grammar's
    // own expr_list production is shared verbatim by call_other's
    // argument list, grammar.y:3580's "expr4 L_ARROW identifier '('
    // expr_list ')'"; confirmed real, not assumed, against
    // Dead Souls 3.8.2's lib/lib/magic.c:85's own
    // "spell->eventParse(this_object(), args...)"). Covers only
    // `args` above, never target/function -- those are separately
    // typed fields, not part of this vector, and real corpus never
    // spreads either one.
    std::vector<bool> argIsSpread;
};

// sscanf(target, format, ...outputVars). Real LPC gives sscanf() its own
// grammar production (grammar.y's "sscanf:" rule uses "lvalue_list", not
// a plain arg list) precisely because its trailing arguments are implicit
// lvalues, not ordinary by-value expressions -- there is no "&var"
// reference syntax in LPC's sscanf, unlike C's. A real LPC lvalue is not
// limited to a bare variable name, though -- confirmed live against a
// real third-party mudlib corpus (row 3.8's TMI-2 boot attempt), not
// assumed: TMI-2's own real, unmodified adm/obj/master/access.c has
// "sscanf(lines[i], "(%s)%s", path, lines[i])", writing its second
// output straight back into the very array element the source string was
// read from, a real, valid indexed-lvalue output argument this driver
// previously rejected outright (see Parser.cpp's own prior "must be a
// plain variable name" error, now relaxed). varNames still covers the
// plain-variable case exactly as before (empty string at position i
// means indexedTargets[i] is used instead); indexedTargets is a parallel,
// same-length vector, holding an IndexExpr (arr[i], map[k], or LDMud
// map[k, n]) at any position whose output argument was an indexed
// expression rather than a bare name, null everywhere else. See
// CodeGen::emitSscanfExpr()'s own comment for why this needs a hidden
// temp local rather than a change to OpCode::Sscanf itself.
struct SscanfExpr : AstNode {
    AstPtr target;
    AstPtr format;
    std::vector<std::string> varNames;
    std::vector<AstPtr> indexedTargets;
};

// "catch(expr)", real LPC's own control-flow construct for trapping a
// runtime error (grammar.y: "catch: L_CATCH expr_or_block", a dedicated
// grammar production, not a function call -- confirmed against the
// FluffOS reference driver directly, not inferred). Only the
// parenthesized-expression form is implemented ("catch(expr)"), not the
// block form ("catch { stmts }") grammar.y's own "expr_or_block: block |
// '(' comma_expr ')'" also allows -- nothing in this mudlib uses the
// block form. See CodeGen::emitCatchExpr()/VM.cpp's PushCatchFrame
// handling for the runtime semantics (guarded's own result is always
// discarded; catch(expr) evaluates to 0 on success or the error message
// string on failure, confirmed against interpret.c's F_END_CATCH/
// do_catch()).
struct CatchExpr : AstNode {
    AstPtr guarded;
};

// "(: name, bound_args... :)" -- a closure/function-pointer literal
// (see Value.hpp's Closure comment for the full citation and the real
// forms deliberately not implemented). Only the bare-identifier form:
// "name" is always a plain identifier token here, resolved lazily at
// call time rather than at parse/codegen time (see
// VM::callClosure()'s own comment) -- there is no expression form
// here, unlike CallOtherExpr's function-name argument.
struct ClosureLiteralExpr : AstNode {
    std::string functionName;
    std::vector<AstPtr> boundArgs;
    // LDMud's own "#'efun::name" prefix (see Value.hpp's Closure::
    // forceEfun for the full real-source citation) -- only ever set true
    // by Parser.cpp's own "#'efun::" recognition; every other closure
    // literal this driver parses ("(: name :)" and bare "#'name") leaves
    // it at the default false, unchanged resolution.
    bool forceEfun = false;
};

// "(: comma_expr :)" -- the general "inline lambda" closure literal
// grammar.y keeps as a distinct production from the bare-identifier one
// above ("L_FUNCTION_OPEN comma_expr ':' ')'", confirmed by direct
// reading, not guessed: real LPC's LALR grammar has two separate rules
// here, told apart at parse time by whether the first token is a bare
// name immediately followed by "," or ":" -- see Parser.cpp's own
// comment at the "(:" recognition site for the disambiguation and the
// two confirmed real call sites this covers, std/user/editor.c lines 31
// and 64). Every expression in bodyExprs is evaluated in order for side
// effect except the last, whose value becomes the closure's return
// value when later invoked -- ordinary comma-expression semantics,
// matching grammar.y's own "comma_expr: expr0 | comma_expr ',' expr0"
// used for the body. Confirmed via grammar.y's own
// "if ($2->kind == NODE_STRING) yywarn(\"Function pointer returning
// string constant is NOT a function call\")": the reference compiler
// accepts a body ending in a bare string constant and only *warns* that
// it looks like a mistake, proving the body is real compiled code run
// at call time (not a disguised "call this method name on this
// object"), i.e. std/user/editor.c's own
// "(: previous_object(), \"abort\" :)" really does evaluate
// previous_object() for effect and then just return the string
// "abort" when invoked, never actually calling abort() on anything.
// See CodeGen.cpp's PendingLambda handling for how this compiles to
// its own anonymous FunctionEntry, and Value.hpp's Closure comment
// (now superseded for this case) for the prior bare-name-only scope
// note.
struct InlineLambdaExpr : AstNode {
    std::vector<AstPtr> bodyExprs;
    // The lambda's own implicit parameter count, i.e. the highest "$N"
    // referenced anywhere in bodyExprs (0 if none) -- see LambdaParamExpr
    // just below and Lexer.cpp's own "$" handling for the real source
    // this mirrors (lex.c's "$var": "current_function_context->
    // num_parameters = yylval.number + 1" on the highest digit seen).
    // Set by Parser::parsePrimary() while this lambda's body is being
    // parsed (Parser::lambdaParamMaxStack_), consumed by CodeGen's
    // emitPendingLambdas() to size the compiled function's own
    // numArgs/numLocals so a caller's arguments land in the right slots.
    int paramCount = 0;
    // Real LPC's own "$(expr)" bound-variable form (grammar.y.pre's own
    // "'$' '(' comma_expr ')'", distinct from a plain "$N" -- see
    // LambdaParamExpr's own comment for the full real-source citation
    // and why this driver now implements it too). Each entry is one
    // "$(expr)" occurrence's own captured expression, in the order
    // encountered while parsing this lambda's body (Parser's own
    // lambdaBoundValuesStack_) -- real semantics: expr is evaluated once,
    // at the *enclosing* function's own current scope, the moment this
    // "(: :)" literal itself is constructed (not each time the closure
    // is later called), and its value is bound ahead of any real
    // call-time argument -- exactly the same real semantics, and this
    // driver's own existing mechanism (Closure::boundArgs,
    // CodeGen::emitExpr()'s own ClosureLiteralExpr case, VM::
    // callClosure()'s own "boundArgs then extraArgs" merge order), the
    // explicit bound-arg-list form "(: name, arg1, arg2 :)" already has
    // -- "$(expr)" is real LPC's own syntactic sugar for the identical
    // mechanism, spelled inline at first use instead of pre-declared in
    // a leading list. Compiled at the literal's own construction site
    // (CodeGen::emitExpr()'s own InlineLambdaExpr case), immediately,
    // in the *caller's* current locals_/objectVars_ scope -- before this
    // lambda's own body is queued for its later, separate, deferred
    // compile (emitPendingLambdas(), a fresh, empty locals_ scope) --
    // giving real access to the enclosing function's own locals, which
    // the body itself (compiled later, in that fresh scope) cannot
    // reach directly, matching real grammar.y.pre's own "current_
    // function_context = current_function_context->parent" switch while
    // parsing "expr" specifically.
    std::vector<AstPtr> boundValueExprs;
};

// "$N" (e.g. "$1", "$2") inside a "(: ... :)" body -- real lex.c's own
// L_PARAMETER token: an implicit reference to the *closure's own* Nth
// call-time argument (1-indexed in source, matching real LPC), not a
// declared local variable. Confirmed real and load-bearing, not
// theoretical: secure/daemon/events.c's own real, unguarded
// "filter(users(), (: $1 && environment($1) :))" (EVENTS_D's day/night
// cycle advancement) -- EVENTS_D backs secure/SimulEfun/time.c's
// night()/day()/hour()/etc and secure/SimulEfun/light.c's day/night
// lighting, so failing to parse this one file at all (this driver's
// lexer previously treated a bare "$" as an unrecognized character and
// threw) broke every one of those simul_efuns' own real call sites the
// instant EVENTS_D was ever loaded.
//
// Real LPC's "$(expr)" bound-variable-capture form (real grammar.y.pre's
// own "'$' '(' comma_expr ')'") is now also implemented (this same node
// kind doubles for both a "$N" reference and a "$(expr)" occurrence's
// own reference to its assigned slot, told apart by isBoundValue below)
// -- an earlier session's own "not implemented, no reachable real use"
// scoping note here is now stale and superseded: Dead Souls 3.8.2's own
// real corpus (this driver's own row 3.10 boot attempt) uses it
// pervasively and reachably -- lib/body.c, lib/door.c, lib/exits.c,
// lib/living.c, lib/magic.c, and more (e.g. lib/firearm.c's own real
// "(: answers_to($(target), $1) :)").
//
// Real semantics (confirmed directly against fluffos-2.23-ds03's own
// icode.c:533-544, "case NODE_PARAMETER: { int which = expr->v.number +
// current_num_values; ... }", where current_num_values is set to the
// real total count of this same closure's own "$(expr)" bindings just
// before its body is walked, icode.c:761-767): every "$(expr)" binding
// occupies a real closure-call-frame slot, in encounter order, starting
// at 0 -- exactly the same slots Closure::boundArgs already occupy at
// runtime (VM::callClosure()'s own "boundArgs then extraArgs" merge) --
// and any *explicit* "$N" elsewhere in the same body is offset by the
// real total "$(expr)" count for that closure, so the programmer's own
// "$1" still means "the closure's own first real call-time argument"
// regardless of how many "$(expr)" bindings appear alongside it. This
// offset is only knowable once the whole body has been parsed (this
// driver's own InlineLambdaExpr::boundValueExprs is only complete then),
// so it is applied at CodeGen time, not parse time -- see CodeGen.cpp's
// own emitPendingLambdas() for exactly where.
struct LambdaParamExpr : AstNode {
    // For an ordinary "$N" (isBoundValue false): 0-indexed real
    // call-time-argument slot ("$1" -> 0, "$2" -> 1, ...), *before* the
    // enclosing closure's own "$(expr)" count is added at CodeGen time.
    // For a "$(expr)" occurrence's own reference (isBoundValue true):
    // the real, final 0-indexed slot directly (its own position in
    // InlineLambdaExpr::boundValueExprs), needing no further offset at
    // all, since bound values always occupy the lowest slots already.
    int index = 0;
    bool isBoundValue = false;
};

struct ExprStmt : AstNode {
    AstPtr expr;
};

struct ReturnStmt : AstNode {
    AstPtr expr;
};

struct VarDeclStmt : AstNode {
    std::string type;
    bool isArray = false;
    std::string name;
    AstPtr initializer;
};

struct AssignStmt : AstNode {
    std::string name;
    AstPtr value;
};

// Assignment as an expression (as opposed to AssignStmt, which is
// statement-level "name = expr;"). Needed so assignment can appear inside a
// for-loop's init/update clauses ("for (i = 0; ...; i = i + 1)") and other
// expression contexts, matching real LPC where "=" is a right-associative
// expression operator (grammar.y: "%right L_ASSIGN", the lowest-precedence
// operator, looser even than "?:"). Only a bare variable name target is
// supported this slice, same scope limitation AssignStmt already has.
// isCompound covers "+=", "-=", "*=", "/=", "%=" (e.g. real usage in
// secure/daemon/master.c: "files += ({ lines[i] });"), each desugaring at
// codegen time to "name = name <op> value". Since the target here is
// always a bare variable name (never an expression with side effects),
// desugaring to a read-modify-write is behaviorally identical to real
// LPC's single-evaluation compound assignment, just simpler to generate.
struct AssignExpr : AstNode {
    std::string name;
    AstPtr value;
    bool isCompound = false;
    BinOp compoundOp = BinOp::Add;
};

enum class IncDecOp { Inc, Dec };

// "++x" / "--x" (prefix) and "x++" / "x--" (postfix). Scoped to a bare
// variable name target for now (needed by for-loop update clauses and
// standalone statements, per the plan); indexed targets like "arr[i]++"
// are not supported and throw a clear parse error rather than silently
// misparsing.
// "name" is used when indexTarget is null (the original bare-variable-
// only scope this struct started with); a non-null indexTarget/indexKey
// pair means an indexed target instead, e.g. std/living.c's own
// "healing[\"intox\"]--" (confirmed live -- the driver previously
// rejected this real mudlib line the same way IndexAssignExpr's own
// comment describes for indexed assignment, and for the same reason:
// only a bare variable name was recognized as a valid ++/-- target).
// See CodeGen::emitIncDecExpr()'s indexed branch for the codegen.
//
// mapColumn mirrors IndexExpr/IndexAssignExpr's own field (LDMud
// map[key, n], ROADMAP.md row 1.9): real LDMud's generic lvalue
// increment machinery has its own genuine F_MAP_INDEX_LVALUE operator
// (prolang.y:17018, interpret.c:16944) backing "map[key, n]++" the same
// way F_INDEX_LVALUE backs a plain "arr[i]++" -- this is not a made-up
// extension. Parser::parsePostfix()/parseUnary() previously parsed an
// IndexExpr with mapColumn set, then silently dropped it while copying
// indexTarget/indexKey onto this struct, so "map[key, 1]++" compiled
// without error but silently mutated column 0 instead of column 1 --
// found and fixed the same session the width-2 slice landed, before
// any real code could rely on the wrong behavior.
struct IncDecExpr : AstNode {
    IncDecOp op = IncDecOp::Inc;
    bool prefix = true;
    std::string name;
    AstPtr indexTarget;
    AstPtr indexKey;
    AstPtr mapColumn;
};

struct ArrayLiteralExpr : AstNode {
    std::vector<AstPtr> elements;
    // Real grammar.y:3197's own array literal ("L_ARRAY_OPEN expr_list
    // '}' ')'") reuses the identical expr_list/expr_list_node production
    // CallExpr::argIsSpread cites -- same "..." spread syntax, same
    // empty-by-default contract, see that field's own comment.
    std::vector<bool> elementIsSpread;
};

struct MappingLiteralExpr : AstNode {
    // values.size() is the mapping width; every entry has the same count
    // (Parser rejects "Inconsistent number of values in mapping literal",
    // real prolang.y:17246). Width 1 (a single value after ':') is the
    // ordinary FluffOS/LDMud shape; width > 1 is LDMud-only
    // ("key: v0; v1; ...", ROADMAP.md row 1.9).
    std::vector<std::pair<AstPtr, std::vector<AstPtr>>> entries;
};

// "<N" inside an index/range bound means "N from the end", real LPC's
// own reverse-indexing syntax (grammar.y: "expr4 '[' '<' comma_expr
// ']'" and its range-form siblings) -- confirmed against eoperators.c's
// f_range()/f_extract_range(): the actual index used is "length - N"
// (so "<1" is the last element, "<2" the second-to-last, ...), computed
// against the target's own runtime length, not resolvable at parse
// time. indexFromEnd/rangeEndFromEnd record which bound(s), if any,
// used this form; see VM.cpp's OpCode::Index/RangeIndex handling for
// where the actual "length - N" conversion happens.
// Real FluffOS's "class <name> { <member decls> }" struct-type member
// access, "instance->member" (ROADMAP.md row 3.10's own class scoping
// report: grammar.y's "expr4 L_ARROW identifier" with no trailing "(",
// F_MEMBER/F_MEMBER_LVALUE, both resolved to a plain compile-time-known
// array index against the target's declared class -- real member
// resolution is 100% compile time, never at runtime). Placeholder value
// for IndexExpr::index/IndexAssignStmt::index/IndexAssignExpr::index
// specifically -- never emitted as an ordinary sub-expression via
// emitExpr(); CodeGen recognizes this node type wherever it would
// otherwise call emitExpr() on an index, and substitutes a compile-
// time-resolved literal index push instead (see CodeGen::emitIndexValue()).
// This reuses IndexExpr/IndexAssignStmt/IndexAssignExpr and the existing
// Index/IndexAssign opcodes wholesale rather than adding a new AST
// statement/expression kind or a new VM opcode for member access --
// parsePostfix()'s own "->identifier" (no paren) branch builds exactly
// the same IndexExpr shape "->identifier(" (call_other) already does,
// just with this node as the index instead of a parsed expression, so
// the pre-existing indexed-assignment-target reinterpretation in
// parseStatement()/the assignment-expression path (both already keyed
// on "is this operand an IndexExpr") pick up a member-access target for
// free, with zero additional statement/expression-level parsing code.
struct MemberNameMarker : AstNode {
    std::string name;
};

// "(class Name)expr" (grammar.y:780-786 cast, basic_type includes
// L_CLASS identifier). Runtime no-op; className is for ->member
// resolution. Ordinary "(string)expr" stays stripped, not this node.
struct TypeCastExpr : AstNode {
    std::string className;
    AstPtr inner;
};

// "class <name> { <member decls> }" -- the declaration itself (real
// grammar.y's own "type_decl: type_modifier_list L_CLASS identifier '{'
// member_list '}'", no trailing ";"). Member types are parsed and
// discarded, matching this driver's existing convention for every other
// declared type (int/string/mixed/... are already cosmetic at runtime
// throughout this codebase -- see ObjectVarDecl::type's own comment);
// only declared member order matters, the real class_def_t/
// class_member_entry_t analog (see CodeGen.hpp's own classDefs_
// comment). FluffOS-dialect-only: real LDMud has no equivalent concept
// (confirmed against temp/ldmud/src/svalue.h in this row's own scoping
// report), so Parser::parseProgram() only ever recognizes this shape
// under LpcDialect::FluffOS -- under any other dialect "class" simply
// stays an ordinary, unreserved identifier, matching the same double-
// gating discipline this codebase already uses for "nil"/"atomic"
// (DGD) and "array" (ARRAY_RESERVED_WORD).
struct ClassDeclStmt : AstNode {
    std::string name;
    std::vector<std::string> memberNames;
};

// "new(class Name field: val, field: val, ...)" -- the construction
// literal (real grammar.y's own "L_NEW '(' L_CLASS L_DEFINED_NAME
// opt_class_init ')'", NOT a positional "(class name val, val, ...)"
// literal -- real class_init is "identifier ':' expr0", named fields,
// any subset, any order). fieldInits holds exactly what was written in
// source order (possibly a subset of the class's real declared
// members, possibly empty for "new(class Name)"); CodeGen::emitExpr()'s
// own NewClassExpr case reorders these into the class's real declared-
// member order and synthesizes a literal 0 for every omitted member,
// mirroring real reorder_class_values() (compiler.c) exactly -- see
// that function's own citation in CodeGen.cpp. FluffOS-dialect-only,
// same reasoning as ClassDeclStmt above.
struct NewClassExpr : AstNode {
    std::string className;
    std::vector<std::pair<std::string, AstPtr>> fieldInits;
};

struct IndexExpr : AstNode {
    AstPtr target;
    AstPtr index;
    AstPtr rangeEnd = nullptr; // non-null means range index, e.g. str[start..end]
    bool indexFromEnd = false;
    bool rangeEndFromEnd = false;
    // LDMud mapping column index, map[key, n] (real index_map_expr,
    // prolang.y:17007, F_MAP_INDEX). Null for ordinary map[key] /
    // arr[i] / str[i]. Range form map[key, n1..n2] is not represented
    // here (still row 1.9 remaining scope).
    AstPtr mapColumn;
};

// isCompound/compoundOp mirror AssignExpr's own fields: "target[index]
// += value" etc, e.g. std/user.c's own "player_data[\"general\"]
// [\"quest points\"] += (int)call_other(...)". target/index are
// evaluated twice by CodeGen::emitIndexAssignStmt() in this case (once
// to read the current value, once to write the new one) rather than
// duplicated on the stack -- harmless for every real target/index this
// mudlib's own compound-indexed-assignment call sites actually use
// (plain variable reads and string-literal keys, no side effects), but
// would double any side effect a more exotic target/index expression
// happened to have. Flagged here rather than silently assumed safe.
struct IndexAssignStmt : AstNode {
    AstPtr target;
    AstPtr index;
    AstPtr value;
    bool isCompound = false;
    BinOp compoundOp = BinOp::Add;
    AstPtr mapColumn; // LDMud map[key, n] = value, same as IndexExpr
};

// The expression-producing counterpart to IndexAssignStmt above, needed
// when an indexed assignment appears as a sub-expression rather than a
// standalone statement -- e.g. std/user/more.c's own
// "if(!(__More[\"class\"] = cl)) ...", confirmed live: the driver's
// parser previously only recognized a bare variable name as an
// assignment target inside an expression (see Parser.cpp's own comment
// at the "sawAssignOp" site) and rejected this real mudlib line
// outright. Same fields as IndexAssignStmt; see
// CodeGen::emitIndexAssignExpr() for why this needs its own codegen
// rather than reusing emitIndexAssignStmt() as-is (OpCode::IndexAssign
// consumes its three operands and leaves nothing behind, matching
// statement-context needs, but an expression use needs the assigned
// value left on the stack afterward).
struct IndexAssignExpr : AstNode {
    AstPtr target;
    AstPtr index;
    AstPtr value;
    bool isCompound = false;
    BinOp compoundOp = BinOp::Add;
    AstPtr mapColumn; // LDMud map[key, n] = value, same as IndexExpr
};

struct Block : AstNode {
    std::vector<AstPtr> statements;
    // True for every real "{ ... }" (a standalone scoping statement, an
    // if/while/for body, or a function's own top-level body): CodeGen
    // gives these their own nested local-variable scope. False only for
    // the synthetic Block Parser::parseVarDeclStatement() builds to wrap
    // a comma-separated declaration ("string a, b;") as a single
    // AstPtr slot -- that Block is not a real scope at all, just an AST
    // convenience, and its declarations must join the *enclosing* scope
    // normally (see CodeGen::emitStatement()'s own comment on this
    // distinction, added after a real regression: treating it as a real
    // scope popped "a"/"b"/"c" back out of locals_ immediately after the
    // one decl statement, breaking every later reference to them in the
    // same function).
    bool isRealScope = true;
};

// "time_expression { <body> }" -- real fluffos-2.23-ds03's own
// benchmarking expression (confirmed directly against that real source,
// not assumed): "grammar.y.pre: time_expression: L_TIME_EXPRESSION ...
// expr_or_block { CREATE_TIME_EXPRESSION($$, $3); }", "expr_or_block:
// block | '(' comma_expr ')'". Real corpus (Dead Souls 3.8.2's own boot
// attempt): all 4 real call sites (secure/daemon/master.c and
// secure/obj/weirder.c) use the real block form, never the parenthesized-
// expression alternative, so only the block form is implemented here --
// the same narrow-by-evidence choice this driver's own existing
// CatchExpr already made for the identical "expr_or_block" grammar
// nonterminal (see its own comment: "nothing in this mudlib uses that
// [block] form", the mirror-image gap of this one).
//
// Real semantics (interpret.c's own F_TIME_EXPRESSION/F_END_TIME_
// EXPRESSION, confirmed directly): records the current time, runs the
// body once for its own side effects (its own value, if it evaluated to
// one internally, is not the result), and evaluates to the real elapsed
// microseconds as a plain int. See CodeGen::emitExpr()'s own
// TimeExpressionExpr case for how this compiles (inline, not deferred/
// hoisted like a closure literal -- this is an ordinary, immediate
// expression, not a first-class value) and VM.cpp's own
// TimeExpressionStart/TimeExpressionEnd opcodes for the runtime timing
// mechanism (a real std::chrono::steady_clock interval, not literally
// mirroring real get_usec_clock()'s own gettimeofday()-based
// implementation, since only the observable elapsed-microseconds result
// matters here, not the underlying clock source).
struct TimeExpressionExpr : AstNode {
    std::unique_ptr<Block> body;
};

struct IfStmt : AstNode {
    AstPtr condition;
    std::unique_ptr<Block> thenBranch;
    std::unique_ptr<Block> elseBranch;
};

struct WhileStmt : AstNode {
    AstPtr condition;
    std::unique_ptr<Block> body;
};

// "do body while (condition);" -- real LPC/C's post-test loop: the body
// always runs at least once, and the condition is checked only after each
// iteration. Deliberately its own node rather than reusing WhileStmt with a
// flag: the codegen shape is genuinely different (condition check after
// the body instead of before, see CodeGen::emitDoWhileStmt), and a shared
// node would make that harder to read at both call sites for no benefit.
struct DoWhileStmt : AstNode {
    AstPtr condition;
    std::unique_ptr<Block> body;
};

// "for (init; condition; update) body". init is either a VarDeclStmt (a
// single declaration, optionally with an initializer, e.g. "int i = 0") or
// a plain expression statement (e.g. "i = 0", reusing AssignExpr); any of
// the three clauses may be empty (null), matching real LPC's
// "for_expr: /* EMPTY */ | comma_expr" grammar rule -- an empty condition
// means "always true", same as C.
struct ForStmt : AstNode {
    AstPtr init;
    AstPtr condition;
    AstPtr update;
    std::unique_ptr<Block> body;
};

struct BreakStmt : AstNode {};
struct ContinueStmt : AstNode {};

// "foreach (var in collection) body" and "foreach (var, valueVar in
// collection) body". Each loop variable may be a pre-existing name
// (declareXxxVar false, resolved like any other variable reference) or
// declare a brand-new local inline ("foreach (string s in ...)",
// declareXxxVar true) -- both are real LPC (grammar.y's foreach_var:
// L_DEFINED_NAME | single_new_local_def). For a mapping, single-variable
// foreach iterates its keys (matching real LPC); two-variable foreach
// iterates (key, value) pairs. For an array, single-variable foreach
// iterates elements; two-variable foreach over a plain array is not
// meaningfully supported (real usage of the two-variable form in this
// mudlib is always over mappings) -- see CodeGen::emitForeachStmt.
// A single "case value:" or "default:" label with no attached statements
// of its own -- it is a marker interleaved into SwitchStmt::body in
// source order, exactly where it appeared, so fallthrough (no implicit
// break between cases, matching real LPC/C) falls out naturally from
// just emitting body statements in that same order. value == nullptr
// means "default:". rangeEnd, non-null, means a real LPC/FluffOS range
// case label ("case A..B:", grammar.y's own "L_CASE case_label L_RANGE
// case_label ':'") -- matches when subject is anywhere in [value,
// rangeEnd] inclusive, not just equal to value. Found live against a
// real third-party mudlib corpus (row 3.8's TMI-2 boot attempt):
// std/user.c's own real "case 2..3:" / "case 4..6:" / ... ladder (a
// level-tier lookup), previously rejected outright with a deliberate
// "not implemented" error since nothing in this driver's own bundled
// mudlib used the form -- see CodeGen::emitSwitchStmt() for the range
// comparison codegen. The open-ended real grammar forms ("case A..:",
// "case ..B:") are a separate, still-unevidenced gap -- neither appears
// anywhere in that same TMI-2 corpus pass -- and stay unimplemented.
struct CaseLabel : AstNode {
    AstPtr value;
    AstPtr rangeEnd;
};

struct SwitchStmt : AstNode {
    AstPtr subject;
    std::vector<AstPtr> body; // CaseLabel and ordinary statement nodes, interleaved
};

struct ForeachStmt : AstNode {
    std::string varName;
    bool declareVar = false;
    bool hasValueVar = false;
    std::string valueVarName;
    bool declareValueVar = false;
    AstPtr collection;
    std::unique_ptr<Block> body;
    // Real corpus: secure/daemon/inet.c's own "foreach(string svc, class
    // service s in Services)" (ROADMAP.md row 3.10's class scoping
    // report). Empty means "not declared with a class type" (the
    // overwhelmingly common case, and the only shape before this row);
    // a declared class name here lets CodeGen resolve a later
    // "s->member" inside the loop body the same way any other class-
    // typed local declaration does -- see Parser::parseForeachVar()'s
    // own comment for why this previously had nowhere to go.
    std::string varClassType;
    std::string valueVarClassType;
};

struct Param {
    std::string type;
    std::string name;
    bool isArray = false;
};

struct FunctionDecl : AstNode {
    std::string returnType;
    bool returnTypeIsArray = false;
    std::string name;
    std::vector<Param> params;
    // Real grammar.y:706-717's own "argument_list L_DOT_DOT_DOT" -- the
    // last declared parameter is a real varargs rest-parameter (real
    // interpret.c:1394-1410's own setup_varargs_variables(), see VM.cpp's
    // own VM::run() comment for the full citation): every caller argument
    // at or past that parameter's own position is collected into a real
    // array bound to it, a real empty array (not undefined/0) when the
    // caller supplied too few arguments to reach it. Parser::parseParamList()
    // used to parse and discard the trailing "..." (see its own prior
    // comment, corrected); this field is what makes it real.
    bool isVarargs = false;
    std::unique_ptr<Block> body;
};

// Real modern FluffOS's own "function(<params>) { <body> }" anonymous-
// function *expression* (row 3.10's own named blocker, ROADMAP.md,
// scoped in full in this session's own prior scoping report before any
// code was written): confirmed directly against Dead Souls 3.8.2's own
// bundled fluffos-2.23-ds03/grammar.y.pre, "expr0: ... | L_BASIC_TYPE
// '(' argument ')' block" gated on the L_BASIC_TYPE token being
// specifically TYPE_FUNCTION, building a real NODE_ANON_FUNC. A real,
// named parameter list (the identical "argument" grammar an ordinary
// function definition's own parameters already use) plus a real,
// full statement-block body, evaluating to a callable closure value.
//
// Deliberately holds real Param/Block, not InlineLambdaExpr's own
// paramCount/bodyExprs shape ("(: comma_expr :)", a structurally
// different, older closure form): real "(: :)" bodies are a single
// comma-expression addressed only by positional "$1"/"$2" references,
// while this form's body is exactly as rich as an ordinary function's
// own (if/while/for/local declarations/multiple statements/explicit
// return), with real, named parameters.
//
// No lexical capture of the enclosing function's own locals -- this is
// not a simplification or a deferred follow-on, it is real FluffOS's
// own actual behavior, confirmed three independent ways directly
// against fluffos-2.23-ds03's real source before writing anything
// here, not assumed: (1) icode.c's own NODE_ANON_FUNC codegen and
// eoperators.c's own f_function_constructor()/FP_ANONYMOUS case never
// touch the evaluation stack for anything but the anon-func's own
// literal byte operands (num_arg/num_local/length), unlike FP_
// FUNCTIONAL's own sibling case which explicitly binds stack values
// via its own "args" parameter; (2) function.c's own
// make_functional_funp() stores only a program pointer, a byte offset,
// num_arg, and num_local on the resulting funptr_t -- there is no
// slot anywhere in funptr_hdr_t/functional_t for a captured value, and
// function.c's own call_function_pointer() FP_FUNCTIONAL case (the
// same runtime kind both this form and "(: :)" actually share --
// FP_ANONYMOUS is purely f_function_constructor()'s own byte-decoding
// tag, never the funptr's own stored type) calls setup_variables()
// with a fresh, isolated frame sized only for the closure's own
// num_arg+num_local, nothing shared with the enclosing call's own
// frame; (3) compiler.c's own deactivate_current_locals(), called on
// entering the anon-func body (grammar.y.pre's own L_BASIC_TYPE(
// function) mid-rule action) sets every currently-active enclosing
// local's own dn.local_num = -1 for the duration -- the real compiler
// cannot even resolve an enclosing local's name while compiling this
// body, confirming this is enforced at parse time too, not just
// missing at runtime. Real corpus code needing outer state instead
// threads it explicitly (confirmed live, not assumed: lib/persist.c's
// own real "call_out(function(object p){...}, 1, prev)", the local
// "prev" passed as call_out()'s own extra call-time argument, becoming
// the closure's own real "p" parameter when it fires) or uses the
// owning object's own object variables (confirmed live: lib/body.c's
// own real "evaluate(function(){ Dying = 0; })", "Dying" a real object
// variable, not a capture) -- both already fall out of this driver's
// own existing, unmodified machinery (real call-time arguments already
// merge into a closure's own params the same way "(: :)"'s boundArgs
// already do; object variables were never cleared by "(: :)"'s own
// existing compilation either, only locals_ was). A corpus-wide scan
// (25 real files) and a direct sample of 5 of them, read in full,
// confirmed every real body stays within this exact real constraint --
// see this session's own prior scoping report, and STATUS.md's own
// entry for this session, for the complete real-source derivation and
// the corpus sample.
struct AnonFunctionExpr : AstNode {
    std::vector<Param> params;
    // See FunctionDecl::isVarargs's own citation -- parseParamList() is
    // shared by both, so a "function(mixed args...) { ... }" body gets
    // the identical real rest-parameter semantics.
    bool isVarargs = false;
    std::unique_ptr<Block> body;
};

struct ObjectVarDecl : AstNode {
    std::string type;
    bool isArray = false;
    std::string name;
    // See Parser.hpp's DeclPrefix::isPrivate comment. Consumed by
    // CodeGen::generate(): a private variable still occupies a real slot
    // in the flattened per-object layout (an inheriting child's own
    // code must land at the same slot offsets a parent's already-
    // compiled bytecode expects), but that slot is recorded under a
    // synthesized, non-collidable placeholder name in the
    // CompiledProgram::objectVarNames a child inherits, rather than its
    // real name -- so the real name stays reachable from this file's
    // own code (ordinary resolveVariable() lookups here still use it)
    // while staying invisible to, and non-collidable with, any child.
    bool isPrivate = false;
    // "type name = expr;" -- real, standard LPC (confirmed against the
    // FluffOS reference driver's grammar), needed live by secure/daemon/
    // wiztools.c's own "string *REISSUED_TOOLS = ({ ... });". Evaluated
    // once per object instance, before create() runs (there is no
    // dedicated grammar-level "initializer" production in real LPC;
    // real compilers thread this into the object's own implicit
    // initialization sequence the same way this driver does -- see
    // CodeGen::generate()'s own synthesized "$objvarinit" function and
    // ObjectManager::runObjectVarInitializers()). Null when this
    // variable has no initializer, the overwhelmingly common case.
    AstPtr initializer;
};

struct Program : AstNode {
    std::vector<std::unique_ptr<FunctionDecl>> functions;
    std::vector<std::unique_ptr<ObjectVarDecl>> objectVars;
    std::vector<std::string> inherits;
    // Real "class <name> { ... }" declarations at this file's own top
    // level (ROADMAP.md row 3.10's class scoping report) -- see
    // ClassDeclStmt's own comment. Empty under every dialect but
    // FluffOS, and under FluffOS whenever this file declares none.
    std::vector<std::unique_ptr<ClassDeclStmt>> classes;
};

} // namespace amlp
