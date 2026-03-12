#include <cassert>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <utility>
#include <vector>

#include "codegen/CodeGen.h"
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
    LParen, RParen, LBrace, RBrace, Comma, Semi,
    Plus, Minus, Star, Slash, Percent,
    Assign, Eq, Ne, Lt, Le, Gt, Ge,
    KwFn, KwReturn, KwIf, KwElif, KwElse, KwWhile, KwFor
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

        ++i;
        switch (c) {
            case '(': return {TokKind::LParen, "("};
            case ')': return {TokKind::RParen, ")"};
            case '{': return {TokKind::LBrace, "{"};
            case '}': return {TokKind::RBrace, "}"};
            case ',': return {TokKind::Comma, ","};
            case ';': return {TokKind::Semi, ";"};
            case '+': return {TokKind::Plus, "+"};
            case '-': return {TokKind::Minus, "-"};
            case '*': return {TokKind::Star, "*"};
            case '/': return {TokKind::Slash, "/"};
            case '%': return {TokKind::Percent, "%"};
            case '=': return {TokKind::Assign, "="};
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
    enum Kind { Num, Var, Bin, Call } kind;
    int num = 0;
    std::string name;
    std::string op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    std::vector<std::unique_ptr<Expr>> args;
};

struct Stmt {
    enum Kind { Assign, Return, If, While, For, ExprStmt } kind;

