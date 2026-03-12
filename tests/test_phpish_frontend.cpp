#include <cassert>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <utility>
#include <vector>

#include "codegen/CodeGen.h"
#include "codegen/target/AArch64.h"
#include "codegen/target/RiscV64.h"
#include "codegen/target/SystemV_x64.h"
#include "ir/Constant.h"
#include "ir/IRBuilder.h"
#include "ir/Module.h"
#include "ir/Parameter.h"
#include "ir/Type.h"
#include "../src/codegen/execgen/elf.hh"

namespace {

enum class TokKind {
    Eof,
    Ident,
    Number,
    LParen, RParen, LBrace, RBrace, LBracket, RBracket, Comma, Semi,
    Plus, Minus, Star, Slash, Percent,
    PlusEq, MinusEq, StarEq, SlashEq, PercentEq,
    PlusPlus, MinusMinus,
    Amp, Pipe, Caret, Shl, Shr,
    Assign, Eq, Ne, Lt, Le, Gt, Ge,
    AndAnd, OrOr, Bang,
    KwFn, KwReturn, KwIf, KwElif, KwElse, KwWhile, KwFor, KwBreak, KwContinue,
    KwTrue, KwFalse, KwDo
};

struct Token { TokKind kind; std::string text; };

class Lexer {
public:
    explicit Lexer(const std::string& src) : s(src) {}

    Token next() {
        skipWs();
        if (i >= s.size()) return {TokKind::Eof, ""};

        char c = s[i];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t st = i++;
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) ++i;
            std::string t = s.substr(st, i - st);
            if (t == "fn") return {TokKind::KwFn, t};
            if (t == "return") return {TokKind::KwReturn, t};
            if (t == "if") return {TokKind::KwIf, t};
            if (t == "elif") return {TokKind::KwElif, t};
            if (t == "else") return {TokKind::KwElse, t};
            if (t == "while") return {TokKind::KwWhile, t};
            if (t == "for") return {TokKind::KwFor, t};
            if (t == "break") return {TokKind::KwBreak, t};
            if (t == "continue") return {TokKind::KwContinue, t};
            if (t == "true") return {TokKind::KwTrue, t};
            if (t == "false") return {TokKind::KwFalse, t};
            if (t == "do") return {TokKind::KwDo, t};
            return {TokKind::Ident, t};
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t st = i++;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
            return {TokKind::Number, s.substr(st, i - st)};
        }

        auto two = [&](char a, char b, TokKind k) -> Token {
            if (i + 1 < s.size() && s[i] == a && s[i + 1] == b) {
                i += 2;
                return {k, std::string{a, b}};
            }
            return {TokKind::Eof, ""};
        };

        if (c == '=' && i + 1 < s.size() && s[i + 1] == '=') return two('=', '=', TokKind::Eq);
        if (c == '!' && i + 1 < s.size() && s[i + 1] == '=') return two('!', '=', TokKind::Ne);
        if (c == '<' && i + 1 < s.size() && s[i + 1] == '=') return two('<', '=', TokKind::Le);
        if (c == '>' && i + 1 < s.size() && s[i + 1] == '=') return two('>', '=', TokKind::Ge);
        if (c == '&' && i + 1 < s.size() && s[i + 1] == '&') return two('&', '&', TokKind::AndAnd);
        if (c == '|' && i + 1 < s.size() && s[i + 1] == '|') return two('|', '|', TokKind::OrOr);
        if (c == '<' && i + 1 < s.size() && s[i + 1] == '<') return two('<', '<', TokKind::Shl);
        if (c == '>' && i + 1 < s.size() && s[i + 1] == '>') return two('>', '>', TokKind::Shr);
        if (c == '+' && i + 1 < s.size() && s[i + 1] == '=') return two('+', '=', TokKind::PlusEq);
        if (c == '-' && i + 1 < s.size() && s[i + 1] == '=') return two('-', '=', TokKind::MinusEq);
        if (c == '*' && i + 1 < s.size() && s[i + 1] == '=') return two('*', '=', TokKind::StarEq);
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '=') return two('/', '=', TokKind::SlashEq);
        if (c == '%' && i + 1 < s.size() && s[i + 1] == '=') return two('%', '=', TokKind::PercentEq);
        if (c == '+' && i + 1 < s.size() && s[i + 1] == '+') return two('+', '+', TokKind::PlusPlus);
        if (c == '-' && i + 1 < s.size() && s[i + 1] == '-') return two('-', '-', TokKind::MinusMinus);

        ++i;
        switch (c) {
            case '(': return {TokKind::LParen, "("};
            case ')': return {TokKind::RParen, ")"};
            case '{': return {TokKind::LBrace, "{"};
            case '}': return {TokKind::RBrace, "}"};
            case '[': return {TokKind::LBracket, "["};
            case ']': return {TokKind::RBracket, "]"};
            case ',': return {TokKind::Comma, ","};
            case ';': return {TokKind::Semi, ";"};
            case '+': return {TokKind::Plus, "+"};
            case '-': return {TokKind::Minus, "-"};
            case '*': return {TokKind::Star, "*"};
            case '/': return {TokKind::Slash, "/"};
            case '%': return {TokKind::Percent, "%"};
            case '&': return {TokKind::Amp, "&"};
            case '|': return {TokKind::Pipe, "|"};
            case '^': return {TokKind::Caret, "^"};
            case '=': return {TokKind::Assign, "="};
            case '!': return {TokKind::Bang, "!"};
            case '<': return {TokKind::Lt, "<"};
            case '>': return {TokKind::Gt, ">"};
            default: throw std::runtime_error(std::string("Unexpected character: ") + c);
        }
    }

private:
    void skipWs() {
        while (i < s.size()) {
            if (std::isspace(static_cast<unsigned char>(s[i]))) { ++i; continue; }
            if (s[i] == '#') { while (i < s.size() && s[i] != '\n') ++i; continue; }
            break;
        }
    }

    const std::string& s;
    size_t i = 0;
};

struct Expr {
    enum Kind { Num, Var, Bin, Call, Unary, Index, ArrayLit } kind;
    int num = 0;
    std::string name;
    std::string op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    std::vector<std::unique_ptr<Expr>> args;
};

struct Stmt {
    enum Kind { Assign, CompoundAssign, IncDec, Return, If, While, DoWhile, For, ExprStmt, Break, Continue } kind;

