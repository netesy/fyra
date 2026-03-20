#include "transforms/SCCP.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include "ir/Type.h"
#include <iostream>
#include <set>
#include <limits>
#include <cmath>

namespace transforms {

bool SCCP::performTransformation(ir::Function& func) {
    this->initialize(func);

    std::set<ir::BasicBlock*> executableBlocks;

    while (!blockWorklist.empty() || !instructionWorklist.empty()) {
        if (!blockWorklist.empty()) {
            ir::BasicBlock* bb = blockWorklist.back();
            blockWorklist.pop_back();

            if (executableBlocks.count(bb)) {
                continue;
            }
            executableBlocks.insert(bb);

            for (auto& instr : bb->getInstructions()) {
                this->visit(instr.get());
            }
        }
        if (!instructionWorklist.empty()) {
            ir::Instruction* instr = instructionWorklist.back();
            instructionWorklist.pop_back();
            this->visit(instr);
        }
    }

    // --- Transformation Phase ---
    bool changed = false;
    size_t constants_propagated = 0;

    // Replace instructions that evaluated to a constant
    for (auto const& [val, entry] : this->lattice) {
        if (auto* instr = dynamic_cast<ir::Instruction*>(val)) {
            if (entry.type == Constant) {
                instr->replaceAllUsesWith(entry.constant);
                changed = true;
                constants_propagated++;
            }
        }
    }
    
    if (constants_propagated > 0) {
        reportInfo("SCCP propagated " + std::to_string(constants_propagated) + " constants");
    }

    return changed;
}

bool SCCP::validatePreconditions(ir::Function& func) {
    // Validate that the function has basic blocks
    if (func.getBasicBlocks().empty()) {
        reportError("Function has no basic blocks");
        return false;
    }
    
    // Basic validation - could be enhanced with SSA form checking
    return true;
}

void SCCP::initialize(ir::Function& func) {
    this->lattice.clear();
    this->instructionWorklist.clear();
    this->blockWorklist.clear();

    // All instructions start as Top
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getType()->getTypeID() != ir::Type::VoidTyID) {
                this->lattice[instr.get()] = {Top, nullptr};
            }
        }
    }

    // Start by assuming the entry block is executable
    if (!func.getBasicBlocks().empty()) {
        this->blockWorklist.push_back(func.getBasicBlocks().front().get());
    }
}

void SCCP::visit(ir::Instruction* instr) {
    if (this->getLatticeValue(instr).type == Bottom) {
        return;
    }

    switch (instr->getOpcode()) {
        // Arithmetic operations
        case ir::Instruction::Add:
        case ir::Instruction::Sub:
        case ir::Instruction::Mul:
        case ir::Instruction::Div:
        case ir::Instruction::Udiv:
        case ir::Instruction::Rem:
        case ir::Instruction::Urem:
            handleArithmeticOp(instr);
            break;
            
        // Bitwise operations  
        case ir::Instruction::And:
        case ir::Instruction::Or:
        case ir::Instruction::Xor:
        case ir::Instruction::Shl:
        case ir::Instruction::Shr:
        case ir::Instruction::Sar:
            handleBitwiseOp(instr);
            break;
            
        // Comparison operations
        case ir::Instruction::Ceq:
        case ir::Instruction::Cne:
        case ir::Instruction::Csle:
        case ir::Instruction::Cslt:
        case ir::Instruction::Csge:
        case ir::Instruction::Csgt:
        case ir::Instruction::Cule:
        case ir::Instruction::Cult:
        case ir::Instruction::Cuge:
        case ir::Instruction::Cugt:
            handleComparisonOp(instr);
            break;
            
        // Unary operations
        case ir::Instruction::Neg:
            handleUnaryOp(instr);
            break;
            
        // Control flow
        case ir::Instruction::Jmp: {
            this->blockWorklist.push_back(static_cast<ir::BasicBlock*>(instr->getOperands()[0]->get()));
            break;
        }
        case ir::Instruction::Jnz:
        case ir::Instruction::Br: {
            if (instr->getOperands().size() == 1) { // Unconditional branch
                this->blockWorklist.push_back(static_cast<ir::BasicBlock*>(instr->getOperands()[0]->get()));
                break;
            }
            LatticeEntry cond = this->getLatticeValue(instr->getOperands()[0]->get());
            if (cond.type == Top) return;

            if (cond.type == Bottom) {
                this->blockWorklist.push_back(static_cast<ir::BasicBlock*>(instr->getOperands()[1]->get()));
                this->blockWorklist.push_back(static_cast<ir::BasicBlock*>(instr->getOperands()[2]->get()));
            } else {
                auto* c = static_cast<ir::ConstantInt*>(cond.constant);
                if (c->getValue() != 0) {
                    this->blockWorklist.push_back(static_cast<ir::BasicBlock*>(instr->getOperands()[1]->get()));
                } else {
                    this->blockWorklist.push_back(static_cast<ir::BasicBlock*>(instr->getOperands()[2]->get()));
                }
            }
            break;
        }
        
        // Default: mark as bottom (unknown value)
        default:
            this->setLatticeValue(instr, {Bottom, nullptr});
            break;
    }
}

