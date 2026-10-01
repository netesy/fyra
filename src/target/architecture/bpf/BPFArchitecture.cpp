#include "target/architecture/bpf/BPFArchitecture.h"
#include "codegen/CodeGen.h"
#include "ir/Instruction.h"
#include "ir/Function.h"
#include "ir/Constant.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include <iostream>
#include <stdexcept>

namespace target {

BPFArchitecture::BPFArchitecture() {
    initRegisters();
}

void BPFArchitecture::initRegisters() {
    integerRegs = {"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10"};
    integerArgRegs = {"r1", "r2", "r3", "r4", "r5"};
    intReturnReg = "r0";
    floatReturnReg = "r0";
}

uint8_t BPFArchitecture::getBpfRegIndex(const std::string& regName) const {
    if (regName.empty()) return 0;
    std::string r = regName;
    if (r[0] == '%') r = r.substr(1);
    if (r[0] == 'r' || r[0] == 'R') {
        try {
            int idx = std::stoi(r.substr(1));
            if (idx >= 0 && idx <= 10) return static_cast<uint8_t>(idx);
        } catch (...) {}
    }
    return 0;
}

TypeInfo BPFArchitecture::getTypeInfo(const ir::Type* type) const {
    if (!type || type->isVoidTy()) return {0, 0, RegisterClass::Integer, false, false};
    if (type->isFloatTy()) return {4, 4, RegisterClass::Float, true, false};
    if (type->isDoubleTy()) return {8, 8, RegisterClass::Float, true, false};
    if (type->isPointerTy()) return {8, 8, RegisterClass::Integer, false, false};
    if (auto* it = dynamic_cast<const ir::IntegerType*>(type)) {
        int w = it->getBitwidth();
        if (w <= 8) return {1, 1, RegisterClass::Integer, false, false};
        if (w <= 16) return {2, 2, RegisterClass::Integer, false, false};
        if (w <= 32) return {4, 4, RegisterClass::Integer, false, false};
        return {8, 8, RegisterClass::Integer, false, false};
    }
    return {8, 8, RegisterClass::Integer, false, false};
}

const std::vector<std::string>& BPFArchitecture::getRegisters(RegisterClass regClass) const {
    return integerRegs;
}

const std::string& BPFArchitecture::getReturnRegister(const ir::Type* type) const {
    return intReturnReg;
}

void BPFArchitecture::emitBpfInst(codegen::CodeGen& cg, uint8_t opcode, uint8_t dst, uint8_t src, int16_t off, int32_t imm) {
    if (auto* os = cg.getTextStream()) {
        *os << "  # bpf_inst: op=" << (int)opcode << " dst=r" << (int)dst << " src=r" << (int)src << " off=" << off << " imm=" << imm << "\n";
    }
    auto& as = cg.getAssembler();
    as.emitByte(opcode);
    as.emitByte((uint8_t)((src << 4) | (dst & 0x0F)));
    as.emitByte((uint8_t)(off & 0xFF));
    as.emitByte((uint8_t)((off >> 8) & 0xFF));
    as.emitDWord(static_cast<uint32_t>(imm));
}

void BPFArchitecture::emitHeader(codegen::CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "  .text\n  .globl main\n";
    }
}

void BPFArchitecture::emitFunctionPrologue(codegen::CodeGen& cg, ir::Function& func) {
}

void BPFArchitecture::emitFunctionEpilogue(codegen::CodeGen& cg, ir::Function& func) {
}

void BPFArchitecture::emitPassArgument(codegen::CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) {
}

void BPFArchitecture::emitGetArgument(codegen::CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) {
}

void BPFArchitecture::emitRet(codegen::CodeGen& cg, ir::Instruction& i) {
    if (!i.getOperands().empty() && i.getOperands()[0]->get() != nullptr) {
        ir::Value* retVal = i.getOperands()[0]->get();
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(retVal)) {
            emitBpfInst(cg, 0xb7, 0, 0, 0, static_cast<int32_t>(ci->getValue()));
        } else {
            std::string srcReg = cg.getValueAsOperand(retVal);
            uint8_t src = getBpfRegIndex(srcReg);
            if (src != 0) {
                emitBpfInst(cg, 0xbf, 0, src, 0, 0);
            }
        }
    }
    emitBpfInst(cg, 0x95, 0, 0, 0, 0);
}