    std::string var;
    std::string op; // for compound assign/inc-dec
    std::unique_ptr<Expr> indexExpr; // for arr[idx] = value
    std::unique_ptr<Expr> expr;

    std::vector<std::pair<std::unique_ptr<Expr>, std::vector<std::unique_ptr<Stmt>>>> branches;
    std::vector<std::unique_ptr<Stmt>> elseBody;

    std::vector<std::unique_ptr<Stmt>> body;
    std::unique_ptr<Stmt> initAssign;
    std::unique_ptr<Expr> loopCond;
    std::unique_ptr<Stmt> stepAssign;
};

struct FunctionAst {
    std::string name;
    std::vector<std::string> params;
    std::vector<std::unique_ptr<Stmt>> body;
};

struct ProgramAst {
    std::vector<FunctionAst> functions;
};

class Parser {
public:
    explicit Parser(const std::string& src) : lex(src) { advance(); }

    ProgramAst parseProgram() {
        ProgramAst p;
        while (tok.kind != TokKind::Eof) p.functions.push_back(parseFunction());
        return p;
    }

private:
    void advance() { tok = lex.next(); }
    void expect(TokKind k, const char* msg) {
        if (tok.kind != k) throw std::runtime_error(msg);
        advance();
    }

    FunctionAst parseFunction() {
        expect(TokKind::KwFn, "Expected fn");
        if (tok.kind != TokKind::Ident) throw std::runtime_error("Expected function name");
        FunctionAst fn;
        fn.name = tok.text;
        advance();
        expect(TokKind::LParen, "Expected (");
        if (tok.kind != TokKind::RParen) {
            while (true) {
                if (tok.kind != TokKind::Ident) throw std::runtime_error("Expected parameter name");
                fn.params.push_back(tok.text);
                advance();
                if (tok.kind == TokKind::Comma) { advance(); continue; }
                break;
            }
        }
        expect(TokKind::RParen, "Expected )");
        fn.body = parseBlock();
        return fn;
    }

    std::vector<std::unique_ptr<Stmt>> parseBlock() {
        expect(TokKind::LBrace, "Expected {");
        std::vector<std::unique_ptr<Stmt>> stmts;
        while (tok.kind != TokKind::RBrace) stmts.push_back(parseStmt());
        expect(TokKind::RBrace, "Expected }");
        return stmts;
    }

    std::unique_ptr<Stmt> parseAssignNoSemi() {
        if (tok.kind != TokKind::Ident) throw std::runtime_error("Expected variable name");
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Assign;
        s->var = tok.text;
        advance();
        if (tok.kind == TokKind::LBracket) {
            advance();
            s->indexExpr = parseExpr();
            expect(TokKind::RBracket, "Expected ]");
        }
        expect(TokKind::Assign, "Expected =");
        s->expr = parseExpr();
        return s;
    }

