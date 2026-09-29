#include "ir/Instruction.h"
#include "ir/Use.h"
#include "ir/Constant.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/PhiNode.h"
#include "target/architecture/riscv64/RiscV64Architecture.h"
#include "codegen/CodeGen.h"
#include "codegen/asm/Assembler.h"
#include "target/core/OperatingSystemInfo.h"
#include <ostream>
#include <vector>

namespace target {

RiscV64Architecture::RiscV64Architecture() {}

void RiscV64Architecture::emitLoadValue(CodeGen& cg, asm_::Assembler& as, ir::Value* val, uint8_t reg) {
    if (auto* instr = dynamic_cast<ir::Instruction*>(val)) {
        if (instr->hasPhysicalRegister()) {
            uint8_t src = static_cast<uint8_t>(instr->getPhysicalRegister());
            if (src == reg) return;
            // addi reg, src, 0
            as.emitDWord((0 << 20) | (src << 15) | (0 << 12) | (reg << 7) | 0x13);
            return;
        }
    }
    if (auto* constInt = dynamic_cast<ir::ConstantInt*>(val)) {
        int64_t v = static_cast<int64_t>(constInt->getValue());
        if (v >= -2048 && v <= 2047) {
            // addi reg, zero, v
            as.emitDWord(((v & 0xFFF) << 20) | (0 << 15) | (0 << 12) | (reg << 7) | 0x13);
        } else {
            int32_t imm32 = static_cast<int32_t>(v);
            int32_t hi = (imm32 + 0x800) >> 12;
            int32_t lo = imm32 - (hi << 12);
            as.emitDWord(((hi & 0xFFFFF) << 12) | (reg << 7) | 0x37); // lui reg, hi
            as.emitDWord(((lo & 0xFFF) << 20) | (reg << 15) | (0 << 12) | (reg << 7) | 0x13); // addi reg, reg, lo
        }
    } else {
        int32_t offset = cg.getStackOffset(val);
        uint32_t funct3 = (val->getType() && val->getType()->getSize() <= 4) ? 2 : 3;
        as.emitDWord(((offset & 0xFFF) << 20) | (8 << 15) | (funct3 << 12) | (reg << 7) | 0x03);
    }
}

void RiscV64Architecture::emitStoreResult(CodeGen& cg, ir::Instruction& instr, uint8_t reg) {
    if (instr.hasPhysicalRegister()) {
        uint8_t dst = static_cast<uint8_t>(instr.getPhysicalRegister());
        if (dst != reg) {
            cg.getAssembler().emitDWord((0 << 20) | (reg << 15) | (0 << 12) | (dst << 7) | 0x13);
        }
        return;
    }
    int32_t offset = cg.getStackOffset(&instr);
    uint32_t funct3 = (instr.getType() && instr.getType()->getSize() <= 4) ? 2 : 3;
    uint32_t imm = offset & 0xFFF;
    uint32_t op = (((imm >> 5) & 0x7F) << 25) | (reg << 20) | (8 << 15) | (funct3 << 12) | ((imm & 0x1F) << 7) | 0x23;
    cg.getAssembler().emitDWord(op);
}

TypeInfo RiscV64Architecture::getTypeInfo(const ir::Type* type) const {
    return {type->getSize() * 8, type->getAlignment() * 8, type->isFloatingPoint() ? RegisterClass::Float : RegisterClass::Integer, type->isFloatingPoint(), true};
}

const std::vector<std::string>& RiscV64Architecture::getRegisters(RegisterClass regClass) const {
    static const std::vector<std::string> intRegs = {"x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x5", "x6", "x7", "x28", "x29", "x30", "x31"};
    static const std::vector<std::string> floatRegs = {"f10", "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f5", "f6", "f7", "f28", "f29", "f30", "f31"};
    static const std::vector<std::string> vecRegs = {"v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15"};
    return (regClass == RegisterClass::Float) ? floatRegs : (regClass == RegisterClass::Vector ? vecRegs : intRegs);
}

const std::string& RiscV64Architecture::getReturnRegister(const ir::Type* type) const {
    static const std::string i = "a0", f = "fa0"; return type->isFloatingPoint() ? f : i;
}
const std::vector<std::string>& RiscV64Architecture::getIntegerArgumentRegisters() const { static const std::vector<std::string> r = {"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"}; return r; }
const std::vector<std::string>& RiscV64Architecture::getFloatArgumentRegisters() const { static const std::vector<std::string> r = {"fa0", "fa1", "fa2", "fa3", "fa4", "fa5", "fa6", "fa7"}; return r; }
const std::string& RiscV64Architecture::getIntegerReturnRegister() const { static const std::string r = "a0"; return r; }
const std::string& RiscV64Architecture::getFloatReturnRegister() const { static const std::string r = "fa0"; return r; }

void RiscV64Architecture::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    currentStackOffset = -16; int i_idx = 0, f_idx = 0, s_arg_idx = 0;
    for (auto& param : func.getParameters()) {
        TypeInfo inf = getTypeInfo(param->getType());
        if (inf.regClass == RegisterClass::Float) { if (f_idx < 8) { currentStackOffset -= 8; cg.getStackOffsets()[param.get()] = currentStackOffset; f_idx++; } else cg.getStackOffsets()[param.get()] = (s_arg_idx++) * 8; }
        else { if (i_idx < 8) { currentStackOffset -= 8; cg.getStackOffsets()[param.get()] = currentStackOffset; i_idx++; } else cg.getStackOffsets()[param.get()] = (s_arg_idx++) * 8; }
    }
    for (auto& bb : func.getBasicBlocks()) { for (auto& instr : bb->getInstructions()) { if (instr->getType()->getTypeID() != ir::Type::VoidTyID) { currentStackOffset -= 8; cg.getStackOffsets()[instr.get()] = currentStackOffset; } } }
    int stack_size = (-currentStackOffset + 15) & ~15;
    if (auto* os = cg.getTextStream()) {
        *os << "  addi sp, sp, -" << stack_size << "\n  sd ra, " << stack_size - 8 << "(sp)\n  sd s0, " << stack_size - 16 << "(sp)\n  addi s0, sp, " << stack_size << "\n";
    } else {
        cg.getAssembler().emitDWord(((-stack_size & 0xFFF) << 20) | (2 << 15) | (0 << 12) | (2 << 7) | 0x13); // addi sp, sp, -stack_size
        uint32_t off1 = (stack_size - 8) & 0xFFF;
        cg.getAssembler().emitDWord((((off1 >> 5) & 0x7F) << 25) | (1 << 20) | (2 << 15) | (3 << 12) | ((off1 & 0x1F) << 7) | 0x23); // sd ra, (stack_size-8)(sp)
        uint32_t off2 = (stack_size - 16) & 0xFFF;
        cg.getAssembler().emitDWord((((off2 >> 5) & 0x7F) << 25) | (8 << 20) | (2 << 15) | (3 << 12) | ((off2 & 0x1F) << 7) | 0x23); // sd s0, (stack_size-16)(sp)
        cg.getAssembler().emitDWord(((stack_size & 0xFFF) << 20) | (2 << 15) | (0 << 12) | (8 << 7) | 0x13); // addi s0, sp, stack_size
        int arg_i = 0;
        for (auto& param : func.getParameters()) {
            if (arg_i < 8) {
                int32_t off = cg.getStackOffset(param.get());
                uint32_t funct3 = (param->getType()->getSize() <= 4) ? 2 : 3;
                uint32_t imm = off & 0xFFF;
                cg.getAssembler().emitDWord((((imm >> 5) & 0x7F) << 25) | ((10 + arg_i) << 20) | (8 << 15) | (funct3 << 12) | ((imm & 0x1F) << 7) | 0x23);
                arg_i++;
            }
        }
    }
}