void BPFArchitecture::emitAdd(codegen::CodeGen& cg, ir::Instruction& i) {
    uint8_t dst = getBpfRegIndex(cg.getValueAsOperand(&i));
    // eBPF ALU instructions are two-address operations.  Materialize the left
    // operand first; register allocation naming the result register does not
    // imply that the register already contains operand zero.
    if (auto* lhs = dynamic_cast<ir::ConstantInt*>(i.getOperands()[0]->get())) {
        emitBpfInst(cg, 0xb7, dst, 0, 0, static_cast<int32_t>(lhs->getValue()));
    } else {
        const uint8_t src = getBpfRegIndex(cg.getValueAsOperand(i.getOperands()[0]->get()));
        if (src != dst) emitBpfInst(cg, 0xbf, dst, src, 0, 0);
    }
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
        emitBpfInst(cg, 0x07, dst, 0, 0, static_cast<int32_t>(ci->getValue()));
    } else {
        uint8_t src = getBpfRegIndex(cg.getValueAsOperand(i.getOperands()[1]->get()));
        emitBpfInst(cg, 0x0f, dst, src, 0, 0);
    }
}

void BPFArchitecture::emitSub(codegen::CodeGen& cg, ir::Instruction& i) {
    uint8_t dst = getBpfRegIndex(cg.getValueAsOperand(&i));
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
        emitBpfInst(cg, 0x17, dst, 0, 0, static_cast<int32_t>(ci->getValue()));
    } else {
        uint8_t src = getBpfRegIndex(cg.getValueAsOperand(i.getOperands()[1]->get()));
        emitBpfInst(cg, 0x1f, dst, src, 0, 0);
    }
}

void BPFArchitecture::emitMul(codegen::CodeGen& cg, ir::Instruction& i) {
    uint8_t dst = getBpfRegIndex(cg.getValueAsOperand(&i));
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
        emitBpfInst(cg, 0x27, dst, 0, 0, static_cast<int32_t>(ci->getValue()));
    } else {
        uint8_t src = getBpfRegIndex(cg.getValueAsOperand(i.getOperands()[1]->get()));
        emitBpfInst(cg, 0x2f, dst, src, 0, 0);
    }
}

void BPFArchitecture::emitDiv(codegen::CodeGen& cg, ir::Instruction& i) {
    uint8_t dst = getBpfRegIndex(cg.getValueAsOperand(&i));
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
        emitBpfInst(cg, 0x37, dst, 0, 0, static_cast<int32_t>(ci->getValue()));
    } else {
        uint8_t src = getBpfRegIndex(cg.getValueAsOperand(i.getOperands()[1]->get()));
        emitBpfInst(cg, 0x3f, dst, src, 0, 0);
    }
}

void BPFArchitecture::emitRem(codegen::CodeGen& cg, ir::Instruction& i) {
    uint8_t dst = getBpfRegIndex(cg.getValueAsOperand(&i));
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
        emitBpfInst(cg, 0x97, dst, 0, 0, static_cast<int32_t>(ci->getValue()));
    } else {
        uint8_t src = getBpfRegIndex(cg.getValueAsOperand(i.getOperands()[1]->get()));
        emitBpfInst(cg, 0x9f, dst, src, 0, 0);
    }
}

void BPFArchitecture::emitAnd(codegen::CodeGen& cg, ir::Instruction& i) {
    uint8_t dst = getBpfRegIndex(cg.getValueAsOperand(&i));
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
        emitBpfInst(cg, 0x57, dst, 0, 0, static_cast<int32_t>(ci->getValue()));
    } else {
        uint8_t src = getBpfRegIndex(cg.getValueAsOperand(i.getOperands()[1]->get()));
        emitBpfInst(cg, 0x5f, dst, src, 0, 0);
    }
}