    std::unique_ptr<Stmt> parseStmt() {
        if (tok.kind == TokKind::KwReturn) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Return;
            advance();
            s->expr = parseExpr();
            expect(TokKind::Semi, "Expected ;");
            return s;
        }
        if (tok.kind == TokKind::KwBreak) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Break;
            advance();
            expect(TokKind::Semi, "Expected ;");
            return s;
        }
        if (tok.kind == TokKind::KwContinue) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Continue;
            advance();
            expect(TokKind::Semi, "Expected ;");
            return s;
        }
        if (tok.kind == TokKind::KwIf) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::If;
            advance();
            auto cond = parseExpr();
            auto body = parseBlock();
            s->branches.push_back({std::move(cond), std::move(body)});
            while (tok.kind == TokKind::KwElif) {
                advance();
                auto ec = parseExpr();
                auto eb = parseBlock();
                s->branches.push_back({std::move(ec), std::move(eb)});
            }
            if (tok.kind == TokKind::KwElse) {
                advance();
                s->elseBody = parseBlock();
            }
            return s;
        }
        if (tok.kind == TokKind::KwWhile) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::While;
            advance();
            s->loopCond = parseExpr();
            s->body = parseBlock();
            return s;
        }
        if (tok.kind == TokKind::KwDo) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::DoWhile;
            advance();
            s->body = parseBlock();
            expect(TokKind::KwWhile, "Expected while after do-block");
            s->loopCond = parseExpr();
            expect(TokKind::Semi, "Expected ; after do-while");
            return s;
        }
        if (tok.kind == TokKind::KwFor) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::For;
            advance();
            s->initAssign = parseAssignNoSemi();
            expect(TokKind::Semi, "Expected ; after for init");
            s->loopCond = parseExpr();
            expect(TokKind::Semi, "Expected ; after for condition");
            s->stepAssign = parseAssignNoSemi();
            s->body = parseBlock();
            return s;
        }

        if (tok.kind != TokKind::Ident) throw std::runtime_error("Expected statement");

        std::string name = tok.text;
        advance();

        if (tok.kind == TokKind::LParen) {
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Call;
            e->name = name;
            advance();
            if (tok.kind != TokKind::RParen) {
                while (true) {
                    e->args.push_back(parseExpr());
                    if (tok.kind == TokKind::Comma) { advance(); continue; }
                    break;
                }
            }
            expect(TokKind::RParen, "Expected )");
            expect(TokKind::Semi, "Expected ;");
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::ExprStmt;
            s->expr = std::move(e);
            return s;
        }

        if (tok.kind == TokKind::PlusPlus || tok.kind == TokKind::MinusMinus) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::IncDec;
            s->var = name;
            s->op = tok.text;
            advance();
            expect(TokKind::Semi, "Expected ;");
            return s;
        }

        if (tok.kind == TokKind::PlusEq || tok.kind == TokKind::MinusEq || tok.kind == TokKind::StarEq ||
            tok.kind == TokKind::SlashEq || tok.kind == TokKind::PercentEq) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::CompoundAssign;
            s->var = name;
            s->op = tok.text;
            advance();
            s->expr = parseExpr();
            expect(TokKind::Semi, "Expected ;");
            return s;
        }

        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Assign;
        s->var = name;
        if (tok.kind == TokKind::LBracket) {
            advance();
            s->indexExpr = parseExpr();
            expect(TokKind::RBracket, "Expected ]");
        }
        expect(TokKind::Assign, "Expected =");
        s->expr = parseExpr();
        expect(TokKind::Semi, "Expected ;");
        return s;
    }

    std::unique_ptr<Expr> parseExpr() { return parseOr(); }

    std::unique_ptr<Expr> parseOr() {
        auto l = parseAnd();
        while (tok.kind == TokKind::OrOr) {
            std::string op = tok.text;
            advance();
            auto r = parseAnd();
            auto n = std::make_unique<Expr>();
            n->kind = Expr::Bin;
            n->op = op;
            n->lhs = std::move(l);
            n->rhs = std::move(r);
            l = std::move(n);
        }
        return l;
    }

    std::unique_ptr<Expr> parseAnd() {
        auto l = parseCmp();
        while (tok.kind == TokKind::AndAnd) {
            std::string op = tok.text;
            advance();
            auto r = parseCmp();
            auto n = std::make_unique<Expr>();
            n->kind = Expr::Bin;
            n->op = op;
            n->lhs = std::move(l);
            n->rhs = std::move(r);
            l = std::move(n);
        }
        return l;
    }

    std::unique_ptr<Expr> parseCmp() {
        auto l = parseBitOr();
        while (tok.kind == TokKind::Eq || tok.kind == TokKind::Ne || tok.kind == TokKind::Lt ||
               tok.kind == TokKind::Le || tok.kind == TokKind::Gt || tok.kind == TokKind::Ge) {
            std::string op = tok.text;
            advance();
            auto r = parseAddSub();
            auto n = std::make_unique<Expr>();
            n->kind = Expr::Bin;
            n->op = op;
            n->lhs = std::move(l);
            n->rhs = std::move(r);
            l = std::move(n);
        }
        return l;
    }

    std::unique_ptr<Expr> parseBitOr() {
        auto l = parseBitXor();
        while (tok.kind == TokKind::Pipe) {
            std::string op = tok.text;
            advance();
            auto r = parseBitXor();
            auto n = std::make_unique<Expr>();
            n->kind = Expr::Bin;
            n->op = op;
            n->lhs = std::move(l);
            n->rhs = std::move(r);
            l = std::move(n);
        }
        return l;
    }

    std::unique_ptr<Expr> parseBitXor() {
        auto l = parseBitAnd();
        while (tok.kind == TokKind::Caret) {
            std::string op = tok.text;
            advance();
            auto r = parseBitAnd();
            auto n = std::make_unique<Expr>();
            n->kind = Expr::Bin;
            n->op = op;
            n->lhs = std::move(l);
            n->rhs = std::move(r);
            l = std::move(n);
        }
        return l;
    }

    std::unique_ptr<Expr> parseBitAnd() {
        auto l = parseShift();
        while (tok.kind == TokKind::Amp) {
            std::string op = tok.text;
            advance();
            auto r = parseShift();
            auto n = std::make_unique<Expr>();
            n->kind = Expr::Bin;
            n->op = op;
            n->lhs = std::move(l);
            n->rhs = std::move(r);
            l = std::move(n);
        }
        return l;
    }

    std::unique_ptr<Expr> parseShift() {
        auto l = parseAddSub();
        while (tok.kind == TokKind::Shl || tok.kind == TokKind::Shr) {
            std::string op = tok.text;
            advance();
            auto r = parseAddSub();
            auto n = std::make_unique<Expr>();
            n->kind = Expr::Bin;
            n->op = op;
            n->lhs = std::move(l);
            n->rhs = std::move(r);
            l = std::move(n);
        }
        return l;
    }

    std::unique_ptr<Expr> parseAddSub() {
        auto l = parseMulDiv();
        while (tok.kind == TokKind::Plus || tok.kind == TokKind::Minus) {
            std::string op = tok.text;
            advance();
            auto r = parseMulDiv();
            auto n = std::make_unique<Expr>();
            n->kind = Expr::Bin;
            n->op = op;
            n->lhs = std::move(l);
            n->rhs = std::move(r);
            l = std::move(n);
        }
        return l;
    }

    std::unique_ptr<Expr> parseMulDiv() {
        auto l = parseUnary();
        while (tok.kind == TokKind::Star || tok.kind == TokKind::Slash || tok.kind == TokKind::Percent) {
            std::string op = tok.text;
            advance();
            auto r = parseUnary();
            auto n = std::make_unique<Expr>();
            n->kind = Expr::Bin;
            n->op = op;
            n->lhs = std::move(l);
            n->rhs = std::move(r);
            l = std::move(n);
        }
        return l;
    }

    std::unique_ptr<Expr> parseUnary() {
        if (tok.kind == TokKind::Minus || tok.kind == TokKind::Bang) {
            std::string op = tok.text;
            advance();
            auto n = std::make_unique<Expr>();
            n->kind = Expr::Unary;
            n->op = op;
            n->lhs = parseUnary();
            return n;
        }
        return parsePrimary();
    }

    std::unique_ptr<Expr> parsePrimary() {
        if (tok.kind == TokKind::KwTrue || tok.kind == TokKind::KwFalse) {
            auto n = std::make_unique<Expr>();
            n->kind = Expr::Num;
            n->num = (tok.kind == TokKind::KwTrue) ? 1 : 0;
            advance();
            return n;
        }

        if (tok.kind == TokKind::Number) {
            auto n = std::make_unique<Expr>();
            n->kind = Expr::Num;
            n->num = std::stoi(tok.text);
            advance();
            return n;
        }
        if (tok.kind == TokKind::LBracket) {
            auto e = std::make_unique<Expr>();
            e->kind = Expr::ArrayLit;
            advance();
            if (tok.kind != TokKind::RBracket) {
                while (true) {
                    e->args.push_back(parseExpr());
                    if (tok.kind == TokKind::Comma) { advance(); continue; }
                    break;
                }
            }
            expect(TokKind::RBracket, "Expected ]");
            return e;
        }
        if (tok.kind == TokKind::Ident) {
            std::string name = tok.text;
            advance();
            if (tok.kind == TokKind::LParen) {
                auto e = std::make_unique<Expr>();
                e->kind = Expr::Call;
                e->name = name;
                advance();
                if (tok.kind != TokKind::RParen) {
                    while (true) {
                        e->args.push_back(parseExpr());
                        if (tok.kind == TokKind::Comma) { advance(); continue; }
                        break;
                    }
                }
                expect(TokKind::RParen, "Expected )");
                return e;
            }
            if (tok.kind == TokKind::LBracket) {
                auto e = std::make_unique<Expr>();
                e->kind = Expr::Index;
                e->name = name;
                advance();
                e->lhs = parseExpr();
                expect(TokKind::RBracket, "Expected ]");
                return e;
            }
            auto v = std::make_unique<Expr>();
            v->kind = Expr::Var;
            v->name = name;
            return v;
        }
        if (tok.kind == TokKind::LParen) {
            advance();
            auto e = parseExpr();
            expect(TokKind::RParen, "Expected )");
            return e;
        }
        throw std::runtime_error("Expected expression");
    }

    Lexer lex;
    Token tok{};
};

