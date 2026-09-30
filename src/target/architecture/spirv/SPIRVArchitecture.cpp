#include "target/architecture/spirv/SPIRVArchitecture.h"
#include "codegen/CodeGen.h"
#include "ir/Instruction.h"
#include "ir/Function.h"
#include "ir/Constant.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include <cstring>
#include <iostream>

namespace target {

SPIRVArchitecture::SPIRVArchitecture() {
    initRegisters();
}

void SPIRVArchitecture::initRegisters() {
    integerRegs = {};
    integerArgRegs = {};
    intReturnReg = "";
    floatReturnReg = "";

    module_.addCapability(1); // Shader capability
    module_.setMemoryModel(0, 1); // Logical addressing, GLSL450 memory model

    voidTypeId_ = module_.allocateId();
    i32TypeId_ = module_.allocateId();
    f32TypeId_ = module_.allocateId();

    module_.addInstruction(19, {voidTypeId_}); // OpTypeVoid
    module_.addInstruction(21, {i32TypeId_, 32, 1}); // OpTypeInt 32-bit signed
    module_.addInstruction(22, {f32TypeId_, 32}); // OpTypeFloat 32-bit
}

TypeInfo SPIRVArchitecture::getTypeInfo(const ir::Type* type) const {
    if (!type || type->isVoidTy()) return {0, 0, RegisterClass::Integer, false, false};
    if (type->isFloatTy()) return {4, 4, RegisterClass::Float, true, false};
    if (type->isDoubleTy()) return {8, 8, RegisterClass::Float, true, false};
    return {4, 4, RegisterClass::Integer, false, false};
}

const std::vector<std::string>& SPIRVArchitecture::getRegisters(RegisterClass regClass) const {
    return integerRegs;
}

const std::string& SPIRVArchitecture::getReturnRegister(const ir::Type* type) const {
    return intReturnReg;
}

uint32_t SPIRVArchitecture::getTypeId(const ir::Type* type) {
    if (!type || type->isVoidTy()) return voidTypeId_;
    if (type->isFloatTy() || type->isDoubleTy()) return f32TypeId_;
    return i32TypeId_;
}

uint32_t SPIRVArchitecture::getOrCreateValueId(codegen::CodeGen& cg, ir::Value* val) {
    if (!val) return 0;
    if (valueIdMap_.count(val)) return valueIdMap_[val];

    if (auto* ci = dynamic_cast<ir::ConstantInt*>(val)) {
        uint32_t constId = module_.allocateId();
        uint32_t valWord = static_cast<uint32_t>(ci->getValue());
        module_.addInstruction(43, {i32TypeId_, constId, valWord}); // OpConstant
        valueIdMap_[val] = constId;
        return constId;
    }
    if (auto* cfp = dynamic_cast<ir::ConstantFP*>(val)) {
        uint32_t constId = module_.allocateId();
        float fval = static_cast<float>(cfp->getValue());
        uint32_t valWord;
        std::memcpy(&valWord, &fval, sizeof(float));
        module_.addInstruction(43, {f32TypeId_, constId, valWord}); // OpConstant
        valueIdMap_[val] = constId;
        return constId;
    }

    uint32_t id = module_.allocateId();
    valueIdMap_[val] = id;
    return id;
}

void SPIRVArchitecture::emitHeader(codegen::CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "; SPIR-V\n; Version: 1.0\n; Bound: 100\n; Schema: 0\n";
        *os << "OpCapability Shader\n";
        *os << "OpMemoryModel Logical GLSL450\n";
    }
}

void SPIRVArchitecture::emitFooter(codegen::CodeGen& cg) {
    if (!cg.getTextStream()) {
        auto bytes = spirv::SPIRVBinaryWriter::write(module_);
        cg.getAssembler().emitBytes(bytes);
    }
}

void SPIRVArchitecture::emitFunctionPrologue(codegen::CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << "OpEntryPoint GLCompute %" << func.getName() << " \"" << func.getName() << "\"\n";
        *os << "OpExecutionMode %" << func.getName() << " LocalSize 1 1 1\n";
    }
    uint32_t fnId = getOrCreateValueId(cg, &func);
    uint32_t retType = getTypeId(func.getType());
    uint32_t fnTypeId = module_.allocateId();

    module_.addInstruction(33, {fnTypeId, retType}); // OpTypeFunction
    module_.addEntryPoint(5, fnId, func.getName(), {}); // OpEntryPoint 5 = GLCompute

    module_.addInstruction(54, {retType, fnId, 0, fnTypeId}); // OpFunction

    uint32_t entryLabelId = module_.allocateId();
    module_.addInstruction(248, {entryLabelId}); // OpLabel
}

void SPIRVArchitecture::emitFunctionEpilogue(codegen::CodeGen& cg, ir::Function& func) {
    module_.addInstruction(56, {}); // OpFunctionEnd
}

void SPIRVArchitecture::emitRet(codegen::CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  OpReturn\n";
    }
    if (!i.getOperands().empty() && i.getOperands()[0]->get() != nullptr) {
        uint32_t valId = getOrCreateValueId(cg, i.getOperands()[0]->get());
        module_.addInstruction(254, {valId}); // OpReturnValue
    } else {
        module_.addInstruction(253, {}); // OpReturn
    }
}

