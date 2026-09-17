#pragma once
#include <memory>
#include <vector>
#include "amlp/compiler/Lexer.hpp"
#include "amlp/compiler/Ast.hpp"

namespace amlp {

class Parser {
public:
    // dialect defaults to FluffOS, same reasoning as Lexer's own default
    // -- every pre-existing "Parser parser(tokens)" call site (this
    // driver's own ~80 direct test constructions) keeps its exact prior
    // behavior unchanged. Only ObjectManager::compile() passes a real,
    // config-derived dialect explicitly.
    explicit Parser(std::vector<Token> tokens, LpcDialect dialect = LpcDialect::FluffOS);
    std::unique_ptr<Program> parseProgram();

private:
    const Token& peek() const;
    const Token& peekAt(size_t offset) const;
    const Token& advance();
    bool check(TokenType type) const;
    bool checkText(const std::string& text) const;
    const Token& expect(TokenType type, const std::string& context);
    const Token& expectText(const std::string& text, const std::string& context);
    bool atEnd() const;
    bool isTypeKeyword(const Token& tok) const;
    bool isModifierKeyword(const Token& tok) const;
    bool consumeArrayMarker();

    // True when a type could start reading here: an ordinary type
    // keyword, or a completely bare "array" (real grammar.y.pre:697-715's
    // own "opt_atomic_type L_ARRAY", opt_atomic_type itself optional,
    // real grammar's own "/* empty */" alternative defaulting to
    // TYPE_ANY/"mixed" -- consumeArrayMarker()'s own two-word "<type>
    // array" form only covers the case where opt_atomic_type is present,
    // e.g. "mixed array x"; this is the bare case, "array x" alone, real
    // corpus: secure/sefun/sockets.c's own "foreach (array item in
    // finalsocks)", lib/std/bane.c's "int SetBane(array arr)",
    // lib/std/story.c's "array GetTaleKeys()"/"array msg;",
    // lib/guard.c's "private static array PendingGuard"). Used wherever
    // a lookahead decides whether a declaration starts here at all.
    bool startsType() const;
    // True when "class <identifier>" starts reading here as a *type*
    // (not the "class <name> { ... }" declaration itself) -- see
    // startsClassType()'s own comment in Parser.cpp.
    bool startsClassType() const;
    struct TypeToken { std::string type; bool isArray; };
    // Consumes one type: startsType()'s bare "array" (synthesized as
    // {"mixed", true}), or an ordinary type keyword followed by
    // consumeArrayMarker()'s own optional two-word marker. Always
    // consumes a real type token or throws -- callers that also allow a
    // type to be omitted entirely (parseDeclPrefix()) check startsType()
    // and check(TokenType::Keyword) separately instead of calling this.
    TypeToken parseTypeToken(const std::string& context);

    // Shared prefix of every top-level declaration (modifiers, type, an
    // optional array '*', and a name), factored out so parseProgram()
    // can consume it exactly once and then branch on whether '(' follows
    // (a function) or not (an object variable declaration), rather than
    // duplicating the modifier/type/star/name consumption to peek ahead.
    struct DeclPrefix {
        std::string type;
        bool isArray;
        std::string name;
        // Whether "private" appeared among this declaration's modifier
        // keywords. Every other modifier (static, public, protected,
        // nomask, varargs) is still discarded unrecorded -- see
        // parseDeclPrefix()'s own comment -- but "private" specifically
        // has to survive onto ObjectVarDecl: real LPC scopes a private
        // object variable to the file that declares it, invisible to
        // (and non-collidable with) an inheriting child's own variable
        // of the same name (confirmed live: std/living.c's own "static
        // private int __Locked, __LastAged;" and std/user.c's separate,
        // unrelated "static int __LastAged;" are two different real
        // mudlib files that legitimately reuse the same name this way).
        // See CodeGen::generate()'s own use of ObjectVarDecl::isPrivate.
        bool isPrivate = false;
    };
    DeclPrefix parseDeclPrefix(const std::string& context);
    std::unique_ptr<FunctionDecl> parseFunctionRest(DeclPrefix prefix);
    std::vector<std::unique_ptr<ObjectVarDecl>> parseObjectVarDeclRest(DeclPrefix prefix);
    // isVarargsOut, when given, is set true if this list ends in a real
    // varargs "..." (see Ast.hpp's FunctionDecl::isVarargs comment).
    // nullptr (the default) is only for a hypothetical caller that has
    // nowhere to record it; both current callers pass a real out-param.
    std::vector<Param> parseParamList(bool* isVarargsOut = nullptr);
    std::unique_ptr<Block> parseBlock();
    std::unique_ptr<Block> parseBranch();
    AstPtr parseStatement();
    AstPtr parseIfStatement();
    AstPtr parseWhileStatement();
    AstPtr parseDoWhileStatement();
    AstPtr parseForStatement();
    AstPtr parseReturnStatement();
    AstPtr parseBreakStatement();
    AstPtr parseContinueStatement();
    AstPtr parseForeachStatement();
    AstPtr parseSwitchStatement();
    // classType is "" for every ordinary declared/pre-existing variable
    // (the overwhelmingly common case); a "class:<Name>" TypeToken (see
    // startsType()/parseTypeToken()'s own comment) sets it to "<Name>" --
    // see ForeachStmt::varClassType/valueVarClassType's own comment for
    // why this previously had nowhere to go.
    struct ForeachVarSpec { std::string name; bool isNewDecl; std::string classType; };
    ForeachVarSpec parseForeachVar();
    // "class <name> { <member decls> }" (ROADMAP.md row 3.10's class
    // scoping report, see Ast.hpp's ClassDeclStmt comment). Called from
    // parseProgram() once its own lookahead confirms this exact shape
    // (modifiers already consumed, "class identifier {" all present) --
    // this only parses from the "identifier" onward.
    std::unique_ptr<ClassDeclStmt> parseClassDecl();
    std::unique_ptr<VarDeclStmt> parseSingleVarDecl(const std::string& typeText, bool isArray);
    AstPtr parseVarDeclStatement();
    AstPtr parseAssignStatement();
    void parseInheritStatement(Program& program);
    std::string parseInheritPathString();