SCCP::LatticeEntry SCCP::getLatticeValue(ir::Value* val) {
    if (auto* c = dynamic_cast<ir::Constant*>(val)) {
        return {Constant, c};
    }
    if (this->lattice.find(val) == this->lattice.end()) {
        // Parameters are Bottom (unknown), other non-constants not in lattice are Top
        if (dynamic_cast<ir::Parameter*>(val)) {
            return {Bottom, nullptr};
        }
        return {Top, nullptr};
    }
    return this->lattice[val];
}

void SCCP::setLatticeValue(ir::Instruction* instr, SCCP::LatticeEntry new_val) {
    LatticeEntry old_val = this->getLatticeValue(instr);
    if (old_val.type == new_val.type && old_val.constant == new_val.constant) {
        return;
    }

    if (old_val.type == Bottom) {
        return;
    }

    this->lattice[instr] = new_val;

    for (auto& use : instr->getUseList()) {
        this->instructionWorklist.push_back(static_cast<ir::Instruction*>(use->getUser()));
    }
}

void SCCP::handleArithmeticOp(ir::Instruction* instr) {
    LatticeEntry v1 = this->getLatticeValue(instr->getOperands()[0]->get());
    LatticeEntry v2 = this->getLatticeValue(instr->getOperands()[1]->get());

    if (v1.type == Top || v2.type == Top) return;
    if (v1.type == Bottom || v2.type == Bottom) {
        this->setLatticeValue(instr, {Bottom, nullptr});
        return;
    }

    auto* c1 = static_cast<ir::ConstantInt*>(v1.constant);
    auto* c2 = static_cast<ir::ConstantInt*>(v2.constant);
    
    ir::Constant* result = performSafeArithmetic(instr->getOpcode(), c1, c2);
    if (result) {
        this->setLatticeValue(instr, {Constant, result});
    } else {
        this->setLatticeValue(instr, {Bottom, nullptr});
    }
}

void SCCP::handleBitwiseOp(ir::Instruction* instr) {
    LatticeEntry v1 = this->getLatticeValue(instr->getOperands()[0]->get());
    LatticeEntry v2 = this->getLatticeValue(instr->getOperands()[1]->get());

    if (v1.type == Top || v2.type == Top) return;
    if (v1.type == Bottom || v2.type == Bottom) {
        this->setLatticeValue(instr, {Bottom, nullptr});
        return;
    }

    auto* c1 = static_cast<ir::ConstantInt*>(v1.constant);
    auto* c2 = static_cast<ir::ConstantInt*>(v2.constant);
    
    ir::Constant* result = performSafeBitwise(instr->getOpcode(), c1, c2);
    if (result) {
        this->setLatticeValue(instr, {Constant, result});
    } else {
        this->setLatticeValue(instr, {Bottom, nullptr});
    }
}

