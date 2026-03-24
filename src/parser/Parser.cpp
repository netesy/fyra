#include "parser/Parser.h"
#include "ir/Type.h"
#include "ir/Constant.h"
#include "ir/PhiNode.h"
#include "ir/GlobalValue.h"
#include "ir/Function.h"
#include "ir/Parameter.h"
#include <iostream>

namespace parser {

Parser::Parser(std::istream& input) : lexer(input), context(std::make_shared<ir::IRContext>()), builder(context), fileFormat(FileFormat::FYRA) {
    getNextToken(); // Prime the first token
}

Parser::Parser(std::istream& input, FileFormat format) : lexer(input), context(std::make_shared<ir::IRContext>()), builder(context), fileFormat(format) {
    getNextToken(); // Prime the first token
}

void Parser::getNextToken() {
    currentToken = lexer.getNextToken();
}

std::unique_ptr<ir::Module> Parser::parseModule() {
    module = std::make_unique<ir::Module>("my_module", context);
    builder.setModule(module.get());

    while (currentToken.type != TokenType::Eof) {
        if (currentToken.type == TokenType::Keyword) {
            if (currentToken.value == "export" || currentToken.value == "function") {
                parseFunction();
            } else if (currentToken.value == "type") {
                parseType();
            } else if (currentToken.value == "data") {
                parseData();
            } else {
                std::cerr << "Skipping unknown top-level token: " << currentToken.value << std::endl;
                getNextToken();
            }
        } else {
            std::cerr << "Skipping unknown top-level token: " << currentToken.value << std::endl;
            getNextToken();
        }
    }
    return std::move(module);
}

void Parser::parseFunction() {
    bool isExported = false;
    if (currentToken.value == "export") {
        isExported = true;
        getNextToken();
    }

    if (currentToken.type != TokenType::Keyword || currentToken.value != "function") return;
    getNextToken();

    if (currentToken.type != TokenType::Global) {
        return;
    }
    std::string funcName = currentToken.value;
    getNextToken();

    valueMap.clear();
    labelMap.clear();

    if (currentToken.type != TokenType::LParen) return;
    getNextToken();

    std::vector<ir::Type*> paramTypes;
    std::vector<std::string> paramNames;
    bool isVariadic = false;

    while (currentToken.type != TokenType::RParen && currentToken.type != TokenType::Eof) {
        if (currentToken.type == TokenType::Ellipsis) {
            isVariadic = true;
            getNextToken();
            break;
        }

        if (currentToken.type != TokenType::Temporary) break;
        std::string paramName = currentToken.value;
        paramNames.push_back(paramName);
        getNextToken();

        if (currentToken.type != TokenType::Colon) break;
        getNextToken();
        ir::Type* paramType = parseIRType();
        paramTypes.push_back(paramType);

        if (currentToken.type == TokenType::Comma) {
            getNextToken();
        } else {
            break;
        }
    }

    if (currentToken.type != TokenType::RParen) return;
    getNextToken();

    ir::Type* returnType = context->getVoidType();
    if (currentToken.type == TokenType::Colon) {
        getNextToken();
        returnType = parseIRType();
    }

    auto* func = builder.createFunction(funcName, returnType, paramTypes, isVariadic);
    func->setExported(isExported);

    auto& params = func->getParameters();
    auto paramIt = params.begin();
    for (size_t i = 0; i < paramNames.size(); ++i) {
        if (paramIt != params.end()) {
            (*paramIt)->setName(paramNames[i]);
            valueMap[paramNames[i]] = paramIt->get();
            ++paramIt;
        }
    }

    if (currentToken.type != TokenType::LCurly) return;
    getNextToken();

    if (currentToken.type == TokenType::Label) {
        while (currentToken.type == TokenType::Label) {
            parseBasicBlock(func);
        }
    } else {
        ir::BasicBlock* bb = builder.createBasicBlock("entry", func);
        builder.setInsertPoint(bb);
        while (currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Eof) {
            if (parseInstruction(bb) == nullptr) {
                getNextToken();
            }
        }
    }

    if (currentToken.type != TokenType::RCurly) return;
    getNextToken();
}

void Parser::parseData() {
    getNextToken(); // consume 'data'

    if (currentToken.type != TokenType::Global) return;
    std::string name = currentToken.value;
    getNextToken();

    if (currentToken.type != TokenType::Equal) return;
    getNextToken();

    if (currentToken.type != TokenType::LCurly) return;
    getNextToken();

    std::vector<ir::Constant*> constants;
    while (currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Eof) {
        if (currentToken.type == TokenType::Keyword) {
            std::string typeStr = currentToken.value;
            getNextToken();
            if (typeStr == "b") {
                if (currentToken.type == TokenType::StringLiteral) {
                    constants.push_back(context->getConstantString(currentToken.value));
                    getNextToken();
                } else if (currentToken.type == TokenType::Number) {
                    constants.push_back(context->getConstantInt(context->getIntegerType(8), std::stoll(currentToken.value, nullptr, 0)));
                    getNextToken();
                }
            } else if (typeStr == "w") {
                if (currentToken.type == TokenType::Number) {
                    constants.push_back(context->getConstantInt(context->getIntegerType(32), std::stoll(currentToken.value, nullptr, 0)));
                    getNextToken();
                }
            } else if (typeStr == "l") {
                if (currentToken.type == TokenType::Number) {
                    constants.push_back(context->getConstantInt(context->getIntegerType(64), std::stoll(currentToken.value, nullptr, 0)));
                    getNextToken();
                }
            } else if (typeStr == "h") {
                if (currentToken.type == TokenType::Number) {
                    constants.push_back(context->getConstantInt(context->getIntegerType(16), std::stoll(currentToken.value, nullptr, 0)));
                    getNextToken();
                }
            }
        } else if (currentToken.type == TokenType::Comma) {
            getNextToken();
        } else {
            getNextToken();
        }
    }
    getNextToken(); // consume '}'

    // This is a simplification. We should create a proper array type.
    auto* arrayType = context->getArrayType(context->getIntegerType(8), 0);
    auto* initializer = ir::ConstantArray::get(arrayType, constants);
    module->addGlobalVariable(std::make_unique<ir::GlobalVariable>(arrayType, name, initializer, false, ""));
}

void Parser::parseBasicBlock(ir::Function* func) {
    std::string labelName = currentToken.value;
    getNextToken(); // consume label

    ir::BasicBlock* bb = nullptr;
    if (labelMap.count(labelName)) {
        bb = labelMap[labelName];
    } else {
        bb = builder.createBasicBlock(labelName, func);
        labelMap[labelName] = bb;
    }
    builder.setInsertPoint(bb);

    while (currentToken.type != TokenType::Label && currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Eof) {
        if (parseInstruction(bb) == nullptr) {
            getNextToken();
        }
    }
}

ir::Instruction* Parser::parseInstruction(ir::BasicBlock* bb) {
    if (currentToken.type == TokenType::Temporary) {
        std::string destName = currentToken.value;
        getNextToken();

        if (currentToken.type != TokenType::Equal) return nullptr;
        getNextToken();

        // Parse instruction opcode
        if (currentToken.type != TokenType::Keyword) return nullptr;
        std::string opcodeStr = currentToken.value;
        getNextToken();

        ir::Instruction* instr = nullptr;
        
        // Parse operands first, then look for colon type syntax
        if (opcodeStr == "add" || opcodeStr == "sub" || opcodeStr == "mul" || opcodeStr == "div" ||
            opcodeStr == "udiv" || opcodeStr == "rem" || opcodeStr == "urem" ||
            opcodeStr == "and" || opcodeStr == "or" || opcodeStr == "xor" ||
            opcodeStr == "shl" || opcodeStr == "shr" || opcodeStr == "sar" ||
            opcodeStr == "eq" || opcodeStr == "ne" ||
            opcodeStr == "slt" || opcodeStr == "sle" || opcodeStr == "sgt" || opcodeStr == "sge" ||
            opcodeStr == "ult" || opcodeStr == "ule" || opcodeStr == "ugt" || opcodeStr == "uge" ||
            opcodeStr == "fadd" || opcodeStr == "fsub" || opcodeStr == "fmul" || opcodeStr == "fdiv" ||
            opcodeStr == "lt" || opcodeStr == "le" || opcodeStr == "gt" || opcodeStr == "ge" ||
            opcodeStr == "co" || opcodeStr == "cuo" || opcodeStr == "cmp") {
            
            // Check if this is a QBE-style comparison: cmp <op> %lhs, %rhs
            if (opcodeStr == "cmp") {
                if (currentToken.type != TokenType::Keyword) return nullptr;
                opcodeStr = currentToken.value;
                getNextToken();
            }

            ir::Value* lhs = parseValue();
            if (currentToken.type != TokenType::Comma) return nullptr;
            getNextToken(); // consume ,
            ir::Value* rhs = parseValue();
            
            // Parse type with colon syntax
            ir::Type* instrType = nullptr;
            if (currentToken.type == TokenType::Colon) {
                getNextToken(); // consume ':'
                instrType = parseIRType();
            }

            // Update constant types if explicit type is provided
            if (instrType && instrType->isIntegerTy()) {
                if (auto* ci = dynamic_cast<ir::ConstantInt*>(lhs)) {
                    lhs = context->getConstantInt(static_cast<ir::IntegerType*>(instrType), ci->getValue());
                }
                if (auto* ci = dynamic_cast<ir::ConstantInt*>(rhs)) {
                    rhs = context->getConstantInt(static_cast<ir::IntegerType*>(instrType), ci->getValue());
                }
            }
            
            // Handle arithmetic and comparison operations
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
            else if (opcodeStr == "eq") {
                if (lhs && (lhs->getType()->isFloatTy() || lhs->getType()->isDoubleTy())) instr = builder.createCeqf(lhs, rhs);
                else instr = builder.createCeq(lhs, rhs);
            } else if (opcodeStr == "ne") {
                if (lhs && (lhs->getType()->isFloatTy() || lhs->getType()->isDoubleTy())) instr = builder.createCnef(lhs, rhs);
                else instr = builder.createCne(lhs, rhs);
            } else if (opcodeStr == "slt") instr = builder.createCslt(lhs, rhs);
            else if (opcodeStr == "sle") instr = builder.createCsle(lhs, rhs);
            else if (opcodeStr == "sgt") instr = builder.createCsgt(lhs, rhs);
            else if (opcodeStr == "sge") instr = builder.createCsge(lhs, rhs);
            else if (opcodeStr == "ult") instr = builder.createCult(lhs, rhs);
            else if (opcodeStr == "ule") instr = builder.createCule(lhs, rhs);
            else if (opcodeStr == "ugt") instr = builder.createCugt(lhs, rhs);
            else if (opcodeStr == "uge") instr = builder.createCuge(lhs, rhs);
            else if (opcodeStr == "fadd") instr = builder.createFAdd(lhs, rhs);
            else if (opcodeStr == "fsub") instr = builder.createFSub(lhs, rhs);
            else if (opcodeStr == "fmul") instr = builder.createFMul(lhs, rhs);
            else if (opcodeStr == "fdiv") instr = builder.createFDiv(lhs, rhs);
            else if (opcodeStr == "lt") {
                if (lhs && (lhs->getType()->isFloatTy() || lhs->getType()->isDoubleTy())) instr = builder.createClt(lhs, rhs);
                else instr = builder.createCslt(lhs, rhs);
            } else if (opcodeStr == "le") {
                if (lhs && (lhs->getType()->isFloatTy() || lhs->getType()->isDoubleTy())) instr = builder.createCle(lhs, rhs);
                else instr = builder.createCsle(lhs, rhs);
            } else if (opcodeStr == "gt") {
                if (lhs && (lhs->getType()->isFloatTy() || lhs->getType()->isDoubleTy())) instr = builder.createCgt(lhs, rhs);
                else instr = builder.createCsgt(lhs, rhs);
            } else if (opcodeStr == "ge") {
                if (lhs && (lhs->getType()->isFloatTy() || lhs->getType()->isDoubleTy())) instr = builder.createCge(lhs, rhs);
                else instr = builder.createCsge(lhs, rhs);
            } else if (opcodeStr == "co") instr = builder.createCo(lhs, rhs);
            else if (opcodeStr == "cuo") instr = builder.createCuo(lhs, rhs);

        } else if (opcodeStr == "phi") {
            ir::Type* phiType = nullptr;
            std::vector<std::pair<ir::BasicBlock*, ir::Value*>> incoming;
            while(currentToken.type == TokenType::Label) {
                std::string labelName = currentToken.value;
                getNextToken();

                ir::Value* val = parseValue();

                ir::BasicBlock* bb = nullptr;
                if (labelMap.count(labelName)) {
                    bb = labelMap[labelName];
                } else {
                    auto* func = builder.getInsertPoint()->getParent();
                    bb = builder.createBasicBlock(labelName, func);
                    labelMap[labelName] = bb;
                }
                incoming.push_back({bb, val});

                if (currentToken.type == TokenType::Comma) {
                    getNextToken();
                } else {
                    break;
                }
            }

            if (currentToken.type == TokenType::Colon) {
                getNextToken();
                phiType = parseIRType();
            }

            ir::PhiNode* phiNode = builder.createPhi(phiType, incoming.size(), nullptr);
            for (auto& pair : incoming) {
                ir::Value* val = pair.second;
                if (phiType && phiType->isIntegerTy()) {
                    if (auto* ci = dynamic_cast<ir::ConstantInt*>(val)) {
                        val = context->getConstantInt(static_cast<ir::IntegerType*>(phiType), ci->getValue());
                    }
                }
                phiNode->addIncoming(val, pair.first);
            }
            instr = phiNode;
        }
        else if (opcodeStr == "neg") {
            ir::Value* op = parseValue();
            if (!op) return nullptr;
            
            // Parse type with colon syntax
            ir::Type* instrType = nullptr;
            if (currentToken.type == TokenType::Colon) {
                getNextToken(); // consume ':'
                instrType = parseIRType();
            }
            
            instr = builder.createNeg(op);
        } else if (opcodeStr == "copy") {
            ir::Value* op = parseValue();
            if (!op) return nullptr;
            
            // Parse type with colon syntax
            ir::Type* instrType = nullptr;
            if (currentToken.type == TokenType::Colon) {
                getNextToken(); // consume ':'
                instrType = parseIRType();
            }
            
            instr = instrType ? builder.createCopy(op, instrType) : builder.createCopy(op);
        } else if (opcodeStr == "alloc4" || opcodeStr == "alloc8" || opcodeStr == "alloc16" || opcodeStr == "alloc") {
            // Fyra memory allocation with optional size
            ir::Value* size = nullptr;
            if (currentToken.type == TokenType::Number || currentToken.type == TokenType::Temporary) {
                size = parseValue();
            }
            
            // Parse type with colon syntax
            ir::Type* instrType = nullptr;
            if (currentToken.type == TokenType::Colon) {
                getNextToken(); // consume ':'
                instrType = parseIRType();
            }
            
            if (opcodeStr == "alloc4") instr = builder.createAlloc4(instrType);
            else if (opcodeStr == "alloc8") instr = builder.createAlloc(size, instrType);
            else if (opcodeStr == "alloc16") instr = builder.createAlloc16(instrType);
            else instr = builder.createAlloc(size, instrType); // default alloc
        } else if (opcodeStr == "load") {
            ir::Value* ptr = parseValue();
            ir::Type* instrType = nullptr;
            if (currentToken.type == TokenType::Colon) {
                getNextToken();
                instrType = parseIRType();
            } else {
                // Type is mandatory for load in Fyra
                return nullptr;
            }

            if (instrType->isIntegerTy() && static_cast<ir::IntegerType*>(instrType)->getBitwidth() == 64) instr = builder.createLoadl(ptr);
            else if (instrType->isFloatTy()) instr = builder.createLoads(ptr);
            else if (instrType->isDoubleTy()) instr = builder.createLoadd(ptr);
            else instr = builder.createLoad(ptr); // Default to word size load

        } else if (opcodeStr == "loadub") {
            ir::Value* ptr = parseValue();
            if (currentToken.type == TokenType::Colon) {
                getNextToken(); // consume ':'
                parseIRType(); // consume type, not used by builder
            }
            instr = builder.createLoadub(ptr);
        } else if (opcodeStr == "loadsb") {
            ir::Value* ptr = parseValue();
            if (currentToken.type == TokenType::Colon) {
                getNextToken();
                parseIRType();
            }
            instr = builder.createLoadsb(ptr);
        } else if (opcodeStr == "loaduh") {
            ir::Value* ptr = parseValue();
            if (currentToken.type == TokenType::Colon) {
                getNextToken();
                parseIRType();
            }
            instr = builder.createLoaduh(ptr);
        } else if (opcodeStr == "loadsh") {
            ir::Value* ptr = parseValue();
            if (currentToken.type == TokenType::Colon) {
                getNextToken();
                parseIRType();
            }
            instr = builder.createLoadsh(ptr);
        } else if (opcodeStr == "loaduw") {
            ir::Value* ptr = parseValue();
            if (currentToken.type == TokenType::Colon) {
                getNextToken();
                parseIRType();
            }
            instr = builder.createLoaduw(ptr);
        } else if (opcodeStr == "extub" || opcodeStr == "extuh" || opcodeStr == "extuw" ||
                   opcodeStr == "extsb" || opcodeStr == "extsh" || opcodeStr == "extsw" ||
                   opcodeStr == "exts" || opcodeStr == "truncd" || opcodeStr == "swtof" ||
                   opcodeStr == "uwtof" || opcodeStr == "dtosi" || opcodeStr == "dtoui" ||
                   opcodeStr == "stosi" || opcodeStr == "stoui" || opcodeStr == "sltof" ||
                   opcodeStr == "ultof" || opcodeStr == "cast") {
            ir::Value* val = parseValue();
            
            // Parse colon and type conversion syntax: : srctype -> desttype
            ir::Type* srcType = nullptr;
            ir::Type* destType = nullptr;
            if (currentToken.type == TokenType::Colon) {
                getNextToken(); // consume ':'
                srcType = parseIRType();
                // Look for -> arrow for type conversion
                // For now, we'll use the instruction opcode to determine the conversion
                // This is a simplified approach - a full implementation would parse the arrow syntax
            }
            
            // For conversion instructions, we need a destination type
            // Using a default approach for now
            if (!destType) {
                if (opcodeStr == "extub" || opcodeStr == "extuh" || opcodeStr == "extsb" || opcodeStr == "extsh") {
                    destType = context->getIntegerType(32); // extend to word
                } else if (opcodeStr == "extuw" || opcodeStr == "extsw") {
                    destType = context->getIntegerType(64); // extend to long
                } else if (opcodeStr == "exts") {
                    destType = context->getDoubleType(); // float to double
                } else if (opcodeStr == "truncd") {
                    destType = context->getFloatType(); // double to float
                } else if (opcodeStr == "swtof" || opcodeStr == "uwtof") {
                    destType = context->getFloatType(); // int to float
                } else if (opcodeStr == "sltof" || opcodeStr == "ultof") {
                    destType = context->getFloatType(); // long to float
                } else if (opcodeStr == "dtosi" || opcodeStr == "dtoui" || opcodeStr == "stosi" || opcodeStr == "stoui") {
                    destType = context->getIntegerType(32); // float/double to int
                } else {
                    destType = srcType; // for cast, keep same type
                }
            }
            
            if (opcodeStr == "extub") instr = builder.createExtUB(val, destType);
            else if (opcodeStr == "extuh") instr = builder.createExtUH(val, destType);
            else if (opcodeStr == "extuw") instr = builder.createExtUW(val, destType);
            else if (opcodeStr == "extsb") instr = builder.createExtSB(val, destType);
            else if (opcodeStr == "extsh") instr = builder.createExtSH(val, destType);
            else if (opcodeStr == "extsw") instr = builder.createExtSW(val, destType);
            else if (opcodeStr == "exts") instr = builder.createExtS(val, destType);
            else if (opcodeStr == "truncd") instr = builder.createTruncD(val, destType);
            else if (opcodeStr == "swtof") instr = builder.createSWtoF(val, destType);
            else if (opcodeStr == "uwtof") instr = builder.createUWtoF(val, destType);
            else if (opcodeStr == "dtosi") instr = builder.createDToSI(val, destType);
            else if (opcodeStr == "dtoui") instr = builder.createDToUI(val, destType);
            else if (opcodeStr == "stosi") instr = builder.createSToSI(val, destType);
            else if (opcodeStr == "stoui") instr = builder.createSToUI(val, destType);
            else if (opcodeStr == "sltof") instr = builder.createSltof(val, destType);
            else if (opcodeStr == "ultof") instr = builder.createUltof(val, destType);
            else if (opcodeStr == "cast") instr = builder.createCast(val, destType);
        } else if (opcodeStr == "vaarg") {
            ir::Value* val = parseValue();
            
            // Parse type with colon syntax
            ir::Type* instrType = nullptr;
            if (currentToken.type == TokenType::Colon) {
                getNextToken(); // consume ':'
                instrType = parseIRType();
            }
            
            instr = builder.createVAArg(val, instrType);
        } else if (opcodeStr == "vastart") {
            ir::Value* val = parseValue();
            instr = builder.createVAStart(val);
        } else if (opcodeStr == "syscall") {
            ir::SyscallId id = ir::SyscallId::None;
            std::vector<ir::Value*> args;
            if (currentToken.type == TokenType::LParen) {
                getNextToken();

                // Check if the first thing in parens is a syscall name
                if (currentToken.type == TokenType::Keyword && currentToken.value.find("sys_") == 0) {
                    id = ir::stringToSyscallId(currentToken.value);
                    getNextToken();
                    if (currentToken.type == TokenType::Comma) getNextToken();
                }

                while (currentToken.type != TokenType::RParen && currentToken.type != TokenType::Eof) {
                    ir::Type* argType = parseIRType();
                    ir::Value* arg = parseValue();
                    args.push_back(arg);
                    if (currentToken.type == TokenType::Comma) getNextToken();
                    else break;
                }
                getNextToken();
            }
            ir::Type* retType = context->getVoidType();
            if (currentToken.type == TokenType::Colon) {
                getNextToken();
                retType = parseIRType();
            }
            if (id != ir::SyscallId::None) {
                instr = builder.createSyscall(id, args, retType);
            } else {
                instr = builder.createSyscall(args, retType);
            }
        } else if (opcodeStr == "call") {
            instr = parseCallInstruction(nullptr);
        }

        if (instr) {
            instr->setName(destName);
            valueMap[destName] = instr;
        }
        return instr;

    } else if (currentToken.type == TokenType::Keyword) {
        std::string opcodeStr = currentToken.value;
        getNextToken();

        if (opcodeStr == "ret") {
            ir::Value* retVal = nullptr;
            if (currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Label && currentToken.type != TokenType::Eof) {
                retVal = parseValue();
                
                // Parse type with colon syntax
                if (currentToken.type == TokenType::Colon) {
                    getNextToken(); // consume ':'
                    ir::Type* retType = parseIRType();
                }
            }
            return builder.createRet(retVal);
        }
        if (opcodeStr == "store") {
            ir::Value* val = parseValue();
            if (currentToken.type != TokenType::Comma) return nullptr;
            getNextToken();
            ir::Value* ptr = parseValue();
            
            ir::Type* instrType = nullptr;
            if (currentToken.type == TokenType::Colon) {
                getNextToken(); // consume ':'
                instrType = parseIRType();
            } else {
                // Type is mandatory for store in Fyra
                return nullptr;
            }

            if (instrType->isIntegerTy() && static_cast<ir::IntegerType*>(instrType)->getBitwidth() == 64) return builder.createStorel(val, ptr);
            if (instrType->isIntegerTy() && static_cast<ir::IntegerType*>(instrType)->getBitwidth() == 16) return builder.createStoreh(val, ptr);
            if (instrType->isIntegerTy() && static_cast<ir::IntegerType*>(instrType)->getBitwidth() == 8) return builder.createStoreb(val, ptr);
            if (instrType->isFloatTy()) return builder.createStores(val, ptr);
            if (instrType->isDoubleTy()) return builder.createStored(val, ptr);
            return builder.createStore(val, ptr); // Default to word size store
        }
        if (opcodeStr == "blit") {
            ir::Value* dst = parseValue();
            if (currentToken.type != TokenType::Comma) return nullptr;
            getNextToken();
            ir::Value* src = parseValue();
            if (currentToken.type != TokenType::Comma) return nullptr;
            getNextToken();
            ir::Value* count = parseValue();
            return builder.createBlit(dst, src, count);
        }
        if (opcodeStr == "jmp") {
            ir::Value* target = parseValue();
            return builder.createJmp(target);
        }
        if (opcodeStr == "jnz") {
            ir::Value* cond = parseValue();
            if (currentToken.type != TokenType::Comma) return nullptr;
            getNextToken();
            ir::Value* targetTrue = parseValue();
            if (currentToken.type != TokenType::Comma) return nullptr;
            getNextToken();
            ir::Value* targetFalse = parseValue();
            return builder.createJnz(cond, targetTrue, targetFalse);
        }
        if (opcodeStr == "hlt") {
            return builder.createHlt();
        }
        if (opcodeStr == "br") {
            ir::Value* cond = parseValue();
            if (currentToken.type != TokenType::Comma) return nullptr;
            getNextToken();
            ir::Value* targetTrue = parseValue();
            if (currentToken.type != TokenType::Comma) return nullptr;
            getNextToken();
            ir::Value* targetFalse = parseValue();

            if (currentToken.type == TokenType::Colon) {
                getNextToken();
                parseIRType();
            }

            return builder.createBr(cond, targetTrue, targetFalse);
        }
        if (opcodeStr == "call") {
            return parseCallInstruction(nullptr);
        }
    }
    // If we got here, it's an unknown instruction, consume token to avoid infinite loop
    getNextToken();
    return nullptr;
}

void Parser::parseType() {
    getNextToken(); // Consume 'type'

    if (currentToken.type != TokenType::Type) {
        // Expected type name (e.g., :mytype)
        return;
    }
    std::string typeName = currentToken.value;
    getNextToken();

    if (currentToken.type != TokenType::Equal) {
        // Expected '='
        return;
    }
    getNextToken();

    // Optional alignment
    if (currentToken.type == TokenType::Align) {
        getNextToken(); // consume 'align'
        // We don't do anything with alignment yet, just parse it
        if (currentToken.type == TokenType::Number) {
            getNextToken();
        }
    }

    if (currentToken.type != TokenType::LCurly) {
        // Expected '{'
        return;
    }

    if (lexer.peek() == '{') {
        // Union type
        std::vector<ir::Type*> unionElements;
        getNextToken(); // consume '{'
        while(currentToken.type == TokenType::LCurly) {
            std::vector<ir::Type*> structElements = parseStructElements();
            // For now, we just add the elements of the largest struct
            if (structElements.size() > unionElements.size()) {
                unionElements = structElements;
            }
            if (currentToken.type == TokenType::Comma) {
                getNextToken();
            } else {
                break;
            }
        }
        getNextToken(); // consume '}'

        auto* unionType = new ir::UnionType(typeName, unionElements);
        // This is a simplification. We should probably add the union type to the module with a different method.
        module->addType(typeName, unionType);
    } else if (currentToken.type == TokenType::Number) {
        // Opaque type
        int size = std::stoi(currentToken.value);
        getNextToken();
        if (currentToken.type != TokenType::RCurly) {
            // Expected '}'
            return;
        }
        getNextToken(); // consume '}'
        auto* structType = context->createStructType(typeName);
        structType->setBody({}, true);
        module->addType(typeName, structType);

    } else {
        std::vector<ir::Type*> elements = parseStructElements();
        auto* structType = context->createStructType(typeName);
        structType->setBody(elements);
        module->addType(typeName, structType);
    }
}

ir::Instruction* Parser::parseCallInstruction(ir::Type* retType) {
    ir::Value* callee = parseValue();
    std::vector<ir::Value*> args;
    if (currentToken.type == TokenType::LParen) {
        getNextToken(); // consume '('
        while (currentToken.type != TokenType::RParen && currentToken.type != TokenType::Eof) {
            // Fyra call argument syntax: type value
            ir::Type* argType = parseIRType();
            ir::Value* arg = parseValue();
            // TODO: We need to make sure the value has the correct type.
            // This is a bit tricky as parseValue() returns a generic ConstantInt.
            // For now, we'll just push the parsed value.
            args.push_back(arg);

            if (currentToken.type == TokenType::Comma) {
                getNextToken();
            } else {
                break;
            }
        }
        getNextToken(); // consume ')'
    }
    
    // Parse return type with colon syntax
    if (currentToken.type == TokenType::Colon) {
        getNextToken(); // consume ':'
        retType = parseIRType();
    }
    
    return builder.createCall(callee, args, retType);
}

ir::Value* Parser::parseValue() {
    if (currentToken.type == TokenType::Number) {
        std::string s = currentToken.value;
        long long val;
        if (s.rfind("0b", 0) == 0 || s.rfind("0B", 0) == 0) {
            val = std::stoll(s.substr(2), nullptr, 2);
        } else if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) {
            val = std::stoll(s.substr(2), nullptr, 16);
        } else {
            val = std::stoll(s);
        }
        getNextToken();

        // Default to word size (32-bit) unless we're in a long-sized operation
        // This is a simplification.
        return context->getConstantInt(context->getIntegerType(32), val);
    }
    if (currentToken.type == TokenType::FloatLiteral) {
        std::string valStr = currentToken.value;
        getNextToken();
        char type = valStr[0];
        double val = std::stod(valStr.substr(2));
        if (type == 's') {
            return context->getConstantFP(context->getFloatType(), val);
        } else {
            return context->getConstantFP(context->getDoubleType(), val);
        }
    }
    if (currentToken.type == TokenType::Temporary) {
        std::string name = currentToken.value;
        getNextToken();
        if (valueMap.count(name)) {
            return valueMap[name];
        }
        return nullptr; // Forward reference not supported yet
    }
    if (currentToken.type == TokenType::Label) {
        std::string name = currentToken.value;
        getNextToken();
        if (labelMap.count(name)) {
            return labelMap[name];
        }
        // Create a forward declaration
        auto* func = builder.getInsertPoint()->getParent();
        auto* bb = builder.createBasicBlock(name, func);
        labelMap[name] = bb;
        return bb;
    }
    if (currentToken.type == TokenType::Global) {
        std::string name = currentToken.value;
        getNextToken();
        // This is a simplification. We don't have a proper function type, so we
        // just use a void type for now.
        return new ir::GlobalValue(context->getVoidType(), name);
    }
    return nullptr;
}

