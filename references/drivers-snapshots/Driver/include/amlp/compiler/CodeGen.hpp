#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "amlp/compiler/Ast.hpp"
#include "amlp/vm/Bytecode.hpp"

namespace amlp {

class CodeGen {
public:
    // inheritedObjectVarNames is the flattened, in-order list of object
    // variable names declared by this program's inherit chain (already
    // resolved by the caller -- ObjectManager -- since CodeGen only ever
    // sees one file's own AST and has no file-loading capability of its
    // own). Passing it in lets this file's own PushObjectVar/StoreObjectVar
    // slot numbers, and any bare references to an inherited variable by
    // name, line up with the same slots the inherited program's own
    // bytecode already uses -- see ObjectManager::compile() for how the
    // two are kept consistent.
    CompiledProgram generate(const Program& program,
                              const std::vector<std::string>& inheritedObjectVarNames = {});

private:
    // Distinguishes a bare identifier resolving to a per-call local
    // (which also covers parameters, since they already share the same
    // slot space as locals) versus a per-object variable. A local always
    // shadows an object variable of the same name, matching real LPC's
    // local-wins-over-global precedence: resolveVariable() checks
    // locals_ before objectVars_.
    enum class VarKind { Local, ObjectVar };
    struct ResolvedVar { VarKind kind; int slot; };
    ResolvedVar resolveVariable(const std::string& name) const;

    int internString(const std::string& s);
    int internFloat(double d);
    void emitExpr(const AstNode& expr);
    // Emits one OpCode::ExpandVarargs per spread-marked position in
    // isSpread, real generate_expr_list()'s own second pass (icode.c:
    // 264-274) -- see Bytecode.hpp's OpCode::ExpandVarargs comment for
    // the full citation. Called after every element the list describes
    // has already been emitted (pushed) in order; a no-op when isSpread
    // is empty (the common, no-spread case).
    void emitSpreadExpansions(const std::vector<bool>& isSpread);
    void emitCallExpr(const CallExpr& call);
    void emitCallOtherExpr(const CallOtherExpr& callOther);
    void emitSscanfExpr(const SscanfExpr& sscanf);
    void emitBinaryExpr(const BinaryExpr& bin);
    void emitLogicalExpr(const BinaryExpr& bin);
    void emitTernaryExpr(const TernaryExpr& tern);
    void emitCatchExpr(const CatchExpr& catchExpr);
    void emitTimeExpressionExpr(const TimeExpressionExpr& timeExpr);
    void emitAssignExpr(const AssignExpr& assign);
    void emitIndexAssignExpr(const IndexAssignExpr& assign);
    void emitIncDecExpr(const IncDecExpr& incDec);
    void emitStatement(const AstNode& stmt);
    void emitReturnStmt(const ReturnStmt& stmt);
    void emitVarDeclStmt(const VarDeclStmt& stmt);
    void emitAssignStmt(const AssignStmt& stmt);
    void emitIndexAssignStmt(const IndexAssignStmt& stmt);
    void emitIfStmt(const IfStmt& stmt);
    void emitWhileStmt(const WhileStmt& stmt);
    void emitDoWhileStmt(const DoWhileStmt& stmt);
    void emitForStmt(const ForStmt& stmt);
    void emitForeachStmt(const ForeachStmt& stmt);
    void emitSwitchStmt(const SwitchStmt& stmt);
    void emitBreakStmt();
    void emitContinueStmt();
    void emitBlock(const Block& block);

    // typeText is the same string Param::type/VarDeclStmt::type/etc
    // already carry -- a "class:<Name>" prefix (see Parser.cpp's own
    // startsClassType() comment) records this local's declared class
    // type into localClassTypes_; anything else (the overwhelmingly
    // common case) is a no-op here, matching every pre-existing call
    // site's behavior exactly via the default argument.
    int declareLocal(const std::string& name, const std::string& typeText = "");

    size_t emitJumpPlaceholder(OpCode op);
    void patchJumpTo(size_t jumpInstrIndex, size_t target);
    void patchJumpToHere(size_t jumpInstrIndex);

