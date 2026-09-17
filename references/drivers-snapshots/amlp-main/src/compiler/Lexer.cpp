#include "amlp/compiler/Lexer.hpp"
#include "amlp/core/Errors.hpp"
#include <cctype>
#include <unordered_set>

namespace amlp {

namespace {
// "array" is not reserved (lex.c does: L_ARRAY). This mudlib uses it as
// a parameter name (exclude_array), never as a type.
const std::unordered_set<std::string> kKeywords = {
    "void", "int", "string", "object", "float", "mapping",
    // lex.c: "status" is TYPE_NUMBER, a synonym for int.
    "status",
    "mixed", "function", "return", "if", "else", "while", "for", "do",
    "static", "private", "public", "protected", "nomask", "varargs",
    "inherit", "break", "continue", "foreach", "in",
    "switch", "case", "default"
};
}

Lexer::Lexer(std::string source, LpcDialect dialect)
    : src_(std::move(source)), dialect_(dialect) {}

bool Lexer::atEnd() const { return pos_ >= src_.size(); }

char Lexer::peek() const { return atEnd() ? '\0' : src_[pos_]; }

char Lexer::peekNext() const {
    return (pos_ + 1 < src_.size()) ? src_[pos_ + 1] : '\0';
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') ++line_;
    return c;
}

void Lexer::skipWhitespaceAndComments() {
    for (;;) {
        if (atEnd()) return;
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
            continue;
        }
        if (c == '/' && peekNext() == '/') {
            while (!atEnd() && peek() != '\n') advance();
            continue;
        }
        if (c == '/' && peekNext() == '*') {
            advance(); advance();
            while (!atEnd() && !(peek() == '*' && peekNext() == '/')) {
                advance();
            }
            if (!atEnd()) { advance(); advance(); }
            continue;
        }
        return;
    }
}

Token Lexer::lexIdentOrKeyword() {
    int startLine = line_;
    std::string text;
    while (!atEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
        text += advance();
    }
    // DGD-only: "atomic" (parser.y C_ATOMIC) and "nil" (T_NIL). Kept
    // out of kKeywords so FluffOS/LDMud can still use them as idents.
    static const std::unordered_set<std::string> kDgdOnlyKeywords = {"atomic", "nil"};
    bool isDialectKeyword = dialect_ == LpcDialect::DGD && kDgdOnlyKeywords.count(text);
    TokenType type = (kKeywords.count(text) || isDialectKeyword) ? TokenType::Keyword : TokenType::Ident;
    return Token{type, text, startLine};
}

Token Lexer::lexNumber() {
    int startLine = line_;
    std::string text;
    while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
        text += advance();
    }

    // Hex "0x1A": keep the prefix in the token text for Parser. Octal
    // is unevidenced and would silently change literals like "010".
    if (text == "0" && (peek() == 'x' || peek() == 'X') &&
        std::isxdigit(static_cast<unsigned char>(peekNext()))) {
        text += advance(); // 'x' / 'X'
        while (!atEnd() && std::isxdigit(static_cast<unsigned char>(peek()))) {
            text += advance();
        }
        return Token{TokenType::Number, text, startLine};
    }

    // Float "1.5": '.' plus a digit, not ".." / "...". Leading-dot ".5"
    // is routed here by tokenize() with text still empty.
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peekNext()))) {
        text += advance(); // '.'
        while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
            text += advance();
        }
    }

    return Token{TokenType::Number, text, startLine};
}

// "$N" (lex.c '$' case). Emitted as Ident "$" + digits. A non-digit
// after '$' is a bare '$' token, not a lexer error.
Token Lexer::lexLambdaParam() {
    int startLine = line_;
    advance(); // '$'
    // lex.c: if the next char is not a digit, return a bare '$' token.
    if (!std::isdigit(static_cast<unsigned char>(peek()))) {
        return Token{TokenType::Symbol, "$", startLine};
    }
    std::string text = "$";
    while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
        text += advance();
    }
    return Token{TokenType::Ident, text, startLine};
}

Token Lexer::lexString() {
    int startLine = line_;
    advance();
    std::string value;
    while (!atEnd() && peek() != '"') {
        char c = advance();
        if (c == '\\' && !atEnd()) {
            char esc = advance();
            switch (esc) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                default: value += esc; break;
            }
        } else {
            value += c;
        }
    }
    if (atEnd()) {
        throw LpcRuntimeError("unterminated string literal at line " + std::to_string(startLine));
    }
    advance();
    return Token{TokenType::String, value, startLine};
}