void RiscV64Architecture::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << func.getName() << "_epilogue:\n  ld ra, -8(s0)\n  ld s0, -16(s0)\n  addi sp, s0, 0\n  jr ra\n";
    } else {
        cg.getAssembler().emitDWord(((-8 & 0xFFF) << 20) | (8 << 15) | (3 << 12) | (1 << 7) | 0x03); // ld ra, -8(s0)
        cg.getAssembler().emitDWord(((-16 & 0xFFF) << 20) | (8 << 15) | (3 << 12) | (8 << 7) | 0x03); // ld s0, -16(s0)
        cg.getAssembler().emitDWord((0 << 20) | (8 << 15) | (0 << 12) | (2 << 7) | 0x13); // addi sp, s0, 0
        cg.getAssembler().emitDWord(0x00008067); // jr ra (jalr x0, ra, 0)
    }
}

void RiscV64Architecture::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << ".text\n.globl _start\n_start:\n  call main\n  li a7, 93\n  ecall\n";
    } else {
        cg.addRelocation({cg.getAssembler().getCodeSize(), "R_RISCV_CALL", 0, "main", ".text"});
        cg.getAssembler().emitDWord(0x000000EF); // jal ra, 0
        cg.getAssembler().emitDWord(((93 & 0xFFF) << 20) | (0 << 15) | (0 << 12) | (17 << 7) | 0x13); // li a7, 93
        cg.getAssembler().emitDWord(0x00000073); // ecall
    }
}

void RiscV64Architecture::emitRet(CodeGen& cg, ir::Instruction& i) {
    if (!i.getOperands().empty() && i.getOperands()[0] && i.getOperands()[0]->get() != nullptr) {
        ir::Value* rv = i.getOperands()[0]->get();
        if (auto* os = cg.getTextStream()) {
            if (rv->getType() && rv->getType()->isFloatingPoint()) {
                std::string flInst = (rv->getType()->getSize() == 4) ? "flw" : "fld";
                *os << "  " << flInst << " fa0, " << cg.getValueAsOperand(rv) << "\n";
            } else {
                std::string lInst = (rv->getType() && rv->getType()->getSize() <= 4) ? "lw" : "ld";
                *os << "  " << lInst << " a0, " << cg.getValueAsOperand(rv) << "\n";
            }
        } else {
            emitLoadValue(cg, cg.getAssembler(), rv, 10); // a0
        }
    }
    if (auto* os = cg.getTextStream()) {
        *os << "  j " << i.getParent()->getParent()->getName() << "_epilogue\n";
    } else {
        emitFunctionEpilogue(cg, *i.getParent()->getParent());
    }
}

