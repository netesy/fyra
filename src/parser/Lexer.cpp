#include "parser/Lexer.h"
#include <cctype>
#include <map>
#include <iostream>

namespace parser {

Lexer::Lexer(std::istream& input) : input(input) {}

int Lexer::getChar() {
    return input.get();
}

char Lexer::peek() {
    return input.peek();
}

Token Lexer::getNextToken() {
    // Skip whitespace
    while (lastChar != EOF && isspace(lastChar)) {
        lastChar = getChar();
    }

    // Skip comments
    if (lastChar == '#') {
        do {
            lastChar = getChar();
        } while (lastChar != EOF && lastChar != '\n' && lastChar != '\r');
        if (lastChar != EOF) {
            return getNextToken(); // Recurse to get the next token
        }
    }

    if (lastChar == EOF) {
        return {TokenType::Eof, ""};
    }

    if ((lastChar == 's' || lastChar == 'd') && input.peek() == '_') {
        std::string floatStr;
        floatStr += lastChar;
        lastChar = getChar(); // consume '_'
        floatStr += lastChar;
        lastChar = getChar();

        if (lastChar == '-') {
            floatStr += lastChar;
            lastChar = getChar();
        }

        while (isdigit(lastChar) || lastChar == '.') {
            floatStr += lastChar;
            lastChar = getChar();
        }
        return {TokenType::FloatLiteral, floatStr};
    }

    if (isalpha(lastChar) || lastChar == '_') { // Identifiers and keywords
        std::string identifierStr;
        identifierStr += lastChar;
        while (isalnum((lastChar = getChar())) || lastChar == '_') {
            identifierStr += lastChar;
        }
        // Check for keywords
        if (identifierStr == "align") return {TokenType::Align, identifierStr};
        static const std::map<std::string, TokenType> keywords = {
            {"function", TokenType::Keyword}, {"export", TokenType::Keyword},
            {"data", TokenType::Keyword}, {"type", TokenType::Keyword},
            // Types
            {"w", TokenType::Keyword}, {"l", TokenType::Keyword},
            {"s", TokenType::Keyword}, {"d", TokenType::Keyword},
            {"b", TokenType::Keyword}, {"h", TokenType::Keyword},
            // Instructions
            {"ret", TokenType::Keyword}, {"add", TokenType::Keyword},
            {"sub", TokenType::Keyword}, {"mul", TokenType::Keyword},
            {"div", TokenType::Keyword}, {"udiv", TokenType::Keyword},
            {"rem", TokenType::Keyword}, {"urem", TokenType::Keyword},
            {"neg", TokenType::Keyword}, {"copy", TokenType::Keyword},
            {"and", TokenType::Keyword}, {"or", TokenType::Keyword},
            {"xor", TokenType::Keyword}, {"shl", TokenType::Keyword},
            {"shr", TokenType::Keyword}, {"sar", TokenType::Keyword},
            {"jmp", TokenType::Keyword}, {"jnz", TokenType::Keyword},
            {"br", TokenType::Keyword}, {"cmp", TokenType::Keyword},
            {"alloc", TokenType::Keyword}, {"alloc4", TokenType::Keyword},
            {"alloc8", TokenType::Keyword}, {"alloc16", TokenType::Keyword},
            {"store", TokenType::Keyword},
            {"load", TokenType::Keyword}, {"loadub", TokenType::Keyword}, {"loadsb", TokenType::Keyword},
            {"loaduh", TokenType::Keyword}, {"loadsh", TokenType::Keyword}, {"loaduw", TokenType::Keyword},
            {"blit", TokenType::Keyword},
            {"extub", TokenType::Keyword}, {"extuh", TokenType::Keyword}, {"extuw", TokenType::Keyword},
            {"extsb", TokenType::Keyword}, {"extsh", TokenType::Keyword}, {"extsw", TokenType::Keyword},
            {"exts", TokenType::Keyword}, {"truncd", TokenType::Keyword},
            {"swtof", TokenType::Keyword}, {"uwtof", TokenType::Keyword},
            {"sltof", TokenType::Keyword}, {"ultof", TokenType::Keyword},
            {"dtosi", TokenType::Keyword}, {"dtoui", TokenType::Keyword},
            {"stosi", TokenType::Keyword}, {"stoui", TokenType::Keyword},
            {"cast", TokenType::Keyword}, {"phi", TokenType::Keyword},
            {"vastart", TokenType::Keyword}, {"vaarg", TokenType::Keyword},
            {"call", TokenType::Keyword}, {"hlt", TokenType::Keyword},
            // Comparison operators
            {"eq", TokenType::Keyword}, {"ne", TokenType::Keyword},
            {"slt", TokenType::Keyword}, {"sle", TokenType::Keyword},
            {"sgt", TokenType::Keyword}, {"sge", TokenType::Keyword},
            {"ult", TokenType::Keyword}, {"ule", TokenType::Keyword},
            {"ugt", TokenType::Keyword}, {"uge", TokenType::Keyword},
            // Floating point comparisons from doc
            {"lt", TokenType::Keyword}, {"le", TokenType::Keyword},
            {"gt", TokenType::Keyword}, {"ge", TokenType::Keyword},
            {"co", TokenType::Keyword}, {"cuo", TokenType::Keyword},
            // Floating point operations
            {"fadd", TokenType::Keyword}, {"fsub", TokenType::Keyword}, {"fmul", TokenType::Keyword},
            {"fdiv", TokenType::Keyword},
            {"syscall", TokenType::Keyword},
            // Syscalls
            {"sys_exit", TokenType::Keyword}, {"sys_execve", TokenType::Keyword}, {"sys_fork", TokenType::Keyword},
            {"sys_clone", TokenType::Keyword}, {"sys_wait4", TokenType::Keyword}, {"sys_kill", TokenType::Keyword},
            {"sys_read", TokenType::Keyword}, {"sys_write", TokenType::Keyword}, {"sys_openat", TokenType::Keyword},
            {"sys_close", TokenType::Keyword}, {"sys_lseek", TokenType::Keyword}, {"sys_mmap", TokenType::Keyword},
            {"sys_munmap", TokenType::Keyword}, {"sys_mprotect", TokenType::Keyword}, {"sys_brk", TokenType::Keyword},
            {"sys_mkdirat", TokenType::Keyword}, {"sys_unlinkat", TokenType::Keyword}, {"sys_renameat", TokenType::Keyword},
            {"sys_getdents64", TokenType::Keyword}, {"sys_clock_gettime", TokenType::Keyword}, {"sys_nanosleep", TokenType::Keyword},
            {"sys_rt_sigaction", TokenType::Keyword}, {"sys_rt_sigprocmask", TokenType::Keyword}, {"sys_rt_sigreturn", TokenType::Keyword},
            {"sys_getrandom", TokenType::Keyword}, {"sys_uname", TokenType::Keyword}, {"sys_getpid", TokenType::Keyword},
            {"sys_gettid", TokenType::Keyword}
        };
        if (keywords.count(identifierStr)) {
            return {keywords.at(identifierStr), identifierStr};
        }
        return {TokenType::Identifier, identifierStr};
    }

    if (isdigit(lastChar) || (lastChar == '-' && isdigit(input.peek()))) { // Numbers
        std::string numStr;
        numStr += lastChar;
        lastChar = getChar();

        if (numStr == "0" && (lastChar == 'b' || lastChar == 'B')) { // Binary
            numStr += lastChar;
            lastChar = getChar();
            while (lastChar == '0' || lastChar == '1') {
                numStr += lastChar;
                lastChar = getChar();
            }
        } else if (numStr == "0" && (lastChar == 'x' || lastChar == 'X')) { // Hex
            numStr += lastChar;
            lastChar = getChar();
            while (isxdigit(lastChar)) {
                numStr += lastChar;
                lastChar = getChar();
            }
        } else { // Decimal
            while (isdigit(lastChar)) {
                numStr += lastChar;
                lastChar = getChar();
            }
        }
        return {TokenType::Number, numStr};
    }

    if (lastChar == '$' || lastChar == '%' || lastChar == '@' || lastChar == ':') {
        char sigil = lastChar;
        if (isalpha(input.peek()) || input.peek() == '_') {
             std::string identifier;
             lastChar = getChar();
             identifier += lastChar;
             while (isalnum((lastChar = getChar())) || lastChar == '_') {
                 identifier += lastChar;
             }
             switch (sigil) {
                 case '$': return {TokenType::Global, identifier};
                 case '%': return {TokenType::Temporary, identifier};
                 case '@': return {TokenType::Label, identifier};
                 case ':': return {TokenType::Type, identifier};
             }
        }
    }

    if (lastChar == '"') { // String literals
        std::string str;
        lastChar = getChar();
        while (lastChar != '"') {
            if (lastChar == '\\') { // Handle escape sequences
                lastChar = getChar();
                // simple escapes for now
            }
            str += lastChar;
            lastChar = getChar();
        }
        lastChar = getChar(); // consume the closing quote
        return {TokenType::StringLiteral, str};
    }

    if (lastChar == EOF || input.eof()) {
        return {TokenType::Eof, ""};
    }

    // Handle single-character tokens
    if (lastChar == '.') {
        std::string s;
        s += lastChar;
        lastChar = getChar();
        s += lastChar;
        lastChar = getChar();
        s += lastChar;
        lastChar = getChar();
        if (s == "...") {
            return {TokenType::Ellipsis, "..."};
        }
        // Fallback for now
    }
    char thisChar = lastChar;
    lastChar = getChar();
    switch (thisChar) {
        case '{': return {TokenType::LCurly, "{"};
        case '}': return {TokenType::RCurly, "}"};
        case '(': return {TokenType::LParen, "("};
        case ')': return {TokenType::RParen, ")"};
        case ',': return {TokenType::Comma, ","};
        case '=': return {TokenType::Equal, "="};
        case ':': return {TokenType::Colon, ":"}; // Note: also a sigil prefix
        case '+': return {TokenType::Plus, "+"};
    }

    return {TokenType::Unknown, std::string(1, thisChar)};
}

} // namespace parser