Token Lexer::lexChar() {
    int startLine = line_;
    advance(); // consume opening '

    if (atEnd()) {
        throw LpcRuntimeError("unterminated character literal at line " +
                               std::to_string(startLine));
    }

    char c = advance();
    int64_t code;
    if (c == '\\' && !atEnd()) {
        char esc = advance();
        switch (esc) {
            case 'n': code = '\n'; break;
            case 't': code = '\t'; break;
            case '\'': code = '\''; break;
            case '"': code = '"'; break;
            case '\\': code = '\\'; break;
            default: code = static_cast<unsigned char>(esc); break;
        }
    } else {
        code = static_cast<unsigned char>(c);
    }

    if (atEnd() || peek() != '\'') {
        throw LpcRuntimeError(
            "character literal must contain exactly one character, at line " +
            std::to_string(startLine));
    }
    advance(); // consume closing '

    return Token{TokenType::Number, std::to_string(code), startLine};
}

    // lex.c case '@': get_terminator() then get_text_block(). Verbatim
    // until a line starting with the terminator. "@@" array-block throws.
Token Lexer::lexHeredoc() {
    int startLine = line_;
    advance(); // consume '@'

    if (peek() == '@') {
        throw NotImplementedError("\"@@\" array-block heredoc syntax");
    }

    std::string terminator;
    while (!atEnd() && peek() != '\n') {
        terminator += advance();
    }
    if (terminator.empty()) {
        throw LpcRuntimeError("heredoc: missing terminator at line " + std::to_string(startLine));
    }
    if (atEnd()) {
        throw LpcRuntimeError("unterminated heredoc: missing closing \"" + terminator +
                               "\" at line " + std::to_string(startLine));
    }
    advance(); // consume the newline ending the "@TERM" line

    std::string value;
    bool atLineStart = true;
    for (;;) {
        if (atLineStart) {
            bool matches = pos_ + terminator.size() <= src_.size() &&
                           src_.compare(pos_, terminator.size(), terminator) == 0;
            if (matches) {
                for (size_t i = 0; i < terminator.size(); ++i) advance();
                break;
            }
        }
        if (atEnd()) {
            throw LpcRuntimeError("unterminated heredoc: missing closing \"" + terminator +
                                   "\" at line " + std::to_string(startLine));
        }
        char c = advance();
        value += c;
        atLineStart = (c == '\n');
    }

    return Token{TokenType::String, value, startLine};
}

Token Lexer::lexSymbol() {
    int startLine = line_;
    char c = advance();

    if (c == '-' && peek() == '>') {
        advance();
        return Token{TokenType::Symbol, "->", startLine};
    }
    if (c == ':' && peek() == ':') {
        advance();
        // "efun::name(...)" (grammar.y L_EFUN L_COLON_COLON identifier).
        return Token{TokenType::Symbol, "::", startLine};
    }
    if (c == '+' && peek() == '+') {
        advance();
        return Token{TokenType::Symbol, "++", startLine};
    }
    if (c == '-' && peek() == '-') {
        advance();
        return Token{TokenType::Symbol, "--", startLine};
    }
    if ((c == '+' || c == '-' || c == '*' || c == '/' || c == '%') && peek() == '=') {
        advance();
        return Token{TokenType::Symbol, std::string(1, c) + "=", startLine};
    }
    // Bitwise compound-assign "|=", "&=", "^=".
    if ((c == '|' || c == '&' || c == '^') && peek() == '=') {
        advance();
        return Token{TokenType::Symbol, std::string(1, c) + "=", startLine};
    }
    if (c == '=' && peek() == '=') {
        advance();
        return Token{TokenType::Symbol, "==", startLine};
    }
    if (c == '!' && peek() == '=') {
        advance();
        return Token{TokenType::Symbol, "!=", startLine};
    }
    // "<<", ">>", "<<=", ">>=". Check "<<=" before "<<".
    if (c == '<' && peek() == '<') {
        advance();
        if (peek() == '=') {
            advance();
            return Token{TokenType::Symbol, "<<=", startLine};
        }
        return Token{TokenType::Symbol, "<<", startLine};
    }
    if (c == '>' && peek() == '>') {
        advance();
        if (peek() == '=') {
            advance();
            return Token{TokenType::Symbol, ">>=", startLine};
        }
        return Token{TokenType::Symbol, ">>", startLine};
    }
    if (c == '<' && peek() == '=') {
        advance();
        return Token{TokenType::Symbol, "<=", startLine};
    }
    if (c == '>' && peek() == '=') {
        advance();
        return Token{TokenType::Symbol, ">=", startLine};
    }
    if (c == '|' && peek() == '|') {
        advance();
        return Token{TokenType::Symbol, "||", startLine};
    }
    if (c == '&' && peek() == '&') {
        advance();
        return Token{TokenType::Symbol, "&&", startLine};
    }
    if (c == '.' && peek() == '.') {
        advance();
        if (peek() == '.') {
            advance();
            // Trailing varargs "args..." (grammar.y argument_list L_DOT_DOT_DOT).
            return Token{TokenType::Symbol, "...", startLine};
        }
        return Token{TokenType::Symbol, "..", startLine};
    }

    return Token{TokenType::Symbol, std::string(1, c), startLine};
}