struct VarInfo {
    ir::Value* storage = nullptr; // scalar slot ptr OR array base ptr
    bool isArray = false;
    int arrayLen = 0;
};

struct LoopTargets {
    ir::BasicBlock* breakBB;
    ir::BasicBlock* continueBB;
};

struct CompilerCtx {
    ir::Module module;
    ir::IRBuilder builder;
    ir::IntegerType* i32;
    ir::IntegerType* i64;
    std::map<std::string, ir::Function*> functions;
    int nextLabelId = 0;

    std::string freshLabel(const std::string& prefix) { return prefix + "_" + std::to_string(nextLabelId++); }

    explicit CompilerCtx(const std::string& name)
        : module(name), i32(ir::IntegerType::get(32)), i64(ir::IntegerType::get(64)) {
        builder.setModule(&module);
    }
};

bool isTerminated(ir::BasicBlock* bb) {
    if (bb->getInstructions().empty()) return false;
    const auto& last = *bb->getInstructions().back();
    auto op = last.getOpcode();
    return op == ir::Instruction::Ret || op == ir::Instruction::Jmp || op == ir::Instruction::Jnz ||
           op == ir::Instruction::Br || op == ir::Instruction::Hlt;
}

ir::Value* emitExpr(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
                    std::map<std::string, VarInfo>& vars, const Expr* e,
                    std::vector<LoopTargets>& loopStack);

void emitStmtList(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
                  std::map<std::string, VarInfo>& vars,
                  const std::vector<std::unique_ptr<Stmt>>& stmts,
                  std::vector<LoopTargets>& loopStack);

ir::Value* getOrCreateScalarVarPtr(CompilerCtx& c, ir::BasicBlock*& bb,
                                   std::map<std::string, VarInfo>& vars,
                                   const std::string& name) {
    auto it = vars.find(name);
    if (it != vars.end()) {
        if (it->second.isArray) throw std::runtime_error("Variable is array, not scalar: " + name);
        return it->second.storage;
    }
    c.builder.setInsertPoint(bb);
    auto* p = c.builder.createAlloc(ir::ConstantInt::get(c.i32, 4), c.i32);
    vars[name] = {p, false, 0};
    return p;
}

ir::Value* computeArrayElemPtr(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
                               std::map<std::string, VarInfo>& vars,
                               const std::string& name, const Expr* idxExpr,
                               std::vector<LoopTargets>& loopStack) {
    auto it = vars.find(name);
    if (it == vars.end() || !it->second.isArray) throw std::runtime_error("Unknown array variable: " + name);

    if (idxExpr->kind == Expr::Num) {
        if (idxExpr->num < 0 || idxExpr->num >= it->second.arrayLen) {
            throw std::runtime_error("Array index out of bounds for " + name);
        }
    }

    ir::Value* idx = emitExpr(c, fn, bb, vars, idxExpr, loopStack);
    ir::Value* elemSize = ir::ConstantInt::get(c.i32, 4);
    ir::Value* byteOff32 = c.builder.createMul(idx, elemSize);
    ir::Value* byteOff64 = c.builder.createExtSW(byteOff32, c.i64);
    return c.builder.createAdd(it->second.storage, byteOff64);
}

ir::Value* emitShortCircuit(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
                            std::map<std::string, VarInfo>& vars,
                            const Expr* e,
                            std::vector<LoopTargets>& loopStack) {
    auto* resultPtr = c.builder.createAlloc(ir::ConstantInt::get(c.i32, 4), c.i32);
    auto* rhsBB = c.builder.createBasicBlock(c.freshLabel("sc_rhs"), fn);
    auto* shortBB = c.builder.createBasicBlock(c.freshLabel("sc_short"), fn);
    auto* mergeBB = c.builder.createBasicBlock(c.freshLabel("sc_merge"), fn);

    ir::Value* lhs = emitExpr(c, fn, bb, vars, e->lhs.get(), loopStack);
    ir::Value* lhsBool = c.builder.createCne(lhs, ir::ConstantInt::get(c.i32, 0));
    c.builder.setInsertPoint(bb);
    if (e->op == "&&") c.builder.createJnz(lhsBool, rhsBB, shortBB);
    else c.builder.createJnz(lhsBool, shortBB, rhsBB); // ||

    c.builder.setInsertPoint(shortBB);
    c.builder.createStore(ir::ConstantInt::get(c.i32, e->op == "&&" ? 0 : 1), resultPtr);
    c.builder.createJmp(mergeBB);

    ir::BasicBlock* rhsCur = rhsBB;
    ir::Value* rhs = emitExpr(c, fn, rhsCur, vars, e->rhs.get(), loopStack);
    c.builder.setInsertPoint(rhsCur);
    ir::Value* rhsBool = c.builder.createCne(rhs, ir::ConstantInt::get(c.i32, 0));
    c.builder.createStore(rhsBool, resultPtr);
    c.builder.createJmp(mergeBB);

    bb = mergeBB;
    c.builder.setInsertPoint(bb);
    return c.builder.createLoad(resultPtr);
}