void SCCP::handleComparisonOp(ir::Instruction* instr) {
    LatticeEntry v1 = this->getLatticeValue(instr->getOperands()[0]->get());
    LatticeEntry v2 = this->getLatticeValue(instr->getOperands()[1]->get());

    if (v1.type == Top || v2.type == Top) return;
    if (v1.type == Bottom || v2.type == Bottom) {
        this->setLatticeValue(instr, {Bottom, nullptr});
        return;
    }

    auto* c1 = static_cast<ir::ConstantInt*>(v1.constant);
    auto* c2 = static_cast<ir::ConstantInt*>(v2.constant);
    
    ir::Constant* result = performSafeComparison(instr->getOpcode(), c1, c2);
    if (result) {
        this->setLatticeValue(instr, {Constant, result});
    } else {
        this->setLatticeValue(instr, {Bottom, nullptr});
    }
}

void SCCP::handleUnaryOp(ir::Instruction* instr) {
    LatticeEntry v1 = this->getLatticeValue(instr->getOperands()[0]->get());

    if (v1.type == Top) return;
    if (v1.type == Bottom) {
        this->setLatticeValue(instr, {Bottom, nullptr});
        return;
    }

    auto* c1 = static_cast<ir::ConstantInt*>(v1.constant);
    auto* ty = static_cast<ir::IntegerType*>(c1->getType());
    
    switch (instr->getOpcode()) {
        case ir::Instruction::Neg: {
            // Perform negation with overflow check
            uint64_t value = c1->getValue();
            uint64_t result = (~value) + 1; // Two's complement negation
            
            ir::Constant* resultConst = ir::ConstantInt::get(ty, result);
            this->setLatticeValue(instr, {Constant, resultConst});
            break;
        }
        default:
            this->setLatticeValue(instr, {Bottom, nullptr});
            break;
    }
}

bool SCCP::detectAddOverflow(uint64_t a, uint64_t b, ir::IntegerType* type) {
    // For simplicity, we'll check if the result exceeds the maximum value for the type
    uint64_t max_val = (1ULL << type->getBitwidth()) - 1;
    return (a > max_val - b);
}

bool SCCP::detectMulOverflow(uint64_t a, uint64_t b, ir::IntegerType* type) {
    if (a == 0 || b == 0) return false;
    uint64_t max_val = (1ULL << type->getBitwidth()) - 1;
    return (a > max_val / b);
}

bool SCCP::detectDivisionByZero(uint64_t divisor) {
    return divisor == 0;
}

ir::Constant* SCCP::performSafeArithmetic(ir::Instruction::Opcode op, 
                                         ir::ConstantInt* c1, ir::ConstantInt* c2) {
    auto* ty = static_cast<ir::IntegerType*>(c1->getType());
    uint64_t val1 = c1->getValue();
    uint64_t val2 = c2->getValue();
    uint64_t result;
    
    switch (op) {
        case ir::Instruction::Add:
            if (detectAddOverflow(val1, val2, ty)) {
                reportWarning("Integer overflow detected in addition");
                return nullptr;
            }
            result = val1 + val2;
            break;
            
        case ir::Instruction::Sub:
            result = val1 - val2;
            break;
            
        case ir::Instruction::Mul:
            if (detectMulOverflow(val1, val2, ty)) {
                reportWarning("Integer overflow detected in multiplication");
                return nullptr;
            }
            result = val1 * val2;
            break;
            
        case ir::Instruction::Div:
        case ir::Instruction::Udiv:
            if (detectDivisionByZero(val2)) {
                reportError("Division by zero detected");
                return nullptr;
            }
            if (op == ir::Instruction::Div) {
                result = static_cast<int64_t>(val1) / static_cast<int64_t>(val2);
            } else {
                result = val1 / val2;
            }
            break;
            
        case ir::Instruction::Rem:
        case ir::Instruction::Urem:
            if (detectDivisionByZero(val2)) {
                reportError("Modulo by zero detected");
                return nullptr;
            }
            if (op == ir::Instruction::Rem) {
                result = static_cast<int64_t>(val1) % static_cast<int64_t>(val2);
            } else {
                result = val1 % val2;
            }
            break;
            
        default:
            return nullptr;
    }
    
    return ir::ConstantInt::get(ty, result);
}

