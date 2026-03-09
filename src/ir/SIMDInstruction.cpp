#include "ir/SIMDInstruction.h"
#include "ir/Type.h"
#include "ir/Use.h"
#include <iostream>
#include <algorithm>

namespace ir {

// VectorInstruction implementation
VectorInstruction::VectorInstruction(Type* ty, Opcode op, const std::vector<Value*>& operands,
                                   unsigned vectorWidth, BasicBlock* parent)
    : Instruction(ty, op, operands, parent), vectorWidth(vectorWidth) {
    
    // If vector width not specified, infer from type
    if (vectorWidth == 0 && ty->isVectorTy()) {
        auto* vecTy = static_cast<VectorType*>(ty);
        vectorWidth = vecTy->getBitWidth();
    }
}

bool VectorInstruction::hasVectorOperands() const {
    for (auto& operand : getOperands()) {
        if (operand->get()->getType()->isVectorTy()) {
            return true;
        }
    }
    return false;
}

VectorType* VectorInstruction::getVectorType() const {
    if (getType()->isVectorTy()) {
        return static_cast<VectorType*>(getType());
    }
    
    // Check operands for vector types
    for (auto& operand : getOperands()) {
        if (operand->get()->getType()->isVectorTy()) {
            return static_cast<VectorType*>(operand->get()->getType());
        }
    }
    return nullptr;
}

Type* VectorInstruction::getElementType() const {
    VectorType* vecTy = getVectorType();
    return vecTy ? vecTy->getElementType() : nullptr;
}

unsigned VectorInstruction::getNumElements() const {
    VectorType* vecTy = getVectorType();
    return vecTy ? vecTy->getNumElements() : 0;
}

bool VectorInstruction::isVectorArithmetic() const {
    switch (getOpcode()) {
        case VAdd: case VSub: case VMul: case VDiv:
        case VFAdd: case VFSub: case VFMul: case VFDiv:
            return true;
        default:
            return false;
    }
}

bool VectorInstruction::isVectorLogical() const {
    switch (getOpcode()) {
        case VAnd: case VOr: case VXor: case VNot:
            return true;
        default:
            return false;
    }
}

bool VectorInstruction::isVectorMemory() const {
    switch (getOpcode()) {
        case VLoad: case VStore: case VGather: case VScatter:
            return true;
        default:
            return false;
    }
}

bool VectorInstruction::isVectorComparison() const {
    return getOpcode() == VCmp;
}

bool VectorInstruction::isVectorShuffle() const {
    switch (getOpcode()) {
        case VShuffle: case VExtract: case VInsert: case VBroadcast:
            return true;
        default:
            return false;
    }
}

void VectorInstruction::print(std::ostream& os) const {
    os << "vector." << getOpcode() << "." << vectorWidth << "bit ";
    Instruction::print(os);
}

// FusedInstruction implementation
FusedInstruction::FusedInstruction(Type* ty, Opcode op, const std::vector<Value*>& operands,
                                 FusedType fusedType, BasicBlock* parent)
    : Instruction(ty, op, operands, parent), fusedType(fusedType) {}

bool FusedInstruction::canBeFused() const {
    // Check if the instruction pattern is fusable
    switch (fusedType) {
        case MultiplyAdd:
        case MultiplySubtract:
            return getOperands().size() == 3; // a, b, c operands
        case NegMultiplyAdd:
        case NegMultiplySubtract:
            return getOperands().size() == 3;
        default:
            return false;
    }
}

bool FusedInstruction::isBeneficial() const {
    // Simple heuristic: fused instructions are generally beneficial
    // In a real implementation, this would consider target capabilities,
    // register pressure, and performance characteristics
    return canBeFused();
}

void FusedInstruction::print(std::ostream& os) const {
    os << "fused." << fusedType << " ";
    Instruction::print(os);
}

// SIMDPatternMatcher implementation
bool SIMDPatternMatcher::isVectorizableLoop(const std::vector<Instruction*>& instructions) {
    // Simple pattern: check if all instructions are vectorizable
    for (Instruction* inst : instructions) {
        if (!canVectorize(inst)) {
            return false;
        }
    }
    return !instructions.empty();
}

bool SIMDPatternMatcher::canVectorize(Instruction* inst) {
    switch (inst->getOpcode()) {
        // Arithmetic operations
        case Instruction::Add:
        case Instruction::Sub:
        case Instruction::Mul:
        case Instruction::FAdd:
        case Instruction::FSub:
        case Instruction::FMul:
        // Logical operations
        case Instruction::And:
        case Instruction::Or:
        case Instruction::Xor:
        // Memory operations (with constraints)
        case Instruction::Load:
        case Instruction::Store:
            return true;
        default:
            return false;
    }
}

unsigned SIMDPatternMatcher::getOptimalVectorWidth(Type* elementType, const std::string& targetArch) {
    // Default vector widths for common architectures
    if (targetArch.find("x86_64") != std::string::npos) {
        // SSE: 128-bit, AVX: 256-bit, AVX-512: 512-bit
        if (elementType->isFloatTy() || elementType->isDoubleTy()) {
            return 256; // Prefer AVX for floating-point
        } else if (elementType->isIntegerTy()) {
            return 128; // Conservative for integer operations
        }
    } else if (targetArch.find("aarch64") != std::string::npos) {
        return 128; // NEON is 128-bit
    } else if (targetArch.find("wasm32") != std::string::npos) {
        return 128; // WASM SIMD is 128-bit
    }
    
    return 128; // Default fallback
}

bool SIMDPatternMatcher::canFuseMultiplyAdd(Instruction* mul, Instruction* add) {
    // Check if mul feeds into add
    if (mul->getOpcode() != Instruction::Mul && mul->getOpcode() != Instruction::FMul) {
        return false;
    }
    
    if (add->getOpcode() != Instruction::Add && add->getOpcode() != Instruction::FAdd) {
        return false;
    }
    
    // Check if mul result is used by add
    for (auto& operand : add->getOperands()) {
        if (operand->get() == mul) {
            return true;
        }
    }
    
    return false;
}

bool SIMDPatternMatcher::hasComplexAddressingMode(Instruction* load) {
    // Check for patterns like [base + index*scale + offset]
    // This would require more sophisticated analysis of the address calculation
    return false; // Simplified for now
}

double SIMDPatternMatcher::getVectorizationBenefit(const std::vector<Instruction*>& instructions, 
                                                  unsigned vectorWidth) {
    // Simple heuristic: benefit is proportional to the number of operations
    // and the vector width
    double baseOps = instructions.size();
    double vectorFactor = vectorWidth / 32.0; // Normalize to 32-bit elements
    
    // Account for overhead
    double overhead = 0.1 * baseOps; // 10% overhead for vector setup
    
    return baseOps * vectorFactor - overhead;
}

bool SIMDPatternMatcher::isProfitableToVectorize(const std::vector<Instruction*>& instructions) {
    if (instructions.size() < 4) {
        return false; // Too few operations to benefit
    }
    
    // Check if the benefit outweighs the cost
    double benefit = getVectorizationBenefit(instructions, 128);
    return benefit > instructions.size(); // Must be better than scalar
}

// SIMDBuilder implementation
VectorInstruction* SIMDBuilder::createVectorAdd(VectorType* type, Value* lhs, Value* rhs) {
    std::vector<Value*> operands = {lhs, rhs};
    Instruction::Opcode op = type->isFloatingPointVector() ? 
        Instruction::VFAdd : Instruction::VAdd;
    return new VectorInstruction(type, op, operands, type->getBitWidth());
}

VectorInstruction* SIMDBuilder::createVectorMul(VectorType* type, Value* lhs, Value* rhs) {
    std::vector<Value*> operands = {lhs, rhs};
    Instruction::Opcode op = type->isFloatingPointVector() ? 
        Instruction::VFMul : Instruction::VMul;
    return new VectorInstruction(type, op, operands, type->getBitWidth());
}

VectorInstruction* SIMDBuilder::createVectorLoad(VectorType* type, Value* ptr) {
    std::vector<Value*> operands = {ptr};
    return new VectorInstruction(type, Instruction::VLoad, operands, type->getBitWidth());
}

VectorInstruction* SIMDBuilder::createVectorStore(Value* vec, Value* ptr) {
    std::vector<Value*> operands = {vec, ptr};
    return new VectorInstruction(VoidType::get(), Instruction::VStore, operands);
}

VectorInstruction* SIMDBuilder::createBroadcast(VectorType* type, Value* scalar) {
    std::vector<Value*> operands = {scalar};
    return new VectorInstruction(type, Instruction::VBroadcast, operands, type->getBitWidth());
}

VectorInstruction* SIMDBuilder::createShuffle(VectorType* type, Value* vec1, Value* vec2, 
                                            const ShuffleMask& mask) {
    std::vector<Value*> operands = {vec1, vec2};
    return new VectorInstruction(type, Instruction::VShuffle, operands, type->getBitWidth());
}

FusedInstruction* SIMDBuilder::createFMA(Type* type, Value* a, Value* b, Value* c) {
    std::vector<Value*> operands = {a, b, c};
    return new FusedInstruction(type, Instruction::FMA, operands, 
                               FusedInstruction::MultiplyAdd);
}

FusedInstruction* SIMDBuilder::createFMS(Type* type, Value* a, Value* b, Value* c) {
    std::vector<Value*> operands = {a, b, c};
    return new FusedInstruction(type, Instruction::FMS, operands, 
                               FusedInstruction::MultiplySubtract);
}

std::vector<VectorInstruction*> SIMDBuilder::vectorizeScalarLoop(
    const std::vector<Instruction*>& scalarInstructions, unsigned vectorWidth) {
    
    std::vector<VectorInstruction*> vectorInstructions;
    
    for (Instruction* inst : scalarInstructions) {
        if (!SIMDPatternMatcher::canVectorize(inst)) {
            continue; // Skip non-vectorizable instructions
        }
        
        // Create vector equivalent
        Type* scalarType = inst->getType();
        unsigned numElements = vectorWidth / (scalarType->getSize() * 8);
        VectorType* vectorType = VectorType::get(scalarType, numElements);
        
        // Map scalar opcode to vector opcode
        Instruction::Opcode vectorOp;
        switch (inst->getOpcode()) {
            case Instruction::Add:
                vectorOp = scalarType->isFloatingPoint() ? 
                    Instruction::VFAdd : Instruction::VAdd;
                break;
            case Instruction::Mul:
                vectorOp = scalarType->isFloatingPoint() ? 
                    Instruction::VFMul : Instruction::VMul;
                break;
            // Add more mappings as needed
            default:
                continue; // Skip unsupported operations
        }
        
        // Create vector instruction with same operands
        std::vector<Value*> operands;
        for (auto& operand : inst->getOperands()) {
            operands.push_back(operand->get());
        }
        
        VectorInstruction* vectorInst = new VectorInstruction(
            vectorType, vectorOp, operands, vectorWidth);
        vectorInstructions.push_back(vectorInst);
    }
    
    return vectorInstructions;
}

} // namespace ir