void SPIRVArchitecture::emitAdd(codegen::CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  %res = OpIAdd %i32\n";
    }
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    uint32_t op2Id = getOrCreateValueId(cg, i.getOperands()[1]->get());
    module_.addInstruction(128, {i32TypeId_, resId, op1Id, op2Id}); // OpIAdd
}

void SPIRVArchitecture::emitSub(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    uint32_t op2Id = getOrCreateValueId(cg, i.getOperands()[1]->get());
    module_.addInstruction(130, {i32TypeId_, resId, op1Id, op2Id}); // OpISub
}

void SPIRVArchitecture::emitMul(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    uint32_t op2Id = getOrCreateValueId(cg, i.getOperands()[1]->get());
    module_.addInstruction(132, {i32TypeId_, resId, op1Id, op2Id}); // OpIMul
}

void SPIRVArchitecture::emitDiv(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    uint32_t op2Id = getOrCreateValueId(cg, i.getOperands()[1]->get());
    module_.addInstruction(135, {i32TypeId_, resId, op1Id, op2Id}); // OpSDiv
}

void SPIRVArchitecture::emitRem(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    uint32_t op2Id = getOrCreateValueId(cg, i.getOperands()[1]->get());
    module_.addInstruction(137, {i32TypeId_, resId, op1Id, op2Id}); // OpSRem
}

void SPIRVArchitecture::emitAnd(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    uint32_t op2Id = getOrCreateValueId(cg, i.getOperands()[1]->get());
    module_.addInstruction(196, {i32TypeId_, resId, op1Id, op2Id}); // OpBitwiseAnd
}

void SPIRVArchitecture::emitOr(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    uint32_t op2Id = getOrCreateValueId(cg, i.getOperands()[1]->get());
    module_.addInstruction(194, {i32TypeId_, resId, op1Id, op2Id}); // OpBitwiseOr
}

void SPIRVArchitecture::emitXor(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    uint32_t op2Id = getOrCreateValueId(cg, i.getOperands()[1]->get());
    module_.addInstruction(195, {i32TypeId_, resId, op1Id, op2Id}); // OpBitwiseXor
}

void SPIRVArchitecture::emitShl(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    uint32_t op2Id = getOrCreateValueId(cg, i.getOperands()[1]->get());
    module_.addInstruction(200, {i32TypeId_, resId, op1Id, op2Id}); // OpShiftLeftLogical
}

void SPIRVArchitecture::emitShr(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    uint32_t op2Id = getOrCreateValueId(cg, i.getOperands()[1]->get());
    module_.addInstruction(201, {i32TypeId_, resId, op1Id, op2Id}); // OpShiftRightLogical
}

void SPIRVArchitecture::emitSar(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    uint32_t op2Id = getOrCreateValueId(cg, i.getOperands()[1]->get());
    module_.addInstruction(202, {i32TypeId_, resId, op1Id, op2Id}); // OpShiftRightArithmetic
}

void SPIRVArchitecture::emitNeg(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    module_.addInstruction(126, {i32TypeId_, resId, op1Id}); // OpSNegate
}

void SPIRVArchitecture::emitNot(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    module_.addInstruction(197, {i32TypeId_, resId, op1Id}); // OpNot
}

void SPIRVArchitecture::emitCopy(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    valueIdMap_[&i] = op1Id;
}

void SPIRVArchitecture::emitCall(codegen::CodeGen& cg, ir::Instruction& i) {
}

void SPIRVArchitecture::emitFAdd(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    uint32_t op2Id = getOrCreateValueId(cg, i.getOperands()[1]->get());
    module_.addInstruction(129, {f32TypeId_, resId, op1Id, op2Id}); // OpFAdd
}

void SPIRVArchitecture::emitFSub(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    uint32_t op2Id = getOrCreateValueId(cg, i.getOperands()[1]->get());
    module_.addInstruction(131, {f32TypeId_, resId, op1Id, op2Id}); // OpFSub
}

void SPIRVArchitecture::emitFMul(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    uint32_t op2Id = getOrCreateValueId(cg, i.getOperands()[1]->get());
    module_.addInstruction(133, {f32TypeId_, resId, op1Id, op2Id}); // OpFMul
}

void SPIRVArchitecture::emitFDiv(codegen::CodeGen& cg, ir::Instruction& i) {
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t op1Id = getOrCreateValueId(cg, i.getOperands()[0]->get());
    uint32_t op2Id = getOrCreateValueId(cg, i.getOperands()[1]->get());
    module_.addInstruction(136, {f32TypeId_, resId, op1Id, op2Id}); // OpFDiv
}

void SPIRVArchitecture::emitCmp(codegen::CodeGen& cg, ir::Instruction& i) {
}

void SPIRVArchitecture::emitCast(codegen::CodeGen& cg, ir::Instruction& i, const ir::Type* from, const ir::Type* to) {
}

void SPIRVArchitecture::emitLoad(codegen::CodeGen& cg, ir::Instruction& i) {
}

void SPIRVArchitecture::emitStore(codegen::CodeGen& cg, ir::Instruction& i) {
}

void SPIRVArchitecture::emitAlloc(codegen::CodeGen& cg, ir::Instruction& i) {
}

void SPIRVArchitecture::emitBr(codegen::CodeGen& cg, ir::Instruction& i) {
}

void SPIRVArchitecture::emitJmp(codegen::CodeGen& cg, ir::Instruction& i) {
}

} // namespace target
