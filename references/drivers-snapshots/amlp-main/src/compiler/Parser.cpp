#include "amlp/compiler/Parser.hpp"
#include "amlp/core/Errors.hpp"

namespace amlp {

Parser::Parser(std::vector<Token> tokens, LpcDialect dialect)
    : tokens_(std::move(tokens)), dialect_(dialect) {}

bool Parser::atEnd() const {
    return pos_ >= tokens_.size() || tokens_[pos_].type == TokenType::End;
}

const Token& Parser::peek() const {
    return tokens_[pos_ < tokens_.size() ? pos_ : tokens_.size() - 1];
}

const Token& Parser::peekAt(size_t offset) const {
    size_t idx = pos_ + offset;
    return tokens_[idx < tokens_.size() ? idx : tokens_.size() - 1];
}

const Token& Parser::advance() {
    const Token& t = peek();
    if (!atEnd()) ++pos_;
    return t;
}

bool Parser::check(TokenType type) const {
    return !atEnd() && peek().type == type;
}

bool Parser::checkText(const std::string& text) const {
    if (atEnd()) return false;
    const Token& t = peek();
    // Every checkText() call site in this file passes a symbol or keyword
    // literal ("(", "if", "++", ...), never an arbitrary identifier/
    // string/number value -- there is no call site anywhere that wants to
    // match a literal's *content*. Without this type guard, a string
    // literal whose contents happen to equal an operator's text (e.g.
    // "!", "-", "for") would spuriously match here too, since Token
    // equality was previously text-only. Restricting to Symbol/Keyword
    // tokens closes that off without changing behavior for any real call
    // site.
    if (t.type != TokenType::Symbol && t.type != TokenType::Keyword) return false;
    return t.text == text;
}

// Real FluffOS's own ARRAY_RESERVED_WORD build option (options.h: "If
// this is defined then the word 'array' can be used to define arrays,
// as in: int array x = ({ .... });"), gated in real grammar.y.pre
// (fluffos-2.23-ds03, Dead Souls 3.8.2's own bundled driver source, not
// the older vendored 2.9 reference which has no such grammar production
// at all) behind "%ifdef ARRAY_RESERVED_WORD" specifically at
// "basic_type: atomic_type | opt_atomic_type L_ARRAY" -- a type-position
// suffix keyword, the exact same real shape "*" already has here, not a
// general reserved word. Real corpus: Dead Souls 3.8.2's own
// fluffos-2.23-ds03/local_options.ds explicitly "#define
// ARRAY_RESERVED_WORD" (confirmed this mudlib's own real intended
// driver build actually turns this on, not merely a driver capability
// nobody exercises), and 172 real files across the mudlib use the form
// (secure/daemon/master.c's own "private static string array
// efuns_arr = ({});" among them).
//
// Deliberately NOT a lexer-level keyword reservation (unlike a real
// yacc-generated parser, which reserves the word globally the moment
// this option is compiled in, real options.h's own documented "side
// effect": "'array' cannot be a variable or function name" once
// enabled) -- "array" still lexes as an ordinary TokenType::Ident here,
// and this helper only ever consumes it in the one syntactic position
// real grammar.y.pre's own "L_ARRAY" actually occupies (immediately
// after a declaration's own base type, the exact spot the existing '*'
// check already looks at every call site below), matching the same
// double-gating discipline this codebase already uses for other
// dialect/build-optional keywords ("atomic"/"nil" for DGD, ROADMAP.md
// row 1.3) rather than risk misreading a genuine identifier legitimately
// named "array" used anywhere else in this driver's own bundled mudlib
// or any other vendored corpus. No live corpus evidence anywhere of a
// real variable/function actually named "array", so this narrower,
// safer scope costs nothing today and avoids ever needing to guess
// which mudlibs would want the word fully reserved.
bool Parser::consumeArrayMarker() {
    if (checkText("*")) {
        advance();
        return true;
    }
    if (check(TokenType::Ident) && peek().text == "array") {
        advance();
        return true;
    }
    return false;
}

// Real "class <name>" used as an ordinary type (ROADMAP.md row 3.10's
// class scoping report, real grammar.y.pre's own "atomic_type: ... |
// L_CLASS L_DEFINED_NAME | L_CLASS L_IDENTIFIER") -- a declared
// variable/parameter/return type, not the "class <name> { ... }"
// declaration itself (that shape always has a "{" right after the name,
// checked by parseProgram()'s own lookahead before this function is
// ever reached for it). "class" lexes as a plain Ident here (never
// reserved, same discipline as "array" -- see consumeArrayMarker()'s
// own comment), so this needs the same two-token lookahead
// (Ident "class" immediately followed by another Ident, the class
// name) to avoid ever misreading a real file's own unrelated variable
// or function literally named "class" elsewhere. FluffOS-dialect-only:
// real LDMud has no equivalent (Value.hpp's own isUndefined comment
// citation applies the same discipline here), so under any other
// dialect "class" simply never matches this branch and falls through
// to being treated as an ordinary bare identifier exactly as before
// this row -- either a real "type omitted" declaration (whose name
// then genuinely is "class", vanishingly unlikely but unchanged
// behavior) or a clean parse error at whatever position actually
// expected something else, never a silent misread.
bool Parser::startsClassType() const {
    return dialect_ == LpcDialect::FluffOS && check(TokenType::Ident) &&
           peek().text == "class" && peekAt(1).type == TokenType::Ident;
}

bool Parser::startsType() const {
    // "object::create()" is a qualified parent call, not a declaration
    // (grammar.y L_BASIC_TYPE L_COLON_COLON identifier).
    if (check(TokenType::Keyword) && isTypeKeyword(peek()) && peekAt(1).text == "::") return false;
    if (check(TokenType::Keyword) && isTypeKeyword(peek())) return true;
    if (startsClassType()) return true;
    return check(TokenType::Ident) && peek().text == "array";
}

Parser::TypeToken Parser::parseTypeToken(const std::string& context) {
    if (startsClassType()) {
        advance(); // "class"
        std::string className = advance().text; // the class name itself
        return TypeToken{"class:" + className, consumeArrayMarker()};
    }
    if (check(TokenType::Ident) && peek().text == "array") {
        advance();
        return TypeToken{"mixed", true};
    }
    Token tok = expect(TokenType::Keyword, context);
    return TypeToken{tok.text, consumeArrayMarker()};
}

const Token& Parser::expect(TokenType type, const std::string& context) {
    if (!check(type)) {
        throw LpcRuntimeError("parse error: expected token type in " + context +
                               " at line " + std::to_string(peek().line) +
                               " (got \"" + peek().text + "\")");
    }
    return advance();
}

const Token& Parser::expectText(const std::string& text, const std::string& context) {
    if (!checkText(text)) {
        throw LpcRuntimeError("parse error: expected \"" + text + "\" in " + context +
                               " at line " + std::to_string(peek().line) +
                               " (got \"" + peek().text + "\")");
    }
    return advance();
}

bool Parser::isTypeKeyword(const Token& tok) const {
    if (tok.type != TokenType::Keyword) return false;
    static const std::vector<std::string> nonTypeKeywords = {
        "return", "if", "else", "while", "for", "do", "inherit", "break", "continue",
        "foreach", "in", "switch", "case", "default",
        "static", "private", "public", "protected", "nomask", "varargs",
        // "atomic" -- DGD's own function-declaration modifier (see
        // isModifierKeyword() below), never a type. "nil" -- DGD's own
        // literal (see parsePrimary()'s own "nil" handling), also never
        // a declarable type: real temp/dgd/src/comp/parser.y's own
        // type_specifier production (INT/FLOAT/STRING/OBJECT/...) has
        // no NIL case at all -- "nil" only ever appears in expression
        // position, never as a declared variable's type, despite also
        // appearing in data.h's own TYPENAMES table (used only for
        // error-message type-name printing there, not real declaration
        // grammar). Both excluded unconditionally: under FluffOS/LDMud
        // neither is ever lexed as a Keyword at all
        // (Lexer::lexIdentOrKeyword()'s own dialect gate), so this only
        // matters, and only needs to matter, under DGD -- listing them
        // here always is harmless and avoids a dialect check this
        // function has no other reason to carry.
        "atomic", "nil"
    };
    for (const auto& kw : nonTypeKeywords) {
        if (tok.text == kw) return false;
    }
    return true;
}

bool Parser::isModifierKeyword(const Token& tok) const {
    if (tok.type != TokenType::Keyword) return false;
    static const std::vector<std::string> modifierKeywords = {
        "static", "private", "public", "protected", "nomask", "varargs"
    };
    for (const auto& kw : modifierKeywords) {
        if (tok.text == kw) return true;
    }
    // DGD's "atomic" (ROADMAP.md row 1.2/1.3 scoping note; confirmed
    // against temp/dgd/src/comp/parser.y's own "ATOMIC { $$ =
    // C_ATOMIC; }", the same modifier-list production as
    // static/nomask/varargs above). Dialect-gated here too, not just at
    // the Lexer -- belt and suspenders: Lexer::lexIdentOrKeyword()
    // already only ever tokenizes "atomic" as a Keyword under DGD, so
    // this token could not reach here as anything but DGD's own in
    // practice, but this function owning its own real check, not just
    // trusting the Lexer's gate to hold forever, is the more defensible
    // shape for the one place that actually decides "is this a
    // function modifier". What "atomic" *means* once accepted (VM-level
    // checkpoint/rollback) is row 1.12's own, separate, still-unstarted
    // concern -- accepting the keyword here only lets it parse.
    if (dialect_ == LpcDialect::DGD && tok.text == "atomic") return true;
    return false;
}

Parser::ArgListResult Parser::parseArgList() {
    ArgListResult result;
    if (checkText(")")) return result;
    std::vector<bool> spreadFlags;
    bool anySpread = false;
    for (;;) {
        result.args.push_back(parseExpr());

        // Real grammar.y:2488-2496's own "expr_list_node: expr0 |
        // expr0 L_DOT_DOT_DOT" -- checked right after the element itself,
        // before the comma/close that ends this element (matching real
        // grammar's own production order).
        bool spread = false;
        if (checkText("...")) {
            advance();
            spread = true;
            anySpread = true;
        }
        spreadFlags.push_back(spread);

        if (checkText(",")) {
            advance();
            // Real grammar.y's own "expr_list2 ','" production: a
            // trailing comma right before the closing ')' is real,
            // explicit LPC syntax (not this driver's own invention --
            // mirrors the same real allowance array/mapping literals
            // already have here), dropped with no extra element added,
            // not a syntax error. Found live against a real third-party
            // mudlib corpus (row 3.8's TMI-2 boot attempt):
            // adm/daemons/newuserd.c's own "body->set(\"PATH\",
            // AUTO_WIZHOOD);", where AUTO_WIZHOOD is a real, deliberately
            // valueless flag #define (config.h's own "#define
            // AUTO_WIZHOOD", no value -- its own header comment: "The
            // AUTO_WIZHOOD define causes all those [logging in] as new
            // users to be [granted wizard status]", a pure boolean
            // #ifdef-style flag), so cpp's real, correct expansion is
            // literally "body->set(\"PATH\", );" -- a real, valid,
            // single-argument call in real LPC's own grammar, previously
            // rejected outright here ("expected expression ... got
            // ')'"), not a malformed one to route around.
            if (checkText(")")) break;
            continue;
        }
        break;
    }
    if (anySpread) {
        result.isSpread = std::move(spreadFlags);
    }
    return result;
}

// "qualifier::name(...)": identifier or L_BASIC_TYPE then L_COLON_COLON.
// Qualifier already consumed; this starts at "::".
AstPtr Parser::parseQualifiedParentCall(const std::string& qualifier) {
    advance(); // "::"
    Token fnNameTok = expect(TokenType::Ident, "qualifier:: function name");
    expectText("(", "qualifier:: call arguments");
    auto qualifiedArgs = parseArgList();
    expectText(")", "qualifier:: call arguments");

    auto call = std::make_unique<CallExpr>();
    call->callee = fnNameTok.text;
    call->args = std::move(qualifiedArgs.args);
    call->argIsSpread = std::move(qualifiedArgs.isSpread);
    call->parentCall = true;
    call->parentQualifier = qualifier;
    return call;
}

AstPtr Parser::parsePrimary() {
    if (check(TokenType::String)) {
        // Adjacent string literals concatenate with no operator, same as
        // C (grammar.y's "string_con2: L_STRING | string_con2 L_STRING",
        // used directly as an expr0 primary). Hit live in
        // secure/daemon/master.c: a message split across two lines purely
        // for source readability --
        //   shout("...Saving all players; "
        //         "please reconnect shortly.\n");
        std::string value = advance().text;
        while (check(TokenType::String)) {
            value += advance().text;
        }
        auto lit = std::make_unique<StringLiteral>();
        lit->value = std::move(value);
        return lit;
    }

    if (check(TokenType::Number)) {
        // The Lexer folds a float's '.' straight into this same Number
        // token's text (see Lexer::lexNumber()'s own comment) rather than
        // using a distinct token type, so int vs float is told apart
        // here, the one place that text is actually consumed.
        const std::string& text = peek().text;
        if (text.find('.') != std::string::npos) {
            auto lit = std::make_unique<FloatLiteral>();
            lit->value = std::stod(advance().text);
            return lit;
        }
        auto lit = std::make_unique<IntLiteral>();
        const std::string& raw = advance().text;
        // Lexer::lexNumber()'s own hex-literal branch keeps the "0x"/"0X"
        // prefix in the token text rather than stripping it -- base 16
        // here (std::stoll itself already understands a "0x"/"0X" prefix
        // once told the base is 16) is the one place that prefix is
        // actually consumed. Every non-hex literal is completely
        // unaffected: plain std::stoll(raw), base 10, exactly as before.
        bool isHex = raw.size() > 2 && raw[0] == '0' && (raw[1] == 'x' || raw[1] == 'X');
        lit->value = isHex ? std::stoll(raw, nullptr, 16) : std::stoll(raw);
        return lit;
    }

    // DGD "nil" literal (ROADMAP.md row 1.2/1.3's greenlit slice; real
    // temp/dgd/src/comp/parser.y: "NIL { $$ = Node::createNil(); }", an
    // ordinary primary-expression literal). Double-gated on dialect_
    // here too, not just at the Lexer -- same belt-and-suspenders
    // discipline as isModifierKeyword()'s own "atomic" check just above
    // it in this file: Lexer::lexIdentOrKeyword() already only ever
    // tokenizes "nil" as a Keyword under DGD, so this branch could not
    // be reached under FluffOS/LDMud in practice, but this is the one
    // place that actually decides "is this a real literal", and it
    // should not silently trust the Lexer's gate to hold forever.
    if (check(TokenType::Keyword) && peek().text == "nil" && dialect_ == LpcDialect::DGD) {
        advance();
        return std::make_unique<NilLiteral>();
    }

    // LDMud "'name" symbol literal (see Ast.hpp's SymbolLiteralExpr and
    // Lexer::lexQuote()'s own comment). Lexer::tokenize() only ever
    // produces a QuotedSymbol token under LpcDialect::LdMud (a bare "'"
    // stays lexChar()'s ordinary char-literal token under FluffOS/DGD),
    // so no extra dialect_ gate is needed here the way "#'"/"nil" double-
    // gate themselves against their own Lexer -- there is no token for
    // this branch to even see under the other two dialects.
    if (check(TokenType::QuotedSymbol)) {
        auto sym = std::make_unique<SymbolLiteralExpr>();
        sym->name = advance().text;
        return sym;
    }

    // "$(expr)" -- real LPC's own bound-variable-capture form, legal
    // only inside a "(: ... :)" body (see Ast.hpp's InlineLambdaExpr::
    // boundValueExprs and LambdaParamExpr's own comment for the full
    // real-source grounding: fluffos-2.23-ds03's own grammar.y.pre
    // "'$' '(' comma_expr ')'" production and icode.c's own
    // current_num_values offsetting). Lexer::lexLambdaParam() now
    // returns a bare "$" Symbol token whenever a digit does not
    // immediately follow (matching real lex.c's own "if (!isdigit(c =
    // *outp++)) { outp--; return '$'; }" exactly, deferring validity to
    // the Parser same as real LPC does), so this is the one place that
    // decides what a bare "$" actually means: real grammar's only
    // production consuming a bare "'$'" token is this one, immediately
    // followed by "(". grammar.y.pre "'$' '(' comma_expr ')'".
    if (checkText("$") && peekAt(1).text == "(") {
        if (lambdaBoundValuesStack_.empty()) {
            throw LpcRuntimeError(
                "$(...) illegal outside of function pointer");
        }
        advance(); // $
        advance(); // (
        AstPtr valueExpr = parseCommaExpr();
        expectText(")", "$(...) bound value");
        int slot = static_cast<int>(lambdaBoundValuesStack_.back().size());
        lambdaBoundValuesStack_.back().push_back(std::move(valueExpr));
        auto param = std::make_unique<LambdaParamExpr>();
        param->index = slot;
        param->isBoundValue = true;
        return param;
    }

    if (checkText("(") && peekAt(1).text == "[") {
        advance(); // (
        advance(); // [
        auto lit = std::make_unique<MappingLiteralExpr>();
        if (!checkText("]")) {
            int expectedWidth = -1;
            for (;;) {
                AstPtr key = parseExpr();
                expectText(":", "mapping literal entry");
                std::vector<AstPtr> values;
                values.push_back(parseExpr());
                // LDMud N-column mapping literal: same-key values
                // separated by ';' (real m_expr_values, prolang.y:17257).
                // FluffOS has no width concept (DGD neither -- assoc_exp
                // is a strict single key-value pair), so extra values
                // stay a parse error there.
                if (dialect_ == LpcDialect::LdMud) {
                    while (checkText(";")) {
                        advance();
                        values.push_back(parseExpr());
                    }
                }
                int thisWidth = static_cast<int>(values.size());
                if (expectedWidth < 0) expectedWidth = thisWidth;
                else if (thisWidth != expectedWidth) {
                    // real prolang.y:17246, exact message.
                    throw LpcRuntimeError("Inconsistent number of values in mapping literal");
                }
                lit->entries.emplace_back(std::move(key), std::move(values));
                if (checkText(",")) {
                    advance();
                    // Trailing comma before the closing "]" -- same real
                    // LPC allowance as the array literal case just above.
                    if (checkText("]")) break;
                    continue;
                }
                break;
            }
        }
        expectText("]", "mapping literal");
        expectText(")", "mapping literal");
        return lit;
    }

    // "#'name" -- LDMud's own closure-literal prefix, bare-name first
    // slice only (ROADMAP.md row 1.2/1.3's own scoping note; see
    // Lexer::lexHashQuote()'s own comment for the full real-source
    // citation and what is deliberately not covered yet -- operator
    // spellings, #'[ forms, #'({ aggregates, scope prefixes). Reuses
    // ClosureLiteralExpr verbatim, the exact same AST node the "(: name
    // :)" case just below builds, rather than a new node kind: a bare
    // "#'name" and a bare "(: name :)" are the same underlying concept
    // (a closure bound to a function by name, no bound args), just two
    // different dialects' own spelling of it -- so this gets CodeGen's
    // existing PushClosure emission and every downstream VM/Closure
    // consumer (funcall, evaluate, map/filter/sort_array callbacks) for
    // free, with zero changes anywhere past this Parser check. Double-
    // gated on dialect_ here too, same belt-and-suspenders discipline as
    // "nil"/"atomic" above -- Lexer::tokenize() already only ever
    // combines "#'" into one token under LpcDialect::LdMud, so this
    // branch could not be reached under FluffOS/DGD in practice, but
    // this is the one place that actually decides "is this a real
    // closure literal", and it should not silently trust the Lexer's
    // gate to hold forever.
    if (checkText("#'") && dialect_ == LpcDialect::LdMud) {
        advance(); // #'

        // "#'efun::name" -- one of LDMud's own real closure-literal scope
        // prefixes (temp/ldmud/doc/LPC/closures: "Closure literals can
        // have prefixes to specify which type of closure shall be
        // created: #'efun::function_name: closure to an efun,
        // #'sefun::function_name: closure to a simul-efun, #'lfun::
        // function_name: closure to an lfun, #'var::variable_name:
        // closure to a global variable. Inherited programs can be given
        // as prefixes, too."). Picked for this slice over the other three
        // (deliberately still not covered: sefun::/lfun:: tier-forcing,
        // var:: -- a genuinely different closure *kind*, a reference to a
        // variable rather than a callable at all, not just a resolution-
        // tier hint on the same shape -- and inherited-program prefixes)
        // by real corpus-frequency ranking across every vendored mudlib
        // this repo has, not by size: efun:: was the *only* scope prefix
        // with any confirmed real occurrence anywhere in that search
        // (`temp/core-lib/secure/simulated-efuns/testing.c`'s own
        // "apply(#'efun::call_out,method,delay,data)"), and the operator-
        // spelling/index-form/aggregate-closure forms had zero confirmed
        // occurrences anywhere at all -- see this session's own STATUS.md
        // entry for the full ranking and methodology. Recognized the same
        // way ordinary "efun::name(...)" calls already are just below in
        // this file (by literal text plus the following "::", not a
        // reserved keyword -- "efun" is not reserved, so there is no
        // ambiguity with a real function or object variable of that
        // name): only advances past "efun::" when both tokens actually
        // match, so a bare "#'efun" with no "::" following it correctly
        // falls through to the plain bare-name case below instead
        // (naming a real function literally called "efun", however
        // unlikely).
        if (check(TokenType::Ident) && peek().text == "efun" && peekAt(1).text == "::") {
            advance(); // efun
            advance(); // ::
            Token nameTok = expect(TokenType::Ident, "closure literal function name after #'efun::");
            auto closure = std::make_unique<ClosureLiteralExpr>();
            closure->functionName = nameTok.text;
            closure->forceEfun = true;
            return closure;
        }

        Token nameTok = expect(TokenType::Ident, "closure literal function name after #'");
        auto closure = std::make_unique<ClosureLiteralExpr>();
        closure->functionName = nameTok.text;
        return closure;
    }

    // "(: name, bound_args... :)" -- closure/function-pointer literal
    // (see Ast.hpp's ClosureLiteralExpr and Value.hpp's Closure for the
    // real-source citation and scope). Recognized the same way "({"/"(["
    // just above/below already are: by the literal two-character open
    // sequence, since the Lexer tokenizes "(" and ":" separately (no
    // dedicated "(:" token, unlike "::"/"->" -- see Lexer::lexSymbol()).
    //
    // Real LPC's grammar.y has two separate productions here, not one:
    // this bare-identifier form (efun/local/simul_efun/global-var
    // reference plus optional bound args, resolved by name at call
    // time), and a general fallback, "L_FUNCTION_OPEN comma_expr ':'
    // ')'" -- any comma-separated expression list at all, compiled to
    // its own anonymous function (see Ast.hpp's InlineLambdaExpr for the
    // full citation and the two real call sites in this mudlib that need
    // it). They are told apart the same way real LPC's LALR parser
    // resolves the ambiguity: only a bare identifier immediately
    // followed by "," or ":" can be this first production (a plain name
    // and nothing else); anything else -- a call expression, a string
    // literal, a parenthesized expression, etc -- falls through to the
    // general comma_expr form below.
    if (checkText("(") && peekAt(1).text == ":") {
        // A "$N" token is also TokenType::Ident (see Lexer::lexLambdaParam()'s
        // own comment), but can never be a legitimate bare closure-literal
        // function name -- "(: $1 :)" is the general InlineLambdaExpr form
        // (a one-argument identity-ish lambda), not a closure literal
        // naming a function called "$1". Excluded here rather than left to
        // fall through, since a lone "$1" immediately followed by ":"
        // would otherwise satisfy the same shape isBareName checks for.
        bool isBareName = peekAt(2).type == TokenType::Ident &&
            !peekAt(2).text.empty() && peekAt(2).text[0] != '$' &&
            peekAt(3).type == TokenType::Symbol &&
            (peekAt(3).text == "," || peekAt(3).text == ":");

        if (isBareName) {
            advance(); // (
            advance(); // :
            Token nameTok = expect(TokenType::Ident, "closure literal function name");
            auto closure = std::make_unique<ClosureLiteralExpr>();
            closure->functionName = nameTok.text;
            if (checkText(",")) {
                advance();
                for (;;) {
                    closure->boundArgs.push_back(parseExpr());
                    if (checkText(",")) {
                        advance();
                        continue;
                    }
                    break;
                }
            }
            expectText(":", "closure literal");
            expectText(")", "closure literal");
            return closure;
        }

        advance(); // (
        advance(); // :
        auto lambda = std::make_unique<InlineLambdaExpr>();
        // Pushed/popped around this lambda's own body only -- a "$N"
        // inside it belongs to *this* lambda, not an outer one it might
        // be nested inside (see lambdaParamMaxStack_'s own comment).
        lambdaParamMaxStack_.push_back(0);
        // Same, for this lambda's own "$(expr)" bound values (see
        // lambdaBoundValuesStack_'s own comment).
        lambdaBoundValuesStack_.emplace_back();
        for (;;) {
            lambda->bodyExprs.push_back(parseExpr());
            if (checkText(",")) {
                advance();
                continue;
            }
            break;
        }
        lambda->paramCount = lambdaParamMaxStack_.back();
        lambdaParamMaxStack_.pop_back();
        lambda->boundValueExprs = std::move(lambdaBoundValuesStack_.back());
        lambdaBoundValuesStack_.pop_back();
        expectText(":", "closure literal");
        expectText(")", "closure literal");
        return lambda;
    }

    // "function(<params>) { <body> }" -- real modern FluffOS's own
    // anonymous-function *expression* (see Ast.hpp's AnonFunctionExpr
    // for the full real-source citation: fluffos-2.23-ds03's own
    // "expr0: ... | L_BASIC_TYPE '(' argument ')' block", gated on the
    // L_BASIC_TYPE token being specifically TYPE_FUNCTION). "function"
    // is already an ordinary type keyword this driver recognizes
    // elsewhere (a parameter/return type, e.g. Dead Souls' own real
    // "mixed apply_unguarded(function f)"); this is the one place that
    // decides whether a "function" token starts a real anonymous-
    // function literal instead -- only when immediately followed by
    // "(" does it, matching the real grammar's own shape exactly and
    // leaving every other use of "function" as an ordinary type token
    // completely unaffected (those never reach parsePrimary() at all,
    // parsed instead by parseDeclPrefix()/parseParamList() in their own
    // type-position contexts).
    if (check(TokenType::Keyword) && peek().text == "function" && peekAt(1).text == "(") {
        advance(); // function
        advance(); // (
        auto anonFn = std::make_unique<AnonFunctionExpr>();
        anonFn->params = parseParamList(&anonFn->isVarargs);
        expectText(")", "anonymous function parameters");
        anonFn->body = parseBlock();
        return anonFn;
    }

    // "object::create()" (grammar.y L_BASIC_TYPE L_COLON_COLON identifier).
    if (check(TokenType::Keyword) && isTypeKeyword(peek()) && peekAt(1).text == "::") {
        std::string qualifier = advance().text;
        return parseQualifiedParentCall(qualifier);
    }

    // "(*fp)(args...)" -- call-through-a-function-pointer-value syntax
    // (grammar.y: "'(' '*' comma_expr ')' '(' expr_list ')'", confirmed
    // by direct reading, not guessed). Real LPC desugars this straight
    // to a call to the core "evaluate" efun (predefs[evaluate_efun]),
    // unconditionally -- not the usual tiered local/inherited/simul_efun
    // lookup a plain "name(...)" call goes through -- with the pointer
    // expression as the first argument ahead of the real call args. This
    // driver's own "evaluate"/"funcall" efuns already do exactly that
    // (EfunTable.cpp's evaluateImpl(), which calls VM::callClosure()),
    // so this is a pure syntax-level desugaring into a forced-efun
    // CallExpr, matching how "efun::name(...)" already reaches the core
    // efun table directly -- no new opcode or VM change needed. Only the
    // single-expression form of the pointer clause is handled (real
    // grammar allows a full comma_expr there too), since every real call
    // site in this mudlib (std/user/editor.c's own "(*__Callback)(...)"/
    // "(*__Abort)()") uses a single bare local variable.
    if (checkText("(") && peekAt(1).text == "*") {
        advance(); // (
        advance(); // *
        AstPtr fpExpr = parseExpr();
        expectText(")", "function pointer call");
        expectText("(", "function pointer call");
        auto call = std::make_unique<CallExpr>();
        call->callee = "evaluate";
        call->forceEfun = true;
        call->args.push_back(std::move(fpExpr));
        auto fpArgs = parseArgList();
        for (auto& arg : fpArgs.args) {
            call->args.push_back(std::move(arg));
        }
        if (!fpArgs.isSpread.empty()) {
            // fpExpr itself (already pushed above) is never spread; pad
            // its own slot with false so argIsSpread stays aligned with
            // call->args (see CallExpr::argIsSpread's own contract).
            call->argIsSpread.assign(1, false);
            for (bool s : fpArgs.isSpread) call->argIsSpread.push_back(s);
        }
        expectText(")", "function pointer call");
        return call;
    }

    if (checkText("(") && peekAt(1).text == "{") {
        advance(); // (
        advance(); // {
        auto lit = std::make_unique<ArrayLiteralExpr>();
        std::vector<bool> elemSpread;
        bool anyElemSpread = false;
        if (!checkText("}")) {
            for (;;) {
                lit->elements.push_back(parseExpr());
                // Real grammar.y:2488-2496's own "expr0 L_DOT_DOT_DOT"
                // spread element, identical production array literals and
                // call argument lists both reuse (see Ast.hpp's
                // ArrayLiteralExpr::elementIsSpread comment).
                bool spread = false;
                if (checkText("...")) {
                    advance();
                    spread = true;
                    anyElemSpread = true;
                }
                elemSpread.push_back(spread);
                if (checkText(",")) {
                    advance();
                    // Trailing comma before the closing "}" (real LPC
                    // allows one, and this mudlib's own code uses it --
                    // secure/SimulEfun/misc.c's own "({ ... "hamster",
                    // })"): without this check the loop would try to
                    // parse another element starting at "}" itself and
                    // fail with "expected expression".
                    if (checkText("}")) break;
                    continue;
                }
                break;
            }
        }
        if (anyElemSpread) {
            lit->elementIsSpread = std::move(elemSpread);
        }
        expectText("}", "array literal");
        expectText(")", "array literal");
        return lit;
    }

    if (checkText("(")) {
        advance();
        auto expr = parseCommaExpr();
        expectText(")", "parenthesized expression");
        return expr;
    }

    // Bare "::name(...)" -- explicit call to an inherited definition of
    // `name`, no identifier before the "::" (see Ast.hpp's CallExpr::
    // parentCall comment; grammar.y's "function_name: L_COLON_COLON
    // identifier"). Checked before the check(TokenType::Ident) branch
    // below since there is no leading identifier token here at all --
    // "::" is Lexer::lexSymbol()'s own single "::" token, so this cannot
    // be confused with a ternary's plain ":".
    if (checkText("::")) {
        advance(); // "::"
        Token fnNameTok = expect(TokenType::Ident, "::name function name");
        expectText("(", "::name call arguments");
        auto parentArgs = parseArgList();
        expectText(")", "::name call arguments");

        auto call = std::make_unique<CallExpr>();
        call->callee = fnNameTok.text;
        call->args = std::move(parentArgs.args);
        call->argIsSpread = std::move(parentArgs.isSpread);
        call->parentCall = true;
        return call;
    }

    if (check(TokenType::Ident)) {
        std::string name = advance().text;

        // "$N" -- see Lexer::lexLambdaParam()'s own comment for why this
        // is lexed as an Ident rather than its own TokenType, and Ast.hpp's
        // LambdaParamExpr for the real citation. Checked first, before any
        // of the other special-cased identifier forms below, since "$N" is
        // never a legal function/variable name to begin with.
        if (!name.empty() && name[0] == '$') {
            if (lambdaParamMaxStack_.empty()) {
                throw LpcRuntimeError(
                    "$var illegal outside of function pointer (\"" + name +
                    "\" used outside a \"(: ... :)\" lambda body)");
            }
            int oneIndexed = std::stoi(name.substr(1));
            if (oneIndexed < 1) {
                throw LpcRuntimeError("In function parameter " + name + ", num must be >= 1");
            }
            lambdaParamMaxStack_.back() = std::max(lambdaParamMaxStack_.back(), oneIndexed);
            auto param = std::make_unique<LambdaParamExpr>();
            param->index = oneIndexed - 1;
            return param;
        }

        // "new(class Name field: val, field: val, ...)" -- real class
        // construction (ROADMAP.md row 3.10's class scoping report, real
        // grammar.y's own "L_NEW '(' L_CLASS L_DEFINED_NAME
        // opt_class_init ')'", class_init: "identifier ':' expr0",
        // named fields, any subset, any order -- NOT a positional
        // "(class name val, val, ...)" literal). Only this exact "new(
        // class ..." shape is recognized here -- real FluffOS also has
        // a separate, unrelated "new(\"/obj/file\")" clone_object-style
        // form (a different real grammar production, "L_NEW '('
        // expr_list ')'") that this row's own scoping explicitly did
        // not cover and is not implemented here; "new" is not reserved
        // as a keyword (same reasoning as "efun" just below, and
        // "class" itself -- see startsClassType()'s own comment), so an
        // ordinary "new(...)" call not immediately followed by "class"
        // falls straight through to the generic named-call path further
        // below unaffected, exactly as it already did before this row.
        // FluffOS-dialect-only, same reasoning as startsClassType().
        if (name == "new" && dialect_ == LpcDialect::FluffOS &&
            checkText("(") && peekAt(1).text == "class") {
            advance(); // "("
            advance(); // "class"
            auto newClass = std::make_unique<NewClassExpr>();
            newClass->className = expect(TokenType::Ident, "new(class ...) class name").text;
            if (!checkText(")")) {
                for (;;) {
                    Token fieldName = expect(TokenType::Ident, "class field initializer name");
                    expectText(":", "class field initializer");
                    AstPtr value = parseExpr();
                    newClass->fieldInits.emplace_back(fieldName.text, std::move(value));
                    if (checkText(",")) {
                        advance();
                        continue;
                    }
                    break;
                }
            }
            expectText(")", "new(class ...) construction");
            return newClass;
        }

        // "efun::name(...)" -- real LPC's explicit escape hatch straight
        // to the core efun table (see CallExpr::forceEfun's own
        // comment). "efun" is not reserved as a keyword (nothing in this
        // mudlib uses it as a plain identifier either way, so there is
        // no ambiguity to worry about), so this is recognized here by
        // its literal text plus the following "::", the same way "->"'s
        // call_other is recognized by its own literal syntax rather than
        // a reserved word.
        if (name == "efun" && checkText("::")) {
            advance(); // "::"
            Token fnNameTok = expect(TokenType::Ident, "efun:: function name");
            expectText("(", "efun:: call arguments");
            auto forcedArgs = parseArgList();
            expectText(")", "efun:: call arguments");

            auto call = std::make_unique<CallExpr>();
            call->callee = fnNameTok.text;
            call->args = std::move(forcedArgs.args);
            call->argIsSpread = std::move(forcedArgs.isSpread);
            call->forceEfun = true;
            return call;
        }

        // "qualifier::name(...)" (grammar.y identifier L_COLON_COLON
        // identifier). "efun" is handled above. Type-keyword form is
        // earlier in this function.
        if (name != "efun" && checkText("::")) {
            return parseQualifiedParentCall(name);
        }

        // "catch(expr)" -- real LPC's own control-flow construct, not a
        // function call (grammar.y: "catch: L_CATCH expr_or_block", a
        // dedicated grammar production -- see Ast.hpp's CatchExpr
        // comment). Recognized the same way "efun::" just above is: by
        // its literal identifier text plus the syntax that follows, not
        // a reserved keyword (matching this parser's existing pattern
        // for sscanf/call_other, both handled the same way a few lines
        // down). Only the parenthesized-expression form is implemented,
        // not real LPC's "catch { block }" alternative -- nothing in
        // this mudlib uses that form.
        if (name == "catch" && checkText("(")) {
            advance(); // "("
            auto guarded = parseCommaExpr();
            expectText(")", "catch expression");

            auto catchExpr = std::make_unique<CatchExpr>();
            catchExpr->guarded = std::move(guarded);
            return catchExpr;
        }

        // "time_expression { <body> }" -- real fluffos-2.23-ds03's own
        // benchmarking expression (see Ast.hpp's TimeExpressionExpr for
        // the full real-source citation and corpus evidence). Real
        // grammar shares the identical "expr_or_block" nonterminal
        // catch(expr) does just above ("block | '(' comma_expr ')'"),
        // and is recognized the same not-a-reserved-keyword way; only
        // the block form is implemented, the mirror image of catch's
        // own "only the parenthesized form" choice -- all 4 real corpus
        // call sites use "{ ... }", none use "( ... )".
        if (name == "time_expression" && checkText("{")) {
            auto timeExpr = std::make_unique<TimeExpressionExpr>();
            timeExpr->body = parseBlock();
            return timeExpr;
        }

        if (!checkText("(")) {
            auto ref = std::make_unique<VarRefExpr>();
            ref->name = name;
            return ref;
        }

        advance();
        auto parsed = parseArgList();
        auto& args = parsed.args;
        expectText(")", "call arguments");

        if (name == "sscanf") {
            if (args.size() < 2) {
                throw LpcRuntimeError("sscanf requires at least (string, format) arguments");
            }
            // sscanf's own trailing arguments are implicit lvalues (real
            // grammar.y's own dedicated "lvalue_list" production, not
            // expr_list at all), not ordinary by-value expressions -- a
            // spread here has no real meaning and no real corpus use, so
            // it is rejected outright rather than silently ignored.
            for (bool s : parsed.isSpread) {
                if (s) {
                    throw LpcRuntimeError(
                        "sscanf: argument spread (\"...\") is not valid on an lvalue output list");
                }
            }
            auto sscanfExpr = std::make_unique<SscanfExpr>();
            sscanfExpr->target = std::move(args[0]);
            sscanfExpr->format = std::move(args[1]);
            for (size_t i = 2; i < args.size(); ++i) {
                if (auto* ref = dynamic_cast<VarRefExpr*>(args[i].get())) {
                    sscanfExpr->varNames.push_back(ref->name);
                    sscanfExpr->indexedTargets.push_back(nullptr);
                    continue;
                }
                // Real LPC's sscanf() output arguments are ordinary
                // lvalues, not restricted to a bare variable name -- see
                // Ast.hpp's own SscanfExpr comment for the real corpus
                // evidence (TMI-2's own access.c) this relaxation is for.
                // A range form ("arr[1..3]") is never a valid lvalue in
                // real LPC either, so it is rejected here the same way a
                // non-lvalue expression is, not silently accepted.
                if (auto* idx = dynamic_cast<IndexExpr*>(args[i].get())) {
                    if (idx->rangeEnd) {
                        throw LpcRuntimeError(
                            "sscanf: output argument " + std::to_string(i - 1) +
                            " cannot be a range expression");
                    }
                    sscanfExpr->varNames.push_back(std::string());
                    sscanfExpr->indexedTargets.push_back(std::move(args[i]));
                    continue;
                }
                throw LpcRuntimeError(
                    "sscanf: output argument " + std::to_string(i - 1) +
                    " must be a plain variable name or an indexed lvalue like arr[i] "
                    "(sscanf's output arguments are implicit lvalues in real LPC, there "
                    "is no \"&var\" reference syntax)");
            }
            return sscanfExpr;
        }

        if (name == "call_other") {
            if (args.size() < 2) {
                throw LpcRuntimeError("call_other requires at least (target, function) arguments");
            }
            // CallOtherExpr::argIsSpread (Ast.hpp) covers only the
            // trailing args, positions 2.. here -- target/function
            // (positions 0/1) have no real corpus use as a spread and no
            // slot to hold one (they are separately typed fields, not
            // part of the args vector), so still rejected outright.
            if (!parsed.isSpread.empty() && (parsed.isSpread[0] || parsed.isSpread[1])) {
                throw LpcRuntimeError(
                    "call_other: the target/function argument cannot be spread (\"...\")");
            }
            auto callOther = std::make_unique<CallOtherExpr>();
            callOther->target = std::move(args[0]);
            callOther->function = std::move(args[1]);
            for (size_t i = 2; i < args.size(); ++i) {
                callOther->args.push_back(std::move(args[i]));
            }
            if (!parsed.isSpread.empty()) {
                callOther->argIsSpread.assign(parsed.isSpread.begin() + 2, parsed.isSpread.end());
            }
            return callOther;
        }

        auto call = std::make_unique<CallExpr>();
        call->callee = name;
        call->args = std::move(args);
        call->argIsSpread = std::move(parsed.isSpread);
        return call;
    }

    throw LpcRuntimeError("parse error: expected expression at line " + std::to_string(peek().line) +
                           " (got \"" + peek().text + "\")");
}

AstPtr Parser::parsePostfix() {
    AstPtr expr = parsePrimary();

    // "->" and "[" both bind at this same postfix level and can chain in
    // either order and repeatedly -- e.g. secure/SimulEfun/misc.c's own
    // "inv[i]->query_property(...)" (index then call_other) or
    // "a->b()->c()" (call_other then call_other again). A single-shot
    // "if" for one followed by a separate "while" for the other (the
    // previous shape here) only handles each starting a chain, not
    // "[" appearing first and being followed by "->": once the index
    // while-loop finished, nothing looped back to check for "->" again,
    // so a leftover "->" was returned unconsumed to the caller. One loop
    // that keeps matching either token, in whatever order they actually
    // appear, handles every ordering and repeat count uniformly.
    for (;;) {
        if (checkText("->")) {
            advance();
            Token nameTok = expect(TokenType::Ident, "call_other operator function name");

            // Real class-member access (ROADMAP.md row 3.10's class
            // scoping report, real grammar.y's own "expr4 L_ARROW
            // identifier" -- no trailing "(") is a real, separate
            // grammar production from call_other's own "expr4 L_ARROW
            // identifier '('" right below, disambiguated purely by
            // whether "(" follows: this driver's own call_other parsing
            // already hard-requires "(" immediately after (the
            // expectText() call just below, unchanged from before this
            // row), so a bare "->identifier" with nothing after it was
            // already a guaranteed parse error here -- genuinely
            // unambiguous to add, no type-inference or grammar conflict
            // at all. Reuses IndexExpr wholesale via MemberNameMarker
            // (see that node's own comment) rather than a dedicated
            // member-access AST node, so the pre-existing indexed-
            // assignment-target reinterpretation (parseStatement()'s
            // own "[" lookahead just below in this same file, and the
            // assignment-expression path) picks up "instance->member =
            // value"/"instance->member" used as a sub-expression for
            // free. FluffOS-dialect-only, same reasoning as
            // startsClassType(); under any other dialect this branch is
            // skipped entirely, so "->identifier" with no "(" keeps
            // throwing the exact same call_other-arguments error it
            // already did before this row.
            if (dialect_ == LpcDialect::FluffOS && !checkText("(")) {
                auto marker = std::make_unique<MemberNameMarker>();
                marker->name = nameTok.text;
                auto idx = std::make_unique<IndexExpr>();
                idx->target = std::move(expr);
                idx->index = std::move(marker);
                expr = std::move(idx);
                continue;
            }

            expectText("(", "call_other operator arguments");
            auto parsed = parseArgList();
            expectText(")", "call_other operator arguments");

            // "->" always names its function literally in the syntax
            // (there is no "target->(expr)(...)" dynamic-dispatch form),
            // so wrap it in a StringLiteral to match CallOtherExpr::
            // function's general expression shape.
            auto funcNameLit = std::make_unique<StringLiteral>();
            funcNameLit->value = nameTok.text;

            auto callOther = std::make_unique<CallOtherExpr>();
            callOther->target = std::move(expr);
            callOther->function = std::move(funcNameLit);
            callOther->args = std::move(parsed.args);
            // Unlike the plain call_other(...) branch above, target is
            // already consumed (the expr4 before "->") and function is
            // always a literal identifier, never part of parsed itself
            // -- so parsed.isSpread already lines up with callOther->args
            // one-to-one, no offset needed. See CallOtherExpr::
            // argIsSpread (Ast.hpp) for the real corpus citation.
            callOther->argIsSpread = std::move(parsed.isSpread);
            expr = std::move(callOther);
            continue;
        }

        if (checkText("[")) {
            advance();

            // "<N" means "N from the end" (real LPC: grammar.y's
            // "expr4 '[' '<' comma_expr ...", see Ast.hpp's IndexExpr
            // comment) -- a bare "<" can never itself start a valid
            // expression, so seeing it as the very first token here is
            // unambiguous, no lookahead needed beyond this one check.
            bool startFromEnd = false;
            if (checkText("<")) {
                advance();
                startFromEnd = true;
            }
            AstPtr startExpr = (dialect_ == LpcDialect::LdMud)
                ? parseExpr()
                : parseCommaExpr();
            AstPtr endExpr = nullptr;
            AstPtr mapColumn = nullptr;
            bool endFromEnd = false;
            if (checkText(",") && dialect_ == LpcDialect::LdMud) {
                // real index_map_expr: '[' expr0 ',' expr0 ']'
                // (prolang.y:17007, F_MAP_INDEX). Reverse-from-end
                // column (`map[key, <n]`) and mapping range
                // (`map[key, n1..n2]`) stay unimplemented this slice.
                advance();
                mapColumn = parseExpr();
            } else if (checkText("..")) {
                advance();
                if (checkText("]")) {
                    // Open-ended range ("arr[start..]", real LPC for
                    // "start to the end" -- grammar.y: "expr4 '['
                    // comma_expr L_RANGE ']'"). Synthesizing a large
                    // sentinel end value works for free with
                    // RangeIndex's existing "clampedEnd = min(end, len -
                    // 1)" clamp at the VM level, no opcode change
                    // needed.
                    auto sentinel = std::make_unique<IntLiteral>();
                    sentinel->value = 2000000000; // clamped down by RangeIndex, see above
                    endExpr = std::move(sentinel);
                } else {
                    if (checkText("<")) {
                        advance();
                        endFromEnd = true;
                    }
                    endExpr = parseExpr();
                }
            }
            expectText("]", "index expression");

            auto idx = std::make_unique<IndexExpr>();
            idx->target = std::move(expr);
            idx->index = std::move(startExpr);
            idx->rangeEnd = std::move(endExpr);
            idx->indexFromEnd = startFromEnd;
            idx->rangeEndFromEnd = endFromEnd;
            idx->mapColumn = std::move(mapColumn);
            expr = std::move(idx);
            continue;
        }

        break;
    }

    if (checkText("++") || checkText("--")) {
        auto* ref = dynamic_cast<VarRefExpr*>(expr.get());
        bool isInc = checkText("++");
        if (ref) {
            std::string name = ref->name;
            advance();
            auto incDec = std::make_unique<IncDecExpr>();
            incDec->op = isInc ? IncDecOp::Inc : IncDecOp::Dec;
            incDec->prefix = false;
            incDec->name = name;
            return incDec;
        }

        // Indexed target, e.g. std/living.c's own "healing[\"intox\"]--"
        // -- see Ast.hpp's IncDecExpr comment.
        auto* idx = dynamic_cast<IndexExpr*>(expr.get());
        if (idx && !idx->rangeEnd) {
            advance();
            auto incDec = std::make_unique<IncDecExpr>();
            incDec->op = isInc ? IncDecOp::Inc : IncDecOp::Dec;
            incDec->prefix = false;
            incDec->indexTarget = std::move(idx->target);
            incDec->indexKey = std::move(idx->index);
            incDec->mapColumn = std::move(idx->mapColumn);
            return incDec;
        }

        throw LpcRuntimeError(
            "parse error: postfix " + peek().text +
            " target must be a simple variable name or indexed target at line " +
            std::to_string(peek().line));
    }

    return expr;
}

AstPtr Parser::parseComparison() {
    AstPtr left = parseShift();

    while (checkText("<") || checkText("<=") || checkText(">") || checkText(">=")) {
        std::string opText = advance().text;
        BinOp op = (opText == "<") ? BinOp::Lt
                 : (opText == "<=") ? BinOp::Lte
                 : (opText == ">") ? BinOp::Gt
                 : BinOp::Gte;

        auto right = parseShift();
        auto bin = std::make_unique<BinaryExpr>();
        bin->op = op;
        bin->left = std::move(left);
        bin->right = std::move(right);
        left = std::move(bin);
    }

    return left;
}

// "<<"/">>" (real C-family bitwise left/right shift). Real
// fluffos-2.23-ds03/grammar.y.pre's own precedence table places these
// ("%left L_LSH L_RSH") strictly between relational ("%left L_ORDER
// '<'", looser) and additive ("%left '+' '-'", tighter) -- confirmed by
// direct reading, not assumed to match plain C. Found live against a
// real third-party mudlib corpus (Dead Souls 3.8.2's own boot attempt):
// secure/daemon/master.c's own real "((1 << 10) | (1 << 0))"
// flag-combining idiom, one of 226 real plain "<<"/">>" call sites
// across the corpus (see Lexer.cpp's own citation for the full count
// and the 2 real compound "<<="/">>=" sites).
AstPtr Parser::parseShift() {
    AstPtr left = parseAdditive();

    while (checkText("<<") || checkText(">>")) {
        std::string opText = advance().text;
        BinOp op = (opText == "<<") ? BinOp::Shl : BinOp::Shr;

        auto right = parseAdditive();
        auto bin = std::make_unique<BinaryExpr>();
        bin->op = op;
        bin->left = std::move(left);
        bin->right = std::move(right);
        left = std::move(bin);
    }

    return left;
}

AstPtr Parser::parseAdditive() {
    AstPtr left = parseMultiplicative();

    while (checkText("+") || checkText("-")) {
        std::string opText = advance().text;
        BinOp op = (opText == "+") ? BinOp::Add : BinOp::Sub;

        auto right = parseMultiplicative();
        auto bin = std::make_unique<BinaryExpr>();
        bin->op = op;
        bin->left = std::move(left);
        bin->right = std::move(right);
        left = std::move(bin);
    }

    return left;
}

AstPtr Parser::parseMultiplicative() {
    AstPtr left = parseUnary();

    while (checkText("*") || checkText("/") || checkText("%")) {
        std::string opText = advance().text;
        BinOp op = (opText == "*") ? BinOp::Mul
                 : (opText == "/") ? BinOp::Div
                 : BinOp::Mod;

        auto right = parseUnary();
        auto bin = std::make_unique<BinaryExpr>();
        bin->op = op;
        bin->left = std::move(left);
        bin->right = std::move(right);
        left = std::move(bin);
    }

    return left;
}

AstPtr Parser::parseUnary() {
    // C-style type cast, e.g. "(string)call_other(...)". grammar.y gives
    // this the exact same precedence as unary "!"/"~" ("cast expr0
    // %prec L_NOT"), and its grammar action is purely a compile-time type
    // annotation -- it just overwrites the inner expression node's
    // declared type for the type-checker ("$$ = $2; $$->type = $1;"),
    // with no runtime conversion efun or opcode involved. This driver has
    // no static type checker to feed that annotation to, so a cast is a
    // genuine no-op here: parse past "(type)" or "(type*)" and return
    // whatever the cast operand itself parses to, unchanged. Only
    // recognized when '(' is immediately followed by a type keyword,
    // which is otherwise never a valid start of a parenthesized
    // expression, so this cannot misfire on a plain "(x)".
    if (checkText("(") && peekAt(1).type == TokenType::Keyword && isTypeKeyword(peekAt(1))) {
        size_t save = pos_;
        advance(); // '('
        advance(); // type keyword
        if (checkText("*")) advance();
        if (checkText(")")) {
            advance(); // ')'
            return parseUnary();
        }
        pos_ = save;
    }
    // grammar.y:780-786: cast is '(' basic_type optional_star ')';
    // basic_type includes L_CLASS identifier (grammar.y:632-668).
    // Kept as TypeCastExpr so ->member can resolve; "(class Name *)"
    // is a no-op strip like "(string *)".
    if (dialect_ == LpcDialect::FluffOS && checkText("(") &&
        peekAt(1).type == TokenType::Ident && peekAt(1).text == "class" &&
        peekAt(2).type == TokenType::Ident) {
        size_t save = pos_;
        advance(); // '('
        advance(); // class
        std::string className = advance().text;
        bool star = false;
        if (checkText("*")) {
            advance();
            star = true;
        }
        if (checkText(")")) {
            advance();
            auto inner = parseUnary();
            if (star) return inner;
            auto cast = std::make_unique<TypeCastExpr>();
            cast->className = std::move(className);
            cast->inner = std::move(inner);
            return cast;
        }
        pos_ = save;
    }
    if (checkText("!")) {
        advance();
        auto operand = parseUnary(); // right-associative: "!!x" chains
        auto notExpr = std::make_unique<UnaryExpr>();
        notExpr->op = UnaryOp::Not;
        notExpr->operand = std::move(operand);
        return notExpr;
    }
    if (checkText("-")) {
        advance();
        auto operand = parseUnary(); // right-associative
        auto negExpr = std::make_unique<UnaryExpr>();
        negExpr->op = UnaryOp::Neg;
        negExpr->operand = std::move(operand);
        return negExpr;
    }
    if (checkText("~")) {
        advance();
        auto operand = parseUnary(); // right-associative, same as "!"/"-"
        auto notExpr = std::make_unique<UnaryExpr>();
        notExpr->op = UnaryOp::BitNot;
        notExpr->operand = std::move(operand);
        return notExpr;
    }
    if (checkText("++") || checkText("--")) {
        bool isInc = checkText("++");
        advance();
        auto operand = parseUnary();
        auto* ref = dynamic_cast<VarRefExpr*>(operand.get());
        if (ref) {
            auto incDec = std::make_unique<IncDecExpr>();
            incDec->op = isInc ? IncDecOp::Inc : IncDecOp::Dec;
            incDec->prefix = true;
            incDec->name = ref->name;
            return incDec;
        }

        // Indexed target -- see Ast.hpp's IncDecExpr comment.
        auto* idx = dynamic_cast<IndexExpr*>(operand.get());
        if (idx && !idx->rangeEnd) {
            auto incDec = std::make_unique<IncDecExpr>();
            incDec->op = isInc ? IncDecOp::Inc : IncDecOp::Dec;
            incDec->prefix = true;
            incDec->indexTarget = std::move(idx->target);
            incDec->indexKey = std::move(idx->index);
            incDec->mapColumn = std::move(idx->mapColumn);
            return incDec;
        }

        throw LpcRuntimeError(
            std::string("parse error: prefix ") + (isInc ? "++" : "--") +
            " target must be a simple variable name or indexed target at line " +
            std::to_string(peek().line));
    }

    AstPtr operand = parsePostfix();

    // Assignment ("=", "+=", "-=", "*=", "/=", "%=") is recognized right
    // here, immediately after parsing what could be an lvalue, rather
    // than only at the very top of the expression grammar: real LPC's
    // grammar rule is "lvalue L_ASSIGN expr0" (a restricted "lvalue"
    // nonterminal on the left, not any expr0), which yacc's LALR parser
    // resolves by committing to an assignment as soon as it sees "IDENT
    // ... =" regardless of surrounding operators. A naive
    // precedence-climbing parser that only checks for "=" after the
    // *entire* tighter-precedence chain has already returned gets this
    // wrong for exactly the shape real code uses:
    // secure/SimulEfun/domains.c's "stringp(val) && val=load_object(val)
    // && domain_exists(...)" -- it would see "stringp(val) && val"
    // reduce to a single (non-lvalue) expression before ever noticing
    // the "=", and fail to parse at all. Checking here, right where a
    // bare identifier operand has just been parsed, matches where the
    // real grammar's "lvalue" actually gets recognized.
    //
    // The right-hand side below is still a full parseExpr(), not some
    // tighter level: real LPC's assignment is its single loosest-binding
    // operator (grammar.y declares "%right L_ASSIGN" before "%left
    // L_LAND", and bison's own generated table for fluffos-2.9-ds2.08's
    // grammar.y shifts on "&&" rather than reducing the assignment right
    // there -- confirmed directly, not just inferred from the precedence
    // declarations). So domains.c's line actually parses as
    // "stringp(val) && (val = (load_object(val) && domain_exists(...)))"
    // -- the assignment's value swallows the trailing "&& domain_exists"
    // too, not just "load_object(val)". That is not a bug to route
    // around: domain_exists(tmp=(string)val->query_domain()) still runs
    // against val's *old* (string) value at that point (relying on real
    // LPC's implicit string-call_other coercion), and the branch returns
    // "tmp" immediately afterward without ever reading the reassigned
    // (now-boolean) val again.
    bool isCompound = false;
    BinOp compoundOp = BinOp::Add;
    bool sawAssignOp = checkText("=");
    if (!sawAssignOp) {
        // The "|="/"&="/"^=" entries need Lexer::lexSymbol() to already
        // tokenize each as one atomic Symbol -- see its own comment for
        // the real corpus evidence (TMI-2's access.c) this pairs with.
        static const std::pair<const char*, BinOp> kCompoundOps[] = {
            {"+=", BinOp::Add}, {"-=", BinOp::Sub}, {"*=", BinOp::Mul},
            {"/=", BinOp::Div}, {"%=", BinOp::Mod},
            {"|=", BinOp::BitOr}, {"&=", BinOp::BitAnd}, {"^=", BinOp::BitXor},
            {"<<=", BinOp::Shl}, {">>=", BinOp::Shr},
        };
        for (const auto& [opText, binOp] : kCompoundOps) {
            if (checkText(opText)) {
                sawAssignOp = true;
                isCompound = true;
                compoundOp = binOp;
                break;
            }
        }
    }

    if (sawAssignOp) {
        auto* ref = dynamic_cast<VarRefExpr*>(operand.get());
        if (ref) {
            std::string name = ref->name;
            advance();
            // Right-associative ("a = b = c" means "a = (b = c)"): the
            // right-hand side is a full expression, which may itself
            // embed another assignment resolved the same way.
            AstPtr value = parseExpr();

            auto assign = std::make_unique<AssignExpr>();
            assign->name = std::move(name);
            assign->value = std::move(value);
            assign->isCompound = isCompound;
            assign->compoundOp = compoundOp;
            return assign;
        }

        // An indexed target used as a sub-expression rather than a bare
        // statement, e.g. std/user/more.c's own
        // "if(!(__More[\"class\"] = cl)) ...": see Ast.hpp's
        // IndexAssignExpr comment for the confirmed real call site and
        // why this needs its own AST node/codegen rather than reusing
        // the statement-level IndexAssignStmt path.
        auto* idx = dynamic_cast<IndexExpr*>(operand.get());
        if (idx && !idx->rangeEnd) {
            advance();
            AstPtr value = parseExpr();

            auto assign = std::make_unique<IndexAssignExpr>();
            assign->target = std::move(idx->target);
            assign->index = std::move(idx->index);
            assign->mapColumn = std::move(idx->mapColumn);
            assign->value = std::move(value);
            assign->isCompound = isCompound;
            assign->compoundOp = compoundOp;
            return assign;
        }

        // Anything else (a range-index target, a call result, etc) is
        // not a real lvalue in LPC either -- matches real grammar.y's
        // own restricted "lvalue" nonterminal.
        throw LpcRuntimeError(
            "parse error: assignment target must be a simple variable name or "
            "indexed target at line " + std::to_string(peek().line));
    }

    return operand;
}

AstPtr Parser::parseEquality() {
    AstPtr left = parseComparison();

    while (checkText("==") || checkText("!=")) {
        std::string opText = advance().text;
        BinOp op = (opText == "==") ? BinOp::Eq : BinOp::Neq;

        auto right = parseComparison();
        auto bin = std::make_unique<BinaryExpr>();
        bin->op = op;
        bin->left = std::move(left);
        bin->right = std::move(right);
        left = std::move(bin);
    }

    return left;
}

AstPtr Parser::parseLogicalOr() {
    AstPtr left = parseLogicalAnd();

    while (checkText("||")) {
        advance();
        auto right = parseLogicalAnd();
        auto bin = std::make_unique<BinaryExpr>();
        bin->op = BinOp::Or;
        bin->left = std::move(left);
        bin->right = std::move(right);
        left = std::move(bin);
    }

    return left;
}

AstPtr Parser::parseLogicalAnd() {
    AstPtr left = parseBitOr();

    while (checkText("&&")) {
        advance();
        auto right = parseBitOr();
        auto bin = std::make_unique<BinaryExpr>();
        bin->op = BinOp::And;
        bin->left = std::move(left);
        bin->right = std::move(right);
        left = std::move(bin);
    }

    return left;
}

// Plain "|" (bitwise/flags OR, int-only this slice -- see Ast.hpp's
// BinOp::BitOr comment). grammar.y places it between "&&" and "^" in
// precedence ("%left L_LAND" declared before "%left '|'", declared
// before "%left '^'"; earlier declarations bind looser). Hit live in
// secure/std/login.c: "input_to(\"get_password\", 1 | 2)".
AstPtr Parser::parseBitOr() {
    AstPtr left = parseBitXor();

    // checkText("|") alone already excludes "||": the Lexer tokenizes
    // that as one distinct two-character token (text "||"), never as two
    // adjacent single-"|" tokens, so there is no ambiguity to resolve
    // here the way BitAnd's "&" vs "&&" needed none either.
    while (checkText("|")) {
        advance();
        auto right = parseBitXor();
        auto bin = std::make_unique<BinaryExpr>();
        bin->op = BinOp::BitOr;
        bin->left = std::move(left);
        bin->right = std::move(right);
        left = std::move(bin);
    }

    return left;
}

// Plain "^" (bitwise XOR, int-only this slice -- see Ast.hpp's
// BinOp::BitXor comment). grammar.y places it between "|" and "&".
AstPtr Parser::parseBitXor() {
    AstPtr left = parseBitAnd();

    while (checkText("^")) {
        advance();
        auto right = parseBitAnd();
        auto bin = std::make_unique<BinaryExpr>();
        bin->op = BinOp::BitXor;
        bin->left = std::move(left);
        bin->right = std::move(right);
        left = std::move(bin);
    }

    return left;
}

// Plain "&" (bitwise AND on numbers, set intersection on arrays -- see
// Ast.hpp's BinOp comment). grammar.y places it between "^" and "==" in
// precedence ("%left '^'" declared before "%left '&'", which is
// declared before "%left L_EQ L_NE"; earlier declarations bind looser).
AstPtr Parser::parseBitAnd() {
    AstPtr left = parseEquality();

    while (checkText("&")) {
        advance();
        auto right = parseEquality();
        auto bin = std::make_unique<BinaryExpr>();
        bin->op = BinOp::BitAnd;
        bin->left = std::move(left);
        bin->right = std::move(right);
        left = std::move(bin);
    }

    return left;
}

AstPtr Parser::parseTernary() {
    AstPtr condition = parseLogicalOr();

    if (checkText("?")) {
        advance();
        AstPtr thenBranch = parseTernary();
        expectText(":", "ternary expression");
        AstPtr elseBranch = parseTernary();

        auto tern = std::make_unique<TernaryExpr>();
        tern->condition = std::move(condition);
        tern->thenBranch = std::move(thenBranch);
        tern->elseBranch = std::move(elseBranch);
        return tern;
    }

    return condition;
}

AstPtr Parser::parseExpr() {
    return parseTernary();
}

// grammar.y:1555-1565: comma_expr is expr0 | comma_expr ',' expr0, with
// CREATE_TWO_VALUES so the last expr0 is the value. Used by return, if,
// while, switch, catch, '(' comma_expr ')', not by expr_list (args and
// array elements stay parseExpr / expr0).
AstPtr Parser::parseCommaExpr() {
    AstPtr left = parseExpr();
    while (checkText(",")) {
        advance();
        auto comma = std::make_unique<CommaExpr>();
        comma->left = std::move(left);
        comma->right = parseExpr();
        left = std::move(comma);
    }
    return left;
}

// Real LPC's for-loop init/update clauses are a "comma_expr" (grammar.y:
// "for_expr: /* EMPTY */ | comma_expr", "comma_expr: expr0 | comma_expr
// ',' expr0"), i.e. one or more comma-separated expressions each
// evaluated in order purely for side effect (confirmed live in
// secure/SimulEfun/misc.c: "for(i = 0, s = sizeof(stack1); i < s; i++)").
// A single expression is returned as-is; more than one is wrapped in a
// Block of ExprStmts, reusing the same "Block standing in for a sequence
// of statements in a single-AstPtr slot" shape the comma-separated local
// var decl already uses -- CodeGen::emitForStmt() dispatches on that
// shape the same way emitStatement() already does for local decls.
AstPtr Parser::parseCommaExprChain() {
    AstPtr first = parseExpr();
    if (!checkText(",")) return first;

    auto wrap = [](AstPtr e) {
        auto s = std::make_unique<ExprStmt>();
        s->expr = std::move(e);
        return s;
    };

    auto block = std::make_unique<Block>();
    block->isRealScope = false;
    block->statements.push_back(wrap(std::move(first)));
    while (checkText(",")) {
        advance();
        block->statements.push_back(wrap(parseExpr()));
    }
    return block;
}

// Real grammar.y's own "statement: comma_expr ';'" (grammar.y:1055) is one
// production for every plain-expression statement: comma_expr itself is
// "expr0 | comma_expr ',' expr0" (grammar.y:1555-1562), the real C-style
// comma operator, evaluating each expr0 in order and discarding every
// result but the last. This driver's own statement parser splits that one
// real production into three separate fast paths below (a bare
// assignment, an indexed assignment, and the general expression-statement
// fallback) rather than always going through parseCommaExprChain() from
// scratch, since each fast path has already committed to parsing its own
// first element a different way by the time it knows whether a comma
// follows. This is the shared tail every one of them calls once their own
// first statement is built, so all three honor the same real rule
// uniformly: if what follows is a comma, real LPC allows chaining further
// ", expr0" elements before the terminating ';', each evaluated for its
// side effect only, exactly like the existing parseCommaExprChain() (used
// for a for-loop's own init/update clause) already desugars this same
// grammar rule. Real corpus: TMI-2's own real cmds/file/_eval.c ->
// doith()'s "inp[i] = inp[i] + \";\"+ inp[i+1], inp -= ({inp[i+1]});" -- an
// indexed assignment followed by a comma-chained whole-array compound
// assignment, previously rejected outright ("expected \";\" in indexed
// assignment statement ... got \",\"") since the indexed-assignment fast
// path required its terminating ';' immediately, with no comma handling
// at all.
AstPtr Parser::continueStatementCommaChain(AstPtr firstStmt) {
    if (!checkText(",")) return firstStmt;
    auto block = std::make_unique<Block>();
    block->isRealScope = false;
    block->statements.push_back(std::move(firstStmt));
    while (checkText(",")) {
        advance();
        auto s = std::make_unique<ExprStmt>();
        s->expr = parseExpr();
        block->statements.push_back(std::move(s));
    }
    return block;
}

AstPtr Parser::parseReturnStatement() {
    expectText("return", "return statement");
    auto stmt = std::make_unique<ReturnStmt>();
    if (!checkText(";")) {
        stmt->expr = parseCommaExpr();
    }
    expectText(";", "return statement");
    return stmt;
}

// Parses one "type name [= expr]" declaration, without consuming a
// trailing ';' or handling comma-separated follow-on names -- the two
// callers below (a plain statement, and a for-loop init clause) each want
// different tail handling, so that part is left to them. The type and
// its array-ness are already resolved by the caller's own
// parseTypeToken() call (startsType()'s own bare "array" needs to be
// told apart from an ordinary type keyword before this function is
// even reached, not after).
std::unique_ptr<VarDeclStmt> Parser::parseSingleVarDecl(const std::string& typeText, bool isArray) {
    Token nameTok = expect(TokenType::Ident, "variable declaration name");

    auto decl = std::make_unique<VarDeclStmt>();
    decl->type = typeText;
    decl->isArray = isArray;
    decl->name = nameTok.text;

    if (checkText("=")) {
        advance();
        decl->initializer = parseExpr();
    }

    return decl;
}

AstPtr Parser::parseVarDeclStatement() {
    TypeToken typeTok = parseTypeToken("variable declaration type");
    auto first = parseSingleVarDecl(typeTok.type, typeTok.isArray);

    // Real LPC allows comma-separated local declarations sharing one type,
    // e.g. "string file, fl, ac;" (confirmed live in the mudlib's
    // secure/daemon/master.c). Each name gets independently wrapped in its
    // own VarDeclStmt and appended as its own statement, mirroring how
    // parseObjectVarDeclRest already handles the same shape for object
    // variables -- there is no multi-name AST node, just several
    // single-name ones emitted back to back.
    if (!checkText(",")) {
        expectText(";", "variable declaration");
        return first;
    }

    auto block = std::make_unique<Block>();
    block->isRealScope = false;
    block->statements.push_back(std::move(first));
    while (checkText(",")) {
        advance();
        // Each comma-continued name gets its own independent array
        // marker, not the first name's -- real corpus: master.c's own
        // "mixed *privs, *ok;" (both starred), and this driver's own
        // pre-existing testBitAndVmExecutionOnArraysIsIntersection.
        // (Regression note: an earlier version of this refactor checked
        // the marker only once, before the loop, reusing the first
        // name's isArray for every subsequent name -- caught by that
        // exact test, fixed before landing.)
        bool isArray = consumeArrayMarker();
        block->statements.push_back(parseSingleVarDecl(typeTok.type, isArray));
    }
    expectText(";", "variable declaration");
    return block;
}

AstPtr Parser::parseAssignStatement() {
    Token nameTok = expect(TokenType::Ident, "assignment target");
    expectText("=", "assignment");
    auto stmt = std::make_unique<AssignStmt>();
    stmt->name = nameTok.text;
    stmt->value = parseExpr();
    AstPtr result = continueStatementCommaChain(std::move(stmt));
    expectText(";", "assignment statement");
    return result;
}

std::unique_ptr<Block> Parser::parseBranch() {
    if (checkText("{")) {
        return parseBlock();
    }
    auto block = std::make_unique<Block>();
    block->isRealScope = false;
    block->statements.push_back(parseStatement());
    return block;
}

AstPtr Parser::parseIfStatement() {
    expectText("if", "if statement");
    expectText("(", "if condition");
    auto stmt = std::make_unique<IfStmt>();
    stmt->condition = parseCommaExpr();
    expectText(")", "if condition");
    stmt->thenBranch = parseBranch();

    if (checkText("else")) {
        advance();
        stmt->elseBranch = parseBranch();
    }

    return stmt;
}

AstPtr Parser::parseWhileStatement() {
    expectText("while", "while statement");
    expectText("(", "while condition");
    auto stmt = std::make_unique<WhileStmt>();
    stmt->condition = parseCommaExpr();
    expectText(")", "while condition");
    stmt->body = parseBranch();
    return stmt;
}

// "do statement while ( expr ) ;" (grammar.y). The body always runs once
// before the condition is ever consulted, so unlike parseWhileStatement()
// there is no leading condition to parse -- the trailing "while (cond);"
// is read only after the body itself.
AstPtr Parser::parseDoWhileStatement() {
    expectText("do", "do-while statement");
    auto stmt = std::make_unique<DoWhileStmt>();
    stmt->body = parseBranch();
    expectText("while", "do-while statement");
    expectText("(", "do-while condition");
    stmt->condition = parseCommaExpr();
    expectText(")", "do-while condition");
    expectText(";", "do-while statement");
    return stmt;
}

// "for (first_for_expr ; for_expr ; for_expr) statement" (grammar.y).
// first_for_expr is either empty, a plain expression, or a single
// declaration with an optional initializer; the other two clauses are
// either empty or a plain expression. None of the three consume their own
// trailing separator -- this function consumes the ';' / ')' delimiters
// itself so the shared parseExpr()/parseSingleVarDecl() helpers don't need
// a "no trailing punctuation" variant beyond the one already factored out
// for var decls.
AstPtr Parser::parseForStatement() {
    expectText("for", "for statement");
    expectText("(", "for statement");

    auto stmt = std::make_unique<ForStmt>();

    if (!checkText(";")) {
        if (startsType()) {
            TypeToken typeTok = parseTypeToken("for statement init clause");
            stmt->init = parseSingleVarDecl(typeTok.type, typeTok.isArray);
        } else {
            stmt->init = parseCommaExprChain();
        }
    }
    expectText(";", "for statement init clause");

    if (!checkText(";")) {
        stmt->condition = parseCommaExpr();
    }
    expectText(";", "for statement condition clause");

    if (!checkText(")")) {
        stmt->update = parseCommaExprChain();
    }
    expectText(")", "for statement update clause");

    stmt->body = parseBranch();
    return stmt;
}

AstPtr Parser::parseBreakStatement() {
    expectText("break", "break statement");
    expectText(";", "break statement");
    return std::make_unique<BreakStmt>();
}

AstPtr Parser::parseContinueStatement() {
    expectText("continue", "continue statement");
    expectText(";", "continue statement");
    return std::make_unique<ContinueStmt>();
}

// A foreach loop variable is either a pre-existing name (a plain
// identifier, resolved like any other variable reference at codegen
// time) or a new inline declaration ("type name", the same shape a
// parameter or local var decl uses, including startsType()'s own bare
// "array" -- real corpus: secure/sefun/sockets.c's own "foreach (array
// item in finalsocks)", this file's own OpCode::ExpandVarargs blocker's
// immediate successor, found continuing the same Dead Souls 3.8.2 boot
// attempt). The type itself (and any array marker, "mixed *item" seen
// live in secure/SimulEfun/misc.c, or "mixed array item") is consumed
// via parseTypeToken() and mostly discarded -- ForeachVarSpec tracks
// only whether this is a new declaration and, as of ROADMAP.md row
// 3.10's class scoping report, a real declared class name specifically
// (real corpus: secure/daemon/inet.c's own "foreach(string svc, class
// service s in Services)"), everything else still just an array-ness
// flag with no name attached to remember.
Parser::ForeachVarSpec Parser::parseForeachVar() {
    if (startsType()) {
        TypeToken typeTok = parseTypeToken("foreach variable type");
        Token nameTok = expect(TokenType::Ident, "foreach variable name");
        // "class:" prefix carried through (see startsClassType()'s own
        // comment); every other type is still discarded exactly as
        // before this row -- ForeachVarSpec::classType stays "" for all
        // of them.
        std::string classType;
        if (typeTok.type.rfind("class:", 0) == 0) {
            classType = typeTok.type.substr(6);
        }
        return ForeachVarSpec{nameTok.text, true, classType};
    }
    Token nameTok = expect(TokenType::Ident, "foreach variable name");
    return ForeachVarSpec{nameTok.text, false, ""};
}

AstPtr Parser::parseForeachStatement() {
    expectText("foreach", "foreach statement");
    expectText("(", "foreach statement");

    ForeachVarSpec first = parseForeachVar();
    ForeachVarSpec second;
    bool hasSecond = false;
    if (checkText(",")) {
        advance();
        second = parseForeachVar();
        hasSecond = true;
    }
    expectText("in", "foreach statement");
    AstPtr collection = parseExpr();
    expectText(")", "foreach statement");

    auto stmt = std::make_unique<ForeachStmt>();
    stmt->varName = first.name;
    stmt->declareVar = first.isNewDecl;
    stmt->varClassType = first.classType;
    if (hasSecond) {
        stmt->hasValueVar = true;
        stmt->valueVarName = second.name;
        stmt->declareValueVar = second.isNewDecl;
        stmt->valueVarClassType = second.classType;
    }
    stmt->collection = std::move(collection);
    stmt->body = parseBranch();
    return stmt;
}

// "switch (subject) { case_body }". case_body is a sequence of
// "case constExpr:" / "default:" labels interleaved with ordinary
// statements, matching real C/LPC fallthrough switch (there is no
// implicit break between cases -- see SwitchStmt's comment). Range case
// labels ("case A..B:", real grammar.y's own "L_CASE case_label L_RANGE
// case_label ':'") are implemented; the open-ended forms ("case A..:",
// "case ..B:") are not -- see CaseLabel's own comment for the real
// corpus evidence and the still-unevidenced gap.
AstPtr Parser::parseSwitchStatement() {
    expectText("switch", "switch statement");
    expectText("(", "switch statement subject");
    AstPtr subject = parseCommaExpr();
    expectText(")", "switch statement subject");
    expectText("{", "switch statement body");

    auto stmt = std::make_unique<SwitchStmt>();
    stmt->subject = std::move(subject);

    while (!checkText("}")) {
        if (atEnd()) {
            throw LpcRuntimeError("parse error: unterminated switch statement");
        }
        if (checkText("case")) {
            advance();
            AstPtr value = parseExpr();
            auto label = std::make_unique<CaseLabel>();
            label->value = std::move(value);
            if (checkText("..")) {
                advance();
                label->rangeEnd = parseExpr();
            }
            expectText(":", "case label");
            stmt->body.push_back(std::move(label));
            continue;
        }
        if (checkText("default")) {
            advance();
            expectText(":", "default label");
            stmt->body.push_back(std::make_unique<CaseLabel>());
            continue;
        }
        stmt->body.push_back(parseStatement());
    }
    expectText("}", "switch statement body");
    return stmt;
}

AstPtr Parser::parseStatement() {
    // Null statement: a bare ";", most commonly a loop whose entire body
    // is the condition's own side effects (e.g. secure/SimulEfun/misc.c's
    // "while( i-- && ( str[i..i] != ":" ) );"). An empty Block is already
    // this codebase's stand-in for "zero or more statements in a single
    // AstPtr slot" (see parseCommaExprChain()/parseVarDeclStatement()),
    // and CodeGen::emitBlock() over zero statements naturally emits
    // nothing, so it doubles as a no-op here with no CodeGen changes
    // needed.
    if (checkText(";")) {
        advance();
        return std::make_unique<Block>();
    }

    // A bare "{ ... }" block used as a standalone statement (not
    // attached to any if/while/for), purely for local-variable scoping
    // -- real, standard LPC/C syntax. Found live compiling std/user.c's
    // own quit()-adjacent cleanup code: a "{ int total_credits,
    // chip_amount; object chip; ... }" block sitting directly in a
    // function body between two other statements. No CodeGen changes
    // needed: CodeGen::emitStatement() already flattens any Block node
    // it encounters as a statement (originally for comma-separated
    // local var decls, "string a, b;"), which is exactly right here
    // too -- this driver has no lexical block-scoping to enforce beyond
    // what already exists for locals declared inside an if/while body
    // via this same parseBlock().
    if (checkText("{")) {
        return parseBlock();
    }

    if (checkText("if")) {
        return parseIfStatement();
    }
    if (checkText("while")) {
        return parseWhileStatement();
    }
    if (checkText("do")) {
        return parseDoWhileStatement();
    }
    if (checkText("for")) {
        return parseForStatement();
    }
    if (checkText("foreach")) {
        return parseForeachStatement();
    }
    if (checkText("switch")) {
        return parseSwitchStatement();
    }
    if (checkText("return")) {
        return parseReturnStatement();
    }
    if (checkText("break")) {
        return parseBreakStatement();
    }
    if (checkText("continue")) {
        return parseContinueStatement();
    }

    if (startsType()) {
        return parseVarDeclStatement();
    }

    if (check(TokenType::Ident) && peekAt(1).type == TokenType::Symbol && peekAt(1).text == "=") {
        return parseAssignStatement();
    }

    if (check(TokenType::Ident) && peekAt(1).type == TokenType::Symbol &&
        (peekAt(1).text == "[" || peekAt(1).text == "->")) {
        // Might be an indexed assignment ("ref[fl] = ...;" or "ref[fl]
        // += ...;", real compound-assignment forms too -- confirmed
        // live compiling std/user.c's own "player_data[\"general\"]
        // [\"quest points\"] += (int)call_other(...)", see Ast.hpp's
        // IndexAssignStmt comment) or just an indexed read used as a
        // bare expression statement ("ref[fl];"). The "->" case is real
        // class-member assignment (ROADMAP.md row 3.10's class scoping
        // report, real corpus: secure/daemon/inet.c's own "s->PortOffset
        // = port_offset;") -- parsePostfix()'s own "->identifier" (no
        // paren) branch builds exactly the same IndexExpr shape the "["
        // case already does (see MemberNameMarker's own comment), so it
        // needs no separate handling below, only this lookahead widened
        // to actually reach parsePostfix() for it; a "->identifier("
        // call_other is unaffected (parsePostfix() itself decides that
        // shape is not an IndexExpr at all, so the dynamic_cast below
        // simply fails for it exactly as it always has). Reuse
        // parsePostfix() to parse the target -- it already knows how to
        // build up chained IndexExpr nodes -- then decide which case
        // this is. If it is not an indexed assignment, rewind and let
        // the plain expression-statement path below parse it from
        // scratch.
        size_t save = pos_;
        AstPtr target = parsePostfix();
        if (auto* idx = dynamic_cast<IndexExpr*>(target.get())) {
            bool isCompound = false;
            BinOp compoundOp = BinOp::Add;
            bool sawAssignOp = checkText("=");
            if (!sawAssignOp) {
                static const std::pair<const char*, BinOp> kCompoundOps[] = {
                    {"+=", BinOp::Add}, {"-=", BinOp::Sub}, {"*=", BinOp::Mul},
                    {"/=", BinOp::Div}, {"%=", BinOp::Mod},
                    {"|=", BinOp::BitOr}, {"&=", BinOp::BitAnd}, {"^=", BinOp::BitXor},
                    {"<<=", BinOp::Shl}, {">>=", BinOp::Shr},
                };
                for (const auto& [opText, binOp] : kCompoundOps) {
                    if (checkText(opText)) {
                        sawAssignOp = true;
                        isCompound = true;
                        compoundOp = binOp;
                        break;
                    }
                }
            }
            if (sawAssignOp) {
                advance();
                auto stmt = std::make_unique<IndexAssignStmt>();
                stmt->target = std::move(idx->target);
                stmt->index = std::move(idx->index);
                stmt->mapColumn = std::move(idx->mapColumn);
                stmt->value = parseExpr();
                stmt->isCompound = isCompound;
                stmt->compoundOp = compoundOp;
                AstPtr result = continueStatementCommaChain(std::move(stmt));
                expectText(";", "indexed assignment statement");
                return result;
            }
        }
        pos_ = save;
    }

    auto expr = parseExpr();
    auto exprStmt = std::make_unique<ExprStmt>();
    exprStmt->expr = std::move(expr);
    AstPtr result = continueStatementCommaChain(std::move(exprStmt));
    expectText(";", "expression statement");
    return result;
}

std::unique_ptr<Block> Parser::parseBlock() {
    expectText("{", "function body");
    auto block = std::make_unique<Block>();
    while (!checkText("}")) {
        if (atEnd()) {
            throw LpcRuntimeError("parse error: unterminated block");
        }
        block->statements.push_back(parseStatement());
    }
    expectText("}", "function body");
    return block;
}

std::vector<Param> Parser::parseParamList(bool* isVarargsOut) {
    std::vector<Param> params;
    if (checkText(")")) return params;
    for (;;) {
        // parseTypeToken() covers both the two-word "<type> array"/
        // "<type> *" marker (e.g. "mixed *info"/"object array e") and a
        // completely bare "array" with no preceding type at all (real
        // corpus: lib/std/bane.c's own "int SetBane(array arr)") -- see
        // its own comment for the real grammar citation. No array
        // semantics are implemented, this only records the flag so the
        // syntax parses instead of erroring or losing the marker.
        TypeToken typeTok = parseTypeToken("function parameter type");
        bool isArray = typeTok.isArray;

        // The name itself is optional -- real LPC allows a parameter to
        // be declared with just its type, no identifier at all (real
        // grammar.y's own "new_arg: arg_type optional_star" alternative,
        // confirmed live: this reference testsuite mudlib's own
        // single/master.c "staticf void crash(string, object, object)"
        // and single/simul_efun.c's "string domain_file(string) { ... }",
        // neither ever referencing its own unnamed argument in the
        // body). Only consumed when an identifier is actually next, so a
        // bare "," or ")" here still just closes this parameter the same
        // way an explicitly-named one would. See CodeGen.cpp's own
        // declareLocal() for how an empty name is handled downstream --
        // still a real, positionally-filled slot, just never reachable
        // by name.
        std::string paramName;
        if (check(TokenType::Ident)) {
            paramName = advance().text;
        }
        params.push_back(Param{typeTok.type, paramName, isArray});
        if (checkText(",")) { advance(); continue; }
        break;
    }

    // Trailing "..." marks the last declared parameter as a real varargs
    // rest-parameter (real grammar.y:706-717's own "argument_list
    // L_DOT_DOT_DOT", e.g. secure/SimulEfun/misc.c's "int true(mixed
    // args...)" -- real interpret.c:1394-1410's own
    // setup_varargs_variables(), see Ast.hpp's FunctionDecl::isVarargs
    // and VM.cpp's VM::run() for the full real-semantics citation and
    // this driver's own implementation). Previously parsed and silently
    // discarded here (this comment used to say so); now recorded via
    // isVarargsOut so the caller can wire real capture semantics.
    if (checkText("...")) {
        advance();
        if (isVarargsOut) *isVarargsOut = true;
    }

    return params;
}

Parser::DeclPrefix Parser::parseDeclPrefix(const std::string& context) {
    // Consume any leading modifier keywords (static, private, public,
    // protected, nomask, varargs), in any order, any number of times.
    // This driver has no visibility or staticness semantics to enforce
    // for most of them, so they are otherwise discarded before reading
    // the real type -- "private" is the one exception, recorded via
    // DeclPrefix::isPrivate (see its own comment) because it actually
    // changes object-variable slot-naming semantics, not just access
    // control this driver doesn't enforce anyway.
    bool isPrivate = false;
    while (check(TokenType::Keyword) && isModifierKeyword(peek())) {
        if (peek().text == "private") isPrivate = true;
        advance();
    }

    std::string type;
    bool isArray = false;

    if (startsClassType()) {
        // Real "class <name>" as a top-level declaration's own type
        // (ROADMAP.md row 3.10's class scoping report), e.g.
        // "class service Registered;" (object variable) or a function
        // returning "class death" -- see startsClassType()'s own
        // comment; this driver's own parseTypeToken()/startsType() pair
        // is not reused here since parseDeclPrefix() has never routed
        // through those (it predates them, see its own bare-"array"
        // check just below, which duplicates the same idea rather than
        // sharing it), so this mirrors that same existing duplication
        // rather than restructuring an unrelated, already-working path.
        advance(); // "class"
        type = "class:" + advance().text; // the class name itself
        isArray = consumeArrayMarker();
    } else if (check(TokenType::Ident) && peek().text == "array") {
        // A completely bare "array" (startsType()'s own citation, real
        // grammar.y.pre:697-715) is real here too, e.g. lib/std/story.c's
        // own "array GetTaleKeys()" (return type) and lib/guard.c's
        // "private static array PendingGuard" (object var) -- checked
        // before the ordinary TokenType::Keyword branch below since
        // "array" lexes as a plain Ident (consumeArrayMarker()'s own
        // comment), not a Keyword, so without this check it would fall
        // straight into the "omitted entirely" branch and be misread as
        // the declaration's own *name* instead of its type.
        advance();
        type = "mixed";
        isArray = true;
    } else if (check(TokenType::Keyword)) {
        type = advance().text;

        // An optional '*' (or, real ARRAY_RESERVED_WORD builds, the
        // bare word "array" immediately after a real type keyword, the
        // two-word form -- see consumeArrayMarker()'s own comment)
        // marks an array/pointer type, e.g. "string *epilog(int x)"/
        // "string array GetTeachingLanguages()" or "mixed *items;".
        // This only records the flag, no array semantics implemented.
        isArray = consumeArrayMarker();
    } else {
        // Real LPC allows the return/declaration type to be omitted
        // entirely when only modifiers precede the name -- grammar.y's
        // own type production is genuinely optional there, historically
        // defaulting to "mixed". Confirmed live compiling std/user.c:
        // "private static register_channels();" (a prototype) and
        // "static private register_channels() { ... }" (its
        // definition), neither naming a return type at all. This driver
        // has no static type checking for the omitted type to feed
        // into anyway, so "mixed" here is just a placeholder label; the
        // next token is the declaration's name, not a type. A bare
        // "array" is the one real Ident-typed exception to that (see
        // the branch above); every other Ident reaching here really is
        // just the name, per the same corpus-checked reasoning
        // consumeArrayMarker()'s own comment already established.
        type = "mixed";
    }

    Token nameTok = expect(TokenType::Ident, context + " name");

    return DeclPrefix{type, isArray, nameTok.text, isPrivate};
}

std::unique_ptr<FunctionDecl> Parser::parseFunctionRest(DeclPrefix prefix) {
    auto fn = std::make_unique<FunctionDecl>();
    fn->returnType = std::move(prefix.type);
    fn->returnTypeIsArray = prefix.isArray;
    fn->name = std::move(prefix.name);

    expectText("(", "function declaration parameters");
    fn->params = parseParamList(&fn->isVarargs);
    expectText(")", "function declaration parameters");

    if (checkText(";")) {
        // Prototype-only declaration, e.g. "void create();". No body to
        // parse; fn->body stays nullptr, matching IfStmt::elseBranch's
        // precedent for an absent optional block.
        advance();
        return fn;
    }

    fn->body = parseBlock();
    return fn;
}

std::vector<std::unique_ptr<ObjectVarDecl>> Parser::parseObjectVarDeclRest(DeclPrefix prefix) {
    std::vector<std::unique_ptr<ObjectVarDecl>> decls;

    // "private" (like every other modifier here) applies to the whole
    // comma-separated declaration list, not per-name -- e.g.
    // std/living.c's own "static private int __Locked, __LastAged;"
    // makes both names private, not just the first.
    bool isPrivate = prefix.isPrivate;
    auto makeDecl = [isPrivate](const std::string& type, bool isArray, const std::string& name,
                                 AstPtr initializer) {
        auto decl = std::make_unique<ObjectVarDecl>();
        decl->type = type;
        decl->isArray = isArray;
        decl->name = name;
        decl->isPrivate = isPrivate;
        decl->initializer = std::move(initializer);
        return decl;
    };

    // Declaration-time initializers ("type name = expr;") -- real,
    // standard LPC, confirmed live needed by secure/daemon/wiztools.c's
    // own "string *REISSUED_TOOLS = ({ ... });" (surfaced walking the
    // admin-bootstrap path, see Ast.hpp's ObjectVarDecl comment). Only a
    // single expression per name is parsed here; CodeGen::generate()
    // decides how it actually gets evaluated (once per instance, before
    // create() runs).
    AstPtr firstInit;
    if (checkText("=")) {
        advance();
        firstInit = parseExpr();
    }
    decls.push_back(makeDecl(prefix.type, prefix.isArray, prefix.name, std::move(firstInit)));

    while (checkText(",")) {
        advance();

        // Each comma-separated name gets its own independent optional
        // '*' (or, real ARRAY_RESERVED_WORD builds, "array" -- see
        // consumeArrayMarker()'s own comment), matching real LPC (e.g.
        // "mixed a, *b;"), not just inheriting the first name's array
        // flag.
        bool isArray = consumeArrayMarker();

        Token nameTok = expect(TokenType::Ident, "object variable declaration name");

        AstPtr init;
        if (checkText("=")) {
            advance();
            init = parseExpr();
        }

        decls.push_back(makeDecl(prefix.type, isArray, nameTok.text, std::move(init)));
    }

    expectText(";", "object variable declaration");
    return decls;
}

// Real LPC lets an inherit target be any compile-time string constant,
// most commonly a macro ("inherit DAEMON;") or a macro concatenation
// ("inherit REFS_D;", where REFS_D expands to "DIR_DAEMONS+\"/refs\"").
// cpp resolves the macros themselves but has no idea "+" between two now-
// literal strings is foldable, so by the time this driver's Lexer sees
// it, the token stream looks like `"..." + "..."`. This mirrors grammar.y's
// own "string_con1: string_con2 | string_con1 '+' string_con1" rule by
// folding a left-associative chain of string literals here, at parse
// time, since Program::inherits needs a final resolved path string, not
// an expression tree -- there is no VM yet to evaluate one against.
//
// grammar.y's own "string_con1" is built on "string_con2", not a bare
// L_STRING token -- "string_con2: L_STRING | string_con2 L_STRING", real
// adjacent string literal concatenation with *no* operator at all
// between them, the exact same real feature parsePrimary() already
// implements for an ordinary expression (see its own citation, this
// same corpus's real secure/daemon/master.c shout() call). This path
// only ever consumed a "+"-joined chain, not a bare-adjacent one, so a
// macro expanding to *adjacent* literals rather than "+"-joined ones
// failed here even though the identical shape already worked fine as
// an ordinary expression. Real corpus: Dead Souls 3.8.2's own
// secure/include/std.h "#define LIB_DAEMON DIR_STD \"/daemon\"", where
// DIR_STD itself further expands (secure/include/dirs.h) to "DIR_LIB
// \"/std\"" and DIR_LIB to plain "\"/lib\"" -- real secure/daemon/
// master.c's own "inherit LIB_DAEMON;" therefore reaches this function
// as three bare-adjacent literals, "\"/lib\" \"/std\" \"/daemon\"", no
// "+" anywhere in the chain. Fixed by consuming a run of adjacent
// String tokens directly, the same way parsePrimary() already does,
// interleaved with the existing "+"-joined handling so either form (or
// a mix of both, a real if unusual possibility) resolves correctly.
std::string Parser::parseInheritPathString() {
    std::string result = expect(TokenType::String, "inherit statement path").text;
    for (;;) {
        if (check(TokenType::String)) {
            result += advance().text;
        } else if (checkText("+")) {
            advance();
            result += expect(TokenType::String, "inherit statement path (string concatenation)").text;
        } else {
            break;
        }
    }
    return result;
}

// Called with "class" already consumed; parses "identifier '{'
// member_list '}'" (real grammar.y's own member_list: "member_list
// basic_type member_name_list ';'", zero or more times, no trailing ";"
// after the closing "}" -- matches real corpus's own exact shape, e.g.
// lib/lib/include/player.h's "class death { int Date; string Enemy; }").
// Member types are parsed via parseTypeToken() (so a member itself
// typed "class <name>" -- real corpus: secure/daemon/inet.c's own
// "class service { ... int SocketClass; ... }" is a plain int here, but
// nothing stops a future real file nesting a class-typed member the
// same production already allows) and discarded; only declared order
// matters (see ClassDeclStmt's own comment).
std::unique_ptr<ClassDeclStmt> Parser::parseClassDecl() {
    auto decl = std::make_unique<ClassDeclStmt>();
    decl->name = expect(TokenType::Ident, "class declaration name").text;
    expectText("{", "class declaration body");
    while (!checkText("}")) {
        if (atEnd()) {
            throw LpcRuntimeError("parse error: unterminated class declaration");
        }
        parseTypeToken("class member type");
        for (;;) {
            consumeArrayMarker();
            Token memberName = expect(TokenType::Ident, "class member name");
            decl->memberNames.push_back(memberName.text);
            if (checkText(",")) {
                advance();
                continue;
            }
            break;
        }
        expectText(";", "class member declaration");
    }
    expectText("}", "class declaration body");
    return decl;
}

void Parser::parseInheritStatement(Program& program) {
    expectText("inherit", "inherit statement");
    program.inherits.push_back(parseInheritPathString());
    expectText(";", "inherit statement");
}

std::unique_ptr<Program> Parser::parseProgram() {
    auto program = std::make_unique<Program>();
    while (!atEnd()) {
        if (checkText("inherit")) {
            parseInheritStatement(*program);
            continue;
        }

        // Real grammar.y's own inherit production is "type_modifier_list
        // L_INHERIT string_con1 ';'" -- zero or more modifiers
        // (private/static/public/protected/nomask/varargs/atomic) may
        // precede "inherit" itself. Found live against a real third-party
        // mudlib corpus (row 3.8's TMI-2 boot attempt): std/user/tsh.c's
        // own real "private inherit STACK_ADT;" -- previously misparsed
        // as an ordinary declaration below, "inherit" (a Keyword token)
        // mistaken for the declaration's own type, then choking on the
        // inherit path string where a declaration name was expected.
        // Modifiers are discarded here exactly like parseDeclPrefix()
        // already discards every modifier but "private" for an ordinary
        // declaration -- this driver has no per-inherit visibility
        // semantics to enforce.
        {
            size_t lookahead = 0;
            while (peekAt(lookahead).type == TokenType::Keyword &&
                   isModifierKeyword(peekAt(lookahead))) {
                ++lookahead;
            }
            if (lookahead > 0 && peekAt(lookahead).text == "inherit") {
                for (size_t i = 0; i < lookahead; ++i) advance();
                parseInheritStatement(*program);
                continue;
            }
        }

        // Real "class <name> { ... }" declaration (ROADMAP.md row
        // 3.10's class scoping report, real grammar.y's own
        // "type_decl: type_modifier_list L_CLASS identifier '{'
        // member_list '}'") -- the same modifier-lookahead shape as the
        // inherit-statement check just above, plus one more token: a
        // "{" must follow the class name, disambiguating this from an
        // ordinary declaration whose own *type* happens to be "class
        // <name>" (e.g. "class service Registered;", handled by
        // parseDeclPrefix()'s own startsClassType() branch instead --
        // that shape has no "{" here). FluffOS-dialect-only, same
        // reasoning as startsClassType()'s own comment: under any other
        // dialect this lookahead never matches (dialect_ != FluffOS
        // short-circuits it before even checking "class"), so "class"
        // stays a completely ordinary, unreserved identifier there.
        if (dialect_ == LpcDialect::FluffOS) {
            size_t lookahead = 0;
            while (peekAt(lookahead).type == TokenType::Keyword &&
                   isModifierKeyword(peekAt(lookahead))) {
                ++lookahead;
            }
            if (peekAt(lookahead).type == TokenType::Ident &&
                peekAt(lookahead).text == "class" &&
                peekAt(lookahead + 1).type == TokenType::Ident &&
                peekAt(lookahead + 2).text == "{") {
                for (size_t i = 0; i < lookahead; ++i) advance(); // modifiers
                advance(); // "class"
                program->classes.push_back(parseClassDecl());
                continue;
            }
        }

        DeclPrefix prefix = parseDeclPrefix("top-level declaration");

        if (checkText("(")) {
            program->functions.push_back(parseFunctionRest(std::move(prefix)));
        } else {
            auto decls = parseObjectVarDeclRest(std::move(prefix));
            for (auto& decl : decls) {
                program->objectVars.push_back(std::move(decl));
            }
        }
    }
    return program;
}

} // namespace amlp