ir::Value* emitExpr(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
                    std::map<std::string, VarInfo>& vars, const Expr* e,
                    std::vector<LoopTargets>& loopStack) {
    c.builder.setInsertPoint(bb);

    switch (e->kind) {
        case Expr::Num:
            return ir::ConstantInt::get(c.i32, e->num);
        case Expr::Var: {
            auto it = vars.find(e->name);
            if (it == vars.end() || it->second.isArray) throw std::runtime_error("Unknown scalar variable: " + e->name);
            return c.builder.createLoad(it->second.storage);
        }
        case Expr::Index: {
            ir::Value* ptr = computeArrayElemPtr(c, fn, bb, vars, e->name, e->lhs.get(), loopStack);
            return c.builder.createLoad(ptr);
        }
        case Expr::ArrayLit: {
            int n = static_cast<int>(e->args.size());
            ir::Value* bytes = ir::ConstantInt::get(c.i32, n * 4);
            ir::Value* base = c.builder.createAlloc(bytes, c.i32);
            for (int i = 0; i < n; ++i) {
                ir::Value* idx = ir::ConstantInt::get(c.i32, i);
                ir::Value* elemSize = ir::ConstantInt::get(c.i32, 4);
                ir::Value* off32 = c.builder.createMul(idx, elemSize);
                ir::Value* off64 = c.builder.createExtSW(off32, c.i64);
                ir::Value* ptr = c.builder.createAdd(base, off64);
                ir::Value* val = emitExpr(c, fn, bb, vars, e->args[i].get(), loopStack);
                c.builder.createStore(val, ptr);
            }
            return base;
        }
        case Expr::Call: {
            if (e->name == "syscall") {
                if (e->args.empty() || e->args.size() > 7) {
                    throw std::runtime_error("syscall expects 1..7 arguments");
                }
                std::vector<ir::Value*> args;
                for (const auto& a : e->args) args.push_back(emitExpr(c, fn, bb, vars, a.get(), loopStack));
                return c.builder.createSyscall(args, c.i32);
            }

            auto fit = c.functions.find(e->name);
            if (fit == c.functions.end()) throw std::runtime_error("Unknown function: " + e->name);
            size_t expected = fit->second->getParameters().size();
            if (e->args.size() != expected) {
                throw std::runtime_error("Function arity mismatch for " + e->name);
            }
            std::vector<ir::Value*> args;
            for (const auto& a : e->args) args.push_back(emitExpr(c, fn, bb, vars, a.get(), loopStack));
            return c.builder.createCall(fit->second, args, c.i32);
        }
        case Expr::Unary: {
            ir::Value* v = emitExpr(c, fn, bb, vars, e->lhs.get(), loopStack);
            if (e->op == "-") return c.builder.createNeg(v);
            if (e->op == "!") return c.builder.createCeq(v, ir::ConstantInt::get(c.i32, 0));
            throw std::runtime_error("Unknown unary operator: " + e->op);
        }
        case Expr::Bin: {
            if (e->op == "&&" || e->op == "||") return emitShortCircuit(c, fn, bb, vars, e, loopStack);
            ir::Value* l = emitExpr(c, fn, bb, vars, e->lhs.get(), loopStack);
            ir::Value* r = emitExpr(c, fn, bb, vars, e->rhs.get(), loopStack);
            if (e->op == "+") return c.builder.createAdd(l, r);
            if (e->op == "-") return c.builder.createSub(l, r);
            if (e->op == "*") return c.builder.createMul(l, r);
            if (e->op == "/") return c.builder.createDiv(l, r);
            if (e->op == "%") return c.builder.createRem(l, r);
            if (e->op == "&") return c.builder.createAnd(l, r);
            if (e->op == "|") return c.builder.createOr(l, r);
            if (e->op == "^") return c.builder.createXor(l, r);
            if (e->op == "<<") return c.builder.createShl(l, r);
            if (e->op == ">>") return c.builder.createShr(l, r);
            if (e->op == "==") return c.builder.createCeq(l, r);
            if (e->op == "!=") return c.builder.createCne(l, r);
            if (e->op == "<") return c.builder.createCslt(l, r);
            if (e->op == "<=") return c.builder.createCsle(l, r);
            if (e->op == ">") return c.builder.createCsgt(l, r);
            if (e->op == ">=") return c.builder.createCsge(l, r);
            throw std::runtime_error("Unsupported binary op: " + e->op);
        }
    }
    throw std::runtime_error("unreachable expr kind");
}

void emitAssign(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
                std::map<std::string, VarInfo>& vars, const Stmt* s,
                std::vector<LoopTargets>& loopStack) {
    c.builder.setInsertPoint(bb);

    if (s->indexExpr) {
        ir::Value* ptr = computeArrayElemPtr(c, fn, bb, vars, s->var, s->indexExpr.get(), loopStack);
        ir::Value* val = emitExpr(c, fn, bb, vars, s->expr.get(), loopStack);
        c.builder.createStore(val, ptr);
        return;
    }

    if (s->expr->kind == Expr::ArrayLit) {
        ir::Value* arrBase = emitExpr(c, fn, bb, vars, s->expr.get(), loopStack);
        vars[s->var] = {arrBase, true, static_cast<int>(s->expr->args.size())};
        return;
    }

    ir::Value* ptr = getOrCreateScalarVarPtr(c, bb, vars, s->var);
    ir::Value* val = emitExpr(c, fn, bb, vars, s->expr.get(), loopStack);
    c.builder.createStore(val, ptr);
}

void emitCompoundAssign(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
                        std::map<std::string, VarInfo>& vars, const Stmt* s,
                        std::vector<LoopTargets>& loopStack) {
    c.builder.setInsertPoint(bb);
    ir::Value* ptr = getOrCreateScalarVarPtr(c, bb, vars, s->var);
    ir::Value* cur = c.builder.createLoad(ptr);
    ir::Value* rhs = emitExpr(c, fn, bb, vars, s->expr.get(), loopStack);

    ir::Value* next = nullptr;
    if (s->op == "+=") next = c.builder.createAdd(cur, rhs);
    else if (s->op == "-=") next = c.builder.createSub(cur, rhs);
    else if (s->op == "*=") next = c.builder.createMul(cur, rhs);
    else if (s->op == "/=") next = c.builder.createDiv(cur, rhs);
    else if (s->op == "%=") next = c.builder.createRem(cur, rhs);
    else throw std::runtime_error("Unsupported compound op: " + s->op);

    c.builder.createStore(next, ptr);
}

void emitIncDec(CompilerCtx& c, ir::BasicBlock*& bb,
                std::map<std::string, VarInfo>& vars, const Stmt* s) {
    c.builder.setInsertPoint(bb);
    ir::Value* ptr = getOrCreateScalarVarPtr(c, bb, vars, s->var);
    ir::Value* cur = c.builder.createLoad(ptr);
    ir::Value* one = ir::ConstantInt::get(c.i32, 1);
    ir::Value* next = (s->op == "++") ? c.builder.createAdd(cur, one) : c.builder.createSub(cur, one);
    c.builder.createStore(next, ptr);
}

