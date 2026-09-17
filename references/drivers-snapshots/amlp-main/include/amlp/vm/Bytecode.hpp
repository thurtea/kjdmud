#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace amlp {

enum class OpCode : uint8_t {
    PushConst,
    PushInt,
    // A float literal ("1.5", ".5"). Not folded into PushInt's own
    // operand (an int32_t cannot hold an arbitrary double) or into
    // PushConst's stringPool (a float is not a string) -- indexes into
    // CompiledProgram::floatPool instead, mirroring PushConst/stringPool
    // exactly.
    PushFloat,
    // DGD's "nil" literal (ROADMAP.md row 1.2/1.3's greenlit slice, row
    // 1.10's minimal real piece -- see Ast.hpp's NilLiteral and
    // Value.hpp's Nil). No operand -- unlike PushConst/PushFloat, nil
    // carries no payload to index into any pool, real DGD's own
    // runtime nil value (data.cpp's "Value nil = { T_NIL, TRUE };")
    // being a stateless singleton too. Only ever emitted under
    // LpcDialect::DGD (see CodeGen::generate()'s own NilLiteral case).
    PushNil,
    // LDMud "'name" symbol literal (ROADMAP.md row 1.7/1.8, see
    // Value.hpp's Symbol and Ast.hpp's SymbolLiteralExpr). operand
    // indexes into CompiledProgram::stringPool for the bare name, the
    // same pool PushConst/PushClosure already use -- a symbol's name is
    // always available at compile time (it is written literally in the
    // source), so there is nothing to compute at run time the way
    // PushNil's stateless singleton has nothing to index at all.
    PushSymbol,
    PushLocal,
    StoreLocal,
    PushObjectVar,
    StoreObjectVar,
    Add, Sub, Mul, Div, Mod,
    // Plain "&": bitwise AND on two ints, set intersection on two arrays
    // (see Ast.hpp's BinOp::BitAnd comment). Named distinctly from the
    // logical &&/|| in BinOp since those never reach a plain opcode at
    // all (they desugar to jumps, see CodeGen::emitLogicalExpr).
    BitAnd,
    // Plain "|"/"^": int-only bitwise OR/XOR this slice (see Ast.hpp's
    // BinOp::BitOr/BitXor comment -- real FluffOS's "|" is also array
    // union, not replicated here since nothing this driver runs yet
    // needs it, only the plain-int flags-bitmask shape).
    BitOr,
    BitXor,
    // "<<"/">>": real C-family bitwise left/right shift, int-only (real
    // interpret.c's own F_LSH/F_RSH, real eoperators.c's own f_lsh()/
    // f_rsh() both gate on "CHECK_TYPES(..., T_NUMBER, ...)" for each
    // operand, confirmed directly -- see Ast.hpp's BinOp::Shl/Shr
    // comment for the real corpus evidence this driver previously had
    // no plain "<<"/">>" binary operator at all). VM.cpp's own error
    // message on a type mismatch follows this driver's own established
    // BitAnd/BitOr/BitXor convention (a driver-authored "Shl/Shr:
    // operands must both be ints" message), not real bad_argument()'s
    // own dynamic multi-line "Bad argument N to ... Expected: ...
    // Got: ..." format, which is not a fixed string worth literally
    // replicating here.
    Shl,
    Shr,
    // Unary "~": one's-complement bitwise NOT, int-only (real
    // interpret.c's own F_COMPL: "if (sp->type != T_NUMBER) error(\"Bad
    // argument to ~\n\"); sp->u.number = ~sp->u.number;" -- see Ast.hpp's
    // UnaryOp::BitNot comment for the real corpus evidence this driver
    // previously lexed as an unrecognized character).
    BitNot,
    Eq, Neq, Lt, Lte, Gt, Gte,
    Not,
    Jump,
    JumpIfFalse,
    // A bare-name call ("foo(...)"), resolved at run time in this order:
    // this program's own functions, then each inherited program's
    // functions (recursively, see VM.cpp's findFunctionInChain), then the
    // efun table. This is the only opcode plain CallExpr codegen emits;
    // CallEfun is reserved for compiler-forced efun calls that must never
    // be shadowed by a same-named local function (currently just the "->"
    // / call_other() translation, see emitCallOtherExpr).
    Call,
    CallEfun,
    // "::name(...)" / "qualifier::name(...)" -- an explicit call to an
    // *inherited* definition, bypassing whatever the currently executing
    // program's own local definition of `name` might be (see Ast.hpp's
    // CallExpr::parentCall comment). operand = function name string-pool
    // index, argCount = number of args, same shape as Call/CallEfun.
    // Always immediately followed by one CallParentQualifierSlot data
    // instruction (never executed directly, exactly like Sscanf's own
    // trailing SscanfVarSlot entries) carrying the qualifier: its operand
    // is a string-pool index for "qualifier::name(...)", or -1 for the
    // bare "::name(...)" form (search the whole inherited chain).
    CallParent,
    CallParentQualifierSlot,
    // "(: name, bound_args... :)" -- constructs a Closure value (see
    // Value.hpp's Closure comment) bound to the currently executing
    // object, the given bare name, and however many already-evaluated
    // bound-arg values are on top of the stack. operand = function name
    // string-pool index, argCount = number of bound args (same shape as
    // Call/CallEfun; no trailing data instruction needed since a
    // closure literal, unlike CallParent, has nothing else to encode).
    PushClosure,
    // LDMud's own "#'efun::name" closure-literal prefix (ROADMAP.md row
    // 1.2/1.3's own real corpus-frequency-ranked slice after bare
    // "#'name" -- see Parser.cpp's own comment on ClosureLiteralExpr::
    // forceEfun for the real doc citation). Identical shape to
    // PushClosure (same operand/argCount meaning), a distinct opcode
    // purely so VM::run() can construct a Closure whose own callClosure()
    // resolution skips straight to the efun table, exactly the same
    // Call/CallEfun split CodeGen.cpp's own forceEfun comment already
    // documents for ordinary calls -- not a new Closure *kind*, just a
    // forced resolution tier on the same underlying value shape.
    PushEfunClosure,
    CallApply,
    // Real icode.c's own F_EXPAND_VARARGS (row 3.10's own array/call
    // spread operator, "expr...", real grammar.y:2488-2496's
    // "expr0 L_DOT_DOT_DOT", scoped in full in this row's own prior
    // scoping report before any code was written). operand = distance
    // from the top of localStack (0 = the topmost value) to the one
    // spread element this instruction splices, real interpret.c:2680-
    // 2724's own "s = sp - i" -- CodeGen emits one of these per spread
    // element, always right after every element of the enclosing list
    // has already been pushed in order (real generate_expr_list(),
    // icode.c:250-275), with each instruction's own operand computed
    // statically at compile time (N-1-position) rather than adjusted
    // for any earlier expansion: real FluffOS's own invariant holds
    // here identically -- an earlier (lower) expansion shifts both the
    // stack top and every later element's own position by the same
    // amount, so a later element's static distance from the top never
    // needs correcting.
    //
    // Requires the value at that slot to be a real array (LpcRuntimeError
    // otherwise, matching real interpret.c:2688-2689's own
    // "Item being expanded with ... is not an array"); splices the
    // array's own elements into that one slot in place (real
    // interpret.c:2695-2721: empty removes the slot, one element
    // replaces it directly, more shifts the stack up to make room) and
    // adds (size - 1) to VM::run()'s own local pendingVarargsDelta
    // (real interpret.c:75/2694's own global num_varargs -- kept as a
    // local here instead: this driver recurses through nested VM::run()
    // calls with a fresh localStack each time rather than real
    // FluffOS's one flat process-wide stack, so a per-call local gives
    // the identical accumulation behavior with no cross-call leakage
    // risk a member variable would need explicit save/restore around).
    // MakeArray, MakeMapping, Call, CallParent, and CallEfun each add
    // that delta to their own static argCount/entryCount and reset it
    // to zero, exactly real interpret.c's own repeated
    // "offset += num_varargs; num_varargs = 0;" pattern at :2747,
    // :2764, :3601, :3704.
    ExpandVarargs,
    MakeArray,
    // operand = mapping width (real mapping->num_values, at least 1);
    // argCount = number of entries. Stack layout per entry is the key
    // followed by `width` values. Width 1 is the ordinary single-column
    // shape (key, value) this opcode has always used; width > 1 is
    // LDMud's N-column mapping literal (ROADMAP.md row 1.9).
    MakeMapping,
    Index,
    IndexAssign,
    RangeIndex,
    // sscanf(target, format, ...vars). operand = number of trailing output
    // vars (N); immediately followed in the instruction stream by N
    // SscanfVarSlot entries, one per var, which the Sscanf handler reads
    // directly and always skips over -- they are data, not opcodes to
    // execute, and reaching one through normal ip advancement is a bug.
    Sscanf,
    SscanfVarSlot,
    // Pops a value; pushes it back unchanged if it is an array (foreach
    // over an array walks its elements directly), or pushes an array of
    // its keys if it is a mapping (foreach over a mapping walks keys, or
    // (key, value) pairs for the two-variable form -- see
    // CodeGen::emitForeachStmt). This is the one piece of foreach's
    // desugaring that genuinely needs a runtime type check rather than
    // just the existing opcode set.
    ForeachKeys,
    // catch(expr) (see Ast.hpp's CatchExpr comment). Mirrors real
    // FluffOS's F_CATCH/F_END_CATCH pair (icode.c's NODE_CATCH case):
    // PushCatchFrame's operand is a forward-patched jump target -- the
    // instruction immediately after the matching PopCatchFrame, i.e.
    // "resume here on error" -- recorded exactly the same way
    // Jump/JumpIfFalse's own targets are (see CodeGen::emitJumpPlaceholder/
    // patchJumpToHere). On normal completion, execution just flows
    // through the guarded bytecode into PopCatchFrame, landing at that
    // same instruction on its own; the operand only matters for the
    // error path, where VM::run() jumps there directly after unwinding.
    PushCatchFrame,
    // Only reached on normal (non-error) completion of the guarded
    // region -- an error unwinds straight to PushCatchFrame's recorded
    // target without ever executing this opcode. Pushes int 0 (real
    // LPC's own "no error" result, confirmed against interpret.c's
    // F_END_CATCH: "catch_value = const0; ... push_number(0)"). The
    // guarded expression's own result was already discarded by an
    // explicit Pop CodeGen emits right before this (matching real
    // FluffOS's own insert_pop_value() on the catch argument, see
    // trees.c) -- catch(expr) never evaluates to expr's own value, only
    // to 0 or the error message.
    PopCatchFrame,
    // "time_expression { <body> }" (see Ast.hpp's TimeExpressionExpr
    // comment for the full real-source citation and corpus evidence).
    // Unlike PushCatchFrame/PopCatchFrame's own jump-target bracketing,
    // these carry no operand at all: TimeExpressionStart records a
    // timestamp on VM's own dedicated timeExpressionStack_ (not the
    // ordinary LPC value stack -- avoids any dependency on the body's
    // own bytecode being perfectly stack-neutral, unlike real FluffOS's
    // own approach of parking the two timer values *underneath* the
    // body's own execution on its single shared stack), the body's own
    // already-compiled bytecode runs immediately after, in place (not
    // deferred/hoisted the way a closure literal's body is -- this is
    // an ordinary, immediate expression, never a first-class value), and
    // TimeExpressionEnd pops that timestamp, computes the real elapsed
    // microseconds, and pushes it as a plain int -- the whole
    // construct's own result.
    TimeExpressionStart,
    TimeExpressionEnd,
    Return,
    Pop,
    Dup,
    Halt,
    // ROADMAP.md row 2.5's own first slice. Pops one numeric delay
    // (seconds, int or float) and co_await's VM's own timer awaiter
    // (VM::suspendFor(), see VM.hpp), parking this coroutine with
    // VM::pendingAsyncResumes_ until that much time has passed, then
    // continuing at the next instruction with the rest of the operand
    // stack untouched. Only ever reached from VM::runAsync() -- VM::run()
    // (the ordinary, synchronous entry point, completely unchanged by
    // this row) has no coroutine frame to suspend and must never
    // execute this opcode; reaching it there is a compiler bug once
    // row 2.6 exists (no such bug is possible yet, since nothing emits
    // this opcode without row 2.6's own grammar, which does not exist
    // today -- this row's own regression tests construct it by hand).
    // Real `await expr` (row 2.6) will eventually need this to also
    // push expr's own awaited result; this first slice's own Suspend
    // pushes nothing back, matching row 2.5's explicit deferral of
    // 2.6/2.7's own value-producing await semantics.
    Suspend
};

