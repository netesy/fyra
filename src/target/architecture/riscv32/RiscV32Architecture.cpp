#include "ir/Instruction.h"
#include "ir/Use.h"
#include "ir/Constant.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/PhiNode.h"
#include "target/architecture/riscv32/RiscV32Architecture.h"
#include "codegen/CodeGen.h"
#include "codegen/asm/Assembler.h"
#include "target/core/OperatingSystemInfo.h"
#include <ostream>
#include <vector>

namespace target {

RiscV32Architecture::RiscV32Architecture() {}

void RiscV32Architecture::emitLoadValue(CodeGen& cg, asm_::Assembler& as, ir::Value* val, uint8_t reg) {
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
        // RV32: values fit in 32-bit, handle immediates
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

void RiscV32Architecture::emitStoreResult(CodeGen& cg, ir::Instruction& instr, uint8_t reg) {
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

TypeInfo RiscV32Architecture::getTypeInfo(const ir::Type* type) const {
    // RV32: pointer size is 32 bits
    return {type->getSize() * 8, type->getAlignment() * 8, type->isFloatingPoint() ? RegisterClass::Float : RegisterClass::Integer, type->isFloatingPoint(), true};
}

const std::vector<std::string>& RiscV32Architecture::getRegisters(RegisterClass regClass) const {
    static const std::vector<std::string> intRegs = {"x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x5", "x6", "x7", "x28", "x29", "x30", "x31"};
    static const std::vector<std::string> floatRegs = {"f10", "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f5", "f6", "f7", "f28", "f29", "f30", "f31"};
    static const std::vector<std::string> vecRegs = {};
    return (regClass == RegisterClass::Float) ? floatRegs : (regClass == RegisterClass::Vector ? vecRegs : intRegs);
}

const std::string& RiscV32Architecture::getReturnRegister(const ir::Type* type) const {
    static const std::string i = "a0", f = "fa0"; return type->isFloatingPoint() ? f : i;
}
const std::vector<std::string>& RiscV32Architecture::getIntegerArgumentRegisters() const { static const std::vector<std::string> r = {"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"}; return r; }
const std::vector<std::string>& RiscV32Architecture::getFloatArgumentRegisters() const { static const std::vector<std::string> r = {"fa0", "fa1", "fa2", "fa3", "fa4", "fa5", "fa6", "fa7"}; return r; }
const std::string& RiscV32Architecture::getIntegerReturnRegister() const { static const std::string r = "a0"; return r; }
const std::string& RiscV32Architecture::getFloatReturnRegister() const { static const std::string r = "fa0"; return r; }

void RiscV32Architecture::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    currentStackOffset = -16; int i_idx = 0, f_idx = 0, s_arg_idx = 0;
    for (auto& param : func.getParameters()) {
        TypeInfo inf = getTypeInfo(param->getType());
        if (inf.regClass == RegisterClass::Float) { if (f_idx < 8) { currentStackOffset -= 4; cg.getStackOffsets()[param.get()] = currentStackOffset; f_idx++; } else cg.getStackOffsets()[param.get()] = (s_arg_idx++) * 4; }
        else { if (i_idx < 8) { currentStackOffset -= 4; cg.getStackOffsets()[param.get()] = currentStackOffset; i_idx++; } else cg.getStackOffsets()[param.get()] = (s_arg_idx++) * 4; }
    }
    for (auto& bb : func.getBasicBlocks()) { for (auto& instr : bb->getInstructions()) { if (instr->getType()->getTypeID() != ir::Type::VoidTyID) { currentStackOffset -= 4; cg.getStackOffsets()[instr.get()] = currentStackOffset; } } }
    int stack_size = (-currentStackOffset + 15) & ~15;
    if (auto* os = cg.getTextStream()) {
        *os << "  addi sp, sp, -" << stack_size << "\n  sw ra, " << stack_size - 4 << "(sp)\n  sw s0, " << stack_size - 8 << "(sp)\n  addi s0, sp, " << stack_size << "\n";
    } else {
        cg.getAssembler().emitDWord(((-stack_size & 0xFFF) << 20) | (2 << 15) | (0 << 12) | (2 << 7) | 0x13); // addi sp, sp, -stack_size
        uint32_t off1 = (stack_size - 4) & 0xFFF;
        cg.getAssembler().emitDWord((((off1 >> 5) & 0x7F) << 25) | (1 << 20) | (2 << 15) | (2 << 12) | ((off1 & 0x1F) << 7) | 0x23); // sw ra, (stack_size-4)(sp)
        uint32_t off2 = (stack_size - 8) & 0xFFF;
        cg.getAssembler().emitDWord((((off2 >> 5) & 0x7F) << 25) | (8 << 20) | (2 << 15) | (2 << 12) | ((off2 & 0x1F) << 7) | 0x23); // sw s0, (stack_size-8)(sp)
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

void RiscV32Architecture::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << func.getName() << "_epilogue:\n  lw ra, -4(s0)\n  lw s0, -8(s0)\n  addi sp, s0, 0\n  jr ra\n";
    } else {
        cg.getAssembler().emitDWord(((-4 & 0xFFF) << 20) | (8 << 15) | (2 << 12) | (1 << 7) | 0x03); // lw ra, -4(s0)
        cg.getAssembler().emitDWord(((-8 & 0xFFF) << 20) | (8 << 15) | (2 << 12) | (8 << 7) | 0x03); // lw s0, -8(s0)
        cg.getAssembler().emitDWord((0 << 20) | (8 << 15) | (0 << 12) | (2 << 7) | 0x13); // addi sp, s0, 0
        cg.getAssembler().emitDWord(0x00008067); // jr ra (jalr x0, ra, 0)
    }
}

void RiscV32Architecture::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << ".text\n.globl _start\n_start:\n  call main\n  li a7, 93\n  ecall\n";
    } else {
        cg.addRelocation({cg.getAssembler().getCodeSize(), "R_RISCV_CALL", 0, "main", ".text"});
        cg.getAssembler().emitDWord(0x000000EF); // jal ra, 0
        cg.getAssembler().emitDWord(((93 & 0xFFF) << 20) | (0 << 15) | (0 << 12) | (17 << 7) | 0x13); // li a7, 93
        cg.getAssembler().emitDWord(0x00000073); // ecall
    }
}