ir::Type* Parser::parseIRType() {
    if (currentToken.type == TokenType::Keyword) {
        if (currentToken.value == "w") { getNextToken(); return context->getIntegerType(32); }
        if (currentToken.value == "l") { getNextToken(); return context->getIntegerType(64); }
        if (currentToken.value == "s") { getNextToken(); return context->getFloatType(); }
        if (currentToken.value == "d") { getNextToken(); return context->getDoubleType(); }
        // Extended Fyra type support
        if (currentToken.value == "b") { getNextToken(); return context->getIntegerType(8); }
        if (currentToken.value == "h") { getNextToken(); return context->getIntegerType(16); }
        if (currentToken.value == "void") { getNextToken(); return context->getVoidType(); }
        // Support for custom types with bit widths
        if (currentToken.value.front() == 'i' && currentToken.value.size() > 1) {
            std::string bitStr = currentToken.value.substr(1);
            try {
                int bits = std::stoi(bitStr);
                getNextToken();
                return context->getIntegerType(bits);
            } catch (...) {
                // Fall through to default
            }
        }
    }
    return context->getVoidType();
}

std::vector<ir::Type*> Parser::parseStructElements() {
    std::vector<ir::Type*> elements;
    if (currentToken.type != TokenType::LCurly) {
        return elements;
    }
    getNextToken(); // consume '{'

    while (currentToken.type != TokenType::RCurly && currentToken.type != TokenType::Eof) {
        ir::Type* elemType = parseIRType();
        elements.push_back(elemType);

        // Optional array size
        if (currentToken.type == TokenType::Number) {
            int count = std::stoi(currentToken.value);
            for (int i = 1; i < count; ++i) {
                elements.push_back(elemType);
            }
            getNextToken();
        }

        if (currentToken.type != TokenType::Comma) {
            break;
        }
        getNextToken();
    }
    getNextToken(); // consume '}'
    return elements;
}

} // namespace parser