struct Instruction {
    OpCode op;
    int32_t operand = 0;
    int32_t argCount = 0;
};

struct FunctionEntry {
    std::string name;
    uint32_t entryPoint = 0;
    uint8_t numArgs = 0;
    uint8_t numLocals = 0;
    // ROADMAP.md row 2.5's own first slice: true only for a function
    // this row's own hand-built test bytecode marks as an async task
    // entry point (real row 2.6 grammar/codegen -- an `async` keyword
    // setting this from real LPC source -- does not exist yet, and is
    // explicitly out of this row's own scope). OpCode::Call checks this
    // on its resolved callee inside VM::runAsync() only: true means
    // co_await a nested VM::runAsync() (the callee may itself suspend,
    // and that suspension must propagate all the way up through this
    // call, the exact "await reached through an intervening plain
    // call" case row 2.5's own scoping session found the old
    // TaskFrame-capture sketch would have broken on); false (the
    // default, i.e. every real function compiled by this driver today)
    // means the plain, unchanged, synchronous VM::run(), exactly as
    // before this row -- VM::run() itself never reads this field at
    // all, so it cannot behave differently for any of the 747 tests
    // that predate this row.
    bool isAsync = false;
    // Real grammar.y:706-717's own "argument_list L_DOT_DOT_DOT" (see
    // Ast.hpp's FunctionDecl::isVarargs comment for the full citation).
    // true means numArgs's own last slot is a real varargs rest-
    // parameter: VM::run() binds it to a real array of every actual
    // argument at or past that position (real interpret.c:1394-1410's
    // own setup_varargs_variables()), not a plain positional value.
    // VM::runAsync() never reads this field -- that coroutine path is
    // still row 2.5's own deliberately tiny hand-built-bytecode subset
    // (PushInt/PushLocal/StoreLocal/Add/Call/Suspend/Return/Halt only,
    // see its own default: case), out of this row's scope.
    bool isVarargs = false;
};

