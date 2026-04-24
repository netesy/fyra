#include <cassert>
#include <cctype>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"
#include "ir/Constant.h"
#include "ir/IRBuilder.h"
#include "ir/Module.h"
#include "ir/Type.h"

namespace {

enum class TokKind {
    Eof, Ident, Number,
    LParen, RParen, LBrace, RBrace, Comma, Semi,
    Plus, Minus, Star,
    Assign, Eq, Lt, Le, Gt, Ge,
    KwFn, KwReturn, KwIf, KwElse, KwWhile
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
            if (t == "else") return {TokKind::KwElse, t};
            if (t == "while") return {TokKind::KwWhile, t};
            return {TokKind::Ident, t};
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t st = i++;
            while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
            return {TokKind::Number, s.substr(st, i - st)};
        }

        auto two = [&](char a, char b, TokKind k) -> bool {
            if (i + 1 < s.size() && s[i] == a && s[i + 1] == b) {
                i += 2;
                tok = {k, std::string{a, b}};
                return true;
            }
            return false;
        };

        if (two('=', '=', TokKind::Eq)) return tok;
        if (two('<', '=', TokKind::Le)) return tok;
        if (two('>', '=', TokKind::Ge)) return tok;

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
            case '=': return {TokKind::Assign, "="};
            case '<': return {TokKind::Lt, "<"};
            case '>': return {TokKind::Gt, ">"};
            default: throw std::runtime_error("Unexpected char");
        }
    }

private:
    void skipWs() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }

    const std::string& s;
    size_t i = 0;
    Token tok{};
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
    enum Kind { Assign, Return, If, While, ExprStmt } kind;
    std::string var;
    std::unique_ptr<Expr> expr;
    std::unique_ptr<Expr> cond;
    std::vector<std::unique_ptr<Stmt>> thenBody;
    std::vector<std::unique_ptr<Stmt>> elseBody;
    std::vector<std::unique_ptr<Stmt>> body;
};