    std::string var;
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
        while (tok.kind != TokKind::Eof) {
            p.functions.push_back(parseFunction());
        }
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
        while (tok.kind != TokKind::RBrace) {
            stmts.push_back(parseStmt());
        }
        expect(TokKind::RBrace, "Expected }");
        return stmts;
    }

    std::unique_ptr<Stmt> parseAssignNoSemi() {
        if (tok.kind != TokKind::Ident) throw std::runtime_error("Expected variable name");
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::Assign;
        s->var = tok.text;
        advance();
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

        // assignment or call statement
        Token ident = tok;
        advance();
        if (tok.kind == TokKind::Assign) {
            auto s = std::make_unique<Stmt>();
            s->kind = Stmt::Assign;
            s->var = ident.text;
            advance();
            s->expr = parseExpr();
            expect(TokKind::Semi, "Expected ;");
            return s;
        }
        if (tok.kind == TokKind::LParen) {
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Call;
            e->name = ident.text;
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
        throw std::runtime_error("Invalid statement form");
    }

    std::unique_ptr<Expr> parseExpr() { return parseCmp(); }

    std::unique_ptr<Expr> parseCmp() {
        auto l = parseAddSub();
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
        auto l = parsePrimary();
        while (tok.kind == TokKind::Star || tok.kind == TokKind::Slash || tok.kind == TokKind::Percent) {
            std::string op = tok.text;
            advance();
            auto r = parsePrimary();
            auto n = std::make_unique<Expr>();
            n->kind = Expr::Bin;
            n->op = op;
            n->lhs = std::move(l);
            n->rhs = std::move(r);
            l = std::move(n);
        }
        return l;
    }

    std::unique_ptr<Expr> parsePrimary() {
        if (tok.kind == TokKind::Number) {
            auto n = std::make_unique<Expr>();
            n->kind = Expr::Num;
            n->num = std::stoi(tok.text);
            advance();
            return n;
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

struct CompilerCtx {
    ir::Module module;
    ir::IRBuilder builder;
    ir::IntegerType* i32;
    std::map<std::string, ir::Function*> functions;
    int nextLabelId = 0;

    std::string freshLabel(const std::string& prefix) { return prefix + "_" + std::to_string(nextLabelId++); }

    explicit CompilerCtx(const std::string& name)
        : module(name), i32(ir::IntegerType::get(32)) {
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
                    std::map<std::string, ir::Value*>& vars, const Expr* e);

void emitStmtList(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
                  std::map<std::string, ir::Value*>& vars,
                  const std::vector<std::unique_ptr<Stmt>>& stmts);

ir::Value* getOrCreateVarPtr(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
                             std::map<std::string, ir::Value*>& vars, const std::string& name) {
    auto it = vars.find(name);
    if (it != vars.end()) return it->second;
    c.builder.setInsertPoint(bb);
    auto* p = c.builder.createAlloc(ir::ConstantInt::get(c.i32, 4), c.i32);
    vars[name] = p;
    return p;
}

ir::Value* emitExpr(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
                    std::map<std::string, ir::Value*>& vars, const Expr* e) {
    c.builder.setInsertPoint(bb);
    switch (e->kind) {
        case Expr::Num:
            return ir::ConstantInt::get(c.i32, e->num);
        case Expr::Var: {
            auto it = vars.find(e->name);
            if (it == vars.end()) throw std::runtime_error("Unknown variable: " + e->name);
            return c.builder.createLoad(it->second);
        }
        case Expr::Call: {
            auto fit = c.functions.find(e->name);
            if (fit == c.functions.end()) throw std::runtime_error("Unknown function: " + e->name);
            std::vector<ir::Value*> args;
            for (const auto& a : e->args) args.push_back(emitExpr(c, fn, bb, vars, a.get()));
            return c.builder.createCall(fit->second, args, c.i32);
        }
        case Expr::Bin: {
            ir::Value* l = emitExpr(c, fn, bb, vars, e->lhs.get());
            ir::Value* r = emitExpr(c, fn, bb, vars, e->rhs.get());
            if (e->op == "+") return c.builder.createAdd(l, r);
            if (e->op == "-") return c.builder.createSub(l, r);
            if (e->op == "*") return c.builder.createMul(l, r);
            if (e->op == "/") return c.builder.createDiv(l, r);
            if (e->op == "%") return c.builder.createRem(l, r);
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
                std::map<std::string, ir::Value*>& vars, const Stmt* s) {
    c.builder.setInsertPoint(bb);
    ir::Value* ptr = getOrCreateVarPtr(c, fn, bb, vars, s->var);
    ir::Value* val = emitExpr(c, fn, bb, vars, s->expr.get());
    c.builder.createStore(val, ptr);
}

void emitIf(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
            std::map<std::string, ir::Value*>& vars, const Stmt* s) {
    auto* merge = c.builder.createBasicBlock(c.freshLabel("if_merge"), fn);
    auto* nextTest = bb;

    for (size_t i = 0; i < s->branches.size(); ++i) {
        const auto& br = s->branches[i];
        c.builder.setInsertPoint(nextTest);
        auto* thenBB = c.builder.createBasicBlock(c.freshLabel("if_then"), fn);
        auto* elseBB = c.builder.createBasicBlock(c.freshLabel("if_next"), fn);
        auto* cond = emitExpr(c, fn, nextTest, vars, br.first.get());
        c.builder.createJnz(cond, thenBB, elseBB);

        ir::BasicBlock* thenCur = thenBB;
        emitStmtList(c, fn, thenCur, vars, br.second);
        if (!isTerminated(thenCur)) {
            c.builder.setInsertPoint(thenCur);
            c.builder.createJmp(merge);
        }

        nextTest = elseBB;

        if (i == s->branches.size() - 1) {
            c.builder.setInsertPoint(nextTest);
            if (!s->elseBody.empty()) {
                ir::BasicBlock* elseCur = nextTest;
                emitStmtList(c, fn, elseCur, vars, s->elseBody);
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
               std::map<std::string, ir::Value*>& vars, const Stmt* s) {
    auto* condBB = c.builder.createBasicBlock(c.freshLabel("while_cond"), fn);
    auto* bodyBB = c.builder.createBasicBlock(c.freshLabel("while_body"), fn);
    auto* endBB = c.builder.createBasicBlock(c.freshLabel("while_end"), fn);

    c.builder.setInsertPoint(bb);
    c.builder.createJmp(condBB);

    c.builder.setInsertPoint(condBB);
    auto* cond = emitExpr(c, fn, condBB, vars, s->loopCond.get());
    c.builder.createJnz(cond, bodyBB, endBB);

    ir::BasicBlock* bodyCur = bodyBB;
    emitStmtList(c, fn, bodyCur, vars, s->body);
    if (!isTerminated(bodyCur)) {
        c.builder.setInsertPoint(bodyCur);
        c.builder.createJmp(condBB);
    }

    bb = endBB;
    c.builder.setInsertPoint(bb);
}

void emitFor(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
             std::map<std::string, ir::Value*>& vars, const Stmt* s) {
    emitAssign(c, fn, bb, vars, s->initAssign.get());

    auto* condBB = c.builder.createBasicBlock(c.freshLabel("for_cond"), fn);
    auto* bodyBB = c.builder.createBasicBlock(c.freshLabel("for_body"), fn);
    auto* stepBB = c.builder.createBasicBlock(c.freshLabel("for_step"), fn);
    auto* endBB = c.builder.createBasicBlock(c.freshLabel("for_end"), fn);

    c.builder.setInsertPoint(bb);
    c.builder.createJmp(condBB);

    c.builder.setInsertPoint(condBB);
    auto* cond = emitExpr(c, fn, condBB, vars, s->loopCond.get());
    c.builder.createJnz(cond, bodyBB, endBB);

    ir::BasicBlock* bodyCur = bodyBB;
    emitStmtList(c, fn, bodyCur, vars, s->body);
    if (!isTerminated(bodyCur)) {
        c.builder.setInsertPoint(bodyCur);
        c.builder.createJmp(stepBB);
    }

    ir::BasicBlock* stepCur = stepBB;
    emitAssign(c, fn, stepCur, vars, s->stepAssign.get());
    if (!isTerminated(stepCur)) {
        c.builder.setInsertPoint(stepCur);
        c.builder.createJmp(condBB);
    }

    bb = endBB;
    c.builder.setInsertPoint(bb);
}

void emitStmtList(CompilerCtx& c, ir::Function* fn, ir::BasicBlock*& bb,
                  std::map<std::string, ir::Value*>& vars,
                  const std::vector<std::unique_ptr<Stmt>>& stmts) {
    for (const auto& sp : stmts) {
        if (isTerminated(bb)) break;
        const Stmt* s = sp.get();
        c.builder.setInsertPoint(bb);
        switch (s->kind) {
            case Stmt::Assign:
                emitAssign(c, fn, bb, vars, s);
                break;
            case Stmt::ExprStmt:
                (void)emitExpr(c, fn, bb, vars, s->expr.get());
                break;
            case Stmt::Return: {
                auto* v = emitExpr(c, fn, bb, vars, s->expr.get());
                c.builder.createRet(v);
                break;
            }
            case Stmt::If:
                emitIf(c, fn, bb, vars, s);
                break;
            case Stmt::While:
                emitWhile(c, fn, bb, vars, s);
                break;
            case Stmt::For:
                emitFor(c, fn, bb, vars, s);
                break;
        }
    }
}

CompilerCtx lowerProgramToIR(const ProgramAst& p, const std::string& moduleName) {
    CompilerCtx c(moduleName);

    // prototypes
    for (const auto& fn : p.functions) {
        std::vector<ir::Type*> params(fn.params.size(), c.i32);
        c.functions[fn.name] = c.builder.createFunction(fn.name, c.i32, params);
    }

    auto mainIt = c.functions.find("main");
    if (mainIt == c.functions.end()) throw std::runtime_error("Program must define fn main()");
    mainIt->second->setExported(true);

    // bodies
    for (const auto& fnAst : p.functions) {
        ir::Function* fn = c.functions.at(fnAst.name);
        auto* entry = c.builder.createBasicBlock(c.freshLabel("entry"), fn);
        c.builder.setInsertPoint(entry);

        std::map<std::string, ir::Value*> vars;

        // Bind params to local variable slots.
        auto pit = fn->getParameters().begin();
        for (const std::string& pname : fnAst.params) {
            auto* slot = c.builder.createAlloc(ir::ConstantInt::get(c.i32, 4), c.i32);
            c.builder.createStore(pit->get(), slot);
            vars[pname] = slot;
            ++pit;
        }

        ir::BasicBlock* cur = entry;
        emitStmtList(c, fn, cur, vars, fnAst.body);
        if (!isTerminated(cur)) {
            c.builder.setInsertPoint(cur);
            c.builder.createRet(ir::ConstantInt::get(c.i32, 0));
        }
    }

    return c;
}

int compileAndRun(const std::string& source, const std::string& tag) {
    Parser parser(source);
    ProgramAst p = parser.parseProgram();

    CompilerCtx c = lowerProgramToIR(p, "phpish_frontend_test_" + tag);

    auto target = std::make_unique<codegen::target::SystemV_x64>();
    codegen::CodeGen cg(c.module, std::move(target), nullptr);
    cg.emit(true);

    std::map<std::string, std::vector<uint8_t>> sections;
    sections[".text"] = cg.getAssembler().getCode();
    sections[".data"] = cg.getRodataAssembler().getCode();

    ElfGenerator elfGen("phpish_frontend_test");
    elfGen.setMachine(62);
    elfGen.setBaseAddress(0x400000);

    std::vector<ElfGenerator::Symbol> symbols;
    for (const auto& sym : cg.getSymbols()) {
        symbols.push_back({sym.name, sym.value, sym.size,
                           static_cast<uint8_t>(sym.type), static_cast<uint8_t>(sym.binding), sym.sectionName});
    }

    std::vector<ElfGenerator::Relocation> relocs;
    for (const auto& reloc : cg.getRelocations()) {
        relocs.push_back({reloc.offset, reloc.type, reloc.addend, reloc.symbolName, reloc.sectionName});
    }

    const std::string out = "./phpish_frontend_test_exec_" + tag;
    if (!elfGen.generateFromCode(sections, symbols, relocs, out)) {
        throw std::runtime_error("ELF generation failed: " + elfGen.getLastError());
    }

    if (std::system(("chmod +x " + out).c_str()) != 0) {
        throw std::runtime_error("chmod failed");
    }

    int result = std::system(out.c_str());
    std::remove(out.c_str());

    if (result == -1) throw std::runtime_error("execution failed");
    return WEXITSTATUS(result);
}

void runCase(const std::string& name, const std::string& source, int expected) {
    int rc = compileAndRun(source, name);
    std::cout << name << " => " << rc << "\n";
    assert(rc == expected);
}

} // namespace

int main() {
    // Complex arithmetic + variables + function call.
    runCase("arith_fn", R"(
fn calc(a, b) {
    x = a * b + 10;
    y = x - 7;
    z = y + 3;
    return z * 2;
}

fn main() {
    return calc(6, 5);
}
)", 72);

    // if / elif / else.
    runCase("if_elif_else", R"(
fn pick(n) {
    if n < 3 {
        return 10;
    } elif n < 4 {
        return 20;
    } else {
        return 30;
    }
}

fn main() {
    return pick(3);
}
)", 20);

    // while loop.
    runCase("while_loop", R"(
fn main() {
    i = 1;
    sum = 0;
    while i <= 5 {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}
)", 15);

    // for loop.
    runCase("for_loop", R"(
fn main() {
    total = 0;
    for i = 0; i < 5; i = i + 1 {
        total = total + i;
    }
    return total;
}
)", 10);

    // Parse error / invalid program coverage.
    bool failed = false;
    try {
        (void)compileAndRun("fn nope( { return 1; }", "bad");
    } catch (const std::exception&) {
        failed = true;
    }
    assert(failed && "Frontend should reject malformed programs");

    std::cout << "All phpish frontend feature tests passed.\n";
    return 0;
}