    // One entry per loop currently being compiled (nested loops push
    // their own), holding the as-yet-unpatched Jump placeholders emitted
    // by any break/continue inside it. Each loop's own emitWhileStmt()/
    // emitForStmt() patches its entry's jumps once it knows where they
    // should land (loop exit for break; the condition re-check for a
    // while's continue, the update clause for a for's) and pops it.
    // isSwitch marks a switch statement's own frame: break targets the
    // innermost frame regardless of kind, but continue must skip past
    // any switch frames to reach the nearest *loop* -- "continue" inside
    // a switch nested in a loop still continues the loop, real LPC/C
    // switch has no continue semantics of its own. See emitContinueStmt.
    struct LoopContext {
        std::vector<size_t> breakJumps;
        std::vector<size_t> continueJumps;
        bool isSwitch = false;
    };
    std::vector<LoopContext> loopStack_;
    // Gives each foreach statement's hidden bookkeeping locals
    // ("$foreach_orig_<n>" etc, see emitForeachStmt) a unique name within
    // the function being compiled, so sibling and nested foreach loops
    // never collide. Reset per function alongside locals_/loopStack_.
    int foreachCounter_ = 0;
    // Same idea as foreachCounter_, for switch's own hidden subject local.
    int switchCounter_ = 0;
    // Same idea again, for emitIndexAssignExpr()'s hidden temp local
    // (holds the newly assigned value so it can be pushed back onto the
    // stack after OpCode::IndexAssign, which itself leaves nothing
    // behind -- see that function's own comment).
    int indexAssignCounter_ = 0;

    CompiledProgram* out_ = nullptr;
    std::unordered_map<std::string, int> locals_;
    int nextLocalSlot_ = 0;
    // Real LPC/C89 block scoping: a "{ ... }" introduces its own nested
    // scope for local declarations, so two sibling blocks (e.g. two
    // separate "{ int me; ... }" groups back to back in the same
    // function, neither nested in the other) may each declare a
    // same-named local without colliding -- confirmed live needed
    // compiling domains/Praxis/setter.c's own "Store PPE"/"Store ISP"
    // blocks, each with their own "int ... me;". emitBlock() pushes a
    // new (empty) entry before compiling a block's statements and, on
    // exit, erases from locals_ every name declared during that block
    // (recorded here by declareLocal()) before popping it -- restoring
    // exactly the set of names visible before the block was entered.
    // Slots themselves are never reused (nextLocalSlot_ only ever
    // grows), which is simpler and harmless: mirrors this driver's own
    // existing preference for simple/monotonic allocation elsewhere
    // (e.g. object variable slots), just uses a few more of a function's
    // numLocals than a slot-reusing implementation would.
    std::vector<std::vector<std::string>> localScopeStack_;
    // Per-object variable slots, populated once per generate() call from
    // Program::objectVars before any function body is compiled, and left
    // untouched afterward (unlike locals_, which is per-function).
    std::unordered_map<std::string, int> objectVars_;

    // Real class_def_t/class_member_entry_t analog (ROADMAP.md row
    // 3.10's class scoping report, class.c/interpret.c's own real
    // per-*program* member-name table -- member resolution is 100%
    // compile time in real FluffOS, never at runtime, see
    // MemberNameMarker's own comment): every "class <name> { ... }"
    // declared in this program, mapped to its declared member names in
    // order. Populated once per generate() call from Program::classes,
    // before any function body is compiled (same timing as objectVars_
    // just above), and never cleared afterward. Empty under every
    // dialect but FluffOS.
    std::unordered_map<std::string, std::vector<std::string>> classDefs_;

    // Declared static class type for a local/parameter (name -> class
    // name, no "class:" prefix here), block-scoped exactly like
    // locals_/localScopeStack_ -- declareLocal() populates an entry
    // only when its own typeText argument carries "class:", and
    // emitBlock() erases the same name from here alongside locals_ on
    // scope exit. Absence (the overwhelmingly common case) means "not
    // known to be class-typed", not "known to be untyped" -- see
    // staticClassTypeOf()'s own comment for how that absence is
    // actually used (only a bare declared-class-typed identifier
    // target resolves; anything else throws a clear error rather than
    // guessing, per this row's own scoping report and this codebase's
    // established "throw rather than silently mishandle" convention).
    std::unordered_map<std::string, std::string> localClassTypes_;
    // Same idea as localClassTypes_, for object variables: populated
    // once per generate() call from Program::objectVars (same loop and
    // timing as objectVars_ itself), never cleared afterward.
    std::unordered_map<std::string, std::string> objectVarClassTypes_;

    // Real class_def_t lookup (class.c's own real "Undefined class"/
    // "Class ... has no member ..." compile errors, see
    // lookup_class_member()'s own citation in this row's scoping
    // report) -- throws a clear LpcRuntimeError for either failure
    // rather than guessing. Returns the member's 0-based index within
    // className's own declared member order.
    int resolveClassMemberIndex(const std::string& className, const std::string& memberName) const;

    // The static (compile-time-known) class type of expr, or "" if none
    // is known -- real FluffOS itself only ever resolves "instance->
    // member" against a statically-known type too (real IS_CLASS($1->
    // type) check, grammar.y:2803-2827, or its own TYPE_ANY fallback
    // via lookup_any_class_member(), a real, documented ambiguity-prone
    // shortcut this driver deliberately does not replicate -- no real
    // corpus site found needing it, see this row's own scoping report).
    // Only a bare VarRefExpr of a class-declared variable, or a
    // TypeCastExpr "(class Name)expr" (grammar.y:780-786). Anything
    // else returns "" and emitIndexValue() throws rather than guessing.
    std::string staticClassTypeOf(const AstNode& expr) const;