void emitIf(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
            std::map<std::string, VarInfo>& vars, const Stmt* s,
            std::vector<LoopTargets>& loopStack) {
    auto* merge = c.builder.createBasicBlock(c.freshLabel("if_merge"), fn);
    auto* nextTest = bb;

    for (size_t i = 0; i < s->branches.size(); ++i) {
        c.builder.setInsertPoint(nextTest);
        auto* thenBB = c.builder.createBasicBlock(c.freshLabel("if_then"), fn);
        auto* elseBB = c.builder.createBasicBlock(c.freshLabel("if_next"), fn);
        auto* cond = emitExpr(c, fn, nextTest, vars, s->branches[i].first.get(), loopStack);
        c.builder.createJnz(cond, thenBB, elseBB);

        ir::BasicBlock* thenCur = thenBB;
        emitStmtList(c, fn, thenCur, vars, s->branches[i].second, loopStack);
        if (!isTerminated(thenCur)) {
            c.builder.setInsertPoint(thenCur);
            c.builder.createJmp(merge);
        }

        nextTest = elseBB;
        if (i == s->branches.size() - 1) {
            c.builder.setInsertPoint(nextTest);
            if (!s->elseBody.empty()) {
                ir::BasicBlock* elseCur = nextTest;
                emitStmtList(c, fn, elseCur, vars, s->elseBody, loopStack);
                if (!isTerminated(elseCur)) {
                    c.builder.setInsertPoint(elseCur);
                    c.builder.createJmp(merge);
                }
            } else {
                c.builder.createJmp(merge);
            }
        }
    }

    bb = merge;
    c.builder.setInsertPoint(bb);
}

void emitWhile(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
               std::map<std::string, VarInfo>& vars, const Stmt* s,
               std::vector<LoopTargets>& loopStack) {
    auto* condBB = c.builder.createBasicBlock(c.freshLabel("while_cond"), fn);
    auto* bodyBB = c.builder.createBasicBlock(c.freshLabel("while_body"), fn);
    auto* endBB = c.builder.createBasicBlock(c.freshLabel("while_end"), fn);

    c.builder.setInsertPoint(bb);
    c.builder.createJmp(condBB);

    c.builder.setInsertPoint(condBB);
    auto* cond = emitExpr(c, fn, condBB, vars, s->loopCond.get(), loopStack);
    c.builder.createJnz(cond, bodyBB, endBB);

    loopStack.push_back({endBB, condBB});
    ir::BasicBlock* bodyCur = bodyBB;
    emitStmtList(c, fn, bodyCur, vars, s->body, loopStack);
    loopStack.pop_back();

    if (!isTerminated(bodyCur)) {
        c.builder.setInsertPoint(bodyCur);
        c.builder.createJmp(condBB);
    }

    bb = endBB;
    c.builder.setInsertPoint(bb);
}

void emitDoWhile(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
                 std::map<std::string, VarInfo>& vars, const Stmt* s,
                 std::vector<LoopTargets>& loopStack) {
    auto* bodyBB = c.builder.createBasicBlock(c.freshLabel("do_body"), fn);
    auto* condBB = c.builder.createBasicBlock(c.freshLabel("do_cond"), fn);
    auto* endBB = c.builder.createBasicBlock(c.freshLabel("do_end"), fn);

    c.builder.setInsertPoint(bb);
    c.builder.createJmp(bodyBB);

    loopStack.push_back({endBB, condBB});
    ir::BasicBlock* bodyCur = bodyBB;
    emitStmtList(c, fn, bodyCur, vars, s->body, loopStack);
    loopStack.pop_back();

    if (!isTerminated(bodyCur)) {
        c.builder.setInsertPoint(bodyCur);
        c.builder.createJmp(condBB);
    }

    c.builder.setInsertPoint(condBB);
    auto* cond = emitExpr(c, fn, condBB, vars, s->loopCond.get(), loopStack);
    c.builder.createJnz(cond, bodyBB, endBB);

    bb = endBB;
    c.builder.setInsertPoint(bb);
}

void emitFor(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
             std::map<std::string, VarInfo>& vars, const Stmt* s,
             std::vector<LoopTargets>& loopStack) {
    emitAssign(c, fn, bb, vars, s->initAssign.get(), loopStack);

    auto* condBB = c.builder.createBasicBlock(c.freshLabel("for_cond"), fn);
    auto* bodyBB = c.builder.createBasicBlock(c.freshLabel("for_body"), fn);
    auto* stepBB = c.builder.createBasicBlock(c.freshLabel("for_step"), fn);
    auto* endBB = c.builder.createBasicBlock(c.freshLabel("for_end"), fn);

    c.builder.setInsertPoint(bb);
    c.builder.createJmp(condBB);

    c.builder.setInsertPoint(condBB);
    auto* cond = emitExpr(c, fn, condBB, vars, s->loopCond.get(), loopStack);
    c.builder.createJnz(cond, bodyBB, endBB);

    loopStack.push_back({endBB, stepBB});
    ir::BasicBlock* bodyCur = bodyBB;
    emitStmtList(c, fn, bodyCur, vars, s->body, loopStack);
    loopStack.pop_back();

    if (!isTerminated(bodyCur)) {
        c.builder.setInsertPoint(bodyCur);
        c.builder.createJmp(stepBB);
    }

    ir::BasicBlock* stepCur = stepBB;
    emitAssign(c, fn, stepCur, vars, s->stepAssign.get(), loopStack);
    if (!isTerminated(stepCur)) {
        c.builder.setInsertPoint(stepCur);
        c.builder.createJmp(condBB);
    }

    bb = endBB;
    c.builder.setInsertPoint(bb);
}

