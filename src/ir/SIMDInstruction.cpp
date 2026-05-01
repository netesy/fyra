#include "ir/SIMDInstruction.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include "ir/Module.h"
#include "ir/IRContext.h"
#include "ir/Use.h"
namespace ir {
bool SIMDPatternMatcher::hasComplexAddressingMode(Instruction* load) {
    if (load->getOpcode() == Instruction::Load || load->getOpcode() == Instruction::Store) {
        if (load->getOperands().empty()) return false;
        auto* addr = load->getOperands()[0]->get();
        if (auto inst = dynamic_cast<Instruction*>(addr)) {
            if (inst->getOpcode() == Instruction::Add) {
                if (inst->getOperands().size() < 2) return false;
                auto* op1 = inst->getOperands()[0]->get(); auto* op2 = inst->getOperands()[1]->get();
                if (dynamic_cast<Instruction*>(op1) && dynamic_cast<Instruction*>(op2)) return true;
                if (auto mul = dynamic_cast<Instruction*>(op2)) { if (mul->getOpcode() == Instruction::Mul) return true; }
            }
        }
    }
    return false;
}
std::vector<VectorInstruction*> SIMDBuilder::vectorizeScalarLoop(const std::vector<Instruction*>& scalarInstructions, unsigned vectorWidth) {
    std::vector<VectorInstruction*> vectorInstructions;
    for (Instruction* inst : scalarInstructions) {
        if (!SIMDPatternMatcher::canVectorize(inst)) continue;
        Type* scalarType = inst->getType(); unsigned numElements = vectorWidth / (scalarType->getSize() * 8);
        auto* ctx = inst->getParent()->getParent()->getParent()->getContext();
        VectorType* vectorType = ctx->getVectorType(scalarType, numElements);
        Instruction::Opcode vectorOp;
        switch (inst->getOpcode()) {
            case Instruction::Add: vectorOp = scalarType->isFloatingPoint() ? Instruction::VFAdd : Instruction::VAdd; break;
            case Instruction::Mul: vectorOp = scalarType->isFloatingPoint() ? Instruction::VFMul : Instruction::VMul; break;
            default: continue;
        }
        std::vector<Value*> operands; for (auto& operand : inst->getOperands()) operands.push_back(operand->get());
        vectorInstructions.push_back(new VectorInstruction(vectorType, vectorOp, operands, vectorWidth));
    }
    return vectorInstructions;
}
}