    AstPtr parseExpr();
    // grammar.y comma_expr (above expr0). parseExpr() stays expr0.
    AstPtr parseCommaExpr();
    AstPtr parseCommaExprChain();
    AstPtr continueStatementCommaChain(AstPtr firstStmt);
    AstPtr parseTernary();
    AstPtr parseLogicalOr();
    AstPtr parseLogicalAnd();
    AstPtr parseBitOr();
    AstPtr parseBitXor();
    AstPtr parseBitAnd();
    AstPtr parseEquality();
    AstPtr parseComparison();
    AstPtr parseShift();
    AstPtr parseAdditive();
    AstPtr parseMultiplicative();
    AstPtr parseUnary();
    AstPtr parsePostfix();
    AstPtr parsePrimary();

    // "qualifier::name(...)" (grammar.y identifier/L_BASIC_TYPE
    // L_COLON_COLON identifier). Qualifier already consumed; at "::".
    AstPtr parseQualifiedParentCall(const std::string& qualifier);

    // A call argument list, real grammar.y:2470-2510's own
    // expr_list/expr_list2/expr_list_node -- see Ast.hpp's CallExpr::
    // argIsSpread comment. isSpread is empty when no argument here is
    // spread (the common case, matching the AST field's own contract),
    // else exactly args.size() long.
    struct ArgListResult {
        std::vector<AstPtr> args;
        std::vector<bool> isSpread;
    };
    ArgListResult parseArgList();

    std::vector<Token> tokens_;
    size_t pos_ = 0;
    LpcDialect dialect_;

    // One entry per "(: ... :)" general-lambda body currently being
    // parsed (nested lambdas push their own), each tracking the highest
    // "$N" seen so far within that lambda specifically -- real lex.c's
    // own "current_function_context" (a stack there too, pushed/popped
    // per nested function-pointer context). Empty means "not currently
    // inside a lambda body", the real "$var illegal outside of function
    // pointer" condition -- see parsePrimary()'s own "$N" handling.
    std::vector<int> lambdaParamMaxStack_;

    // Parallel to lambdaParamMaxStack_ above, one entry per "(: ... :)"
    // body currently being parsed: the real "$(expr)" bound-value
    // expressions encountered so far within that lambda specifically, in
    // encounter order (see Ast.hpp's InlineLambdaExpr::boundValueExprs
    // for the full real-source citation). Each "expr" inside a "$(expr)"
    // is itself parsed and pushed here immediately, then this lambda's
    // whole accumulated vector is moved into InlineLambdaExpr::
    // boundValueExprs once its body finishes parsing -- see parsePrimary()'s
    // own "(: :)" handling and its own "$(" handling.
    std::vector<std::vector<AstPtr>> lambdaBoundValuesStack_;
};

} // namespace amlp