struct FunctionAst { std::string name; std::vector<std::string> params; std::vector<std::unique_ptr<Stmt>> body; };
struct ProgramAst { std::vector<FunctionAst> functions; };

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
    void expect(TokKind k, const char* msg) { if (tok.kind != k) throw std::runtime_error(msg); advance(); }

    FunctionAst parseFunction() {
        expect(TokKind::KwFn, "expected fn");
        if (tok.kind != TokKind::Ident) throw std::runtime_error("expected function name");
        FunctionAst fn; fn.name = tok.text; advance();
        expect(TokKind::LParen, "expected (");
        if (tok.kind != TokKind::RParen) {
            while (true) {
                if (tok.kind != TokKind::Ident) throw std::runtime_error("expected param");
                fn.params.push_back(tok.text);
                advance();
                if (tok.kind == TokKind::Comma) { advance(); continue; }
                break;
            }
        }
        expect(TokKind::RParen, "expected )");
        fn.body = parseBlock();
        return fn;
    }

    std::vector<std::unique_ptr<Stmt>> parseBlock() {
        expect(TokKind::LBrace, "expected {");
        std::vector<std::unique_ptr<Stmt>> out;
        while (tok.kind != TokKind::RBrace) out.push_back(parseStmt());
        expect(TokKind::RBrace, "expected }");
        return out;
    }

    std::unique_ptr<Stmt> parseStmt() {
        if (tok.kind == TokKind::KwReturn) {
            auto s = std::make_unique<Stmt>(); s->kind = Stmt::Return; advance();
            s->expr = parseExpr(); expect(TokKind::Semi, "expected ;"); return s;
        }
        if (tok.kind == TokKind::KwIf) {
            auto s = std::make_unique<Stmt>(); s->kind = Stmt::If; advance();
            s->cond = parseExpr(); s->thenBody = parseBlock();
            if (tok.kind == TokKind::KwElse) { advance(); s->elseBody = parseBlock(); }
            return s;
        }
        if (tok.kind == TokKind::KwWhile) {
            auto s = std::make_unique<Stmt>(); s->kind = Stmt::While; advance();
            s->cond = parseExpr(); s->body = parseBlock(); return s;
        }
        if (tok.kind != TokKind::Ident) throw std::runtime_error("expected statement");

        std::string name = tok.text; advance();
        if (tok.kind == TokKind::Assign) {
            auto s = std::make_unique<Stmt>(); s->kind = Stmt::Assign; s->var = name; advance();
            s->expr = parseExpr(); expect(TokKind::Semi, "expected ;"); return s;
        }
        if (tok.kind == TokKind::LParen) {
            auto e = std::make_unique<Expr>(); e->kind = Expr::Call; e->name = name; advance();
            if (tok.kind != TokKind::RParen) {
                while (true) {
                    e->args.push_back(parseExpr());
                    if (tok.kind == TokKind::Comma) { advance(); continue; }
                    break;
                }
            }
            expect(TokKind::RParen, "expected )");
            expect(TokKind::Semi, "expected ;");
            auto s = std::make_unique<Stmt>(); s->kind = Stmt::ExprStmt; s->expr = std::move(e); return s;
        }
        throw std::runtime_error("invalid statement");
    }

    std::unique_ptr<Expr> parseExpr() { return parseCmp(); }

    std::unique_ptr<Expr> parseCmp() {
        auto l = parseAdd();
        while (tok.kind == TokKind::Eq || tok.kind == TokKind::Lt || tok.kind == TokKind::Le ||
               tok.kind == TokKind::Gt || tok.kind == TokKind::Ge) {
            auto n = std::make_unique<Expr>(); n->kind = Expr::Bin; n->op = tok.text; advance();
            n->lhs = std::move(l); n->rhs = parseAdd(); l = std::move(n);
        }
        return l;
    }

    std::unique_ptr<Expr> parseAdd() {
        auto l = parseMul();
        while (tok.kind == TokKind::Plus || tok.kind == TokKind::Minus) {
            auto n = std::make_unique<Expr>(); n->kind = Expr::Bin; n->op = tok.text; advance();
            n->lhs = std::move(l); n->rhs = parseMul(); l = std::move(n);
        }
        return l;
    }

    std::unique_ptr<Expr> parseMul() {
        auto l = parsePrimary();
        while (tok.kind == TokKind::Star) {
            auto n = std::make_unique<Expr>(); n->kind = Expr::Bin; n->op = tok.text; advance();
            n->lhs = std::move(l); n->rhs = parsePrimary(); l = std::move(n);
        }
        return l;
    }

    std::unique_ptr<Expr> parsePrimary() {
        if (tok.kind == TokKind::Number) {
            auto n = std::make_unique<Expr>(); n->kind = Expr::Num; n->num = std::stoi(tok.text); advance(); return n;
        }
        if (tok.kind == TokKind::Ident) {
            std::string name = tok.text; advance();
            if (tok.kind == TokKind::LParen) {
                auto c = std::make_unique<Expr>(); c->kind = Expr::Call; c->name = name; advance();
                if (tok.kind != TokKind::RParen) {
                    while (true) {
                        c->args.push_back(parseExpr());
                        if (tok.kind == TokKind::Comma) { advance(); continue; }
                        break;
                    }
                }
                expect(TokKind::RParen, "expected )");
                return c;
            }
            auto v = std::make_unique<Expr>(); v->kind = Expr::Var; v->name = name; return v;
        }
        if (tok.kind == TokKind::LParen) { advance(); auto e = parseExpr(); expect(TokKind::RParen, "expected )"); return e; }
        throw std::runtime_error("expected expression");
    }

    Lexer lex;
    Token tok{};
};

struct Ctx {
    ir::Module module;
    ir::IRBuilder b;
    ir::IntegerType* i32;
    std::map<std::string, ir::Function*> funcs;
    int id = 0;
    explicit Ctx(const std::string& name) : module(name), i32(ir::IntegerType::get(32)) { b.setModule(&module); }
    std::string fresh(const std::string& p) { return p + "_" + std::to_string(id++); }
};