void RiscV64Architecture::emitAdd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  ld a0, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            int64_t val = static_cast<int64_t>(ci->getValue());
            if (val >= -2048 && val <= 2047) {
                *os << "  addi a0, a0, " << val << "\n";
                *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
                return;
            }
        }
        *os << "  ld a1, " << cg.getValueAsOperand(r) << "\n";
        *os << "  add a0, a0, a1\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV64Architecture::emitSMin(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  ld a1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  min a0, a0, a1\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV64Architecture::emitSMax(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  ld a1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  max a0, a0, a1\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitSub(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  ld a0, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            int64_t val = static_cast<int64_t>(ci->getValue());
            if (-val >= -2048 && -val <= 2047) {
                *os << "  addi a0, a0, " << -val << "\n";
                *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
                return;
            }
        }
        *os << "  ld a1, " << cg.getValueAsOperand(r) << "\n";
        *os << "  sub a0, a0, a1\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitMul(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  ld a1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  mul a0, a0, a1\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitDiv(CodeGen& cg, ir::Instruction& i) {
    bool u = (i.getOpcode() == ir::Instruction::Udiv);
    if (auto* os = cg.getTextStream()) {
        *os << "  ld a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  ld a1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  " << (u ? "divu " : "div ") << "a0, a0, a1\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitRem(CodeGen& cg, ir::Instruction& i) {
    bool u = (i.getOpcode() == ir::Instruction::Urem);
    if (auto* os = cg.getTextStream()) {
        *os << "  ld a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  ld a1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  " << (u ? "remu " : "rem ") << "a0, a0, a1\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitAnd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  ld a0, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            int64_t val = static_cast<int64_t>(ci->getValue());
            if (val >= -2048 && val <= 2047) {
                *os << "  andi a0, a0, " << val << "\n";
                *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
                return;
            }
        }
        *os << "  ld a1, " << cg.getValueAsOperand(r) << "\n";
        *os << "  and a0, a0, a1\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitOr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  ld a0, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            int64_t val = static_cast<int64_t>(ci->getValue());
            if (val >= -2048 && val <= 2047) {
                *os << "  ori a0, a0, " << val << "\n";
                *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
                return;
            }
        }
        *os << "  ld a1, " << cg.getValueAsOperand(r) << "\n";
        *os << "  or a0, a0, a1\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitXor(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  ld a0, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            int64_t val = static_cast<int64_t>(ci->getValue());
            if (val >= -2048 && val <= 2047) {
                *os << "  xori a0, a0, " << val << "\n";
                *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
                return;
            }
        }
        *os << "  ld a1, " << cg.getValueAsOperand(r) << "\n";
        *os << "  xor a0, a0, a1\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitShl(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  ld a0, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            *os << "  slli a0, a0, " << (ci->getValue() & 63) << "\n";
            *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
            return;
        }
        *os << "  ld a1, " << cg.getValueAsOperand(r) << "\n";
        *os << "  sll a0, a0, a1\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitShr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  ld a0, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            *os << "  srli a0, a0, " << (ci->getValue() & 63) << "\n";
            *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
            return;
        }
        *os << "  ld a1, " << cg.getValueAsOperand(r) << "\n";
        *os << "  srl a0, a0, a1\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitSar(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  ld a0, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            *os << "  srai a0, a0, " << (ci->getValue() & 63) << "\n";
            *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
            return;
        }
        *os << "  ld a1, " << cg.getValueAsOperand(r) << "\n";
        *os << "  sra a0, a0, a1\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitNeg(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  neg a0, a0\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitNot(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  not a0, a0\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitCopy(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitCall(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        for (size_t j = 1; j < std::min(i.getOperands().size(), (size_t)9); ++j) {
            *os << "  ld a" << (j - 1) << ", " << cg.getValueAsOperand(i.getOperands()[j]->get()) << "\n";
        }
        ir::Value* calleeVal = (!i.getOperands().empty() && i.getOperands()[0]) ? i.getOperands()[0]->get() : nullptr;
        bool isDirect = calleeVal && (dynamic_cast<ir::Function*>(calleeVal) != nullptr ||
                                     (dynamic_cast<ir::GlobalValue*>(calleeVal) != nullptr && dynamic_cast<ir::GlobalVariable*>(calleeVal) == nullptr));
        if (isDirect) {
            *os << "  call " << calleeVal->getName() << "\n";
        } else if (calleeVal) {
            *os << "  ld t0, " << cg.getValueAsOperand(calleeVal) << "\n";
            *os << "  jalr t0\n";
        } else {
            *os << "  call unk\n";
        }
        if (i.getType() && !i.getType()->isVoidTy()) {
            *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

bool RiscV64Architecture::emitTailCall(CodeGen& cg, ir::Instruction& callInst, ir::Instruction& retInst) {
    ir::Value* calleeVal = callInst.getOperands()[0]->get();
    if (!calleeVal) return false;

    if (callInst.getType()->getTypeID() != ir::Type::VoidTyID) {
        if (retInst.getOperands().empty() || retInst.getOperands()[0]->get() != &callInst) {
            return false;
        }
    } else {
        if (!retInst.getOperands().empty()) return false;
    }

    size_t numArgs = callInst.getOperands().size() - 1;
    if (numArgs > 8) return false;

    if (auto* os = cg.getTextStream()) {
        for (size_t j = 1; j < callInst.getOperands().size(); ++j) {
            *os << "  ld a" << (j - 1) << ", " << cg.getValueAsOperand(callInst.getOperands()[j]->get()) << "\n";
        }
        bool isDirect = (dynamic_cast<ir::Function*>(calleeVal) != nullptr ||
                        (dynamic_cast<ir::GlobalValue*>(calleeVal) != nullptr && dynamic_cast<ir::GlobalVariable*>(calleeVal) == nullptr));
        if (isDirect) {
            *os << "  j " << calleeVal->getName() << "\n";
        } else {
            *os << "  ld t0, " << cg.getValueAsOperand(calleeVal) << "\n";
            *os << "  jr t0\n";
        }
        return true;
    }
    return false;
}
void RiscV64Architecture::emitFAdd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        bool isFloat = i.getType() && i.getType()->isFloatTy();
        *os << "  fl" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  fl" << (isFloat ? "w" : "d") << " fa1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  fadd." << (isFloat ? "s" : "d") << " fa0, fa0, fa1\n";
        *os << "  fs" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitFSub(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        bool isFloat = i.getType() && i.getType()->isFloatTy();
        *os << "  fl" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  fl" << (isFloat ? "w" : "d") << " fa1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  fsub." << (isFloat ? "s" : "d") << " fa0, fa0, fa1\n";
        *os << "  fs" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitFMul(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        bool isFloat = i.getType() && i.getType()->isFloatTy();
        *os << "  fl" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  fl" << (isFloat ? "w" : "d") << " fa1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  fmul." << (isFloat ? "s" : "d") << " fa0, fa0, fa1\n";
        *os << "  fs" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitFDiv(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        bool isFloat = i.getType() && i.getType()->isFloatTy();
        *os << "  fl" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  fl" << (isFloat ? "w" : "d") << " fa1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  fdiv." << (isFloat ? "s" : "d") << " fa0, fa0, fa1\n";
        *os << "  fs" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitCmp(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  ld a1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        switch (i.getOpcode()) {
            case ir::Instruction::Ceq: *os << "  sub a0, a0, a1\n  seqz a0, a0\n"; break;
            case ir::Instruction::Cne: *os << "  sub a0, a0, a1\n  snez a0, a0\n"; break;
            case ir::Instruction::Cslt: *os << "  slt a0, a0, a1\n"; break;
            case ir::Instruction::Csle: *os << "  slt a0, a1, a0\n  xori a0, a0, 1\n"; break;
            case ir::Instruction::Csgt: *os << "  slt a0, a1, a0\n"; break;
            case ir::Instruction::Csge: *os << "  slt a0, a0, a1\n  xori a0, a0, 1\n"; break;
            default: *os << "  sub a0, a0, a1\n  seqz a0, a0\n"; break;
        }
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* f, const ir::Type* t) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* src = i.getOperands()[0]->get();
        ir::Instruction::Opcode op = i.getOpcode();

        if (op == ir::Instruction::ExtUB) {
            *os << "  ld a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  andi a0, a0, 255\n";
            *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtUH) {
            *os << "  ld a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  andi a0, a0, 65535\n";
            *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtUW) {
            *os << "  ld a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  slli a0, a0, 32\n";
            *os << "  srli a0, a0, 32\n";
            *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtSB) {
            *os << "  ld a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  slli a0, a0, 56\n";
            *os << "  srai a0, a0, 56\n";
            *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtSH) {
            *os << "  ld a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  slli a0, a0, 48\n";
            *os << "  srai a0, a0, 48\n";
            *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtSW) {
            *os << "  ld a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  sext.w a0, a0\n";
            *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::UWtoF || op == ir::Instruction::Ultof) {
            bool isFloat = (t && t->getSize() == 4);
            *os << "  ld a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  fcvt." << (isFloat ? "s" : "d") << ".lu fa0, a0\n";
            *os << "  fs" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::SWtoF || op == ir::Instruction::Sltof) {
            bool isFloat = (t && t->getSize() == 4);
            *os << "  ld a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  fcvt." << (isFloat ? "s" : "d") << ".l fa0, a0\n";
            *os << "  fs" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::DToUI || op == ir::Instruction::SToUI) {
            bool isFloat = (f && f->getSize() == 4);
            *os << "  fl" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  fcvt.lu." << (isFloat ? "s" : "d") << " a0, fa0\n";
            *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::TruncD) {
            *os << "  ld a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  sext.w a0, a0\n";
            *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  ld a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}
void RiscV64Architecture::emitVAStart(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitVAArg(CodeGen& cg, ir::Instruction& i) {}
RiscV64ComplexAddress RiscV64Architecture::matchComplexAddress(CodeGen& cg, ir::Value* val) const {
    RiscV64ComplexAddress result;
    if (!val) return result;

    auto* inst = dynamic_cast<ir::Instruction*>(val);
    if (!inst) return result;

    if (inst->getOpcode() == ir::Instruction::Add && inst->getOperands().size() == 2) {
        ir::Value* op0 = inst->getOperands()[0]->get();
        ir::Value* op1 = inst->getOperands()[1]->get();
        ir::Value* baseVal = nullptr;
        int64_t disp = 0;
        if (auto* c1 = dynamic_cast<ir::ConstantInt*>(op1)) {
            disp = static_cast<int64_t>(c1->getValue());
            baseVal = op0;
        } else if (auto* c0 = dynamic_cast<ir::ConstantInt*>(op0)) {
            disp = static_cast<int64_t>(c0->getValue());
            baseVal = op1;
        }
        if (baseVal && disp >= -2048 && disp <= 2047) {
            std::string bStr = cg.getValueAsOperand(baseVal);
            if (!bStr.empty() && (bStr[0] == 'a' || bStr[0] == 't' || bStr[0] == 's' || bStr[0] == 'x') && bStr.find('(') == std::string::npos) {
                result.base = bStr;
                result.disp = disp;
                result.isValid = true;
                return result;
            }
        }
    }
    return result;
}

void RiscV64Architecture::emitLoad(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        bool isFloat = i.getType() && i.getType()->isFloatingPoint();
        std::string loadMnemonic = "ld";
        size_t size = i.getType() ? i.getType()->getSize() : 8;
        if (isFloat) {
            loadMnemonic = (size == 4) ? "flw" : "fld";
        } else {
            if (size == 1) loadMnemonic = "lb";
            else if (size == 2) loadMnemonic = "lh";
            else if (size == 4) loadMnemonic = "lw";
        }

        ir::Value* ptrVal = i.getOperands()[0]->get();
        RiscV64ComplexAddress addr = matchComplexAddress(cg, ptrVal);
        std::string regName = isFloat ? "fa1" : "a1";
        std::string storeBackInst = isFloat ? ((size == 4) ? "fsw" : "fsd") : "sd";

        if (addr.isValid) {
            *os << "  " << loadMnemonic << " " << regName << ", " << addr.format() << "\n";
            *os << "  " << storeBackInst << " " << regName << ", " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  ld a0, " << cg.getValueAsOperand(ptrVal) << "\n";
            *os << "  " << loadMnemonic << " " << regName << ", 0(a0)\n";
            *os << "  " << storeBackInst << " " << regName << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void RiscV64Architecture::emitStore(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* val = i.getOperands()[0]->get();
        bool isFloat = val->getType() && val->getType()->isFloatingPoint();
        std::string storeMnemonic = "sd";
        size_t size = val->getType() ? val->getType()->getSize() : 8;
        if (isFloat) {
            storeMnemonic = (size == 4) ? "fsw" : "fsd";
        } else {
            if (size == 1) storeMnemonic = "sb";
            else if (size == 2) storeMnemonic = "sh";
            else if (size == 4) storeMnemonic = "sw";
        }

        std::string regName = isFloat ? "fa1" : "a1";
        std::string loadValInst = isFloat ? ((size == 4) ? "flw" : "fld") : "ld";

        ir::Value* ptrVal = i.getOperands()[1]->get();
        RiscV64ComplexAddress addr = matchComplexAddress(cg, ptrVal);
        if (addr.isValid) {
            *os << "  " << loadValInst << " " << regName << ", " << cg.getValueAsOperand(val) << "\n";
            *os << "  " << storeMnemonic << " " << regName << ", " << addr.format() << "\n";
        } else {
            *os << "  ld a0, " << cg.getValueAsOperand(ptrVal) << "\n";
            *os << "  " << loadValInst << " " << regName << ", " << cg.getValueAsOperand(val) << "\n";
            *os << "  " << storeMnemonic << " " << regName << ", 0(a0)\n";
        }
    }
}
void RiscV64Architecture::emitAlloc(CodeGen& cg, ir::Instruction& i) {
    uint64_t size = 8;
    if (i.getOpcode() == ir::Instruction::Alloc4) size = 4;
    else if (i.getOpcode() == ir::Instruction::Alloc16) size = 16;
    else if (!i.getOperands().empty()) {
        if (auto* sizeConst = dynamic_cast<ir::ConstantInt*>(i.getOperands()[0]->get()))
            size = sizeConst->getValue();
    }
    uint64_t alignedSize = (size + 7) & ~7;

    if (auto* os = cg.getTextStream()) {
        *os << "  # RISC-V Bump Allocation\n";
        *os << "  la a0, heap_ptr\n";
        *os << "  ld a1, 0(a0)\n";
        *os << "  sd a1, " << cg.getValueAsOperand(&i) << "\n";
        *os << "  addi a1, a1, " << alignedSize << "\n";
        *os << "  sd a1, 0(a0)\n";
    }
}
void RiscV64Architecture::emitPhiCopies(CodeGen& cg, ir::BasicBlock* source, ir::BasicBlock* target) {
    if (!target) return;
    std::vector<std::pair<ir::Value*, ir::PhiNode*>> phiMoves;
    for (auto& instr : target->getInstructions()) {
        if (auto* phi = dynamic_cast<ir::PhiNode*>(instr.get())) {
            ir::Value* incomingVal = phi->getIncomingValueForBlock(source);
            if (incomingVal) {
                phiMoves.push_back({incomingVal, phi});
            }
        }
    }
    if (phiMoves.empty()) return;

    auto* os = cg.getTextStream();
    if (!os) return;

    bool canDirectMove = true;
    if (phiMoves.size() > 1) {
        for (size_t i = 0; i < phiMoves.size(); ++i) {
            std::string dest_i = cg.getValueAsOperand(phiMoves[i].second);
            for (size_t j = i + 1; j < phiMoves.size(); ++j) {
                std::string src_j = cg.getValueAsOperand(phiMoves[j].first);
                if (dest_i == src_j) {
                    canDirectMove = false;
                    break;
                }
            }
            if (!canDirectMove) break;
        }
    }

    if (canDirectMove) {
        for (const auto& move : phiMoves) {
            ir::Value* incomingVal = move.first;
            ir::PhiNode* phi = move.second;
            std::string srcOp = cg.getValueAsOperand(incomingVal);
            std::string destOp = cg.getValueAsOperand(phi);
            if (srcOp == destOp) continue;

            const ir::Type* type = phi->getType();
            if (type && type->isFloatingPoint()) {
                std::string flInst = (type->getSize() == 4) ? "flw" : "fld";
                std::string fsInst = (type->getSize() == 4) ? "fsw" : "fsd";
                *os << "  " << flInst << " ft0, " << srcOp << "\n";
                *os << "  " << fsInst << " ft0, " << destOp << "\n";
            } else {
                std::string lInst = (type && type->getSize() <= 4) ? "lw" : "ld";
                std::string sInst = (type && type->getSize() <= 4) ? "sw" : "sd";
                *os << "  " << lInst << " t0, " << srcOp << "\n";
                *os << "  " << sInst << " t0, " << destOp << "\n";
            }
        }
        return;
    }

    for (const auto& move : phiMoves) {
        ir::Value* incomingVal = move.first;
        std::string srcOp = cg.getValueAsOperand(incomingVal);
        const ir::Type* type = incomingVal->getType();
        if (type && type->isFloatingPoint()) {
            std::string flInst = (type->getSize() == 4) ? "flw" : "fld";
            *os << "  " << flInst << " ft0, " << srcOp << "\n";
            *os << "  addi sp, sp, -16\n";
            *os << "  fsd ft0, 0(sp)\n";
        } else {
            std::string lInst = (type && type->getSize() <= 4) ? "lw" : "ld";
            *os << "  " << lInst << " t0, " << srcOp << "\n";
            *os << "  addi sp, sp, -16\n";
            *os << "  sd t0, 0(sp)\n";
        }
    }

    for (auto it = phiMoves.rbegin(); it != phiMoves.rend(); ++it) {
        ir::PhiNode* phi = it->second;
        std::string destOp = cg.getValueAsOperand(phi);
        const ir::Type* type = phi->getType();
        if (type && type->isFloatingPoint()) {
            std::string fsInst = (type->getSize() == 4) ? "fsw" : "fsd";
            *os << "  fld ft0, 0(sp)\n";
            *os << "  addi sp, sp, 16\n";
            *os << "  " << fsInst << " ft0, " << destOp << "\n";
        } else {
            std::string sInst = (type && type->getSize() <= 4) ? "sw" : "sd";
            *os << "  ld t0, 0(sp)\n";
            *os << "  addi sp, sp, 16\n";
            *os << "  " << sInst << " t0, " << destOp << "\n";
        }
    }
}

void RiscV64Architecture::emitBr(CodeGen& cg, ir::Instruction& i) {
    if (i.getOperands().size() == 1) {
        auto* targetBB = dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get());
        emitPhiCopies(cg, i.getParent(), targetBB);
        if (auto* os = cg.getTextStream()) {
            *os << "  j " << cg.getTargetInfo()->getBBLabel(targetBB) << "\n";
        }
        return;
    }

    auto* targetTrue = dynamic_cast<ir::BasicBlock*>(i.getOperands()[1]->get());
    auto* targetFalse = dynamic_cast<ir::BasicBlock*>(i.getOperands()[2]->get());

    if (auto* os = cg.getTextStream()) {
        *os << "  ld a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";

        std::string trueLabel = cg.getTargetInfo()->getBBLabel(targetTrue);
        std::string falseLabel = cg.getTargetInfo()->getBBLabel(targetFalse);

        bool trueHasPhis = false;
        if (targetTrue) {
            for (auto& inst : targetTrue->getInstructions()) if (dynamic_cast<ir::PhiNode*>(inst.get())) trueHasPhis = true;
        }
        bool falseHasPhis = false;
        if (targetFalse) {
            for (auto& inst : targetFalse->getInstructions()) if (dynamic_cast<ir::PhiNode*>(inst.get())) falseHasPhis = true;
        }

        if (trueHasPhis || falseHasPhis) {
            std::string labelTrueCopies = ".L_true_copies_" + std::to_string((uintptr_t)&i);
            std::string labelFalseCopies = ".L_false_copies_" + std::to_string((uintptr_t)&i);

            *os << "  bnez a0, " << labelTrueCopies << "\n";
            *os << "  j " << labelFalseCopies << "\n";

            *os << labelTrueCopies << ":\n";
            emitPhiCopies(cg, i.getParent(), targetTrue);
            *os << "  j " << trueLabel << "\n";

            *os << labelFalseCopies << ":\n";
            emitPhiCopies(cg, i.getParent(), targetFalse);
            *os << "  j " << falseLabel << "\n";
        } else {
            *os << "  bnez a0, " << trueLabel << "\n";
            *os << "  j " << falseLabel << "\n";
        }
    }
}

void RiscV64Architecture::emitJmp(CodeGen& cg, ir::Instruction& i) {
    auto* targetBB = dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get());
    emitPhiCopies(cg, i.getParent(), targetBB);
    if (auto* os = cg.getTextStream()) {
        *os << "  j " << cg.getTargetInfo()->getBBLabel(targetBB) << "\n";
    }
}

bool RiscV64Architecture::emitMulAddFusion(CodeGen& cg, ir::Instruction& mul, ir::Instruction& add) {
    if (mul.getUseList().size() != 1) return false;
    if (!mul.getType() || !mul.getType()->isInteger()) return false;
    if (!add.getType() || !add.getType()->isInteger()) return false;

    ir::Value* m0 = mul.getOperands()[0]->get();
    ir::Value* m1 = mul.getOperands()[1]->get();
    ir::Value* a0 = add.getOperands()[0]->get();
    ir::Value* a1 = add.getOperands()[1]->get();

    if (a0 != &mul && a1 != &mul) return false;

    ir::Value* addOther = (a0 == &mul) ? a1 : a0;

    if (auto* os = cg.getTextStream()) {
        *os << "  ld a0, " << cg.getValueAsOperand(m0) << "\n";
        *os << "  ld a1, " << cg.getValueAsOperand(m1) << "\n";
        *os << "  ld a2, " << cg.getValueAsOperand(addOther) << "\n";
        *os << "  mul a0, a0, a1\n";
        *os << "  add a0, a0, a2\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&add) << "\n";
        return true;
    }
    return false;
}

bool RiscV64Architecture::emitCmpAndBranchFusion(CodeGen& cg, ir::Instruction& cmp, ir::Instruction& br) {
    if (br.getOperands().size() < 3) return false;
    auto* targetTrue = dynamic_cast<ir::BasicBlock*>(br.getOperands()[1]->get());
    auto* targetFalse = dynamic_cast<ir::BasicBlock*>(br.getOperands()[2]->get());
    if (!targetTrue || !targetFalse) return false;

    ir::Value* l = cmp.getOperands()[0]->get();
    ir::Value* r = cmp.getOperands()[1]->get();

    std::string bOp = "beq";
    bool swapOps = false;
    bool isFloatCmp = (l->getType() && l->getType()->isFloatingPoint());

    if (isFloatCmp) {
        bool isFloat = (l->getType()->getSize() == 4);
        std::string fcmpInst = "feq." + std::string(isFloat ? "s" : "d");
        bool invertFloat = false;

        switch (cmp.getOpcode()) {
            case ir::Instruction::Ceqf: fcmpInst = "feq." + std::string(isFloat ? "s" : "d"); break;
            case ir::Instruction::Cnef: fcmpInst = "feq." + std::string(isFloat ? "s" : "d"); invertFloat = true; break;
            case ir::Instruction::Clt:  fcmpInst = "flt." + std::string(isFloat ? "s" : "d"); break;
            case ir::Instruction::Cle:  fcmpInst = "fle." + std::string(isFloat ? "s" : "d"); break;
            case ir::Instruction::Cgt:  fcmpInst = "flt." + std::string(isFloat ? "s" : "d"); swapOps = true; break;
            case ir::Instruction::Cge:  fcmpInst = "fle." + std::string(isFloat ? "s" : "d"); swapOps = true; break;
            default:                    fcmpInst = "feq." + std::string(isFloat ? "s" : "d"); break;
        }

        if (swapOps) std::swap(l, r);

        if (auto* os = cg.getTextStream()) {
            *os << "  fl" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(l) << "\n";
            *os << "  fl" << (isFloat ? "w" : "d") << " fa1, " << cg.getValueAsOperand(r) << "\n";
            *os << "  " << fcmpInst << " a0, fa0, fa1\n";

            bOp = invertFloat ? "beqz" : "bnez";
        }
    } else {
        switch (cmp.getOpcode()) {
            case ir::Instruction::Ceq:  bOp = "beq"; break;
            case ir::Instruction::Cne:  bOp = "bne"; break;
            case ir::Instruction::Cslt: bOp = "blt"; break;
            case ir::Instruction::Cult: bOp = "bltu"; break;
            case ir::Instruction::Csge: bOp = "bge"; break;
            case ir::Instruction::Cuge: bOp = "bgeu"; break;
            case ir::Instruction::Csgt: bOp = "blt"; swapOps = true; break;
            case ir::Instruction::Cugt: bOp = "bltu"; swapOps = true; break;
            case ir::Instruction::Csle: bOp = "bge"; swapOps = true; break;
            case ir::Instruction::Cule: bOp = "bgeu"; swapOps = true; break;
            default:                    bOp = "bne"; break;
        }

        if (swapOps) std::swap(l, r);

        if (auto* os = cg.getTextStream()) {
            *os << "  ld a0, " << cg.getValueAsOperand(l) << "\n";
            *os << "  ld a1, " << cg.getValueAsOperand(r) << "\n";
        }
    }

    if (auto* os = cg.getTextStream()) {
        std::string trueLabel = cg.getTargetInfo()->getBBLabel(targetTrue);
        std::string falseLabel = cg.getTargetInfo()->getBBLabel(targetFalse);

        bool trueHasPhis = false;
        if (targetTrue) {
            for (auto& inst : targetTrue->getInstructions()) if (dynamic_cast<ir::PhiNode*>(inst.get())) trueHasPhis = true;
        }
        bool falseHasPhis = false;
        if (targetFalse) {
            for (auto& inst : targetFalse->getInstructions()) if (dynamic_cast<ir::PhiNode*>(inst.get())) falseHasPhis = true;
        }

        if (trueHasPhis || falseHasPhis) {
            std::string labelTrueCopies = ".L_true_copies_" + std::to_string((uintptr_t)&br);
            std::string labelFalseCopies = ".L_false_copies_" + std::to_string((uintptr_t)&br);

            if (bOp == "bnez" || bOp == "beqz") *os << "  " << bOp << " a0, " << labelTrueCopies << "\n";
            else *os << "  " << bOp << " a0, a1, " << labelTrueCopies << "\n";
            *os << "  j " << labelFalseCopies << "\n";

            *os << labelTrueCopies << ":\n";
            emitPhiCopies(cg, br.getParent(), targetTrue);
            *os << "  j " << trueLabel << "\n";

            *os << labelFalseCopies << ":\n";
            emitPhiCopies(cg, br.getParent(), targetFalse);
            *os << "  j " << falseLabel << "\n";
        } else {
            if (bOp == "bnez" || bOp == "beqz") *os << "  " << bOp << " a0, " << trueLabel << "\n";
            else *os << "  " << bOp << " a0, a1, " << trueLabel << "\n";
            *os << "  j " << falseLabel << "\n";
        }
        return true;
    }
    return false;
}

void RiscV64Architecture::emitSyscall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {
    if (auto* os = cg.getTextStream()) {
        *os << "  li a7, " << osInfo.getSyscallNumber(dynamic_cast<ir::SyscallInstruction*>(&i)->getSyscallId()) << "\n";
        for (size_t j = 0; j < std::min(i.getOperands().size(), (size_t)6); ++j) *os << "  ld a" << j << ", " << cg.getValueAsOperand(i.getOperands()[j]->get()) << "\n";
        *os << "  ecall\n";
    }
}

void RiscV64Architecture::emitExternCall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {
    auto* ei = dynamic_cast<ir::ExternCallInstruction*>(&i); if (!ei) return;
    const auto* spec = cg.getTargetInfo()->findCapability(ei->getCapability());
    if (!spec || !cg.getTargetInfo()->validateCapability(i, *spec)) { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, spec); return; }
    cg.getTargetInfo()->emitDomainCapability(cg, i, *spec);
}

void RiscV64Architecture::emitNativeSyscall(CodeGen& cg, uint64_t syscallNum, const std::vector<ir::Value*>& args) {
    if (auto* os = cg.getTextStream()) {
        *os << "  li a7, " << syscallNum << "\n";
        for (size_t i = 0; i < std::min(args.size(), (size_t)6); ++i) {
            *os << "  ld a" << i << ", " << cg.getValueAsOperand(args[i]) << "\n";
        }
        *os << "  ecall\n";
    }
}

void RiscV64Architecture::emitNativeLibraryCall(CodeGen& cg, const std::string& name, const std::vector<ir::Value*>& args) {
    if (auto* os = cg.getTextStream()) {
        for (size_t i = 0; i < std::min(args.size(), (size_t)8); ++i) {
            *os << "  ld a" << i << ", " << cg.getValueAsOperand(args[i]) << "\n";
        }
        *os << "  call " << name << "\n";
    }
}

std::string RiscV64Architecture::formatStackOperand(int o) const { return std::to_string(o) + "(s0)"; }
std::string RiscV64Architecture::formatGlobalOperand(const std::string& n) const { return n; }
bool RiscV64Architecture::isCallerSaved(const std::string& r) const { return (r[0] == 't' || r[0] == 'a' || r[0] == 'f' || r[0] == 'v'); }
bool RiscV64Architecture::isCalleeSaved(const std::string& r) const { return (r[0] == 's' || r == "gp" || r == "tp" || r == "fp"); }

void RiscV64Architecture::emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) {}
void RiscV64Architecture::emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) {}

VectorCapabilities RiscV64Architecture::getVectorCapabilities() const {
    VectorCapabilities caps;
    caps.maxVectorWidth = 128;
    caps.supportedWidths = {64, 128};
    caps.supportsFloatVectors = true;
    caps.supportsIntegerVectors = true;
    caps.supportsDoubleVectors = true;
    caps.simdExtension = "RVV";
    return caps;
}

bool RiscV64Architecture::supportsVectorWidth(unsigned width) const {
    return width == 64 || width == 128;
}

bool RiscV64Architecture::supportsVectorType(const ir::VectorType* type) const {
    if (!type) return false;
    unsigned totalBits = static_cast<unsigned>(type->getSize() * 8);
    if (totalBits != 64 && totalBits != 128) return false;
    auto* elemTy = type->getElementType();
    if (!elemTy) return false;
    size_t elemBits = elemTy->getSize() * 8;
    return elemBits == 8 || elemBits == 16 || elemBits == 32 || elemBits == 64;
}

bool RiscV64Architecture::supportsVectorOperation(ir::Instruction::Opcode op, const ir::VectorType* type) const {
    if (!supportsVectorType(type)) return false;
    switch (op) {
        case ir::Instruction::VAdd:
        case ir::Instruction::VSub:
        case ir::Instruction::VMul:
        case ir::Instruction::VFAdd:
        case ir::Instruction::VFSub:
        case ir::Instruction::VFMul:
        case ir::Instruction::VFDiv:
        case ir::Instruction::VAnd:
        case ir::Instruction::VOr:
        case ir::Instruction::VXor:
        case ir::Instruction::VLoad:
        case ir::Instruction::VStore:
        case ir::Instruction::VBroadcast:
        case ir::Instruction::VExtract:
        case ir::Instruction::VInsert:
        case ir::Instruction::VCmp:
        case ir::Instruction::VSelect:
        case ir::Instruction::VMin:
        case ir::Instruction::VMax:
        case ir::Instruction::VFMin:
        case ir::Instruction::VFMax:
        case ir::Instruction::VSExt:
        case ir::Instruction::VZExt:
        case ir::Instruction::VTrunc:
        case ir::Instruction::VGather:
        case ir::Instruction::VScatter:
            return true;
        default:
            return false;
    }
}

bool RiscV64Architecture::supportsVectorConversion(ir::Instruction::Opcode op, const ir::VectorType* srcType, const ir::VectorType* dstType) const {
    if (!srcType || !dstType) return false;
    if (op == ir::Instruction::VSExt || op == ir::Instruction::VZExt || op == ir::Instruction::VTrunc) return true;
    return false;
}

void RiscV64Architecture::emitVectorLoad(CodeGen& cg, ir::VectorInstruction& i) {
    auto* os = cg.getTextStream();
    if (!os) return;
    auto* vecTy = dynamic_cast<const ir::VectorType*>(i.getType());
    unsigned numElems = vecTy ? vecTy->getNumElements() : 4;
    unsigned elemBits = (vecTy && vecTy->getElementType()) ? vecTy->getElementType()->getSize() * 8 : 32;
    std::string eew = "e" + std::to_string(elemBits);
    std::string ptr = cg.getValueAsOperand(i.getOperands()[0]->get());
    std::string dst = cg.getValueAsOperand(&i);
    if (dst.empty() || dst[0] != 'v') dst = "v0";

    *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
    *os << "  ld a0, " << ptr << "\n";
    *os << "  vle" << elemBits << ".v " << dst << ", (a0)\n";
}

void RiscV64Architecture::emitVectorStore(CodeGen& cg, ir::VectorInstruction& i) {
    auto* os = cg.getTextStream();
    if (!os) return;
    auto* vecTy = dynamic_cast<const ir::VectorType*>(i.getOperands()[0]->get()->getType());
    unsigned numElems = vecTy ? vecTy->getNumElements() : 4;
    unsigned elemBits = (vecTy && vecTy->getElementType()) ? vecTy->getElementType()->getSize() * 8 : 32;
    std::string eew = "e" + std::to_string(elemBits);
    std::string val = cg.getValueAsOperand(i.getOperands()[0]->get());
    std::string ptr = cg.getValueAsOperand(i.getOperands()[1]->get());
    if (val.empty() || val[0] != 'v') val = "v0";

    *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
    *os << "  ld a0, " << ptr << "\n";
    *os << "  vse" << elemBits << ".v " << val << ", (a0)\n";
}

void RiscV64Architecture::emitVectorReduction(CodeGen& cg, ir::VectorInstruction& i) {
    auto* os = cg.getTextStream();
    if (!os) return;

    auto* vecTy = dynamic_cast<const ir::VectorType*>(i.getOperands()[0]->get()->getType());
    unsigned numElems = vecTy ? vecTy->getNumElements() : 4;
    unsigned elemBits = (vecTy && vecTy->getElementType()) ? vecTy->getElementType()->getSize() * 8 : 32;
    bool isFloat = (vecTy && vecTy->getElementType() && vecTy->getElementType()->isFloatingPoint());
    std::string eew = "e" + std::to_string(elemBits);
    std::string val = cg.getValueAsOperand(i.getOperands()[0]->get());
    std::string dst = cg.getValueAsOperand(&i);

    if (val.empty() || val[0] != 'v') {
        *os << "  vle" << elemBits << ".v v0, (" << val << ")\n";
        val = "v0";
    }

    *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
    if (isFloat) {
        switch (i.getOpcode()) {
            case ir::Instruction::VAdd: *os << "  vfredusum.vs v1, " << val << ", v1\n"; break;
            case ir::Instruction::VFMin: *os << "  vfredmin.vs v1, " << val << ", v1\n"; break;
            case ir::Instruction::VFMax: *os << "  vfredmax.vs v1, " << val << ", v1\n"; break;
            default: *os << "  vfredusum.vs v1, " << val << ", v1\n"; break;
        }
        *os << "  vfmv.f.s fa0, v1\n";
        *os << "  fs" << (elemBits == 32 ? "w" : "d") << " fa0, " << dst << "\n";
    } else {
        switch (i.getOpcode()) {
            case ir::Instruction::VAdd: *os << "  vredsum.vs v1, " << val << ", v1\n"; break;
            case ir::Instruction::VMin: *os << "  vredmin.vs v1, " << val << ", v1\n"; break;
            case ir::Instruction::VMax: *os << "  vredmax.vs v1, " << val << ", v1\n"; break;
            default: *os << "  vredsum.vs v1, " << val << ", v1\n"; break;
        }
        *os << "  vmv.x.s a0, v1\n";
        *os << "  sd a0, " << dst << "\n";
    }
}

void RiscV64Architecture::emitVectorHorizontalOp(CodeGen& cg, ir::VectorInstruction& i) {
    emitVectorReduction(cg, i);
}

void RiscV64Architecture::emitVectorArithmetic(CodeGen& cg, ir::VectorInstruction& i) {
    auto* os = cg.getTextStream();
    if (!os) return;

    auto* vecTy = dynamic_cast<const ir::VectorType*>(i.getType());
    if (!vecTy && !i.getOperands().empty() && i.getOperands()[0]->get()) {
        vecTy = dynamic_cast<const ir::VectorType*>(i.getOperands()[0]->get()->getType());
    }

    unsigned numElems = vecTy ? vecTy->getNumElements() : 4;
    unsigned elemBits = vecTy && vecTy->getElementType() ? vecTy->getElementType()->getSize() * 8 : 32;
    std::string eew = "e" + std::to_string(elemBits);

    std::string dst = cg.getValueAsOperand(&i);
    std::string op0 = !i.getOperands().empty() && i.getOperands()[0]->get() ? cg.getValueAsOperand(i.getOperands()[0]->get()) : "v0";
    std::string op1 = i.getOperands().size() > 1 && i.getOperands()[1]->get() ? cg.getValueAsOperand(i.getOperands()[1]->get()) : "v1";

    if (dst.empty() || dst[0] != 'v') dst = "v0";
    if (op0.empty() || op0[0] != 'v') op0 = "v0";
    if (op1.empty() || op1[0] != 'v') op1 = "v1";

    switch (i.getOpcode()) {
        case ir::Instruction::VLoad: {
            std::string ptr = cg.getValueAsOperand(i.getOperands()[0]->get());
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  ld a0, " << ptr << "\n";
            *os << "  vle" << elemBits << ".v " << dst << ", (a0)\n";
            break;
        }
        case ir::Instruction::VStore: {
            std::string ptr = cg.getValueAsOperand(i.getOperands()[1]->get());
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  ld a0, " << ptr << "\n";
            *os << "  vse" << elemBits << ".v " << op0 << ", (a0)\n";
            break;
        }
        case ir::Instruction::VAdd: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vadd.vv " << dst << ", " << op0 << ", " << op1 << "\n";
            break;
        }
        case ir::Instruction::VSub: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vsub.vv " << dst << ", " << op0 << ", " << op1 << "\n";
            break;
        }
        case ir::Instruction::VMul: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vmul.vv " << dst << ", " << op0 << ", " << op1 << "\n";
            break;
        }
        case ir::Instruction::VFAdd: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vfadd.vv " << dst << ", " << op0 << ", " << op1 << "\n";
            break;
        }
        case ir::Instruction::VFSub: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vfsub.vv " << dst << ", " << op0 << ", " << op1 << "\n";
            break;
        }
        case ir::Instruction::VFMul: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vfmul.vv " << dst << ", " << op0 << ", " << op1 << "\n";
            break;
        }
        case ir::Instruction::VFDiv: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vfdiv.vv " << dst << ", " << op0 << ", " << op1 << "\n";
            break;
        }
        case ir::Instruction::VAnd: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vand.vv " << dst << ", " << op0 << ", " << op1 << "\n";
            break;
        }
        case ir::Instruction::VOr: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vor.vv " << dst << ", " << op0 << ", " << op1 << "\n";
            break;
        }
        case ir::Instruction::VXor: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vxor.vv " << dst << ", " << op0 << ", " << op1 << "\n";
            break;
        }
        case ir::Instruction::VMin: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vmin.vv " << dst << ", " << op0 << ", " << op1 << "\n";
            break;
        }
        case ir::Instruction::VMax: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vmax.vv " << dst << ", " << op0 << ", " << op1 << "\n";
            break;
        }
        case ir::Instruction::VFMin: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vfmin.vv " << dst << ", " << op0 << ", " << op1 << "\n";
            break;
        }
        case ir::Instruction::VFMax: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vfmax.vv " << dst << ", " << op0 << ", " << op1 << "\n";
            break;
        }
        case ir::Instruction::VBroadcast: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            bool isFloat = (vecTy && vecTy->getElementType() && vecTy->getElementType()->isFloatingPoint());
            std::string scalar = cg.getValueAsOperand(i.getOperands()[0]->get());
            if (isFloat) {
                *os << "  fl" << (elemBits == 32 ? "w" : "d") << " fa0, " << scalar << "\n";
                *os << "  vfmv.v.f " << dst << ", fa0\n";
            } else {
                *os << "  ld a0, " << scalar << "\n";
                *os << "  vmv.v.x " << dst << ", a0\n";
            }
            break;
        }
        case ir::Instruction::VExtract: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            bool isFloat = (vecTy && vecTy->getElementType() && vecTy->getElementType()->isFloatingPoint());
            std::string rawDst = cg.getValueAsOperand(&i);
            uint64_t idx = 0;
            if (i.getOperands().size() > 1 && i.getOperands()[1]->get()) {
                if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
                    idx = ci->getValue();
                }
            }
            std::string srcVec = op0;
            if (idx > 0) {
                *os << "  vslidedown.vi v1, " << op0 << ", " << idx << "\n";
                srcVec = "v1";
            }
            if (isFloat) {
                *os << "  vfmv.f.s fa0, " << srcVec << "\n";
                *os << "  fs" << (elemBits == 32 ? "w" : "d") << " fa0, " << rawDst << "\n";
            } else {
                *os << "  vmv.x.s a0, " << srcVec << "\n";
                *os << "  sd a0, " << rawDst << "\n";
            }
            break;
        }
        case ir::Instruction::VInsert: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            bool isFloat = (vecTy && vecTy->getElementType() && vecTy->getElementType()->isFloatingPoint());
            std::string valStr = cg.getValueAsOperand(i.getOperands()[1]->get());
            uint64_t idx = 0;
            if (i.getOperands().size() > 2 && i.getOperands()[2]->get()) {
                if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[2]->get())) {
                    idx = ci->getValue();
                }
            }
            if (isFloat) {
                *os << "  fl" << (elemBits == 32 ? "w" : "d") << " fa0, " << valStr << "\n";
                if (idx == 0) {
                    *os << "  vfmv.s.f " << dst << ", fa0\n";
                } else {
                    *os << "  vfmv.s.f v1, fa0\n";
                    *os << "  vslideup.vi " << dst << ", v1, " << idx << "\n";
                }
            } else {
                *os << "  ld a0, " << valStr << "\n";
                if (idx == 0) {
                    *os << "  vmv.s.x " << dst << ", a0\n";
                } else {
                    *os << "  vmv.s.x v1, a0\n";
                    *os << "  vslideup.vi " << dst << ", v1, " << idx << "\n";
                }
            }
            break;
        }
        case ir::Instruction::VCmp: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vmseq.vv " << dst << ", " << op0 << ", " << op1 << "\n";
            break;
        }
        case ir::Instruction::VSelect: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            std::string mask = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string tVal = cg.getValueAsOperand(i.getOperands()[1]->get());
            std::string fVal = cg.getValueAsOperand(i.getOperands()[2]->get());
            if (mask.empty() || mask[0] != 'v') mask = "v0";
            if (tVal.empty() || tVal[0] != 'v') tVal = "v1";
            if (fVal.empty() || fVal[0] != 'v') fVal = "v2";
            *os << "  vmerge.vvm " << dst << ", " << fVal << ", " << tVal << ", " << mask << "\n";
            break;
        }
        case ir::Instruction::VSExt: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vsext.vf2 " << dst << ", " << op0 << "\n";
            break;
        }
        case ir::Instruction::VZExt: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vzext.vf2 " << dst << ", " << op0 << "\n";
            break;
        }
        case ir::Instruction::VTrunc: {
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  vncvt.x.x.w " << dst << ", " << op0 << "\n";
            break;
        }
        case ir::Instruction::VGather: {
            std::string basePtr = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string indexVec = op1;
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  ld a0, " << basePtr << "\n";
            *os << "  vluxei32.v " << dst << ", (a0), " << indexVec << "\n";
            break;
        }
        case ir::Instruction::VScatter: {
            std::string basePtr = cg.getValueAsOperand(i.getOperands()[1]->get());
            std::string indexVec = (i.getOperands().size() > 2 && i.getOperands()[2]->get()) ? cg.getValueAsOperand(i.getOperands()[2]->get()) : "v1";
            if (indexVec.empty() || indexVec[0] != 'v') indexVec = "v1";
            *os << "  vsetvli t0, " << numElems << ", " << eew << ", m1, ta, ma\n";
            *os << "  ld a0, " << basePtr << "\n";
            *os << "  vsuxei32.v " << op0 << ", (a0), " << indexVec << "\n";
            break;
        }
        default:
            *os << "  # Unsupported RISC-V vector opcode: " << i.getOpcode() << "\n";
            break;
    }
}

}