// LDMud lex.c:6186-6266: "'" is a char constant or a "'name" symbol.
// Length-1 then quote stays a char constant. FluffOS/DGD keep lexChar().
Token Lexer::lexQuote() {
    size_t p = pos_ + 1;
    if (p < src_.size() &&
        (std::isalpha(static_cast<unsigned char>(src_[p])) || src_[p] == '_')) {
        size_t identStart = p;
        while (p < src_.size() &&
               (std::isalnum(static_cast<unsigned char>(src_[p])) || src_[p] == '_')) {
            ++p;
        }
        bool singleCharThenQuote = (p - identStart == 1) && p < src_.size() && src_[p] == '\'';
        if (!singleCharThenQuote) {
            int startLine = line_;
            advance(); // '
            std::string name;
            while (!atEnd() &&
                   (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
                name += advance();
            }
            return Token{TokenType::QuotedSymbol, name, startLine};
        }
    }
    return lexChar();
}

// LDMud "#'name" (lex.c:6158-6162). Bare-name first slice only; operator
// spellings and scope prefixes are unimplemented. One Symbol token "#'".
Token Lexer::lexHashQuote() {
    int startLine = line_;
    advance(); // #
    advance(); // '
    return Token{TokenType::Symbol, "#'", startLine};
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;

    for (;;) {
        skipWhitespaceAndComments();
        if (atEnd()) break;

        char c = peek();
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            tokens.push_back(lexIdentOrKeyword());
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            tokens.push_back(lexNumber());
        } else if (c == '.' && std::isdigit(static_cast<unsigned char>(peekNext()))) {
            // Leading-dot float ".5". Bare '.' is ".." / "..." via lexSymbol.
            tokens.push_back(lexNumber());
        } else if (c == '"') {
            tokens.push_back(lexString());
        } else if (c == '\'') {
            // LDMud-only "'name" symbol; FluffOS/DGD keep lexChar().
            tokens.push_back(dialect_ == LpcDialect::LdMud ? lexQuote() : lexChar());
        } else if (c == '#' && dialect_ == LpcDialect::LdMud && peekNext() == '\'') {
            // LDMud-only "#'". Other dialects still reject bare '#'.
            tokens.push_back(lexHashQuote());
        } else if (c == '@') {
            tokens.push_back(lexHeredoc());
        } else if (c == '$') {
            tokens.push_back(lexLambdaParam());
        } else if (c == '(' || c == ')' || c == '{' || c == '}' ||
                   c == '[' || c == ']' || c == ':' ||
                   c == ';' || c == ',' || c == '-' || c == '=' ||
                   c == '!' || c == '<' || c == '>' || c == '*' || c == '+' ||
                   c == '|' || c == '&' || c == '/' || c == '%' || c == '.' ||
                   c == '?' || c == '^' || c == '~') {
            tokens.push_back(lexSymbol());
        } else {
            int errLine = line_;
            char unrecognized = advance();
            throw LpcRuntimeError(
                "lexer: unrecognized character '" + std::string(1, unrecognized) +
                "' at line " + std::to_string(errLine));
        }
    }

    tokens.push_back(Token{TokenType::End, "", line_});
    return tokens;
}

} // namespace amlp