void BPFArchitecture::emitOr(codegen::CodeGen& cg, ir::Instruction& i) {
    uint8_t dst = getBpfRegIndex(cg.getValueAsOperand(&i));
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
        emitBpfInst(cg, 0x47, dst, 0, 0, static_cast<int32_t>(ci->getValue()));
    } else {
        uint8_t src = getBpfRegIndex(cg.getValueAsOperand(i.getOperands()[1]->get()));
        emitBpfInst(cg, 0x4f, dst, src, 0, 0);
    }
}

void BPFArchitecture::emitXor(codegen::CodeGen& cg, ir::Instruction& i) {
    uint8_t dst = getBpfRegIndex(cg.getValueAsOperand(&i));
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
        emitBpfInst(cg, 0xa7, dst, 0, 0, static_cast<int32_t>(ci->getValue()));
    } else {
        uint8_t src = getBpfRegIndex(cg.getValueAsOperand(i.getOperands()[1]->get()));
        emitBpfInst(cg, 0xaf, dst, src, 0, 0);
    }
}

void BPFArchitecture::emitShl(codegen::CodeGen& cg, ir::Instruction& i) {
    uint8_t dst = getBpfRegIndex(cg.getValueAsOperand(&i));
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
        emitBpfInst(cg, 0x67, dst, 0, 0, static_cast<int32_t>(ci->getValue()));
    } else {
        uint8_t src = getBpfRegIndex(cg.getValueAsOperand(i.getOperands()[1]->get()));
        emitBpfInst(cg, 0x6f, dst, src, 0, 0);
    }
}

void BPFArchitecture::emitShr(codegen::CodeGen& cg, ir::Instruction& i) {
    uint8_t dst = getBpfRegIndex(cg.getValueAsOperand(&i));
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
        emitBpfInst(cg, 0x77, dst, 0, 0, static_cast<int32_t>(ci->getValue()));
    } else {
        uint8_t src = getBpfRegIndex(cg.getValueAsOperand(i.getOperands()[1]->get()));
        emitBpfInst(cg, 0x7f, dst, src, 0, 0);
    }
}

void BPFArchitecture::emitSar(codegen::CodeGen& cg, ir::Instruction& i) {
    uint8_t dst = getBpfRegIndex(cg.getValueAsOperand(&i));
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
        emitBpfInst(cg, 0xc7, dst, 0, 0, static_cast<int32_t>(ci->getValue()));
    } else {
        uint8_t src = getBpfRegIndex(cg.getValueAsOperand(i.getOperands()[1]->get()));
        emitBpfInst(cg, 0xcf, dst, src, 0, 0);
    }
}

void BPFArchitecture::emitNeg(codegen::CodeGen& cg, ir::Instruction& i) {
    uint8_t dst = getBpfRegIndex(cg.getValueAsOperand(&i));
    emitBpfInst(cg, 0x87, dst, 0, 0, 0);
}

void BPFArchitecture::emitNot(codegen::CodeGen& cg, ir::Instruction& i) {
    uint8_t dst = getBpfRegIndex(cg.getValueAsOperand(&i));
    emitBpfInst(cg, 0xa7, dst, 0, 0, -1);
}

void BPFArchitecture::emitCopy(codegen::CodeGen& cg, ir::Instruction& i) {
    uint8_t dst = getBpfRegIndex(cg.getValueAsOperand(&i));
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[0]->get())) {
        emitBpfInst(cg, 0xb7, dst, 0, 0, static_cast<int32_t>(ci->getValue()));
    } else {
        uint8_t src = getBpfRegIndex(cg.getValueAsOperand(i.getOperands()[0]->get()));
        if (dst != src) {
            emitBpfInst(cg, 0xbf, dst, src, 0, 0);
        }
    }
}

void BPFArchitecture::emitCall(codegen::CodeGen& cg, ir::Instruction& i) {
    int32_t helperId = 1;
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[0]->get())) {
        helperId = static_cast<int32_t>(ci->getValue());
    }
    emitBpfInst(cg, 0x85, 0, 0, 0, helperId);
}