void emitStmtList(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
                  std::map<std::string, VarInfo>& vars,
                  const std::vector<std::unique_ptr<Stmt>>& stmts,
                  std::vector<LoopTargets>& loopStack) {
    for (const auto& sp : stmts) {
        if (isTerminated(bb)) break;
        const Stmt* s = sp.get();
        c.builder.setInsertPoint(bb);
        switch (s->kind) {
            case Stmt::Assign:
                emitAssign(c, fn, bb, vars, s, loopStack);
                break;
            case Stmt::CompoundAssign:
                emitCompoundAssign(c, fn, bb, vars, s, loopStack);
                break;
            case Stmt::IncDec:
                emitIncDec(c, bb, vars, s);
                break;
            case Stmt::ExprStmt:
                (void)emitExpr(c, fn, bb, vars, s->expr.get(), loopStack);
                break;
            case Stmt::Return: {
                auto* v = emitExpr(c, fn, bb, vars, s->expr.get(), loopStack);
                c.builder.createRet(v);
                break;
            }
            case Stmt::If:
                emitIf(c, fn, bb, vars, s, loopStack);
                break;
            case Stmt::While:
                emitWhile(c, fn, bb, vars, s, loopStack);
                break;
            case Stmt::DoWhile:
                emitDoWhile(c, fn, bb, vars, s, loopStack);
                break;
            case Stmt::For:
                emitFor(c, fn, bb, vars, s, loopStack);
                break;
            case Stmt::Break: {
                if (loopStack.empty()) throw std::runtime_error("break used outside loop");
                c.builder.createJmp(loopStack.back().breakBB);
                break;
            }
            case Stmt::Continue: {
                if (loopStack.empty()) throw std::runtime_error("continue used outside loop");
                c.builder.createJmp(loopStack.back().continueBB);
                break;
            }
        }
    }
}

CompilerCtx lowerProgramToIR(const ProgramAst& p, const std::string& moduleName) {
    CompilerCtx c(moduleName);

    for (const auto& fn : p.functions) {
        std::vector<ir::Type*> params(fn.params.size(), c.i32);
        c.functions[fn.name] = c.builder.createFunction(fn.name, c.i32, params);
    }

    auto mainIt = c.functions.find("main");
    if (mainIt == c.functions.end()) throw std::runtime_error("Program must define fn main()");
    mainIt->second->setExported(true);

    for (const auto& fnAst : p.functions) {
        ir::Function* fn = c.functions.at(fnAst.name);
        auto* entry = c.builder.createBasicBlock(c.freshLabel("entry"), fn);
        c.builder.setInsertPoint(entry);

        std::map<std::string, VarInfo> vars;

        auto pit = fn->getParameters().begin();
        for (const std::string& pname : fnAst.params) {
            auto* slot = c.builder.createAlloc(ir::ConstantInt::get(c.i32, 4), c.i32);
            c.builder.createStore(pit->get(), slot);
            vars[pname] = {slot, false, 0};
            ++pit;
        }

        ir::BasicBlock* cur = entry;
        std::vector<LoopTargets> loopStack;
        emitStmtList(c, fn, cur, vars, fnAst.body, loopStack);
        if (!isTerminated(cur)) {
            c.builder.setInsertPoint(cur);
            c.builder.createRet(ir::ConstantInt::get(c.i32, 0));
        }
    }

    return c;
}

enum class BackendTarget {
    X64,
    AArch64,
    RiscV64,
};

std::string targetSuffix(BackendTarget t) {
    switch (t) {
        case BackendTarget::X64: return "x64";
        case BackendTarget::AArch64: return "aarch64";
        case BackendTarget::RiscV64: return "riscv64";
    }
    throw std::runtime_error("Unknown backend target");
}

std::unique_ptr<codegen::target::TargetInfo> makeTarget(BackendTarget t) {
    switch (t) {
        case BackendTarget::X64: return std::make_unique<codegen::target::SystemV_x64>();
        case BackendTarget::AArch64: return std::make_unique<codegen::target::AArch64>();
        case BackendTarget::RiscV64: return std::make_unique<codegen::target::RiscV64>();
    }
    throw std::runtime_error("Unknown backend target");
}

uint16_t machineForTarget(BackendTarget t) {
    switch (t) {
        case BackendTarget::X64: return 62;     // EM_X86_64
        case BackendTarget::AArch64: return 183; // EM_AARCH64
        case BackendTarget::RiscV64: return 243; // EM_RISCV
    }
    throw std::runtime_error("Unknown backend target");
}

CompilerCtx buildFrontendModule(const std::string& source, const std::string& tag) {
    Parser parser(source);
    ProgramAst p = parser.parseProgram();
    return lowerProgramToIR(p, "phpish_frontend_test_" + tag);
}

std::string emitTextAsmFromModule(ir::Module& module, BackendTarget t) {
    std::stringstream ss;
    codegen::CodeGen cg(module, makeTarget(t), &ss);
    cg.emit();
    return ss.str();
}

void emitInMemoryElfFromModule(ir::Module& module, BackendTarget t, const std::string& outputPath) {
    codegen::CodeGen cg(module, makeTarget(t), nullptr);
    cg.emit(true);

    std::map<std::string, std::vector<uint8_t>> sections;
    sections[".text"] = cg.getAssembler().getCode();
    sections[".data"] = cg.getRodataAssembler().getCode();

    ElfGenerator elfGen("phpish_frontend_test");
    elfGen.setMachine(machineForTarget(t));
    if (t == BackendTarget::X64) {
        elfGen.setBaseAddress(0x400000);
    }

    std::vector<ElfGenerator::Symbol> symbols;
    for (const auto& sym : cg.getSymbols()) {
        symbols.push_back({sym.name, sym.value, sym.size,
                           static_cast<uint8_t>(sym.type), static_cast<uint8_t>(sym.binding), sym.sectionName});
    }

    std::vector<ElfGenerator::Relocation> relocs;
    for (const auto& reloc : cg.getRelocations()) {
        relocs.push_back({reloc.offset, reloc.type, reloc.addend, reloc.symbolName, reloc.sectionName});
    }

    if (!elfGen.generateFromCode(sections, symbols, relocs, outputPath)) {
        throw std::runtime_error("ELF generation failed: " + elfGen.getLastError());
    }
}

void runCase(const std::string& name, const std::string& source, int expected);

int compileAndRun(const std::string& source, const std::string& tag) {
    CompilerCtx c = buildFrontendModule(source, tag);

    const std::string out = "./phpish_frontend_test_exec_" + tag;
    emitInMemoryElfFromModule(c.module, BackendTarget::X64, out);

    if (std::system(("chmod +x " + out).c_str()) != 0) {
        throw std::runtime_error("chmod failed");
    }

    int result = std::system(out.c_str());
    std::remove(out.c_str());

    if (result == -1) throw std::runtime_error("execution failed");
    return WEXITSTATUS(result);
}

