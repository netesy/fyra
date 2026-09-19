#include "ir/SIMDInstruction.h"
#include <iostream>
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include "ir/Module.h"
#include "ir/IRContext.h"
#include "ir/Use.h"
#include "ir/Constant.h"

namespace ir {

static void printValue(std::ostream& os, Value* v) {
    if (!v) {
        os << "null";
        return;
    }
    if (auto* ci = dynamic_cast<ConstantInt*>(v)) {
        os << ci->getValue();
    } else if (auto* cf = dynamic_cast<ConstantFP*>(v)) {
        os << cf->getValue();
    } else if (auto* cs = dynamic_cast<ConstantString*>(v)) {
        os << "\"" << cs->getValue() << "\"";
    } else if (v->getName().empty()) {
        os << "<unnamed>";
    } else {
        os << "%" << v->getName();
    }
}

// SIMD Pattern Matcher Implementation
bool SIMDPatternMatcher::hasComplexAddressingMode(Instruction* load) {
    if (load->getOpcode() == Instruction::Load || load->getOpcode() == Instruction::Store) {
        if (load->getOperands().empty()) return false;
        auto* addr = load->getOperands()[0]->get();
        if (auto inst = dynamic_cast<Instruction*>(addr)) {
            if (inst->getOpcode() == Instruction::Add) {
                if (inst->getOperands().size() < 2) return false;
                auto* op1 = inst->getOperands()[0]->get();
                auto* op2 = inst->getOperands()[1]->get();
                if (dynamic_cast<Instruction*>(op1) && dynamic_cast<Instruction*>(op2)) return true;
                if (auto mul = dynamic_cast<Instruction*>(op2)) {
                    if (mul->getOpcode() == Instruction::Mul) return true;
                }
            }
        }
    }
    return false;
}

bool SIMDPatternMatcher::canVectorize(Instruction* inst) {
    if (!inst) return false;

    // Direct check for instructions that are already vector instructions
    if (dynamic_cast<VectorInstruction*>(inst)) return true;

    // Check scalar opcodes that can be vectorized
    switch (inst->getOpcode()) {
        case Instruction::Add:
        case Instruction::Sub:
        case Instruction::Mul:
        case Instruction::Div:
        case Instruction::Udiv:
        case Instruction::Rem:
        case Instruction::Urem:
        case Instruction::And:
        case Instruction::Or:
        case Instruction::Xor:
        case Instruction::Shl:
        case Instruction::Shr:
        case Instruction::Sar:
        case Instruction::FAdd:
        case Instruction::FSub:
        case Instruction::FMul:
        case Instruction::FDiv:
        case Instruction::SMin:
        case Instruction::SMax:
        case Instruction::Load:
        case Instruction::Store:
        case Instruction::Ceq:
        case Instruction::Cne:
        case Instruction::Csle:
        case Instruction::Cslt:
        case Instruction::Csge:
        case Instruction::Csgt:
        case Instruction::Cule:
        case Instruction::Cult:
        case Instruction::Cuge:
        case Instruction::Cugt:
        case Instruction::Ceqf:
        case Instruction::Cnef:
        case Instruction::Cle:
        case Instruction::Clt:
        case Instruction::Cge:
        case Instruction::Cgt:
            return true;
        default:
            return false;
    }
}

bool SIMDPatternMatcher::isVectorizableLoop(const std::vector<Instruction*>& instructions) {
    if (instructions.empty()) return false;
    for (Instruction* inst : instructions) {
        if (!inst) continue;
        // Function calls, returns, jumps inside vector loop block are not simple vector loop
        if (inst->getOpcode() == Instruction::Call || inst->getOpcode() == Instruction::Ret ||
            inst->getOpcode() == Instruction::Jmp || inst->getOpcode() == Instruction::Br) {
            return false;
        }
        if (!canVectorize(inst) && inst->getOpcode() != Instruction::Phi) {
            return false;
        }
    }
    return true;
}

unsigned SIMDPatternMatcher::getOptimalVectorWidth(Type* elementType, const std::string& targetArch) {
    if (!elementType) return 128;
    return 128;
}

bool SIMDPatternMatcher::canFuseMultiplyAdd(Instruction* mul, Instruction* add) {
    if (!mul || !add) return false;
    bool isMul = (mul->getOpcode() == Instruction::Mul || mul->getOpcode() == Instruction::FMul ||
                  mul->getOpcode() == Instruction::VMul || mul->getOpcode() == Instruction::VFMul);
    bool isAdd = (add->getOpcode() == Instruction::Add || add->getOpcode() == Instruction::FAdd ||
                  add->getOpcode() == Instruction::VAdd || add->getOpcode() == Instruction::VFAdd);
    if (!isMul || !isAdd) return false;

    for (auto& op : add->getOperands()) {
        if (op->get() == mul) return true;
    }
    return false;
}

double SIMDPatternMatcher::getVectorizationBenefit(const std::vector<Instruction*>& instructions, unsigned vectorWidth) {
    if (instructions.empty() || vectorWidth == 0) return 0.0;
    unsigned vectorizableCount = 0;
    for (Instruction* inst : instructions) {
        if (canVectorize(inst)) vectorizableCount++;
    }
    double speedupFactor = static_cast<double>(vectorWidth) / 32.0;
    return (static_cast<double>(vectorizableCount) / instructions.size()) * speedupFactor;
}

bool SIMDPatternMatcher::isProfitableToVectorize(const std::vector<Instruction*>& instructions) {
    return getVectorizationBenefit(instructions, 128) > 1.2;
}


// SIMD Builder Implementation
VectorInstruction* SIMDBuilder::createVectorAdd(VectorType* type, Value* lhs, Value* rhs) {
    unsigned width = type ? static_cast<unsigned>(type->getSize() * 8) : 128;
    Instruction::Opcode op = (type && type->getElementType()->isFloatingPoint()) ? Instruction::VFAdd : Instruction::VAdd;
    return new VectorInstruction(type, op, {lhs, rhs}, width);
}

VectorInstruction* SIMDBuilder::createVectorSub(VectorType* type, Value* lhs, Value* rhs) {
    unsigned width = type ? static_cast<unsigned>(type->getSize() * 8) : 128;
    Instruction::Opcode op = (type && type->getElementType()->isFloatingPoint()) ? Instruction::VFSub : Instruction::VSub;
    return new VectorInstruction(type, op, {lhs, rhs}, width);
}

VectorInstruction* SIMDBuilder::createVectorMul(VectorType* type, Value* lhs, Value* rhs) {
    unsigned width = type ? static_cast<unsigned>(type->getSize() * 8) : 128;
    Instruction::Opcode op = (type && type->getElementType()->isFloatingPoint()) ? Instruction::VFMul : Instruction::VMul;
    return new VectorInstruction(type, op, {lhs, rhs}, width);
}

VectorInstruction* SIMDBuilder::createVectorLoad(VectorType* type, Value* ptr) {
    unsigned width = type ? static_cast<unsigned>(type->getSize() * 8) : 128;
    return new VectorInstruction(type, Instruction::VLoad, {ptr}, width);
}

VectorInstruction* SIMDBuilder::createVectorStore(Value* vec, Value* ptr) {
    auto* vecTy = dynamic_cast<VectorType*>(vec ? vec->getType() : nullptr);
    unsigned width = vecTy ? static_cast<unsigned>(vecTy->getSize() * 8) : 128;
    return new VectorInstruction(vec ? vec->getType() : nullptr, Instruction::VStore, {vec, ptr}, width);
}

VectorInstruction* SIMDBuilder::createBroadcast(VectorType* type, Value* scalar) {
    unsigned width = type ? static_cast<unsigned>(type->getSize() * 8) : 128;
    return new VectorInstruction(type, Instruction::VBroadcast, {scalar}, width);
}

VectorInstruction* SIMDBuilder::createShuffle(VectorType* type, Value* vec1, Value* vec2, const ShuffleMask& mask) {
    unsigned width = type ? static_cast<unsigned>(type->getSize() * 8) : 128;
    auto* inst = new VectorInstruction(type, Instruction::VShuffle, {vec1, vec2}, width);
    inst->setShuffleMask(mask);
    return inst;
}

FusedInstruction* SIMDBuilder::createFMA(Type* type, Value* a, Value* b, Value* c) {
    Instruction::Opcode op = (type && type->isFloatingPoint()) ? Instruction::VFAdd : Instruction::VAdd;
    return new FusedInstruction(type, op, {a, b, c}, FusedInstruction::MultiplyAdd);
}

FusedInstruction* SIMDBuilder::createFMS(Type* type, Value* a, Value* b, Value* c) {
    Instruction::Opcode op = (type && type->isFloatingPoint()) ? Instruction::VFSub : Instruction::VSub;
    return new FusedInstruction(type, op, {a, b, c}, FusedInstruction::MultiplySubtract);
}

std::vector<VectorInstruction*> SIMDBuilder::vectorizeScalarLoop(const std::vector<Instruction*>& scalarInstructions, unsigned vectorWidth) {
    std::vector<VectorInstruction*> vectorInstructions;
    for (Instruction* inst : scalarInstructions) {
        if (!SIMDPatternMatcher::canVectorize(inst)) continue;
        Type* scalarType = inst->getType();
        if (!scalarType || scalarType->isVoidTy()) continue;
        unsigned elemSize = static_cast<unsigned>(scalarType->getSize());
        if (elemSize == 0) continue;
        unsigned numElements = vectorWidth / (elemSize * 8);
        if (numElements == 0) numElements = 1;

        IRContext* ctx = nullptr;
        if (inst->getParent() && inst->getParent()->getParent() && inst->getParent()->getParent()->getParent()) {
            ctx = inst->getParent()->getParent()->getParent()->getContext();
        }
        if (!ctx) continue;

        VectorType* vectorType = ctx->getVectorType(scalarType, numElements);
        Instruction::Opcode vectorOp;
        switch (inst->getOpcode()) {
            case Instruction::Add: vectorOp = scalarType->isFloatingPoint() ? Instruction::VFAdd : Instruction::VAdd; break;
            case Instruction::Sub: vectorOp = scalarType->isFloatingPoint() ? Instruction::VFSub : Instruction::VSub; break;
            case Instruction::Mul: vectorOp = scalarType->isFloatingPoint() ? Instruction::VFMul : Instruction::VMul; break;
            case Instruction::Div: vectorOp = scalarType->isFloatingPoint() ? Instruction::VFDiv : Instruction::VDiv; break;
            case Instruction::Load: vectorOp = Instruction::VLoad; break;
            case Instruction::Store: vectorOp = Instruction::VStore; break;
            default: continue;
        }
        std::vector<Value*> operands;
        for (auto& operand : inst->getOperands()) operands.push_back(operand->get());
        vectorInstructions.push_back(new VectorInstruction(vectorType, vectorOp, operands, vectorWidth));
    }
    return vectorInstructions;
}


// VectorInstruction Implementation
VectorInstruction::VectorInstruction(Type* ty, Opcode op, const std::vector<Value*>& operands, 
                                     unsigned vectorWidth, BasicBlock* parent)
    : Instruction(ty, op, operands, parent), vectorWidth(vectorWidth) {
    if (this->vectorWidth == 0 && ty) {
        if (auto* vt = dynamic_cast<VectorType*>(ty)) {
            this->vectorWidth = static_cast<unsigned>(vt->getSize() * 8);
        }
    }
}

bool VectorInstruction::hasVectorOperands() const {
    for (const auto& op : getOperands()) {
        if (op && op->get() && op->get()->getType() && op->get()->getType()->isVectorTy()) {
            return true;
        }
    }
    return false;
}

VectorType* VectorInstruction::getVectorType() const {
    return dynamic_cast<VectorType*>(getType());
}

Type* VectorInstruction::getElementType() const {
    if (auto* vt = getVectorType()) {
        return vt->getElementType();
    }
    return nullptr;
}

unsigned VectorInstruction::getNumElements() const {
    if (auto* vt = getVectorType()) {
        return vt->getNumElements();
    }
    return 0;
}

bool VectorInstruction::isVectorArithmetic() const {
    switch (getOpcode()) {
        case VAdd: case VSub: case VMul: case VDiv:
        case VFAdd: case VFSub: case VFMul: case VFDiv:
        case VHAdd: case VHSub: case VHMul:
            return true;
        default:
            return false;
    }
}

bool VectorInstruction::isVectorLogical() const {
    switch (getOpcode()) {
        case VAnd: case VOr: case VXor:
        case VHAnd: case VHOr: case VHXor:
            return true;
        default:
            return false;
    }
}

bool VectorInstruction::isVectorMemory() const {
    return getOpcode() == VLoad || getOpcode() == VStore;
}

bool VectorInstruction::isVectorComparison() const {
    return getOpcode() == VCmp;
}

bool VectorInstruction::isVectorShuffle() const {
    return getOpcode() == VShuffle || getOpcode() == VBroadcast || getOpcode() == VExtract || getOpcode() == VInsert;
}

void VectorInstruction::print(std::ostream& os) const {
    if (!getName().empty() && getType() && !getType()->isVoidTy()) {
        os << "%" << getName() << " = ";
    }

    switch (getOpcode()) {
        case VAdd: os << "vadd"; break;
        case VSub: os << "vsub"; break;
        case VMul: os << "vmul"; break;
        case VDiv: os << "vdiv"; break;
        case VFAdd: os << "vfadd"; break;
        case VFSub: os << "vfsub"; break;
        case VFMul: os << "vfmul"; break;
        case VFDiv: os << "vfdiv"; break;
        case VAnd: os << "vand"; break;
        case VOr: os << "vor"; break;
        case VXor: os << "vxor"; break;
        case VLoad: os << "vload"; break;
        case VStore: os << "vstore"; break;
        case VBroadcast: os << "vbroadcast"; break;
        case VExtract: os << "vextract"; break;
        case VInsert: os << "vinsert"; break;
        case VShuffle: os << "vshuffle"; break;
        case VSelect: os << "vselect"; break;
        case VCmp: os << "vcmp"; break;
        case VHAdd: os << "vhadd"; break;
        case VHSub: os << "vhsub"; break;
        case VHMul: os << "vhmul"; break;
        case VSExt: os << "vsext"; break;
        case VZExt: os << "vzext"; break;
        case VTrunc: os << "vtrunc"; break;
        default: os << "vec_op"; break;
    }

    if (vectorWidth > 0) {
        os << "." << vectorWidth;
    }

    for (const auto& operand : getOperands()) {
        os << " ";
        if (operand) printValue(os, operand->get());
        else os << "null_use";
    }

    if (shuffleMask) {
        os << " mask=" << shuffleMask->toString();
    }

    if (getType() && !getType()->isVoidTy()) {
        os << " : " << getType()->toString();
    }
}

// FusedInstruction Implementation
FusedInstruction::FusedInstruction(Type* ty, Opcode op, const std::vector<Value*>& operands,
                                   FusedType fusedType, BasicBlock* parent)
    : Instruction(ty, op, operands, parent), fusedType(fusedType) {}

bool FusedInstruction::canBeFused() const {
    return true;
}

bool FusedInstruction::isBeneficial() const {
    return true;
}

void FusedInstruction::print(std::ostream& os) const {
    if (!getName().empty() && getType() && !getType()->isVoidTy()) {
        os << "%" << getName() << " = ";
    }

    switch (fusedType) {
        case MultiplyAdd: os << "fma"; break;
        case MultiplySubtract: os << "fms"; break;
        case NegMultiplyAdd: os << "fnma"; break;
        case NegMultiplySubtract: os << "fnms"; break;
        case AddressCalculation: os << "fused_addr"; break;
        case CompareAndBranch: os << "fused_cmp_br"; break;
        case LoadAndOperate: os << "fused_load_op"; break;
        case StoreWithUpdate: os << "fused_store_upd"; break;
    }

    for (const auto& operand : getOperands()) {
        os << " ";
        if (operand) printValue(os, operand->get());
        else os << "null_use";
    }

    if (getType() && !getType()->isVoidTy()) {
        os << " : " << getType()->toString();
    }
}

} // namespace ir