struct CompiledProgram {
    std::vector<Instruction> code;
    std::vector<FunctionEntry> functions;
    std::vector<std::string> stringPool;
    std::vector<double> floatPool;
    std::vector<std::string> objectVarNames;
    // Raw "inherit \"path\";" targets as parsed, before resolution.
    std::vector<std::string> inherits;
    // The above paths resolved and compiled, in the same order, by
    // ObjectManager::compile() after CodeGen produces this program (CodeGen
    // itself has no file-loading capability). Empty until then. Only the
    // immediate parents are stored here, but since each of those programs
    // carries its own inheritedPrograms too (if it itself inherits
    // something), function-call and object-variable resolution both walk
    // the full chain -- see VM.cpp's findFunctionInChain.
    std::vector<std::shared_ptr<CompiledProgram>> inheritedPrograms;

    // Every program transitively anywhere in this program's own inherit
    // tree (immediate parents and their own ancestors, recursively),
    // mapped to the absolute base offset at which that program's own
    // object variables start within *this* program's flattened
    // objectVarNames/variables() layout. A program's own bytecode always
    // addresses its object variables with slot numbers relative only to
    // its own direct inherit chain (correct when that program runs
    // completely on its own); when one of its functions instead runs as
    // part of a *different*, larger object -- because it was reached via
    // inheritance, and CompiledProgram is cached and reused verbatim
    // everywhere a file is inherited (see ObjectManager::compile()'s own
    // comment) -- the VM must add this offset before indexing
    // LpcObject::variables(), or two unrelated sibling files that are
    // each inherited directly (no parents of their own) will silently
    // alias each other's low slot numbers. Populated by
    // ObjectManager::compile() right after CodeGen::generate() returns,
    // by combining each direct parent's own running prefix offset with
    // that parent's own ancestorBaseOffsets (recursive composition, not
    // just one level). Does not include an entry for this program
    // itself: code belonging to *this* program running directly against
    // an object whose own program() is this same program needs no
    // adjustment (offset 0), which VM::run() treats as the fast-path
    // default rather than a map lookup.
    //
    // Scope note: this assumes single-copy (non-diamond) inheritance --
    // the same ancestor file reached via two different inherit paths
    // within one object gets only one entry here (the second path's
    // offset silently wins), matching how real LPC would actually give
    // that ancestor two separate flattened copies at two different
    // offsets instead. No diamond shape has been found in this mudlib's
    // actual inherit graph as of this fix; if one turns up, this map
    // needs to become multi-valued (or inheritance resolution needs to
    // duplicate the ancestor's slots per path) rather than silently
    // picking one.
    std::unordered_map<const CompiledProgram*, int> ancestorBaseOffsets;