bool term(ir::BasicBlock* bb) {
    if (bb->getInstructions().empty()) return false;
    auto op = bb->getInstructions().back()->getOpcode();
    return op == ir::Instruction::Ret || op == ir::Instruction::Jmp || op == ir::Instruction::Jnz;
}

ir::Value* emitExpr(Ctx& c, ir::Function* fn, ir::BasicBlock*& bb,
                    std::map<std::string, ir::Value*>& vars, const Expr* e) {
    c.b.setInsertPoint(bb);
    switch (e->kind) {
        case Expr::Num: return ir::ConstantInt::get(c.i32, e->num);
        case Expr::Var: {
            auto it = vars.find(e->name);
            if (it == vars.end()) throw std::runtime_error("unknown var");
            return c.b.createLoad(it->second);
        }
        case Expr::Call: {
            auto it = c.funcs.find(e->name);
            if (it == c.funcs.end()) throw std::runtime_error("unknown function");
            std::vector<ir::Value*> args;
            for (const auto& a : e->args) args.push_back(emitExpr(c, fn, bb, vars, a.get()));
            return c.b.createCall(it->second, args, c.i32);
        }
        case Expr::Bin: {
            auto* l = emitExpr(c, fn, bb, vars, e->lhs.get());
            auto* r = emitExpr(c, fn, bb, vars, e->rhs.get());
            if (e->op == "+") return c.b.createAdd(l, r);
            if (e->op == "-") return c.b.createSub(l, r);
            if (e->op == "*") return c.b.createMul(l, r);
            if (e->op == "==") return c.b.createCeq(l, r);
            if (e->op == "<") return c.b.createCslt(l, r);
            if (e->op == "<=") return c.b.createCsle(l, r);
            if (e->op == ">") return c.b.createCsgt(l, r);
            if (e->op == ">=") return c.b.createCsge(l, r);
            throw std::runtime_error("unsupported binop");
        }
    }
    throw std::runtime_error("unreachable");
}

void emitStmts(Ctx& c, ir::Function* fn, ir::BasicBlock*& bb, std::map<std::string, ir::Value*>& vars,
               const std::vector<std::unique_ptr<Stmt>>& stmts) {
    for (const auto& sp : stmts) {
        if (term(bb)) break;
        const Stmt* s = sp.get();
        c.b.setInsertPoint(bb);
        if (s->kind == Stmt::Assign) {
            ir::Value* slot;
            if (!vars.count(s->var)) {
                slot = c.b.createAlloc(ir::ConstantInt::get(c.i32, 4), c.i32);
                vars[s->var] = slot;
            } else slot = vars[s->var];
            auto* v = emitExpr(c, fn, bb, vars, s->expr.get());
            c.b.createStore(v, slot);
        } else if (s->kind == Stmt::ExprStmt) {
            (void)emitExpr(c, fn, bb, vars, s->expr.get());
        } else if (s->kind == Stmt::Return) {
            auto* v = emitExpr(c, fn, bb, vars, s->expr.get());
            c.b.createRet(v);
        } else if (s->kind == Stmt::If) {
            auto* tbb = c.b.createBasicBlock(c.fresh("if_t"), fn);
            auto* ebb = c.b.createBasicBlock(c.fresh("if_e"), fn);
            auto* mbb = c.b.createBasicBlock(c.fresh("if_m"), fn);
            auto* cond = emitExpr(c, fn, bb, vars, s->cond.get());
            c.b.createJnz(cond, tbb, ebb);

            ir::BasicBlock* tc = tbb;
            emitStmts(c, fn, tc, vars, s->thenBody);
            if (!term(tc)) { c.b.setInsertPoint(tc); c.b.createJmp(mbb); }

            ir::BasicBlock* ec = ebb;
            emitStmts(c, fn, ec, vars, s->elseBody);
            if (!term(ec)) { c.b.setInsertPoint(ec); c.b.createJmp(mbb); }

            bb = mbb;
        } else if (s->kind == Stmt::While) {
            auto* cbb = c.b.createBasicBlock(c.fresh("wh_c"), fn);
            auto* bbb = c.b.createBasicBlock(c.fresh("wh_b"), fn);
            auto* ebb = c.b.createBasicBlock(c.fresh("wh_e"), fn);
            c.b.createJmp(cbb);
            c.b.setInsertPoint(cbb);
            auto* cond = emitExpr(c, fn, cbb, vars, s->cond.get());
            c.b.createJnz(cond, bbb, ebb);
            ir::BasicBlock* bc = bbb;
            emitStmts(c, fn, bc, vars, s->body);
            if (!term(bc)) { c.b.setInsertPoint(bc); c.b.createJmp(cbb); }
            bb = ebb;
        }
    }
}