void RiscV32Architecture::emitRet(CodeGen& cg, ir::Instruction& i) {
    if (!i.getOperands().empty() && i.getOperands()[0] && i.getOperands()[0]->get() != nullptr) {
        ir::Value* rv = i.getOperands()[0]->get();
        if (auto* os = cg.getTextStream()) {
            if (rv->getType() && rv->getType()->isFloatingPoint()) {
                std::string flInst = (rv->getType()->getSize() == 4) ? "flw" : "fld";
                *os << "  " << flInst << " fa0, " << cg.getValueAsOperand(rv) << "\n";
            } else {
                std::string lInst = (rv->getType() && rv->getType()->getSize() <= 4) ? "lw" : "lw";
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

void RiscV32Architecture::emitAdd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  lw a0, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            int64_t val = static_cast<int64_t>(ci->getValue());
            if (val >= -2048 && val <= 2047) {
                *os << "  addi a0, a0, " << val << "\n";
                *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
                return;
            }
        }
        *os << "  lw a1, " << cg.getValueAsOperand(r) << "\n";
        *os << "  add a0, a0, a1\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitSMin(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  lw a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  lw a1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  min a0, a0, a1\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitSMax(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  lw a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  lw a1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  max a0, a0, a1\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitSub(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  lw a0, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            int64_t val = static_cast<int64_t>(ci->getValue());
            if (-val >= -2048 && -val <= 2047) {
                *os << "  addi a0, a0, " << -val << "\n";
                *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
                return;
            }
        }
        *os << "  lw a1, " << cg.getValueAsOperand(r) << "\n";
        *os << "  sub a0, a0, a1\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitMul(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  lw a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  lw a1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  mul a0, a0, a1\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitDiv(CodeGen& cg, ir::Instruction& i) {
    bool u = (i.getOpcode() == ir::Instruction::Udiv);
    if (auto* os = cg.getTextStream()) {
        *os << "  lw a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  lw a1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  " << (u ? "divu " : "div ") << "a0, a0, a1\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitRem(CodeGen& cg, ir::Instruction& i) {
    bool u = (i.getOpcode() == ir::Instruction::Urem);
    if (auto* os = cg.getTextStream()) {
        *os << "  lw a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  lw a1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  " << (u ? "remu " : "rem ") << "a0, a0, a1\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitAnd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  lw a0, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            int64_t val = static_cast<int64_t>(ci->getValue());
            if (val >= -2048 && val <= 2047) {
                *os << "  andi a0, a0, " << val << "\n";
                *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
                return;
            }
        }
        *os << "  lw a1, " << cg.getValueAsOperand(r) << "\n";
        *os << "  and a0, a0, a1\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitOr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  lw a0, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            int64_t val = static_cast<int64_t>(ci->getValue());
            if (val >= -2048 && val <= 2047) {
                *os << "  ori a0, a0, " << val << "\n";
                *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
                return;
            }
        }
        *os << "  lw a1, " << cg.getValueAsOperand(r) << "\n";
        *os << "  or a0, a0, a1\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitXor(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  lw a0, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            int64_t val = static_cast<int64_t>(ci->getValue());
            if (val >= -2048 && val <= 2047) {
                *os << "  xori a0, a0, " << val << "\n";
                *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
                return;
            }
        }
        *os << "  lw a1, " << cg.getValueAsOperand(r) << "\n";
        *os << "  xor a0, a0, a1\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitShl(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  lw a0, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            *os << "  slli a0, a0, " << (ci->getValue() & 31) << "\n";
            *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
            return;
        }
        *os << "  lw a1, " << cg.getValueAsOperand(r) << "\n";
        *os << "  sll a0, a0, a1\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitShr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  lw a0, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            *os << "  srli a0, a0, " << (ci->getValue() & 31) << "\n";
            *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
            return;
        }
        *os << "  lw a1, " << cg.getValueAsOperand(r) << "\n";
        *os << "  srl a0, a0, a1\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitSar(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* l = i.getOperands()[0]->get();
        ir::Value* r = i.getOperands()[1]->get();
        *os << "  lw a0, " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            *os << "  srai a0, a0, " << (ci->getValue() & 31) << "\n";
            *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
            return;
        }
        *os << "  lw a1, " << cg.getValueAsOperand(r) << "\n";
        *os << "  sra a0, a0, a1\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitNeg(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  lw a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  neg a0, a0\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitNot(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  lw a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  not a0, a0\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitCopy(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  lw a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitCall(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        for (size_t j = 1; j < std::min(i.getOperands().size(), (size_t)9); ++j) {
            *os << "  lw a" << (j - 1) << ", " << cg.getValueAsOperand(i.getOperands()[j]->get()) << "\n";
        }
        ir::Value* calleeVal = (!i.getOperands().empty() && i.getOperands()[0]) ? i.getOperands()[0]->get() : nullptr;
        bool isDirect = calleeVal && (dynamic_cast<ir::Function*>(calleeVal) != nullptr ||
                                     (dynamic_cast<ir::GlobalValue*>(calleeVal) != nullptr && dynamic_cast<ir::GlobalVariable*>(calleeVal) == nullptr));
        if (isDirect) {
            *os << "  call " << calleeVal->getName() << "\n";
        } else if (calleeVal) {
            *os << "  lw t0, " << cg.getValueAsOperand(calleeVal) << "\n";
            *os << "  jalr t0\n";
        } else {
            *os << "  call unk\n";
        }
        if (i.getType() && !i.getType()->isVoidTy()) {
            *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

bool RiscV32Architecture::emitTailCall(CodeGen& cg, ir::Instruction& callInst, ir::Instruction& retInst) {
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
            *os << "  lw a" << (j - 1) << ", " << cg.getValueAsOperand(callInst.getOperands()[j]->get()) << "\n";
        }
        bool isDirect = (dynamic_cast<ir::Function*>(calleeVal) != nullptr ||
                        (dynamic_cast<ir::GlobalValue*>(calleeVal) != nullptr && dynamic_cast<ir::GlobalVariable*>(calleeVal) == nullptr));
        if (isDirect) {
            *os << "  j " << calleeVal->getName() << "\n";
        } else {
            *os << "  lw t0, " << cg.getValueAsOperand(calleeVal) << "\n";
            *os << "  jr t0\n";
        }
        return true;
    }
    return false;
}