    // Shared by emitExpr()'s own IndexExpr case and
    // emitIndexAssignStmt()/emitIndexAssignExpr(), every place that
    // used to just call emitExpr(*index) directly: indexNode is either
    // an ordinary index expression (emitted exactly as before this
    // row) or a MemberNameMarker (real "instance->member" access, see
    // that node's own comment), in which case this resolves the
    // member's compile-time index against targetExpr's own static
    // class type (staticClassTypeOf()/resolveClassMemberIndex() above)
    // and emits a plain PushInt for it instead -- the actual "no new
    // VM opcode" reuse of OpCode::Index/IndexAssign this row's own
    // scoping report promised. targetExpr is only actually read when
    // indexNode is a MemberNameMarker (an ordinary index never needs
    // its own target's type), so this does not force resolving/
    // emitting a target twice in the ordinary case -- callers already
    // emit the target separately, exactly as before this row.
    void emitIndexValue(const AstNode& targetExpr, const AstNode& indexNode);

    // An InlineLambdaExpr (see Ast.hpp) encountered while compiling some
    // enclosing function's body cannot have its own bytecode emitted
    // in-place: that would splice the lambda's Return into the middle of
    // the enclosing function's own instruction stream, since every
    // function in a CompiledProgram shares one flat code_ array
    // addressed only by entryPoint (see generate()'s own per-function
    // loop). Instead, emitExpr() records the pending body here and emits
    // an ordinary PushClosure referencing a synthesized name; generate()
    // drains this list right after finishing the enclosing function's
    // own Return, compiling each one exactly like a normal top-level
    // function (own locals_/entryPoint/FunctionEntry) so it lands at a
    // distinct, self-contained offset. The synthesized name is never a
    // legal LPC identifier (see nextLambdaId_'s use in CodeGen.cpp), so
    // it can never collide with a real function and is only ever reached
    // via the Closure's stored functionName at call time -- the exact
    // same findFunctionInChain() lookup an ordinary bare-name closure
    // already uses, no VM.cpp changes needed.
    struct PendingLambda {
        std::string name;
        const InlineLambdaExpr* expr = nullptr;
    };
    std::vector<PendingLambda> pendingLambdas_;
    int nextLambdaId_ = 0;
    void emitPendingLambdas();

    // Real "$(expr)" offsetting (see Ast.hpp's LambdaParamExpr::
    // isBoundValue comment and icode.c's own current_num_values): set by
    // emitPendingLambdas() right before compiling each pending lambda's
    // own bodyExprs, to that lambda's own InlineLambdaExpr::
    // boundValueExprs.size() -- the number of slots its own "$(expr)"
    // bindings already occupy (0..K-1), so every ordinary "$N" reference
    // encountered while compiling that same body can be shifted past
    // them (K..K+paramCount-1). Read only inside the LambdaParamExpr
    // emitExpr() case; a nested "(: :)" lambda's own body is never
    // compiled here (it only gets queued into pendingLambdas_, see
    // InlineLambdaExpr's own emitExpr() case), so this is never stale by
    // the time it is actually read.
    size_t currentLambdaBoundValueCount_ = 0;

    // Same deferred-compilation shape as PendingLambda just above, for
    // real "function(<params>) { <body> }" anonymous-function literals
    // (Ast.hpp's AnonFunctionExpr) instead -- a genuinely different
    // compilation path (real named parameters via declareLocal(), a
    // real Block body via emitBlock(), not InlineLambdaExpr's own
    // paramCount/bodyExprs shape), so kept as its own parallel list
    // rather than folded into PendingLambda itself. A separate queue,
    // not a variant sharing one, since the two bodies are compiled
    // through genuinely different logic (declareLocal()+emitBlock() vs.
    // the $N-slot-reservation+comma-expression loop) -- see
    // emitPendingAnonFuncs()'s own comment. generate()'s own top-level
    // loop drains both this and pendingLambdas_ in alternation until
    // both are empty, since a body compiled while draining either one
    // can itself queue more of either kind (a "(: :)" nested inside a
    // "function(){}" body, or vice versa) -- no real corpus evidence
    // either nesting actually occurs in any vendored mudlib, but this
    // costs nothing extra to get right regardless, the same "handle a
    // lambda body queuing more lambdas" defensiveness
    // emitPendingLambdas() already has for its own single kind.
    struct PendingAnonFunc {
        std::string name;
        const AnonFunctionExpr* expr = nullptr;
    };
    std::vector<PendingAnonFunc> pendingAnonFuncs_;
    int nextAnonFuncId_ = 0;
    void emitPendingAnonFuncs();
};

} // namespace amlp
