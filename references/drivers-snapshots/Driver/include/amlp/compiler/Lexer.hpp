#pragma once
#include "amlp/dialect/LpcDialect.hpp"
#include <string>
#include <vector>

namespace amlp {

// QuotedSymbol: LDMud's own "'name" symbol literal (see Value.hpp's
// Symbol comment, ROADMAP.md row 1.7/1.8). Kept distinct from Symbol
// (this Lexer's existing name for punctuation/operator tokens like
// "->"/"::") to avoid confusing the two -- Token::text is the bare
// identifier with the leading quote already stripped.
enum class TokenType { Ident, Number, String, Symbol, QuotedSymbol, Keyword, End };

struct Token {
    TokenType type;
    std::string text;
    int line = 1;
};

class Lexer {
public:
    // dialect defaults to FluffOS so every pre-existing call site (this
    // driver's own ~100 direct "Lexer lexer(source)" test constructions,
    // plus any future one that doesn't care) keeps its exact prior
    // behavior with no change at all -- ROADMAP.md row 1.2's own "zero
    // behavior change" plumbing slice. Only ObjectManager::compile()
    // passes a real, config-derived dialect explicitly.
    explicit Lexer(std::string source, LpcDialect dialect = LpcDialect::FluffOS);
    std::vector<Token> tokenize();

private:
    char peek() const;
    char peekNext() const;
    char advance();
    bool atEnd() const;
    void skipWhitespaceAndComments();
    Token lexIdentOrKeyword();
    Token lexNumber();
    Token lexString();
    Token lexChar();
    Token lexQuote();
    Token lexSymbol();
    Token lexHashQuote();
    Token lexHeredoc();
    Token lexLambdaParam();

    std::string src_;
    size_t pos_ = 0;
    int line_ = 1;
    LpcDialect dialect_;
};

} // namespace amlp