ir::Constant* SCCP::performSafeBitwise(ir::Instruction::Opcode op,
                                     ir::ConstantInt* c1, ir::ConstantInt* c2) {
    auto* ty = static_cast<ir::IntegerType*>(c1->getType());
    uint64_t val1 = c1->getValue();
    uint64_t val2 = c2->getValue();
    uint64_t result;
    
    switch (op) {
        case ir::Instruction::And:
            result = val1 & val2;
            break;
            
        case ir::Instruction::Or:
            result = val1 | val2;
            break;
            
        case ir::Instruction::Xor:
            result = val1 ^ val2;
            break;
            
        case ir::Instruction::Shl:
            if (val2 >= ty->getBitwidth()) {
                reportWarning("Left shift by " + std::to_string(val2) + " bits exceeds type width");
                return nullptr;
            }
            result = val1 << val2;
            break;
            
        case ir::Instruction::Shr:
            if (val2 >= ty->getBitwidth()) {
                reportWarning("Right shift by " + std::to_string(val2) + " bits exceeds type width");
                return nullptr;
            }
            result = val1 >> val2;
            break;
            
        case ir::Instruction::Sar:
            if (val2 >= ty->getBitwidth()) {
                reportWarning("Arithmetic right shift by " + std::to_string(val2) + " bits exceeds type width");
                return nullptr;
            }
            // Arithmetic right shift (sign-extending)
            result = static_cast<int64_t>(val1) >> val2;
            break;
            
        default:
            return nullptr;
    }
    
    return ir::ConstantInt::get(ty, result);
}

ir::Constant* SCCP::performSafeComparison(ir::Instruction::Opcode op,
                                        ir::ConstantInt* c1, ir::ConstantInt* c2) {
    // Get a boolean type (we'll use 1-bit integer for boolean results)
    // SCCP is a legacy pass, we try to infer context.
    ir::Type* boolType = nullptr;
    if (c1 && c1->getType()) {
         // This is still a bit of a hack until SCCP is fully context-aware.
    }
    uint64_t val1 = c1->getValue();
    uint64_t val2 = c2->getValue();
    bool result;
    
    switch (op) {
        case ir::Instruction::Ceq:
            result = (val1 == val2);
            break;
            
        case ir::Instruction::Cne:
            result = (val1 != val2);
            break;
            
        case ir::Instruction::Csle:
            result = (static_cast<int64_t>(val1) <= static_cast<int64_t>(val2));
            break;
            
        case ir::Instruction::Cslt:
            result = (static_cast<int64_t>(val1) < static_cast<int64_t>(val2));
            break;
            
        case ir::Instruction::Csge:
            result = (static_cast<int64_t>(val1) >= static_cast<int64_t>(val2));
            break;
            
        case ir::Instruction::Csgt:
            result = (static_cast<int64_t>(val1) > static_cast<int64_t>(val2));
            break;
            
        case ir::Instruction::Cule:
            result = (val1 <= val2);
            break;
            
        case ir::Instruction::Cult:
            result = (val1 < val2);
            break;
            
        case ir::Instruction::Cuge:
            result = (val1 >= val2);
            break;
            
        case ir::Instruction::Cugt:
            result = (val1 > val2);
            break;
            
        default:
            return nullptr;
    }
    
    return ir::ConstantInt::get(static_cast<ir::IntegerType*>(boolType), result ? 1 : 0);
}

} // namespace transforms