void RiscV32Architecture::emitFAdd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        bool isFloat = i.getType() && i.getType()->isFloatTy();
        *os << "  fl" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  fl" << (isFloat ? "w" : "d") << " fa1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  fadd." << (isFloat ? "s" : "d") << " fa0, fa0, fa1\n";
        *os << "  fs" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitFSub(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        bool isFloat = i.getType() && i.getType()->isFloatTy();
        *os << "  fl" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  fl" << (isFloat ? "w" : "d") << " fa1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  fsub." << (isFloat ? "s" : "d") << " fa0, fa0, fa1\n";
        *os << "  fs" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitFMul(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        bool isFloat = i.getType() && i.getType()->isFloatTy();
        *os << "  fl" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  fl" << (isFloat ? "w" : "d") << " fa1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  fmul." << (isFloat ? "s" : "d") << " fa0, fa0, fa1\n";
        *os << "  fs" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitFDiv(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        bool isFloat = i.getType() && i.getType()->isFloatTy();
        *os << "  fl" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  fl" << (isFloat ? "w" : "d") << " fa1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  fdiv." << (isFloat ? "s" : "d") << " fa0, fa0, fa1\n";
        *os << "  fs" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitCmp(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  lw a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  lw a1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        switch (i.getOpcode()) {
            case ir::Instruction::Ceq: *os << "  sub a0, a0, a1\n  seqz a0, a0\n"; break;
            case ir::Instruction::Cne: *os << "  sub a0, a0, a1\n  snez a0, a0\n"; break;
            case ir::Instruction::Cslt: *os << "  slt a0, a0, a1\n"; break;
            case ir::Instruction::Csle: *os << "  slt a0, a1, a0\n  xori a0, a0, 1\n"; break;
            case ir::Instruction::Csgt: *os << "  slt a0, a1, a0\n"; break;
            case ir::Instruction::Csge: *os << "  slt a0, a0, a1\n  xori a0, a0, 1\n"; break;
            default: *os << "  sub a0, a0, a1\n  seqz a0, a0\n"; break;
        }
        *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void RiscV32Architecture::emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* f, const ir::Type* t) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* src = i.getOperands()[0]->get();
        ir::Instruction::Opcode op = i.getOpcode();

        if (op == ir::Instruction::ExtUB) {
            *os << "  lw a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  andi a0, a0, 255\n";
            *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtUH) {
            *os << "  lw a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  andi a0, a0, 65535\n";
            *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtUW) {
            *os << "  lw a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtSB) {
            *os << "  lw a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  slli a0, a0, 24\n";
            *os << "  srai a0, a0, 24\n";
            *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtSH) {
            *os << "  lw a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  slli a0, a0, 16\n";
            *os << "  srai a0, a0, 16\n";
            *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtSW) {
            *os << "  lw a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::UWtoF || op == ir::Instruction::Ultof) {
            bool isFloat = (t && t->getSize() == 4);
            *os << "  lw a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  fcvt." << (isFloat ? "s" : "d") << ".wu fa0, a0\n";
            *os << "  fs" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::SWtoF || op == ir::Instruction::Sltof) {
            bool isFloat = (t && t->getSize() == 4);
            *os << "  lw a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  fcvt." << (isFloat ? "s" : "d") << ".w fa0, a0\n";
            *os << "  fs" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::DToUI || op == ir::Instruction::SToUI) {
            bool isFloat = (f && f->getSize() == 4);
            *os << "  fl" << (isFloat ? "w" : "d") << " fa0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  fcvt.wu." << (isFloat ? "s" : "d") << " a0, fa0\n";
            *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::TruncD) {
            *os << "  lw a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  lw a0, " << cg.getValueAsOperand(src) << "\n";
            *os << "  sw a0, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void RiscV32Architecture::emitVAStart(CodeGen& cg, ir::Instruction& i) {}
void RiscV32Architecture::emitVAArg(CodeGen& cg, ir::Instruction& i) {}

RiscV32ComplexAddress RiscV32Architecture::matchComplexAddress(CodeGen& cg, ir::Value* val) const {
    RiscV32ComplexAddress result;
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
                result.disp = static_cast<int32_t>(disp);
                result.isValid = true;
                return result;
            }
        }
    }
    return result;
}

void RiscV32Architecture::emitLoad(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        bool isFloat = i.getType() && i.getType()->isFloatingPoint();
        std::string loadMnemonic = "lw";
        size_t size = i.getType() ? i.getType()->getSize() : 4;
        if (isFloat) {
            loadMnemonic = (size == 4) ? "flw" : "fld";
        } else {
            if (size == 1) loadMnemonic = "lb";
            else if (size == 2) loadMnemonic = "lh";
            else if (size == 4) loadMnemonic = "lw";
        }

        ir::Value* ptrVal = i.getOperands()[0]->get();
        RiscV32ComplexAddress addr = matchComplexAddress(cg, ptrVal);
        std::string regName = isFloat ? "fa1" : "a1";
        std::string storeBackInst = isFloat ? ((size == 4) ? "fsw" : "fsd") : "sw";

        if (addr.isValid) {
            *os << "  " << loadMnemonic << " " << regName << ", " << addr.format() << "\n";
            *os << "  " << storeBackInst << " " << regName << ", " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  lw a0, " << cg.getValueAsOperand(ptrVal) << "\n";
            *os << "  " << loadMnemonic << " " << regName << ", 0(a0)\n";
            *os << "  " << storeBackInst << " " << regName << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void RiscV32Architecture::emitStore(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* val = i.getOperands()[0]->get();
        bool isFloat = val->getType() && val->getType()->isFloatingPoint();
        std::string storeMnemonic = "sw";
        size_t size = val->getType() ? val->getType()->getSize() : 4;
        if (isFloat) {
            storeMnemonic = (size == 4) ? "fsw" : "fsd";
        } else {
            if (size == 1) storeMnemonic = "sb";
            else if (size == 2) storeMnemonic = "sh";
            else if (size == 4) storeMnemonic = "sw";
        }

        std::string regName = isFloat ? "fa1" : "a1";
        std::string loadValInst = isFloat ? ((size == 4) ? "flw" : "fld") : "lw";

        ir::Value* ptrVal = i.getOperands()[1]->get();
        RiscV32ComplexAddress addr = matchComplexAddress(cg, ptrVal);
        if (addr.isValid) {
            *os << "  " << loadValInst << " " << regName << ", " << cg.getValueAsOperand(val) << "\n";
            *os << "  " << storeMnemonic << " " << regName << ", " << addr.format() << "\n";
        } else {
            *os << "  lw a0, " << cg.getValueAsOperand(ptrVal) << "\n";
            *os << "  " << loadValInst << " " << regName << ", " << cg.getValueAsOperand(val) << "\n";
            *os << "  " << storeMnemonic << " " << regName << ", 0(a0)\n";
        }
    }
}

void RiscV32Architecture::emitAlloc(CodeGen& cg, ir::Instruction& i) {
    uint64_t size = 4;
    if (i.getOpcode() == ir::Instruction::Alloc4) size = 4;
    else if (i.getOpcode() == ir::Instruction::Alloc16) size = 16;
    else if (!i.getOperands().empty()) {
        if (auto* sizeConst = dynamic_cast<ir::ConstantInt*>(i.getOperands()[0]->get()))
            size = sizeConst->getValue();
    }
    uint64_t alignedSize = (size + 3) & ~3;

    if (auto* os = cg.getTextStream()) {
        *os << "  # RISC-V 32-bit Bump Allocation\n";
        *os << "  la a0, heap_ptr\n";
        *os << "  lw a1, 0(a0)\n";
        *os << "  sw a1, " << cg.getValueAsOperand(&i) << "\n";
        *os << "  addi a1, a1, " << alignedSize << "\n";
        *os << "  sw a1, 0(a0)\n";
    }
}

void RiscV32Architecture::emitPhiCopies(CodeGen& cg, ir::BasicBlock* source, ir::BasicBlock* target) {
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
                std::string lInst = (type && type->getSize() <= 4) ? "lw" : "lw";
                std::string sInst = (type && type->getSize() <= 4) ? "sw" : "sw";
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
            std::string lInst = (type && type->getSize() <= 4) ? "lw" : "lw";
            *os << "  " << lInst << " t0, " << srcOp << "\n";
            *os << "  addi sp, sp, -16\n";
            *os << "  sw t0, 0(sp)\n";
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
            std::string sInst = (type && type->getSize() <= 4) ? "sw" : "sw";
            *os << "  lw t0, 0(sp)\n";
            *os << "  addi sp, sp, 16\n";
            *os << "  " << sInst << " t0, " << destOp << "\n";
        }
    }
}

