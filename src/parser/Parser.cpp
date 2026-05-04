#include "parser/Parser.h"
#include "ir/Type.h"
#include "ir/FunctionType.h"
#include "ir/Constant.h"
#include "ir/PhiNode.h"
#include "ir/GlobalValue.h"
#include "ir/Function.h"
#include "ir/Parameter.h"
#include <iostream>
namespace parser {
Parser::Parser(std::istream& input) : lexer(input), context(std::make_shared<ir::IRContext>()), builder(context), fileFormat(FileFormat::FYRA) { getNextToken(); }
Parser::Parser(std::istream& input, FileFormat format) : lexer(input), context(std::make_shared<ir::IRContext>()), builder(context), fileFormat(format) { getNextToken(); }
void Parser::getNextToken() { currentToken = lexer.getNextToken(); }
std::unique_ptr<ir::Module> Parser::parseModule() {
    module = std::make_unique<ir::Module>("my_module", context); builder.setModule(module.get());
    while (currentToken.type != TokenType::Eof) {
        if (currentToken.type == TokenType::Keyword) { if (currentToken.value == "export" || currentToken.value == "function") parseFunction(); else if (currentToken.value == "type") parseType(); else if (currentToken.value == "data") parseData(); else getNextToken(); }
        else if (currentToken.type == TokenType::Extern) {
            getNextToken(); std::string capability = currentToken.value; for (char& c : capability) if (c == '_') c = '.';
            getNextToken(); std::vector<ir::Type*> paramTypes;
            if (currentToken.type == TokenType::LParen) { getNextToken(); while (currentToken.type != TokenType::RParen && currentToken.type != TokenType::Eof) { paramTypes.push_back(parseIRType()); if (currentToken.type == TokenType::Comma) getNextToken(); else break; } if (currentToken.type == TokenType::RParen) getNextToken(); }
            ir::Type* returnType = context->getVoidType(); if (currentToken.type == TokenType::Colon) { getNextToken(); returnType = parseIRType(); }
            module->addExternDecl(capability, {capability, paramTypes, returnType});
        } else getNextToken();
    }
    return std::move(module);
}
void Parser::parseFunction() {
    bool isExported = false; if (currentToken.value == "export") { isExported = true; getNextToken(); }
    if (currentToken.type != TokenType::Keyword || currentToken.value != "function") return;
    getNextToken(); if (currentToken.type != TokenType::Global) return;
    std::string funcName = currentToken.value; getNextToken();
    valueMap.clear(); labelMap.clear();
    if (currentToken.type != TokenType::LParen) return;
    getNextToken(); std::vector<ir::Type*> paramTypes; std::vector<std::string> paramNames; bool isVariadic = false;
    while (currentToken.type != TokenType::RParen && currentToken.type != TokenType::Eof) {
        if (currentToken.type == TokenType::Ellipsis) { isVariadic = true; getNextToken(); break; }
        if (currentToken.type != TokenType::Temporary) break;
        std::string paramName = currentToken.value; paramNames.push_back(paramName); getNextToken();
        if (currentToken.type != TokenType::Colon) break;
        getNextToken(); ir::Type* paramType = parseIRType(); paramTypes.push_back(paramType);
        if (currentToken.type == TokenType::Comma) getNextToken(); else break;
    }
    if (currentToken.type != TokenType::RParen) return;
    getNextToken(); ir::Type* returnType = context->getVoidType();
    if (currentToken.type == TokenType::Colon) { getNextToken(); returnType = parseIRType(); }
    else if (match(TokenType::Arrow)) { returnType = parseIRType(); }
    auto* func = builder.createFunction(funcName, returnType, paramTypes, isVariadic); func->setExported(isExported);
    auto& params = func->getParameters(); auto paramIt = params.begin();
    for (size_t i = 0; i < paramNames.size(); ++i) { if (paramIt != params.end()) { (*paramIt)->setName(paramNames[i]); valueMap[paramNames[i]] = paramIt->get(); ++paramIt; } }
    if (currentToken.type != TokenType::LCurly) return;
    getNextToken();
    if (currentToken.type == TokenType::Label) { while (currentToken.type == TokenType::Label) parseBasicBlock(func); }
    else { ir::BasicBlock* bb = builder.createBasicBlock("entry", func); builder.setInsertPoint(bb); while (currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Eof) { if (parseInstruction(bb) == nullptr) getNextToken(); } }
    if (currentToken.type != TokenType::RCurly) return;
    getNextToken();
}
void Parser::parseData() {
    getNextToken(); if (currentToken.type != TokenType::Global) return;
    std::string name = currentToken.value; getNextToken(); if (currentToken.type != TokenType::Equal) return;
    getNextToken(); if (currentToken.type != TokenType::LCurly) return;
    getNextToken(); std::vector<ir::Constant*> constants;
    while (currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Eof) {
        if (currentToken.type == TokenType::Keyword) {
            std::string typeStr = currentToken.value; getNextToken();
            if (typeStr == "b") { if (currentToken.type == TokenType::StringLiteral) { constants.push_back(context->getConstantString(currentToken.value)); getNextToken(); } else if (currentToken.type == TokenType::Number) { constants.push_back(context->getConstantInt(context->getIntegerType(8), std::stoll(currentToken.value, nullptr, 0))); getNextToken(); } }
            else if (typeStr == "w") { if (currentToken.type == TokenType::Number) { constants.push_back(context->getConstantInt(context->getIntegerType(32), std::stoll(currentToken.value, nullptr, 0))); getNextToken(); } }
            else if (typeStr == "l") { if (currentToken.type == TokenType::Number) { constants.push_back(context->getConstantInt(context->getIntegerType(64), std::stoll(currentToken.value, nullptr, 0))); getNextToken(); } }
            else if (typeStr == "h") { if (currentToken.type == TokenType::Number) { constants.push_back(context->getConstantInt(context->getIntegerType(16), std::stoll(currentToken.value, nullptr, 0))); getNextToken(); } }
        } else getNextToken();
    }
    getNextToken(); auto* arrayType = context->getArrayType(context->getIntegerType(8), constants.size()); auto* initializer = ir::ConstantArray::get(static_cast<ir::ArrayType*>(arrayType), constants); module->addGlobalVariable(std::make_unique<ir::GlobalVariable>(arrayType, name, initializer, false, ""));
}
void Parser::parseBasicBlock(ir::Function* func) {
    std::string labelName = currentToken.value; getNextToken(); ir::BasicBlock* bb = labelMap.count(labelName) ? labelMap[labelName] : builder.createBasicBlock(labelName, func); labelMap[labelName] = bb; builder.setInsertPoint(bb);
    while (currentToken.type != TokenType::Label && currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Eof) { if (parseInstruction(bb) == nullptr) getNextToken(); }
}
ir::Instruction* Parser::parseInstruction(ir::BasicBlock* bb) {
    if (currentToken.type == TokenType::Temporary) {
        std::string destName = currentToken.value; getNextToken(); if (currentToken.type != TokenType::Equal) return nullptr;
        getNextToken(); if (currentToken.type != TokenType::Keyword && currentToken.type != TokenType::Extern) return nullptr;
        std::string opcodeStr = currentToken.value; getNextToken(); ir::Instruction* instr = nullptr;
        if (opcodeStr == "add" || opcodeStr == "sub" || opcodeStr == "mul" || opcodeStr == "div" || opcodeStr == "udiv" || opcodeStr == "rem" || opcodeStr == "urem" || opcodeStr == "and" || opcodeStr == "or" || opcodeStr == "xor" || opcodeStr == "shl" || opcodeStr == "shr" || opcodeStr == "sar" || opcodeStr == "eq" || opcodeStr == "ne" || opcodeStr == "slt" || opcodeStr == "sle" || opcodeStr == "sgt" || opcodeStr == "sge" || opcodeStr == "ult" || opcodeStr == "ule" || opcodeStr == "ugt" || opcodeStr == "uge" || opcodeStr == "fadd" || opcodeStr == "fsub" || opcodeStr == "fmul" || opcodeStr == "fdiv" || opcodeStr == "lt" || opcodeStr == "le" || opcodeStr == "gt" || opcodeStr == "ge" || opcodeStr == "co" || opcodeStr == "cuo") {
            ir::Value* lhs = parseValue(); if (currentToken.type != TokenType::Comma) return nullptr; getNextToken(); ir::Value* rhs = parseValue();
            ir::Type* instrType = (currentToken.type == TokenType::Colon) ? (getNextToken(), parseIRType()) : nullptr;
            if (instrType && instrType->isIntegerTy()) { if (auto* ci = dynamic_cast<ir::ConstantInt*>(lhs)) lhs = context->getConstantInt(static_cast<ir::IntegerType*>(instrType), ci->getValue()); if (auto* ci = dynamic_cast<ir::ConstantInt*>(rhs)) rhs = context->getConstantInt(static_cast<ir::IntegerType*>(instrType), ci->getValue()); }
            if (opcodeStr == "add") instr = builder.createAdd(lhs, rhs, instrType ? instrType : lhs->getType());
            else if (opcodeStr == "sub") instr = builder.createSub(lhs, rhs, instrType ? instrType : lhs->getType());
            else if (opcodeStr == "mul") instr = builder.createMul(lhs, rhs, instrType ? instrType : lhs->getType());
            else if (opcodeStr == "div") instr = builder.createDiv(lhs, rhs, instrType ? instrType : lhs->getType());
            else if (opcodeStr == "udiv") instr = builder.createUdiv(lhs, rhs, instrType ? instrType : lhs->getType());
            else if (opcodeStr == "rem") instr = builder.createRem(lhs, rhs, instrType ? instrType : lhs->getType());
            else if (opcodeStr == "urem") instr = builder.createUrem(lhs, rhs, instrType ? instrType : lhs->getType());
            else if (opcodeStr == "and") instr = builder.createAnd(lhs, rhs, instrType ? instrType : lhs->getType());
            else if (opcodeStr == "or") instr = builder.createOr(lhs, rhs, instrType ? instrType : lhs->getType());
            else if (opcodeStr == "xor") instr = builder.createXor(lhs, rhs, instrType ? instrType : lhs->getType());
            else if (opcodeStr == "shl") instr = builder.createShl(lhs, rhs, instrType ? instrType : lhs->getType());
            else if (opcodeStr == "shr") instr = builder.createShr(lhs, rhs, instrType ? instrType : lhs->getType());
            else if (opcodeStr == "sar") instr = builder.createSar(lhs, rhs, instrType ? instrType : lhs->getType());
            else if (opcodeStr == "eq") { if (lhs->getType()->isFloatingPoint()) instr = builder.createCeqf(lhs, rhs); else instr = builder.createCeq(lhs, rhs); }
            else if (opcodeStr == "ne") { if (lhs->getType()->isFloatingPoint()) instr = builder.createCnef(lhs, rhs); else instr = builder.createCne(lhs, rhs); }
            else if (opcodeStr == "slt") instr = builder.createCslt(lhs, rhs); else if (opcodeStr == "sle") instr = builder.createCsle(lhs, rhs); else if (opcodeStr == "sgt") instr = builder.createCsgt(lhs, rhs); else if (opcodeStr == "sge") instr = builder.createCsge(lhs, rhs);
            else if (opcodeStr == "ult") instr = builder.createCult(lhs, rhs); else if (opcodeStr == "ule") instr = builder.createCule(lhs, rhs); else if (opcodeStr == "ugt") instr = builder.createCugt(lhs, rhs); else if (opcodeStr == "uge") instr = builder.createCuge(lhs, rhs);
            else if (opcodeStr == "fadd") instr = builder.createFAdd(lhs, rhs); else if (opcodeStr == "fsub") instr = builder.createFSub(lhs, rhs); else if (opcodeStr == "fmul") instr = builder.createFMul(lhs, rhs); else if (opcodeStr == "fdiv") instr = builder.createFDiv(lhs, rhs);
            else if (opcodeStr == "lt") { if (lhs->getType()->isFloatingPoint()) instr = builder.createClt(lhs, rhs); else instr = builder.createCslt(lhs, rhs); }
            else if (opcodeStr == "le") { if (lhs->getType()->isFloatingPoint()) instr = builder.createCle(lhs, rhs); else instr = builder.createCsle(lhs, rhs); }
            else if (opcodeStr == "gt") { if (lhs->getType()->isFloatingPoint()) instr = builder.createCgt(lhs, rhs); else instr = builder.createCsgt(lhs, rhs); }
            else if (opcodeStr == "ge") { if (lhs->getType()->isFloatingPoint()) instr = builder.createCge(lhs, rhs); else instr = builder.createCsge(lhs, rhs); }
            else if (opcodeStr == "co") instr = builder.createCo(lhs, rhs); else if (opcodeStr == "cuo") instr = builder.createCuo(lhs, rhs);
        } else if (opcodeStr == "phi") {
            std::vector<std::pair<std::string, ir::Value*>> incoming; while(currentToken.type == TokenType::Label) { std::string lbl = currentToken.value; getNextToken(); ir::Value* v = parseValue(); incoming.push_back({lbl, v}); if (currentToken.type == TokenType::Comma) getNextToken(); else break; }
            ir::Type* ty = (currentToken.type == TokenType::Colon) ? (getNextToken(), parseIRType()) : nullptr;
            ir::PhiNode* phi = builder.createPhi(ty, incoming.size(), nullptr);
            for (auto& p : incoming) { ir::Value* v = p.second; if (ty && ty->isIntegerTy()) { if (auto* ci = dynamic_cast<ir::ConstantInt*>(v)) v = context->getConstantInt(static_cast<ir::IntegerType*>(ty), ci->getValue()); } ir::BasicBlock* bbPhi = labelMap.count(p.first) ? labelMap[p.first] : builder.createBasicBlock(p.first, builder.getInsertPoint()->getParent()); labelMap[p.first] = bbPhi; phi->addIncoming(v, bbPhi); }
            instr = phi;
        } else if (opcodeStr == "call") { instr = parseCallInstruction(nullptr); }
        if (instr) { instr->setName(destName); valueMap[destName] = instr; } return instr;
    } else if (currentToken.type == TokenType::Keyword) {
        std::string opcodeStr = currentToken.value; getNextToken();
        if (opcodeStr == "ret") { ir::Value* v = (currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Label && currentToken.type != TokenType::Eof) ? parseValue() : nullptr; return builder.createRet(v); }
        if (opcodeStr == "store") { ir::Value* v = parseValue(); getNextToken(); ir::Value* p = parseValue(); ir::Type* ty = (currentToken.type == TokenType::Colon) ? (getNextToken(), parseIRType()) : nullptr; if (!ty) return nullptr; return builder.createStore(v, p); }
        if (opcodeStr == "jmp") return builder.createJmp(parseValue());
        if (opcodeStr == "jnz") { ir::Value* c = parseValue(); getNextToken(); ir::Value* t = parseValue(); getNextToken(); ir::Value* f = parseValue(); return builder.createJnz(c, t, f); }
        if (opcodeStr == "call") return parseCallInstruction(nullptr);
    }
    getNextToken(); return nullptr;
}
void Parser::parseType() {
    getNextToken(); std::string name = currentToken.value; getNextToken(); getNextToken(); getNextToken();
    if (match(TokenType::LCurly)) {
        std::vector<ir::Type*> members; while (!check(TokenType::RCurly)) { members.push_back(parseType_actual()); consume(TokenType::Semicolon, "Expected ;"); }
        consume(TokenType::RCurly, "Expected }"); auto* ut = ir::Type::getUnionTy(members); module->addType(name, ut);
    } else { std::vector<ir::Type*> elements = parseStructElements(); auto* st = context->createStructType(name); st->setBody(elements); module->addType(name, st); }
}
ir::Instruction* Parser::parseCallInstruction(ir::Type* retType) {
    ir::Value* callee = parseValue(); std::vector<ir::Value*> args;
    if (currentToken.type == TokenType::LParen) { getNextToken(); while (currentToken.type != TokenType::RParen && currentToken.type != TokenType::Eof) { args.push_back(parseValue()); if (currentToken.type == TokenType::Comma) getNextToken(); else break; } getNextToken(); }
    if (currentToken.type == TokenType::Colon) { getNextToken(); retType = parseIRType(); }
    if (auto* f = dynamic_cast<ir::Function*>(callee)) {
        auto& pTypes = static_cast<ir::FunctionType*>(f->getType())->getParamTypes();
        for (size_t i = 0; i < std::min(args.size(), pTypes.size()); ++i) { if (args[i]->getType() != pTypes[i]) throw std::runtime_error("Argument type mismatch"); }
    }
    return builder.createCall(callee, args, retType);
}
ir::Value* Parser::parseValue() {
    if (currentToken.type == TokenType::Number) { long long v = std::stoll(currentToken.value, nullptr, 0); getNextToken(); return context->getConstantInt(context->getIntegerType(32), v); }
    if (currentToken.type == TokenType::Temporary) { std::string n = currentToken.value; getNextToken(); return valueMap.count(n) ? valueMap[n] : nullptr; }
    if (currentToken.type == TokenType::Global) { std::string n = currentToken.value; getNextToken(); for (auto& gv : module->getGlobalVariables()) if (gv->getName() == n) return gv.get(); return new ir::GlobalValue(context->getVoidType(), n); }
    if (currentToken.type == TokenType::Label) { std::string n = currentToken.value; getNextToken(); return labelMap.count(n) ? labelMap[n] : (labelMap[n] = builder.createBasicBlock(n, builder.getInsertPoint()->getParent())); }
    return nullptr;
}
ir::Type* Parser::parseIRType() {
    if (currentToken.type == TokenType::Keyword) { std::string v = currentToken.value; getNextToken(); if (v == "w") return context->getIntegerType(32); if (v == "l") return context->getIntegerType(64); if (v == "s") return context->getFloatType(); if (v == "d") return context->getDoubleType(); }
    return context->getVoidType();
}
std::vector<ir::Type*> Parser::parseStructElements() {
    std::vector<ir::Type*> elements; if (currentToken.type != TokenType::LCurly) return elements;
    getNextToken(); while (currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Eof) { elements.push_back(parseIRType()); if (currentToken.type == TokenType::Comma) getNextToken(); else break; }
    getNextToken(); return elements;
}
bool Parser::match(TokenType t) { if (currentToken.type == t) { getNextToken(); return true; } return false; }
bool Parser::check(TokenType t) { return currentToken.type == t; }
ir::Type* Parser::parseType_actual() { return parseIRType(); }
void Parser::consume(TokenType t, const std::string& msg) { if (!match(t)) throw std::runtime_error(msg); }
}