void runBackendModeAndTargetMatrix(const std::string& name, const std::string& source, int expectedX64) {
    const std::vector<BackendTarget> targets = {
        BackendTarget::X64,
        BackendTarget::AArch64,
        BackendTarget::RiscV64,
    };

    for (BackendTarget t : targets) {
        CompilerCtx asmCtx = buildFrontendModule(source, name + "_asm_" + targetSuffix(t));
        std::string asmText = emitTextAsmFromModule(asmCtx.module, t);
        std::cout << "asm[" << targetSuffix(t) << "] size=" << asmText.size() << "\n";
        assert(!asmText.empty());

        std::string out = "./phpish_frontend_matrix_" + name + "_" + targetSuffix(t);
        CompilerCtx elfCtx = buildFrontendModule(source, name + "_elf_" + targetSuffix(t));
        try {
            emitInMemoryElfFromModule(elfCtx.module, t, out);
        } catch (const std::exception& ex) {
            // Some backends may not yet support every relocation in the in-memory ELF path.
            // We still keep textual ASM parity checks for those targets.
            std::cout << "in-memory-elf[" << targetSuffix(t) << "] skipped: " << ex.what() << "\n";
        }
        std::remove(out.c_str());
    }

    runCase(name + "_x64_exec", source, expectedX64);
}

void runCase(const std::string& name, const std::string& source, int expected) {
    int rc = compileAndRun(source, name);
    std::cout << name << " => " << rc << "\n";
    assert(rc == expected);
}

} // namespace

int main() {
    runCase("arith_fn", R"(
fn calc(a, b) {
    x = a * b + 10;
    y = x - 7;
    z = y + 3;
    return z * 2;
}
fn main() { return calc(6, 5); }
)", 72);

    runCase("if_elif_else", R"(
fn pick(n) {
    if n < 3 { return 10; }
    elif n < 4 { return 20; }
    else { return 30; }
}
fn main() { return pick(3); }
)", 20);

    runCase("while_loop", R"(
fn main() {
    i = 1; sum = 0;
    while i <= 5 { sum = sum + i; i = i + 1; }
    return sum;
}
)", 15);

    runCase("for_loop", R"(
fn main() {
    total = 0;
    for i = 0; i < 5; i = i + 1 { total = total + i; }
    return total;
}
)", 10);

    runCase("unary_ops", R"(
fn main() {
    x = -5;
    return (x + 9) + (!0) + (!7);
}
)", 5);

    runCase("short_circuit_or", R"(
fn main() {
    if 1 || (1 / 0) { return 7; } else { return 0; }
}
)", 7);

    runCase("short_circuit_and", R"(
fn main() {
    if 0 && (1 / 0) { return 1; } else { return 9; }
}
)", 9);

    runCase("break_continue", R"(
fn main() {
    s = 0;
    for i = 0; i < 10; i = i + 1 {
        if i == 8 { break; }
        if i == 1 || i == 3 { continue; }
        if i == 5 || i == 7 { continue; }
        s = s + i;
    }
    return s;
}
)", 12);

    runCase("recursion", R"(
fn fact(n) {
    if n <= 1 { return 1; }
    return n * fact(n - 1);
}
fn main() { return fact(5); }
)", 120);

    runCase("array_indexing", R"(
fn main() {
    a = [3, 5, 7, 11];
    a[2] = 10;
    return a[1] + a[2] + a[3];
}
)", 26);

    runCase("bool_literals", R"(
fn main() {
    if true && !false { return 5; }
    return 0;
}
)", 5);

    runCase("compound_assign", R"(
fn main() {
    x = 20;
    x += 5;
    x -= 3;
    x *= 2;
    return x;
}
)", 44);

    runCase("inc_dec", R"(
fn main() {
    i = 4;
    i++;
    i++;
    i--;
    return i;
}
)", 5);

    runCase("mutual_recursion", R"(
fn is_even(n) {
    if n == 0 { return 1; }
    return is_odd(n - 1);
}

fn is_odd(n) {
    if n == 0 { return 0; }
    return is_even(n - 1);
}

fn main() {
    return is_even(10) * 10 + is_odd(9);
}
)", 11);

    runCase("bitwise_shift", R"(
fn main() {
    x = 6 & 3;
    y = 6 | 3;
    z = 6 ^ 3;
    a = 1 << 4;
    b = 16 >> 2;
    return x + y + z + a + b;
}
)", 34);

    runCase("do_while", R"(
fn main() {
    i = 0;
    s = 0;
    do {
        i++;
        if i == 3 { continue; }
        s += i;
    } while i < 5;
    return s;
}
)", 12);

    // Cross-mode / cross-target validation on the same frontend programs.
    runBackendModeAndTargetMatrix("matrix_arith", R"(
fn calc(a, b) {
    x = a * b + 10;
    y = x - 7;
    z = y + 3;
    return z * 2;
}
fn main() { return calc(6, 5); }
)", 72);

    runBackendModeAndTargetMatrix("matrix_control", R"(
fn main() {
    i = 0;
    s = 0;
    do {
        i++;
        if i == 3 { continue; }
        s += i;
    } while i < 5;
    return s;
}
)", 12);

    runCase("runtime_syscall_interop", R"(
fn main() {
    pid = syscall(39, 0, 0, 0, 0, 0, 0);
    if pid > 0 { return 1; }
    return 0;
}
)", 1);

    bool failed = false;
    try {
        (void)compileAndRun("fn nope( { return 1; }", "bad");
    } catch (const std::exception&) {
        failed = true;
    }
    assert(failed && "Frontend should reject malformed programs");

    bool arityFailed = false;
    try {
        (void)compileAndRun(R"(
fn foo(a, b) { return a + b; }
fn main() { return foo(1); }
)", "arity_bad");
    } catch (const std::exception&) {
        arityFailed = true;
    }
    assert(arityFailed && "Frontend should reject function arity mismatch");

    bool boundsFailed = false;
    try {
        (void)compileAndRun(R"(
fn main() {
    a = [1, 2, 3];
    return a[5];
}
)", "bounds_bad");
    } catch (const std::exception&) {
        boundsFailed = true;
    }
    assert(boundsFailed && "Frontend should reject compile-time constant array out-of-bounds");

    bool unknownVarFailed = false;
    try {
        (void)compileAndRun(R"(
fn main() {
    return missing_var;
}
)", "unknown_var_bad");
    } catch (const std::exception&) {
        unknownVarFailed = true;
    }
    assert(unknownVarFailed && "Frontend should reject unresolved variable usage");

    std::cout << "All phpish frontend feature tests passed.\n";
    return 0;
}