void BPFArchitecture::emitFAdd(codegen::CodeGen& cg, ir::Instruction& i) {
    throw std::runtime_error("Floating point operations are unsupported in eBPF target");
}

void BPFArchitecture::emitFSub(codegen::CodeGen& cg, ir::Instruction& i) {
    throw std::runtime_error("Floating point operations are unsupported in eBPF target");
}

void BPFArchitecture::emitFMul(codegen::CodeGen& cg, ir::Instruction& i) {
    throw std::runtime_error("Floating point operations are unsupported in eBPF target");
}

void BPFArchitecture::emitFDiv(codegen::CodeGen& cg, ir::Instruction& i) {
    throw std::runtime_error("Floating point operations are unsupported in eBPF target");
}

void BPFArchitecture::emitCmp(codegen::CodeGen& cg, ir::Instruction& i) {
}

void BPFArchitecture::emitCast(codegen::CodeGen& cg, ir::Instruction& i, const ir::Type* from, const ir::Type* to) {
    emitCopy(cg, i);
}

void BPFArchitecture::emitLoad(codegen::CodeGen& cg, ir::Instruction& i) {
    uint8_t dst = getBpfRegIndex(cg.getValueAsOperand(&i));
    uint8_t src = getBpfRegIndex(cg.getValueAsOperand(i.getOperands()[0]->get()));
    uint8_t op = 0x79;
    if (i.getType()) {
        size_t sz = getTypeInfo(i.getType()).size;
        if (sz == 1) op = 0x71;
        else if (sz == 2) op = 0x69;
        else if (sz == 4) op = 0x61;
    }
    emitBpfInst(cg, op, dst, src, 0, 0);
}

void BPFArchitecture::emitStore(codegen::CodeGen& cg, ir::Instruction& i) {
    uint8_t src = getBpfRegIndex(cg.getValueAsOperand(i.getOperands()[0]->get()));
    uint8_t dst = getBpfRegIndex(cg.getValueAsOperand(i.getOperands()[1]->get()));
    uint8_t op = 0x7b;
    if (i.getOperands()[0]->get()->getType()) {
        size_t sz = getTypeInfo(i.getOperands()[0]->get()->getType()).size;
        if (sz == 1) op = 0x73;
        else if (sz == 2) op = 0x6b;
        else if (sz == 4) op = 0x63;
    }
    emitBpfInst(cg, op, dst, src, 0, 0);
}

void BPFArchitecture::emitAlloc(codegen::CodeGen& cg, ir::Instruction& i) {
}

void BPFArchitecture::emitBr(codegen::CodeGen& cg, ir::Instruction& i) {
    emitBpfInst(cg, 0x05, 0, 0, 1, 0);
}

void BPFArchitecture::emitJmp(codegen::CodeGen& cg, ir::Instruction& i) {
    emitBpfInst(cg, 0x05, 0, 0, 0, 0);
}

std::string BPFArchitecture::formatStackOperand(int offset) const {
    return "[r10 - " + std::to_string(std::abs(offset)) + "]";
}

std::string BPFArchitecture::formatGlobalOperand(const std::string& name) const {
    return name;
}

bool BPFArchitecture::isCallerSaved(const std::string& reg) const {
    return reg == "r1" || reg == "r2" || reg == "r3" || reg == "r4" || reg == "r5";
}

bool BPFArchitecture::isCalleeSaved(const std::string& reg) const {
    return reg == "r6" || reg == "r7" || reg == "r8" || reg == "r9";
}

bool BPFArchitecture::validateLegality(ir::Function& func, std::string& errorMsg) const {
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getType() && instr->getType()->isFloatingPoint()) {
                errorMsg = "floating-point operations are unsupported in eBPF target";
                return false;
            }
            switch (instr->getOpcode()) {
                case ir::Instruction::FAdd:
                case ir::Instruction::FSub:
                case ir::Instruction::FMul:
                case ir::Instruction::FDiv:
                    errorMsg = "floating-point instructions are unsupported in eBPF target";
                    return false;
                default:
                    break;
            }
        }
    }
    return true;
}

} // namespace target
