#pragma once

#include <string>
#include <istream>

namespace parser {

enum class TokenType {
    Eof,
    Identifier,     // main, x, loop
    Global,         // $main
    Temporary,      // %x
    Label,          // @loop
    Type,           // :mytype
    Keyword,        // function, export, data, type, w, l, s, d, etc.
    Number,         // 123, -45
    FloatLiteral,   // s_1.5, d_2.5
    LCurly, RCurly, // {, }
    LParen, RParen, // (, )
    LBrack, RBrack, // [, ]
    Comma,
    Equal,
    Colon,
    Plus,
    Ellipsis,       // ...
    StringLiteral,  // "hello"

    // Keywords that are not just identifiers
    Align,
    Thread,
    Section,

    Unknown,
};

struct Token {
    TokenType type;
    std::string value;
};

class Lexer {
public:
    Lexer(std::istream& input);

    Token getNextToken();
    char peek();

private:
    std::istream& input;
    int lastChar = ' ';

    int getChar();
};

} // namespace parser