void RiscV32Architecture::emitBr(CodeGen& cg, ir::Instruction& i) {
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
        *os << "  lw a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";

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

void RiscV32Architecture::emitJmp(CodeGen& cg, ir::Instruction& i) {
    auto* targetBB = dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get());
    emitPhiCopies(cg, i.getParent(), targetBB);
    if (auto* os = cg.getTextStream()) {
        *os << "  j " << cg.getTargetInfo()->getBBLabel(targetBB) << "\n";
    }
}

bool RiscV32Architecture::emitMulAddFusion(CodeGen& cg, ir::Instruction& mul, ir::Instruction& add) {
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
        *os << "  lw a0, " << cg.getValueAsOperand(m0) << "\n";
        *os << "  lw a1, " << cg.getValueAsOperand(m1) << "\n";
        *os << "  lw a2, " << cg.getValueAsOperand(addOther) << "\n";
        *os << "  mul a0, a0, a1\n";
        *os << "  add a0, a0, a2\n";
        *os << "  sw a0, " << cg.getValueAsOperand(&add) << "\n";
        return true;
    }
    return false;
}

bool RiscV32Architecture::emitCmpAndBranchFusion(CodeGen& cg, ir::Instruction& cmp, ir::Instruction& br) {
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
            *os << "  lw a0, " << cg.getValueAsOperand(l) << "\n";
            *os << "  lw a1, " << cg.getValueAsOperand(r) << "\n";
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

void RiscV32Architecture::emitSyscall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {
    if (auto* os = cg.getTextStream()) {
        *os << "  li a7, " << osInfo.getSyscallNumber(dynamic_cast<ir::SyscallInstruction*>(&i)->getSyscallId()) << "\n";
        for (size_t j = 0; j < std::min(i.getOperands().size(), (size_t)6); ++j) *os << "  lw a" << j << ", " << cg.getValueAsOperand(i.getOperands()[j]->get()) << "\n";
        *os << "  ecall\n";
    }
}

void RiscV32Architecture::emitExternCall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {
    auto* ei = dynamic_cast<ir::ExternCallInstruction*>(&i); if (!ei) return;
    const auto* spec = cg.getTargetInfo()->findCapability(ei->getCapability());
    if (!spec || !cg.getTargetInfo()->validateCapability(i, *spec)) { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, spec); return; }
    cg.getTargetInfo()->emitDomainCapability(cg, i, *spec);
}