    // Phase 2 row 2.9's apply cache: memoizes VM.cpp's own
    // findFunctionInChain(*this, name) -- the walk that resolves a bare
    // call/apply name against this program's own functions first, then
    // depth-first against inheritedPrograms, real motivation confirmed
    // already live: Scheduler.cpp's own call_heart_beat() equivalent
    // calls vm_.callFunction(obj, "heart_beat", {}) -- which bottoms out
    // in exactly this walk -- on every living object every real-time
    // tick.
    //
    // Deliberately keyed and stored differently from src/apply/
    // instruct.md's own literal sketch of this row
    // ("unordered_map<pair<LpcObject*,string>, FunctionEntry*>" with
    // manual invalidation on recompile and on destructObject()) --
    // confirmed real scope from source before writing any code, the
    // same discipline every prior Phase 2 row has used, and it changed
    // this row's actual shape:
    //
    //  - findFunctionInChain() takes a "const CompiledProgram&", not an
    //    LpcObject* -- its result depends only on program identity, not
    //    on which object asked. ObjectManager's own programCache_
    //    (keyed by filename) hands the *same* shared_ptr<CompiledProgram>
    //    to every clone of a blueprint (confirmed directly,
    //    ObjectManager.cpp's own cloneObject()/compile()), so an
    //    LpcObject*-keyed cache would populate one redundant entry per
    //    clone for what is provably always the same answer -- keying by
    //    program instead means every clone of a blueprint shares one
    //    entry, a strictly bigger real win against the row's own cited
    //    heart_beat() hot path (many living clones, one shared program).
    //  - "Invalidate on recompile" turns out to need no explicit code at
    //    all: ObjectManager::compile()'s own recompile branch (confirmed
    //    directly, its own comment right above "programCache_[filename]
    //    = program;") never mutates an existing CompiledProgram in
    //    place -- a recompile always std::make_shared's a brand new one,
    //    leaving already-loaded objects running the old, untouched
    //    instance. Storing the cache as a member of CompiledProgram
    //    itself (here) rather than in an external map means a fresh
    //    recompile automatically starts with an empty cache (a freshly
    //    default-constructed unordered_map, per this same field on the
    //    new instance) with no explicit purge step, and the old
    //    program's own now-superseded cache simply stops being
    //    reachable, right along with the rest of that no-longer-current
    //    CompiledProgram, once nothing (LpcObject::program_,
    //    ObjectManager::programCache_, or another program's
    //    inheritedPrograms) still holds it.
    //  - "Invalidate on destructObject()" likewise needs no explicit
    //    code: an external map keyed by raw CompiledProgram* would risk
    //    a genuine dangling-pointer read once every owner of some
    //    program let go and nothing pruned that map's own entries for
    //    it first -- a real memory-safety hazard the literal sketch's
    //    own two-event invalidation plan was implicitly trying to avoid.
    //    A cache stored as a member here can't dangle: it shares this
    //    struct's own lifetime exactly, so it is destroyed together with
    //    the functions/inheritedPrograms data it caches pointers into,
    //    never separately from it.
    //  - replace_program() (VM.cpp's own ob->setProgram(...) call,
    //    LDMud dialect) reassigns a live object's program_ pointer to
    //    one of its own already-compiled ancestors rather than
    //    constructing a new program -- handled correctly for free too,
    //    since every lookup re-reads obj->program()'s *current* pointer
    //    fresh and reuses (or populates) that specific program's own
    //    cache, never a stale one captured earlier.
    //
    // FunctionEntry pointers cached here stay valid for this program's
    // entire lifetime: CodeGen.cpp is the only writer of `functions`,
    // and only during this program's own initial construction (confirmed
    // directly, no other call site appends to it afterward), so the
    // vector never reallocates once a shared_ptr<CompiledProgram> exists
    // to hand pointers into it out from.
    struct FunctionChainCacheEntry {
        // nullptr on both fields is the real, valid "confirmed not
        // present anywhere in the chain" negative-cache entry -- e.g.
        // most objects have no reset()/clean_up()/heart_beat() at all,
        // and functionExists()/the scheduler's own optional-hook probes
        // re-ask that every tick just as often as a real hit.
        const CompiledProgram* program = nullptr;
        const FunctionEntry* fn = nullptr;
    };
    mutable std::unordered_map<std::string, FunctionChainCacheEntry> functionChainCache_;
};

} // namespace amlp
