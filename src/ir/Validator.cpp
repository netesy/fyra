#include "ir/Validator.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include "ir/Parameter.h"
#include "ir/Type.h"
#include "ir/Use.h"
#include "ir/FunctionType.h"
#include <set>
#include <map>
#include <iostream>

namespace ir {

bool Validator::validateModule(const Module& module, std::vector<std::string>& errors) {
    bool valid = true;

    for (const auto& func : module.getFunctions()) {
        if (!func) continue;

        std::set<std::string> definedValues;
        std::set<BasicBlock*> validBlocks;

        // 1. Gather all valid basic blocks
        for (const auto& bb : func->getBasicBlocks()) {
            if (bb) {
                validBlocks.insert(bb.get());
            }
        }

        // 2. Gather all defined value names (parameters and instruction results)
        for (const auto& param : func->getParameters()) {
            if (param && !param->getName().empty()) {
                definedValues.insert(param->getName());
            }
        }

        for (const auto& bb : func->getBasicBlocks()) {
            if (!bb) continue;
            for (const auto& inst : bb->getInstructions()) {
                if (inst && !inst->getName().empty()) {
                    definedValues.insert(inst->getName());
                }
            }
        }

        // 3. Validate blocks and instructions
        for (const auto& bb : func->getBasicBlocks()) {
            if (!bb) continue;

            if (bb->getInstructions().empty()) {
                errors.push_back("Function '" + func->getName() + "' contains empty basic block '" + bb->getName() + "'");
                valid = false;
                continue;
            }

            for (const auto& inst : bb->getInstructions()) {
                if (!inst) continue;

                // Check operand types and definitions
                for (size_t i = 0; i < inst->getOperands().size(); ++i) {
                    Value* op = inst->getOperands()[i]->get();
                    if (!op) {
                        errors.push_back("Instruction '" + inst->getName() + "' in block '" + bb->getName() + "' has null operand at index " + std::to_string(i));
                        valid = false;
                        continue;
                    }

                    // Check for undefined values (e.g. raw unresolved placeholders)
                    bool isInstruction = (dynamic_cast<Instruction*>(op) != nullptr);
                    bool isParameter = (dynamic_cast<Parameter*>(op) != nullptr);
                    bool isConstant = (dynamic_cast<Constant*>(op) != nullptr);
                    bool isGlobalVal = (dynamic_cast<GlobalValue*>(op) != nullptr);
                    bool isGlobalVar = (dynamic_cast<GlobalVariable*>(op) != nullptr);
                    bool isBB = (dynamic_cast<BasicBlock*>(op) != nullptr);
                    bool isFunction = (dynamic_cast<Function*>(op) != nullptr);

                    if (!isInstruction && !isParameter && !isConstant && !isGlobalVal && !isGlobalVar && !isBB && !isFunction) {
                        errors.push_back("Use of undefined value '" + op->getName() + "' in instruction '" + inst->getName() + "'");
                        valid = false;
                    }

                    // Check for basic block operands (must be valid targets in the same function)
                    if (isBB) {
                        auto* targetBB = dynamic_cast<BasicBlock*>(op);
                        if (validBlocks.find(targetBB) == validBlocks.end()) {
                            errors.push_back("Instruction '" + inst->getName() + "' references invalid block target '" + targetBB->getName() + "'");
                            valid = false;
                        }
                    }
                }

                // Check terminator logic and branch targets
                Instruction::Opcode opc = inst->getOpcode();
                if (opc == Instruction::Jmp) {
                    if (inst->getOperands().empty()) {
                        errors.push_back("Jmp instruction in block '" + bb->getName() + "' has no target operand");
                        valid = false;
                    } else {
                        auto* target = dynamic_cast<BasicBlock*>(inst->getOperands()[0]->get());
                        if (!target) {
                            errors.push_back("Jmp target in block '" + bb->getName() + "' is not a basic block");
                            valid = false;
                        }
                    }
                } else if (opc == Instruction::Jnz || opc == Instruction::Jz) {
                    if (inst->getOperands().size() < 3) {
                        errors.push_back("Conditional branch instruction in block '" + bb->getName() + "' requires 3 operands");
                        valid = false;
                    } else {
                        auto* targetTrue = dynamic_cast<BasicBlock*>(inst->getOperands()[1]->get());
                        auto* targetFalse = dynamic_cast<BasicBlock*>(inst->getOperands()[2]->get());
                        if (!targetTrue || !targetFalse) {
                            errors.push_back("Conditional branch targets in block '" + bb->getName() + "' must be basic blocks");
                            valid = false;
                        }
                    }
                } else if (opc == Instruction::Br) {
                    if (inst->getOperands().empty()) {
                        errors.push_back("Br instruction in block '" + bb->getName() + "' has no targets");
                        valid = false;
                    } else if (inst->getOperands().size() == 1) {
                        auto* target = dynamic_cast<BasicBlock*>(inst->getOperands()[0]->get());
                        if (!target) {
                            errors.push_back("Br target in block '" + bb->getName() + "' is not a basic block");
                            valid = false;
                        }
                    } else if (inst->getOperands().size() >= 3) {
                        auto* targetTrue = dynamic_cast<BasicBlock*>(inst->getOperands()[1]->get());
                        auto* targetFalse = dynamic_cast<BasicBlock*>(inst->getOperands()[2]->get());
                        if (!targetTrue || !targetFalse) {
                            errors.push_back("Br conditional targets in block '" + bb->getName() + "' must be basic blocks");
                            valid = false;
                        }
                    }
                } else if (opc == Instruction::Ret) {
                    Type* funcRetTy = nullptr;
                    if (auto* ft = dynamic_cast<FunctionType*>(func->getType())) {
                        funcRetTy = ft->getReturnType();
                    }
                    if (inst->getOperands().empty()) {
                        if (funcRetTy && !funcRetTy->isVoidTy()) {
                            errors.push_back("Empty return in function '" + func->getName() + "' which expects return type '" + funcRetTy->toString() + "'");
                            valid = false;
                        }
                    } else {
                        Value* retVal = inst->getOperands()[0]->get();
                        if (funcRetTy && funcRetTy->isVoidTy()) {
                            errors.push_back("Return with value in void function '" + func->getName() + "'");
                            valid = false;
                        } else if (funcRetTy && retVal->getType() && funcRetTy->getTypeID() != retVal->getType()->getTypeID()) {
                            // Only report error if we have valid concrete types to check
                            if (!funcRetTy->isVoidTy() && !retVal->getType()->isVoidTy()) {
                                errors.push_back("Return type mismatch in function '" + func->getName() + "': expected '" + funcRetTy->toString() + "', got '" + retVal->getType()->toString() + "'");
                                valid = false;
                            }
                        }
                    }
                }

                // Check type mismatches on binary operators
                if (opc == Instruction::Add || opc == Instruction::Sub || opc == Instruction::Mul ||
                    opc == Instruction::Div || opc == Instruction::Udiv || opc == Instruction::Rem || opc == Instruction::Urem) {
                    if (inst->getOperands().size() == 2) {
                        Type* t0 = inst->getOperands()[0]->get()->getType();
                        Type* t1 = inst->getOperands()[1]->get()->getType();
                        if (t0 && t1 && t0->getTypeID() != t1->getTypeID() && !t0->isVoidTy() && !t1->isVoidTy()) {
                            errors.push_back("Operand type mismatch in binary instruction '" + inst->getName() + "': '" + t0->toString() + "' vs '" + t1->toString() + "'");
                            valid = false;
                        }
                    }
                }
            }
        }
    }

    return valid;
}

} // namespace ir