Ctx lower(const std::string& source, const std::string& name) {
    Parser p(source);
    ProgramAst ast = p.parseProgram();
    Ctx c(name);

    for (const auto& f : ast.functions) {
        std::vector<ir::Type*> params(f.params.size(), c.i32);
        c.funcs[f.name] = c.b.createFunction(f.name, c.i32, params);
    }
    if (!c.funcs.count("main")) throw std::runtime_error("missing main");
    c.funcs["main"]->setExported(true);

    for (const auto& f : ast.functions) {
        auto* fn = c.funcs[f.name];
        auto* entry = c.b.createBasicBlock(c.fresh("entry"), fn);
        c.b.setInsertPoint(entry);
        std::map<std::string, ir::Value*> vars;

        auto pit = fn->getParameters().begin();
        for (const auto& pname : f.params) {
            auto* slot = c.b.createAlloc(ir::ConstantInt::get(c.i32, 4), c.i32);
            c.b.createStore(pit->get(), slot);
            vars[pname] = slot;
            ++pit;
        }

        ir::BasicBlock* cur = entry;
        emitStmts(c, fn, cur, vars, f.body);
        if (!term(cur)) { c.b.setInsertPoint(cur); c.b.createRet(ir::ConstantInt::get(c.i32, 0)); }
    }

    return c;
}

void checkWat(const std::string& source, const std::string& tag) {
    Ctx c = lower(source, "voilet_wat_" + tag);
    std::stringstream ss;
    codegen::CodeGen cg(c.module, codegen::target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI}), &ss);
    cg.emit();

    const std::string wat = ss.str();
    assert(!wat.empty());
    assert(wat.find("(module") != std::string::npos);
    assert(wat.find("(func ") != std::string::npos);
    assert(wat.find("(export \"main\"") != std::string::npos);
}

void checkWasm(const std::string& source, const std::string& tag) {
    Ctx c = lower(source, "voilet_wasm_" + tag);
    codegen::CodeGen cg(c.module, codegen::target::TargetResolver::resolve({::target::Arch::WASM32, ::target::OS::WASI}));
    cg.emit();

    const auto& code = cg.getAssembler().getCode();
    assert(code.size() >= 8);
    assert(code[0] == 0x00 && code[1] == 0x61 && code[2] == 0x73 && code[3] == 0x6d);
    assert(code[4] == 0x01 && code[5] == 0x00 && code[6] == 0x00 && code[7] == 0x00);
}

void runVoiletCase(const std::string& name, const std::string& src) {
    checkWat(src, name);
    checkWasm(src, name);
    std::cout << "voilet " << name << " passed (WAT+WASM)\n";
}

} // namespace

int main() {
    runVoiletCase("arith_and_call", R"(
fn addmul(a, b) {
    c = a + b;
    return c * 2;
}
fn main() {
    return addmul(5, 6);
}
)");

    runVoiletCase("if_while", R"(
fn main() {
    i = 0;
    s = 0;
    while i < 5 {
        if i == 3 {
            s = s + 10;
        } else {
            s = s + i;
        }
        i = i + 1;
    }
    return s;
}
)");

    bool parseFailed = false;
    try {
        checkWat("fn main( { return 1; }", "bad_parse");
    } catch (const std::exception&) {
        parseFailed = true;
    }
    assert(parseFailed);

    std::cout << "All voilet frontend tests passed.\n";
    return 0;
}