void RiscV32Architecture::emitNativeSyscall(CodeGen& cg, uint64_t syscallNum, const std::vector<ir::Value*>& args) {
    if (auto* os = cg.getTextStream()) {
        *os << "  li a7, " << syscallNum << "\n";
        for (size_t i = 0; i < std::min(args.size(), (size_t)6); ++i) {
            *os << "  lw a" << i << ", " << cg.getValueAsOperand(args[i]) << "\n";
        }
        *os << "  ecall\n";
    }
}

void RiscV32Architecture::emitNativeLibraryCall(CodeGen& cg, const std::string& name, const std::vector<ir::Value*>& args) {
    if (auto* os = cg.getTextStream()) {
        for (size_t i = 0; i < std::min(args.size(), (size_t)8); ++i) {
            *os << "  lw a" << i << ", " << cg.getValueAsOperand(args[i]) << "\n";
        }
        *os << "  call " << name << "\n";
    }
}

std::string RiscV32Architecture::formatStackOperand(int o) const { return std::to_string(o) + "(s0)"; }
std::string RiscV32Architecture::formatGlobalOperand(const std::string& n) const { return n; }
bool RiscV32Architecture::isCallerSaved(const std::string& r) const { return (r[0] == 't' || r[0] == 'a' || r[0] == 'f' || r[0] == 'v'); }
bool RiscV32Architecture::isCalleeSaved(const std::string& r) const { return (r[0] == 's' || r == "gp" || r == "tp" || r == "fp"); }

