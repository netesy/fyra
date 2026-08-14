#include "parser/Parser.h"
#include "ir/Instruction.h"
#include "ir/Function.h"
#include "ir/IRBuilder.h"
#include "ir/GlobalValue.h"
#include "ir/GlobalVariable.h"
#include "ir/Constant.h"
#include "ir/PhiNode.h"
#include "ir/FunctionType.h"
#include <iostream>

namespace parser {

Parser::Parser(std::istream& input) : lexer(input), fileFormat(FileFormat::FYRA) {
    context = std::make_shared<ir::IRContext>();
    builder.setContext(context);
    getNextToken();
}

Parser::Parser(std::istream& input, FileFormat format) : lexer(input), fileFormat(format) {
    context = std::make_shared<ir::IRContext>();
    builder.setContext(context);
    getNextToken();
}

std::unique_ptr<ir::Module> Parser::parseModule() {
    module = std::make_unique<ir::Module>("my_module", context); builder.setModule(module.get());
    builder.setLine(currentToken.line);
    while (currentToken.type != TokenType::Eof) {
        builder.setLine(currentToken.line);
        if (currentToken.type == TokenType::Keyword) {
            if (currentToken.value == "export" || currentToken.value == "function") parseFunction();
            else if (currentToken.value == "type") parseType();
            else if (currentToken.value == "data") parseData();
            else getNextToken();
        } else if (currentToken.type == TokenType::Extern) {
            getNextToken(); std::string capability = currentToken.value;
            for (char& c : capability) if (c == '_') c = '.';
            getNextToken();
            std::vector<ir::Type*> paramTypes;
            if (currentToken.type == TokenType::LParen) {
                getNextToken();
                while (currentToken.type != TokenType::RParen && currentToken.type != TokenType::Eof) {
                    paramTypes.push_back(parseIRType());
                    if (currentToken.type == TokenType::Comma) getNextToken();
                    else break;
                }
                if (currentToken.type == TokenType::RParen) getNextToken();
            }
            ir::Type* returnType = context->getVoidType();
            if (currentToken.type == TokenType::Colon) { getNextToken(); returnType = parseIRType(); }
            module->addExternDecl(capability, {capability, paramTypes, returnType});
        } else {
            getNextToken();
        }
    }
    return std::move(module);
}

void Parser::parseFunction() {
    bool isExported = false;
    if (currentToken.value == "export") { isExported = true; getNextToken(); }
    if (currentToken.type != TokenType::Keyword || currentToken.value != "function") return;
    getNextToken();
    if (currentToken.type != TokenType::Global) return;
    std::string funcName = currentToken.value;
    getNextToken();

    valueMap.clear();
    labelMap.clear();
    placeholders.clear();

    std::vector<ir::Type*> paramTypes;
    std::vector<std::string> paramNames;
    bool isVariadic = false;

    if (currentToken.type == TokenType::LParen) {
        getNextToken();
        while (currentToken.type != TokenType::RParen && currentToken.type != TokenType::Eof) {
            if (currentToken.type == TokenType::Ellipsis) { isVariadic = true; getNextToken(); break; }
            if (currentToken.type != TokenType::Temporary) break;
            std::string pName = currentToken.value;
            paramNames.push_back(pName);
            getNextToken();
            if (currentToken.type != TokenType::Colon) break;
            getNextToken();
            paramTypes.push_back(parseIRType());
            if (currentToken.type == TokenType::Comma) getNextToken();
            else break;
        }
        if (currentToken.type == TokenType::RParen) getNextToken();
    }

    ir::Type* returnType = context->getVoidType();
    if (currentToken.type == TokenType::Colon) { getNextToken(); returnType = parseIRType(); }
    else if (match(TokenType::Arrow)) { returnType = parseIRType(); }

    auto* func = builder.createFunction(funcName, returnType, paramTypes, isVariadic);
    func->setExported(isExported);

    auto& params = func->getParameters();
    auto paramIt = params.begin();
    for (size_t i = 0; i < paramNames.size() && paramIt != params.end(); ++i, ++paramIt) {
        paramIt->get()->setName(paramNames[i]);
        valueMap[paramNames[i]] = paramIt->get();
    }

    if (currentToken.type != TokenType::LCurly) return;
    getNextToken();

    if (currentToken.type == TokenType::Label) {
        while (currentToken.type == TokenType::Label) parseBasicBlock(func);
    } else {
        ir::BasicBlock* bb = builder.createBasicBlock("entry", func);
        builder.setInsertPoint(bb);
        while (currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Eof) {
            if (parseInstruction(bb) == nullptr) {
                if (currentToken.type == TokenType::RCurly || currentToken.type == TokenType::Eof) break;
                getNextToken();
            }
        }
    }

    if (currentToken.type == TokenType::RCurly) getNextToken();
}

ir::Instruction* Parser::parseInstruction(ir::BasicBlock* bb) {
    builder.setLine(currentToken.line);
    std::string destName;
    if (currentToken.type == TokenType::Temporary) {
        destName = currentToken.value;
        getNextToken();
        if (currentToken.type != TokenType::Equal) return nullptr;
        getNextToken();
    }

    if (currentToken.type != TokenType::Keyword && currentToken.type != TokenType::Extern) return nullptr;
    std::string opcodeStr = currentToken.value;
    getNextToken();

    ir::Instruction* instr = nullptr;
    const bool isLoadOp = (opcodeStr == "load" || opcodeStr == "loadl" || opcodeStr == "loads" ||
                           opcodeStr == "loadd" || opcodeStr == "loaduw" || opcodeStr == "loadsh" ||
                           opcodeStr == "loaduh" || opcodeStr == "loadsb" || opcodeStr == "loadub");
    const bool isCastOp = (opcodeStr == "extub" || opcodeStr == "extuh" || opcodeStr == "extuw" ||
                           opcodeStr == "extsb" || opcodeStr == "extsh" || opcodeStr == "extsw" ||
                           opcodeStr == "exts" || opcodeStr == "truncd" || opcodeStr == "swtof" ||
                           opcodeStr == "uwtof" || opcodeStr == "sltof" || opcodeStr == "ultof" ||
                           opcodeStr == "dtosi" || opcodeStr == "dtoui" || opcodeStr == "stosi" ||
                           opcodeStr == "stoui" || opcodeStr == "cast");

    if (opcodeStr == "add" || opcodeStr == "sub" || opcodeStr == "mul" || opcodeStr == "div" ||
        opcodeStr == "udiv" || opcodeStr == "rem" || opcodeStr == "urem" || opcodeStr == "and" ||
        opcodeStr == "or" || opcodeStr == "xor" || opcodeStr == "shl" || opcodeStr == "shr" ||
        opcodeStr == "sar" || opcodeStr == "eq" || opcodeStr == "ne" || opcodeStr == "slt" ||
        opcodeStr == "sle" || opcodeStr == "sgt" || opcodeStr == "sge" || opcodeStr == "ult" ||
        opcodeStr == "ule" || opcodeStr == "ugt" || opcodeStr == "uge" || opcodeStr == "fadd" ||
        opcodeStr == "fsub" || opcodeStr == "fmul" || opcodeStr == "fdiv" || opcodeStr == "lt" ||
        opcodeStr == "le" || opcodeStr == "gt" || opcodeStr == "ge" || opcodeStr == "co" ||
        opcodeStr == "cuo" || opcodeStr == "neg" || opcodeStr == "alloc" || opcodeStr == "copy" || isLoadOp) {

        ir::Value* lhs = parseValue();
        ir::Value* rhs = nullptr;
        if (opcodeStr != "neg" && opcodeStr != "alloc" && opcodeStr != "copy" && !isLoadOp) {
            if (currentToken.type == TokenType::Comma) getNextToken();
            rhs = parseValue();
        }

        ir::Type* instrType = nullptr;
        if (currentToken.type == TokenType::Colon) { getNextToken(); instrType = parseIRType(); }

        if (opcodeStr == "add") instr = builder.createAdd(lhs, rhs, instrType);
        else if (opcodeStr == "sub") instr = builder.createSub(lhs, rhs, instrType);
        else if (opcodeStr == "mul") instr = builder.createMul(lhs, rhs, instrType);
        else if (opcodeStr == "div") instr = builder.createDiv(lhs, rhs, instrType);
        else if (opcodeStr == "udiv") instr = builder.createUdiv(lhs, rhs, instrType);
        else if (opcodeStr == "rem") instr = builder.createRem(lhs, rhs, instrType);
        else if (opcodeStr == "urem") instr = builder.createUrem(lhs, rhs, instrType);
        else if (opcodeStr == "and") instr = builder.createAnd(lhs, rhs, instrType);
        else if (opcodeStr == "or") instr = builder.createOr(lhs, rhs, instrType);
        else if (opcodeStr == "xor") instr = builder.createXor(lhs, rhs, instrType);
        else if (opcodeStr == "shl") instr = builder.createShl(lhs, rhs, instrType);
        else if (opcodeStr == "shr") instr = builder.createShr(lhs, rhs, instrType);
        else if (opcodeStr == "sar") instr = builder.createSar(lhs, rhs, instrType);
        else if (opcodeStr == "copy") instr = builder.createCopy(lhs, instrType);
        else if (opcodeStr == "neg") instr = builder.createNeg(lhs);
        else if (opcodeStr == "load") instr = builder.createLoad(lhs);
        else if (opcodeStr == "loadl") instr = builder.createLoadl(lhs);
        else if (opcodeStr == "loads") instr = builder.createLoads(lhs);
        else if (opcodeStr == "loadd") instr = builder.createLoadd(lhs);
        else if (opcodeStr == "loaduw") instr = builder.createLoaduw(lhs);
        else if (opcodeStr == "loadsh") instr = builder.createLoadsh(lhs);
        else if (opcodeStr == "loaduh") instr = builder.createLoaduh(lhs);
        else if (opcodeStr == "loadsb") instr = builder.createLoadsb(lhs);
        else if (opcodeStr == "loadub") instr = builder.createLoadub(lhs);
        else if (opcodeStr == "alloc") instr = builder.createAlloc(lhs, instrType ? instrType : context->getIntegerType(8));
        else if (opcodeStr == "fadd") instr = builder.createFAdd(lhs, rhs);
        else if (opcodeStr == "fsub") instr = builder.createFSub(lhs, rhs);
        else if (opcodeStr == "fmul") instr = builder.createFMul(lhs, rhs);
        else if (opcodeStr == "fdiv") instr = builder.createFDiv(lhs, rhs);
        else if (opcodeStr == "eq") instr = (lhs->getType()->isFloatingPoint() ? builder.createCeqf(lhs, rhs) : builder.createCeq(lhs, rhs));
        else if (opcodeStr == "ne") instr = (lhs->getType()->isFloatingPoint() ? builder.createCnef(lhs, rhs) : builder.createCne(lhs, rhs));
        else if (opcodeStr == "slt") instr = builder.createCslt(lhs, rhs);
        else if (opcodeStr == "sle") instr = builder.createCsle(lhs, rhs);
        else if (opcodeStr == "sgt") instr = builder.createCsgt(lhs, rhs);
        else if (opcodeStr == "sge") instr = builder.createCsge(lhs, rhs);
        else if (opcodeStr == "ult") instr = builder.createCult(lhs, rhs);
        else if (opcodeStr == "ule") instr = builder.createCule(lhs, rhs);
        else if (opcodeStr == "ugt") instr = builder.createCugt(lhs, rhs);
        else if (opcodeStr == "uge") instr = builder.createCuge(lhs, rhs);
        else if (opcodeStr == "lt") instr = (lhs->getType()->isFloatingPoint() ? builder.createClt(lhs, rhs) : builder.createCslt(lhs, rhs));
        else if (opcodeStr == "le") instr = (lhs->getType()->isFloatingPoint() ? builder.createCle(lhs, rhs) : builder.createCsle(lhs, rhs));
        else if (opcodeStr == "gt") instr = (lhs->getType()->isFloatingPoint() ? builder.createCgt(lhs, rhs) : builder.createCsgt(lhs, rhs));
        else if (opcodeStr == "ge") instr = (lhs->getType()->isFloatingPoint() ? builder.createCge(lhs, rhs) : builder.createCsge(lhs, rhs));
        else if (opcodeStr == "co") instr = builder.createCo(lhs, rhs);
        else if (opcodeStr == "cuo") instr = builder.createCuo(lhs, rhs);

        if (instr && instrType && isLoadOp) {
            instr->setType(instrType);
        }
    } else if (isCastOp) {
        ir::Value* val = parseValue();
        ir::Type* destTy = nullptr;
        if (currentToken.type == TokenType::Colon) {
            getNextToken();
            destTy = parseIRType();
        } else {
            destTy = context->getVoidType();
        }
        if (opcodeStr == "extub") instr = builder.createExtUB(val, destTy);
        else if (opcodeStr == "extuh") instr = builder.createExtUH(val, destTy);
        else if (opcodeStr == "extuw") instr = builder.createExtUW(val, destTy);
        else if (opcodeStr == "extsb") instr = builder.createExtSB(val, destTy);
        else if (opcodeStr == "extsh") instr = builder.createExtSH(val, destTy);
        else if (opcodeStr == "extsw") instr = builder.createExtSW(val, destTy);
        else if (opcodeStr == "exts") instr = builder.createExtS(val, destTy);
        else if (opcodeStr == "truncd") instr = builder.createTruncD(val, destTy);
        else if (opcodeStr == "swtof") instr = builder.createSWtoF(val, destTy);
        else if (opcodeStr == "uwtof") instr = builder.createUWtoF(val, destTy);
        else if (opcodeStr == "sltof") instr = builder.createSltof(val, destTy);
        else if (opcodeStr == "ultof") instr = builder.createUltof(val, destTy);
        else if (opcodeStr == "dtosi") instr = builder.createDToSI(val, destTy);
        else if (opcodeStr == "dtoui") instr = builder.createDToUI(val, destTy);
        else if (opcodeStr == "stosi") instr = builder.createSToSI(val, destTy);
        else if (opcodeStr == "stoui") instr = builder.createSToUI(val, destTy);
        else if (opcodeStr == "cast") instr = builder.createCast(val, destTy);
    } else if (opcodeStr == "call") {
        instr = parseCallInstruction(nullptr);
    } else if (opcodeStr == "extern") {
        std::string capability = currentToken.value; getNextToken();
        std::vector<ir::Value*> args;
        if (currentToken.type == TokenType::LParen) {
            getNextToken();
            while (currentToken.type != TokenType::RParen && currentToken.type != TokenType::Eof) {
                args.push_back(parseValue());
                if (currentToken.type == TokenType::Comma) getNextToken();
                else break;
            }
            if (currentToken.type == TokenType::RParen) getNextToken();
        }
        ir::Type* retTy = (currentToken.type == TokenType::Colon) ? (getNextToken(), parseIRType()) : context->getVoidType();
        instr = builder.createExternCall(capability, args, retTy);
    } else if (opcodeStr == "ret") {
        ir::Value* v = (currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Label && currentToken.type != TokenType::Eof) ? parseValue() : nullptr;
        instr = builder.createRet(v);
    } else if (opcodeStr == "jmp") {
        instr = builder.createJmp(parseValue());
    } else if (opcodeStr == "jnz") {
        ir::Value* c = parseValue(); if (currentToken.type == TokenType::Comma) getNextToken();
        ir::Value* t = parseValue(); if (currentToken.type == TokenType::Comma) getNextToken();
        ir::Value* f = parseValue();
        instr = builder.createJnz(c, t, f);
    } else if (opcodeStr == "store") {
        ir::Value* v = parseValue(); if (currentToken.type == TokenType::Comma) getNextToken();
        ir::Value* p = parseValue();
        instr = builder.createStore(v, p);
    } else if (opcodeStr == "phi") {
        std::vector<std::pair<std::string, ir::Value*>> incoming;
        while(currentToken.type == TokenType::Label) {
            std::string lbl = currentToken.value; getNextToken();
            ir::Value* v = parseValue(); incoming.push_back({lbl, v});
            if (currentToken.type == TokenType::Comma) getNextToken(); else break;
        }
        ir::Type* ty = (currentToken.type == TokenType::Colon) ? (getNextToken(), parseIRType()) : nullptr;
        ir::PhiNode* phi = builder.createPhi(ty, incoming.size(), nullptr);
        for (auto& p : incoming) {
            ir::Value* v = p.second;
            ir::BasicBlock* bbPhi = labelMap.count(p.first) ? labelMap[p.first] : builder.createBasicBlock(p.first, builder.getInsertPoint()->getParent());
            labelMap[p.first] = bbPhi;
            phi->addIncoming(v, bbPhi);
        }
        instr = phi;
    } else if (opcodeStr == "vaarg") {
        ir::Value* ap = parseValue();
        consume(TokenType::Colon, "Expected : for vaarg type");
        instr = builder.createVAArg(ap, parseIRType());
    } else if (opcodeStr == "blit") {
        ir::Value* dst = parseValue(); if (currentToken.type == TokenType::Comma) getNextToken();
        ir::Value* src = parseValue(); if (currentToken.type == TokenType::Comma) getNextToken();
        ir::Value* count = parseValue();
        instr = builder.createBlit(dst, src, count);
    } else if (opcodeStr == "vastart") {
        instr = builder.createVAStart(parseValue());
    } else if (opcodeStr == "hlt") {
        instr = builder.createHlt();
    }

    if (currentToken.type == TokenType::Colon) {
        getNextToken();
        parseIRType();
    }

    if (instr && !destName.empty()) {
        instr->setName(destName);
        if (auto it = placeholders.find(destName); it != placeholders.end()) {
            ir::Value* ph = it->second;
            ph->replaceAllUsesWith(instr);
            placeholders.erase(it);
        }
        valueMap[destName] = instr;
    }
    return instr;
}

void Parser::parseBasicBlock(ir::Function* func) {
    std::string labelName = currentToken.value; getNextToken();
    ir::BasicBlock* bb = nullptr;
    if (labelMap.count(labelName)) {
        bb = labelMap[labelName];
        func->moveBasicBlockToBack(bb);
    } else {
        bb = builder.createBasicBlock(labelName, func);
        labelMap[labelName] = bb;
    }
    builder.setInsertPoint(bb);
    while (currentToken.type != TokenType::Label && currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Eof) {
        if (parseInstruction(bb) == nullptr) {
             if (currentToken.type == TokenType::Eof || currentToken.type == TokenType::RCurly || currentToken.type == TokenType::Label) break;
             getNextToken();
        }
    }
}

ir::Instruction* Parser::parseCallInstruction(ir::Type* retType) {
    ir::Value* callee = parseValue();
    std::vector<ir::Value*> args;
    if (currentToken.type == TokenType::LParen) {
        getNextToken();
        while (currentToken.type != TokenType::RParen && currentToken.type != TokenType::Eof) {
            args.push_back(parseValue());
            if (currentToken.type == TokenType::Comma) getNextToken();
            else break;
        }
        if (currentToken.type == TokenType::RParen) getNextToken();
    }
    if (currentToken.type == TokenType::Colon) { getNextToken(); retType = parseIRType(); }
    return builder.createCall(callee, args, retType);
}

ir::Value* Parser::parseValue() {
    if (currentToken.type == TokenType::Keyword) {
        std::string k = currentToken.value;
        if (k == "w" || k == "l" || k == "s" || k == "d") {
            ir::Type* ty = parseIRType();
            ir::Value* val = parseValue();
            if (!val) return nullptr;
            if (auto* ci = dynamic_cast<ir::ConstantInt*>(val)) return context->getConstantInt(static_cast<ir::IntegerType*>(ty), ci->getValue());
            if (auto* cf = dynamic_cast<ir::ConstantFP*>(val)) return context->getConstantFP(ty, cf->getValue());
            return val;
        }
    }
    if (currentToken.type == TokenType::Number) {
        long long v = std::stoll(currentToken.value, nullptr, 0);
        getNextToken();
        return context->getConstantInt(context->getIntegerType(32), v);
    }
    if (currentToken.type == TokenType::Temporary) {
        std::string n = currentToken.value; getNextToken();
        if (valueMap.count(n)) {
            return valueMap[n];
        }
        if (placeholders.count(n)) {
            return placeholders[n];
        }
        ir::Value* ph = new ir::Value(context->getVoidType());
        ph->setName(n);
        placeholders[n] = ph;
        valueMap[n] = ph;
        return ph;
    }
    if (currentToken.type == TokenType::Global) {
        std::string n = currentToken.value; getNextToken();
        for (auto& gv : module->getGlobalVariables()) if (gv->getName() == n) return gv.get();
        for (auto& f : module->getFunctions()) if (f->getName() == n) return f.get();
        return new ir::GlobalValue(context->getVoidType(), n);
    }
    if (currentToken.type == TokenType::Label) {
        std::string n = currentToken.value; getNextToken();
        return labelMap.count(n) ? labelMap[n] : (labelMap[n] = builder.createBasicBlock(n, builder.getInsertPoint()->getParent()));
    }
    if (currentToken.type == TokenType::StringLiteral) {
        std::string s = currentToken.value; getNextToken();
        return context->getConstantString(s);
    }
    if (currentToken.type == TokenType::FloatLiteral) {
        std::string val = currentToken.value;
        ir::Type* ty = (val[0] == 's') ? (ir::Type*)context->getFloatType() : (ir::Type*)context->getDoubleType();
        double d = std::stod(val.substr(2));
        getNextToken();
        return context->getConstantFP(ty, d);
    }
    return nullptr;
}

ir::Type* Parser::parseIRType() {
    if (currentToken.type == TokenType::LCurly) {
        getNextToken();
        std::vector<ir::Type*> elements;
        while (currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Eof) {
            elements.push_back(parseIRType());
            if (currentToken.type == TokenType::Comma) getNextToken();
        }
        if (currentToken.type == TokenType::RCurly) getNextToken();
        return context->getStructTypeFromElements(elements);
    }
    if (currentToken.type == TokenType::Keyword) {
        std::string v = currentToken.value; getNextToken();
        if (v == "w") return context->getIntegerType(32);
        if (v == "l") return context->getIntegerType(64);
        if (v == "s") return context->getFloatType();
        if (v == "d") return context->getDoubleType();
    }
    return context->getVoidType();
}

void Parser::parseData() {
    getNextToken(); if (currentToken.type != TokenType::Global) return;
    std::string name = currentToken.value; getNextToken(); consume(TokenType::Equal, "Expected =");
    consume(TokenType::LCurly, "Expected {");
    std::vector<ir::Constant*> constants;
    while (currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Eof) {
        if (currentToken.type == TokenType::Keyword) {
            std::string typeStr = currentToken.value; getNextToken();
            if (typeStr == "b") { if (currentToken.type == TokenType::StringLiteral) { constants.push_back(context->getConstantString(currentToken.value)); getNextToken(); } else if (currentToken.type == TokenType::Number) { constants.push_back(context->getConstantInt(context->getIntegerType(8), std::stoll(currentToken.value, nullptr, 0))); getNextToken(); } }
            else if (typeStr == "w") { if (currentToken.type == TokenType::Number) { constants.push_back(context->getConstantInt(context->getIntegerType(32), std::stoll(currentToken.value, nullptr, 0))); getNextToken(); } }
                else if (typeStr == "l") { if (currentToken.type == TokenType::Number) { constants.push_back(context->getConstantInt(context->getIntegerType(64), std::stoll(currentToken.value, nullptr, 0))); getNextToken(); } }
            if (currentToken.type == TokenType::Comma) getNextToken();
        } else getNextToken();
    }
    getNextToken();
    auto* arrayType = context->getArrayType(context->getIntegerType(8), constants.size());
    auto* initializer = ir::ConstantArray::get(static_cast<ir::ArrayType*>(arrayType), constants);
    module->addGlobalVariable(std::make_unique<ir::GlobalVariable>(arrayType, name, initializer, false, ""));
}

void Parser::parseType() {
    getNextToken(); if (currentToken.type != TokenType::Type && currentToken.type != TokenType::Global) return;
    std::string typeName = currentToken.value; getNextToken(); consume(TokenType::Equal, "Expected =");
    if (currentToken.type == TokenType::Keyword && currentToken.value == "union") {
        getNextToken();
        if (currentToken.type != TokenType::LCurly) return;
        getNextToken();
        std::vector<ir::Type*> members;
        while (currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Eof) {
            members.push_back(parseIRType());
            if (currentToken.type == TokenType::Comma) getNextToken();
            else break;
        }
        getNextToken();
        auto* unionTy = context->createUnionType(typeName);
        module->addType(typeName, unionTy);
        for (auto* m : members) unionTy->addMember(m);
    } else {
        std::vector<ir::Type*> elements;
        if (currentToken.type == TokenType::LCurly) {
            getNextToken();
            while (currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Eof) {
                elements.push_back(parseIRType());
                if (currentToken.type == TokenType::Comma) getNextToken();
                else break;
            }
            getNextToken();
        }
        auto* structTy = context->getStructTypeFromElements(elements);
        module->addType(typeName, structTy);
    }
}

void Parser::consume(TokenType type, const std::string& message) {
    if (currentToken.type != type) throw std::runtime_error(message);
    getNextToken();
}

void Parser::getNextToken() { currentToken = lexer.getNextToken(); }
bool Parser::match(TokenType type) { if (currentToken.type == type) { getNextToken(); return true; } return false; }

} // namespace parser