void RiscV32Architecture::emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) {}
void RiscV32Architecture::emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) {}

VectorCapabilities RiscV32Architecture::getVectorCapabilities() const {
    VectorCapabilities caps;
    caps.maxVectorWidth = 0;
    caps.supportedWidths = {};
    caps.supportsFloatVectors = false;
    caps.supportsIntegerVectors = false;
    caps.supportsDoubleVectors = false;
    caps.simdExtension = "";
    return caps;
}

bool RiscV32Architecture::supportsVectorWidth(unsigned width) const { return false; }
bool RiscV32Architecture::supportsVectorType(const ir::VectorType* type) const { return false; }
bool RiscV32Architecture::supportsVectorOperation(ir::Instruction::Opcode op, const ir::VectorType* type) const { return false; }
bool RiscV32Architecture::supportsVectorConversion(ir::Instruction::Opcode op, const ir::VectorType* srcType, const ir::VectorType* dstType) const { return false; }
void RiscV32Architecture::emitVectorLoad(CodeGen& cg, ir::VectorInstruction& i) {}
void RiscV32Architecture::emitVectorStore(CodeGen& cg, ir::VectorInstruction& i) {}
void RiscV32Architecture::emitVectorArithmetic(CodeGen& cg, ir::VectorInstruction& i) {}
void RiscV32Architecture::emitVectorReduction(CodeGen& cg, ir::VectorInstruction& i) {}
void RiscV32Architecture::emitVectorHorizontalOp(CodeGen& cg, ir::VectorInstruction& i) {}

}
