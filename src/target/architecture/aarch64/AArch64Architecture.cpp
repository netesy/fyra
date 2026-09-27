#include "target/architecture/aarch64/AArch64Architecture.h"
#include "codegen/CodeGen.h"
#include "target/core/OperatingSystemInfo.h"
#include "codegen/asm/Assembler.h"
#include "ir/Instruction.h"
#include "ir/Function.h"
#include "ir/Constant.h"
#include "ir/BasicBlock.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include "transforms/CFGBuilder.h"
#include <ostream>
#include <algorithm>
#include <cstring>
#include <set>

namespace target {

AArch64Architecture::AArch64Architecture() {}

void AArch64Architecture::emitLoadValue(CodeGen& cg, asm_::Assembler& assembler, ir::Value* val, uint8_t reg) {
    if (auto* instr = dynamic_cast<ir::Instruction*>(val)) {
        if (instr->hasPhysicalRegister()) {
            uint8_t src_reg = static_cast<uint8_t>(instr->getPhysicalRegister());
            if (src_reg == reg) return;
            uint32_t instruction = 0xAA0003E0 | ((src_reg & 0x1F) << 16) | (reg & 0x1F);
            if (val->getType()->getSize() <= 4) instruction &= ~0x80000000;
            else instruction |= 0x80000000;
            assembler.emitDWord(instruction);
            return;
        }
    }
    if (auto* constInt = dynamic_cast<ir::ConstantInt*>(val)) {
        uint16_t imm = constInt->getValue() & 0xFFFF;
        uint32_t instruction = 0xD2800000 | (imm << 5) | (reg & 0x1F);
        if (val->getType()->getSize() > 4) instruction |= 0x80000000;
        else instruction &= 0x7FFFFFFF;
        assembler.emitDWord(instruction);
    } else {
        int32_t offset = cg.getStackOffset(val);
        const ir::Type* type = val->getType();
        uint32_t base = type->isFloatingPoint() ? ((type->getSize() > 4) ? 0xFC400000 : 0xBC400000) : ((type->getSize() > 4) ? 0xF8400000 : 0xB8400000);
        uint32_t instruction = base | ((offset & 0x1FF) << 12) | (29 << 5) | (reg & 0x1F);
        assembler.emitDWord(instruction);
    }
}

TypeInfo AArch64Architecture::getTypeInfo(const ir::Type* type) const {
    if (auto* intTy = dynamic_cast<const ir::IntegerType*>(type)) {
        uint64_t bitWidth = intTy->getBitwidth();
        return {bitWidth, bitWidth, RegisterClass::Integer, false, true};
    } else if (type->isFloatTy()) { return {32, 32, RegisterClass::Float, true, true}; }
    else if (type->isDoubleTy()) { return {64, 64, RegisterClass::Float, true, true}; }
    else if (type->isPointerTy()) { return {64, 64, RegisterClass::Integer, false, false}; }
    return {32, 32, RegisterClass::Integer, false, true};
}

const std::vector<std::string>& AArch64Architecture::getRegisters(RegisterClass regClass) const {
    static const std::vector<std::string> intRegs = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28"};
    static const std::vector<std::string> floatRegs = {"v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"};
    return (regClass == RegisterClass::Float || RegisterClass::Vector == regClass) ? floatRegs : intRegs;
}

const std::string& AArch64Architecture::getReturnRegister(const ir::Type* type) const {
    return (type->isFloatTy() || type->isDoubleTy()) ? getFloatReturnRegister() : getIntegerReturnRegister();
}

const std::vector<std::string>& AArch64Architecture::getIntegerArgumentRegisters() const {
    static const std::vector<std::string> regs = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"}; return regs; }
const std::vector<std::string>& AArch64Architecture::getFloatArgumentRegisters() const {
    static const std::vector<std::string> regs = {"v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7"}; return regs; }
const std::string& AArch64Architecture::getIntegerReturnRegister() const { static const std::string reg = "x0"; return reg; }
const std::string& AArch64Architecture::getFloatReturnRegister() const { static const std::string reg = "v0"; return reg; }

void AArch64Architecture::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    currentStackOffset = 0;
    int int_idx = 0, float_idx = 0, stack_arg_idx = 0;
    for (auto& param : func.getParameters()) {
        TypeInfo info = getTypeInfo(param->getType());
        if (info.regClass == RegisterClass::Float) { if (float_idx < 8) { currentStackOffset -= 8; cg.getStackOffsets()[param.get()] = currentStackOffset; float_idx++; } else cg.getStackOffsets()[param.get()] = 16 + (stack_arg_idx++) * 8; }
        else { if (int_idx < 8) { currentStackOffset -= 8; cg.getStackOffsets()[param.get()] = currentStackOffset; int_idx++; } else cg.getStackOffsets()[param.get()] = 16 + (stack_arg_idx++) * 8; }
    }
    for (auto& bb : func.getBasicBlocks()) { for (auto& instr : bb->getInstructions()) { if (instr->getType()->getTypeID() != ir::Type::VoidTyID) { currentStackOffset -= 8; cg.getStackOffsets()[instr.get()] = currentStackOffset; } } }
    bool hasCalls = false;
    for (auto& bb : func.getBasicBlocks()) { for (auto& instr : bb->getInstructions()) { if (instr->getOpcode() == ir::Instruction::Call) hasCalls = true; } }
    size_t local_area_size = -currentStackOffset;
    std::set<std::string> usedCS;
    for (auto& bb : func.getBasicBlocks()) { for (auto& instr : bb->getInstructions()) { if (instr->hasPhysicalRegister()) { std::string reg = getRegisters(RegisterClass::Integer)[instr->getPhysicalRegister()]; if (isCalleeSaved(reg)) usedCS.insert(reg); } } }
    bool isLeaf = !hasCalls;
    bool needsFrame = !isLeaf || local_area_size > 0 || !usedCS.empty();
    if (!needsFrame) return;
    size_t total_frame_size = align_to_16(local_area_size + (usedCS.size() * 8) + (isLeaf ? 0 : 16));
    if (auto* os = cg.getTextStream()) {
        *os << "  .cfi_startproc\n";
        *os << "  stp x29, x30, [sp, #-16]!\n";
        *os << "  .cfi_def_cfa_offset 16\n";
        *os << "  .cfi_offset 29, -16\n";
        *os << "  .cfi_offset 30, -8\n";
        *os << "  mov x29, sp\n";
        *os << "  .cfi_def_cfa_register 29\n";
        if (total_frame_size > 0) { if (total_frame_size <= 4095) *os << "  sub sp, sp, #" << total_frame_size << "\n"; else { *os << "  mov x9, #" << total_frame_size << "\n  sub sp, sp, x9\n"; } }
        int cs_off = isLeaf ? 0 : 16; auto it = usedCS.begin();
        while (it != usedCS.end()) {
            std::string r1 = *it++;
            if (it != usedCS.end()) {
                std::string r2 = *it++;
                *os << "  stp " << r1 << ", " << r2 << ", [sp, #" << cs_off << "]\n";
                if (r1[0] == 'x') *os << "  .cfi_offset " << r1.substr(1) << ", " << (int)cs_off - (int)total_frame_size - 16 << "\n";
                if (r2[0] == 'x') *os << "  .cfi_offset " << r2.substr(1) << ", " << (int)cs_off + 8 - (int)total_frame_size - 16 << "\n";
                cs_off += 16;
            } else {
                *os << "  str " << r1 << ", [sp, #" << cs_off << "]\n";
                if (r1[0] == 'x') *os << "  .cfi_offset " << r1.substr(1) << ", " << (int)cs_off - (int)total_frame_size - 16 << "\n";
                cs_off += 8;
            }
        }
        int i_idx = 0, f_idx = 0;
        for (auto& param : func.getParameters()) {
            TypeInfo info = getTypeInfo(param->getType());
            if (info.regClass == RegisterClass::Float) { if (f_idx < 8) { std::string reg = (info.size == 32) ? "s" : "d"; reg += std::to_string(f_idx++); *os << "  str " << reg << ", [x29, #" << cg.getStackOffset(param.get()) << "]\n"; } }
            else { if (i_idx < 8) { std::string reg = getRegisterName("x" + std::to_string(i_idx++), param->getType()); *os << "  str " << reg << ", [x29, #" << cg.getStackOffset(param.get()) << "]\n"; } }
        }
    }
}

void AArch64Architecture::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) { *os << func.getName() << "_epilogue:\n"; }
    bool hasCalls = false;
    for (auto& bb : func.getBasicBlocks()) { for (auto& instr : bb->getInstructions()) { if (instr->getOpcode() == ir::Instruction::Call) { hasCalls = true; break; } } if (hasCalls) break; }
    size_t local_area_size = -currentStackOffset;
    std::set<std::string> usedCS;
    for (auto& bb : func.getBasicBlocks()) { for (auto& instr : bb->getInstructions()) { if (instr->hasPhysicalRegister()) { std::string reg = getRegisters(RegisterClass::Integer)[instr->getPhysicalRegister()]; if (isCalleeSaved(reg)) usedCS.insert(reg); } } }
    bool isLeaf = !hasCalls;
    bool needsFrame = !isLeaf || local_area_size > 0 || !usedCS.empty();
    if (!needsFrame) {
        if (auto* os = cg.getTextStream()) {
            *os << "  ret\n";
            *os << "  .cfi_endproc\n";
        } else {
            cg.getAssembler().emitDWord(0xD65F03C0);
        }
        return;
    }
    size_t total_frame_size = align_to_16(local_area_size + (usedCS.size() * 8) + (isLeaf ? 0 : 16));
    if (auto* os = cg.getTextStream()) {
        int cs_off = isLeaf ? 0 : 16; auto it = usedCS.begin();
        while (it != usedCS.end()) {
            std::string r1 = *it++;
            if (it != usedCS.end()) {
                std::string r2 = *it++;
                *os << "  ldp " << r1 << ", " << r2 << ", [sp, #" << cs_off << "]\n";
                if (r1[0] == 'x') *os << "  .cfi_restore " << r1.substr(1) << "\n";
                if (r2[0] == 'x') *os << "  .cfi_restore " << r2.substr(1) << "\n";
                cs_off += 16;
            } else {
                *os << "  ldr " << r1 << ", [sp, #" << cs_off << "]\n";
                if (r1[0] == 'x') *os << "  .cfi_restore " << r1.substr(1) << "\n";
                cs_off += 8;
            }
        }
        if (!isLeaf) {
            *os << "  mov sp, x29\n";
            *os << "  .cfi_def_cfa_register 31\n";
            *os << "  ldp x29, x30, [sp], #16\n";
            *os << "  .cfi_def_cfa_offset 0\n";
            *os << "  .cfi_restore 30\n";
            *os << "  .cfi_restore 29\n";
        } else if (total_frame_size > 0) {
            *os << "  add sp, sp, #" << total_frame_size << "\n";
        }
        *os << "  ret\n";
        *os << "  .cfi_endproc\n";
    }
}

void AArch64Architecture::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << ".section .rodata\n.Lproc_environ:\n  .string \"/proc/self/environ\"\n.Lproc_cmdline:\n  .string \"/proc/self/cmdline\"\n.text\n.globl _start\n_start:\n  bl main\n  mov x8, #93\n  svc #0\n";
    }
}

void AArch64Architecture::emitRet(CodeGen& cg, ir::Instruction& i) {
    if (!i.getOperands().empty() && i.getOperands()[0] && i.getOperands()[0]->get() != nullptr) {
        ir::Value* rv = i.getOperands()[0]->get();
        if (auto* os = cg.getTextStream()) {
            std::string regName = "x0";
            if (rv->getType() && rv->getType()->isFloatingPoint()) {
                regName = (rv->getType()->getSize() == 4) ? "s0" : "d0";
            } else {
                regName = getRegisterName("x0", rv->getType());
            }
            *os << "  ldr " << regName << ", " << cg.getValueAsOperand(rv) << "\n";
        }
        else emitLoadValue(cg, cg.getAssembler(), rv, 0);
    }
    ir::Function* func = i.getParent()->getParent();
    size_t local_area_size = -currentStackOffset;
    std::set<std::string> usedCS;
    bool hasCalls = false;
    for (auto& bb : func->getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getOpcode() == ir::Instruction::Call) hasCalls = true;
            if (instr->hasPhysicalRegister()) {
                std::string reg = getRegisters(RegisterClass::Integer)[instr->getPhysicalRegister()];
                if (isCalleeSaved(reg)) usedCS.insert(reg);
            }
        }
    }
    bool isLeaf = !hasCalls;
    bool needsFrame = !isLeaf || local_area_size > 0 || !usedCS.empty();
    if (auto* os = cg.getTextStream()) {
        if (!needsFrame) {
            *os << "  ret\n";
        } else {
            *os << "  b " << func->getName() << "_epilogue\n";
        }
    }
    else emitFunctionEpilogue(cg, *func);
}

void AArch64Architecture::emitAdd(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) {
        std::string r1 = getRegisterName("x9", l->getType());
        *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            uint64_t val = ci->getValue();
            if (val <= 4095) {
                *os << "  add " << r1 << ", " << r1 << ", #" << val << "\n";
                *os << "  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n";
                return;
            }
        }
        std::string r2 = getRegisterName("x10", r->getType());
        *os << "  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n";
        *os << "  add " << r1 << ", " << r1 << ", " << r2 << "\n";
        *os << "  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n";
    }
}

void AArch64Architecture::emitSMin(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) {
        std::string r1 = getRegisterName("x9", l->getType());
        std::string r2 = getRegisterName("x10", r->getType());
        *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n";
        *os << "  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n";
        *os << "  cmp " << r1 << ", " << r2 << "\n";
        *os << "  csel " << r1 << ", " << r1 << ", " << r2 << ", lt\n";
        *os << "  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n";
    }
}

void AArch64Architecture::emitSMax(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) {
        std::string r1 = getRegisterName("x9", l->getType());
        std::string r2 = getRegisterName("x10", r->getType());
        *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n";
        *os << "  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n";
        *os << "  cmp " << r1 << ", " << r2 << "\n";
        *os << "  csel " << r1 << ", " << r1 << ", " << r2 << ", gt\n";
        *os << "  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n";
    }
}
void AArch64Architecture::emitSub(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) {
        std::string r1 = getRegisterName("x9", l->getType());
        *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            uint64_t val = ci->getValue();
            if (val <= 4095) {
                *os << "  sub " << r1 << ", " << r1 << ", #" << val << "\n";
                *os << "  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n";
                return;
            }
        }
        std::string r2 = getRegisterName("x10", r->getType());
        *os << "  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n";
        *os << "  sub " << r1 << ", " << r1 << ", " << r2 << "\n";
        *os << "  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n";
    }
}
void AArch64Architecture::emitMul(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) { std::string r1 = getRegisterName("x9", l->getType()), r2 = getRegisterName("x10", r->getType()); *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n  mul " << r1 << ", " << r1 << ", " << r2 << "\n  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n"; }
}
void AArch64Architecture::emitDiv(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    bool u = (i.getOpcode() == ir::Instruction::Udiv);
    if (auto* os = cg.getTextStream()) { std::string r1 = getRegisterName("x9", l->getType()), r2 = getRegisterName("x10", r->getType()); *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n  " << (u ? "udiv " : "sdiv ") << r1 << ", " << r1 << ", " << r2 << "\n  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n"; }
}
void AArch64Architecture::emitRem(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    bool u = (i.getOpcode() == ir::Instruction::Urem);
    if (auto* os = cg.getTextStream()) { std::string r1 = getRegisterName("x9", l->getType()), r2 = getRegisterName("x10", r->getType()), r3 = getRegisterName("x11", l->getType()); *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n  " << (u ? "udiv " : "sdiv ") << r3 << ", " << r1 << ", " << r2 << "\n  msub " << r1 << ", " << r3 << ", " << r2 << ", " << r1 << "\n  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n"; }
}
void AArch64Architecture::emitAnd(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) { std::string r1 = getRegisterName("x9", l->getType()), r2 = getRegisterName("x10", r->getType()); *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n  and " << r1 << ", " << r1 << ", " << r2 << "\n  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n"; }
}
void AArch64Architecture::emitOr(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) { std::string r1 = getRegisterName("x9", l->getType()), r2 = getRegisterName("x10", r->getType()); *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n  orr " << r1 << ", " << r1 << ", " << r2 << "\n  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n"; }
}
void AArch64Architecture::emitXor(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) { std::string r1 = getRegisterName("x9", l->getType()), r2 = getRegisterName("x10", r->getType()); *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n  eor " << r1 << ", " << r1 << ", " << r2 << "\n  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n"; }
}
void AArch64Architecture::emitShl(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) {
        std::string r1 = getRegisterName("x9", l->getType());
        *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            *os << "  lsl " << r1 << ", " << r1 << ", #" << ci->getValue() << "\n";
            *os << "  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n";
            return;
        }
        std::string r2 = getRegisterName("x10", r->getType());
        *os << "  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n";
        *os << "  lsl " << r1 << ", " << r1 << ", " << r2 << "\n";
        *os << "  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n";
    }
}
void AArch64Architecture::emitShr(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) {
        std::string r1 = getRegisterName("x9", l->getType());
        *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            *os << "  lsr " << r1 << ", " << r1 << ", #" << ci->getValue() << "\n";
            *os << "  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n";
            return;
        }
        std::string r2 = getRegisterName("x10", r->getType());
        *os << "  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n";
        *os << "  lsr " << r1 << ", " << r1 << ", " << r2 << "\n";
        *os << "  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n";
    }
}
void AArch64Architecture::emitSar(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) {
        std::string r1 = getRegisterName("x9", l->getType());
        *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n";
        if (auto* ci = dynamic_cast<ir::ConstantInt*>(r)) {
            *os << "  asr " << r1 << ", " << r1 << ", #" << ci->getValue() << "\n";
            *os << "  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n";
            return;
        }
        std::string r2 = getRegisterName("x10", r->getType());
        *os << "  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n";
        *os << "  asr " << r1 << ", " << r1 << ", " << r2 << "\n";
        *os << "  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n";
    }
}
void AArch64Architecture::emitNeg(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *o = i.getOperands()[0]->get();
    if (auto* os = cg.getTextStream()) { std::string r = getRegisterName("x9", o->getType()); *os << "  ldr " << r << ", " << cg.getValueAsOperand(o) << "\n  neg " << r << ", " << r << "\n  str " << r << ", " << cg.getValueAsOperand(d) << "\n"; }
}
void AArch64Architecture::emitNot(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *o = i.getOperands()[0]->get();
    if (auto* os = cg.getTextStream()) { std::string r = getRegisterName("x9", o->getType()); *os << "  ldr " << r << ", " << cg.getValueAsOperand(o) << "\n  mvn " << r << ", " << r << "\n  str " << r << ", " << cg.getValueAsOperand(d) << "\n"; }
}
void AArch64Architecture::emitCopy(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        const ir::Type* type = i.getType();
        ir::Value* src = i.getOperands()[0]->get();
        if (type && (type->isVectorTy() || dynamic_cast<const ir::VectorType*>(type) != nullptr)) {
            *os << "  ldr q16, " << cg.getValueAsOperand(src) << "\n";
            *os << "  str q16, " << cg.getValueAsOperand(&i) << "\n";
        } else if (type && type->isFloatingPoint()) {
            std::string reg = (type->getSize() == 4) ? "s16" : "d16";
            *os << "  ldr " << reg << ", " << cg.getValueAsOperand(src) << "\n";
            *os << "  str " << reg << ", " << cg.getValueAsOperand(&i) << "\n";
        } else if (type && type->isInteger() && type->getSize() <= 4) {
            *os << "  ldr w9, " << cg.getValueAsOperand(src) << "\n";
            *os << "  str w9, " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  ldr x9, " << cg.getValueAsOperand(src) << "\n";
            *os << "  str x9, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}
void AArch64Architecture::emitCall(CodeGen& cg, ir::Instruction& i) {
    unsigned i_idx = 0, f_idx = 0; std::vector<ir::Value*> s_args;
    if (auto* os = cg.getTextStream()) {
        for (size_t j = 1; j < i.getOperands().size(); ++j) {
            ir::Value* a = i.getOperands()[j]->get(); TypeInfo inf = getTypeInfo(a->getType()); std::string op = cg.getValueAsOperand(a);
            if (inf.regClass == RegisterClass::Float) { if (f_idx < 8) { std::string r = (inf.size == 32) ? "s" : "d"; r += std::to_string(f_idx++); *os << "  ldr " << r << ", " << op << "\n"; } else s_args.push_back(a); }
            else { if (i_idx < 8) { std::string r = getRegisterName("x" + std::to_string(i_idx++), a->getType()); *os << "  ldr " << r << ", " << op << "\n"; } else s_args.push_back(a); }
        }
        std::reverse(s_args.begin(), s_args.end());
        for (auto* a : s_args) { *os << "  ldr x9, " << cg.getValueAsOperand(a) << "\n  str x9, [sp, #-16]!\n"; }
        *os << "  bl " << i.getOperands()[0]->get()->getName() << "\n";
        if (!s_args.empty()) *os << "  add sp, sp, #" << s_args.size() * 16 << "\n";
        if (i.getType()->getTypeID() != ir::Type::VoidTyID) { std::string r = getRegisterName("x0", i.getType()); if (i.getType()->isFloatingPoint()) r = (i.getType()->getSize() == 4) ? "s0" : "d0"; *os << "  str " << r << ", " << cg.getValueAsOperand(&i) << "\n"; }
    }
}

bool AArch64Architecture::emitTailCall(CodeGen& cg, ir::Instruction& callInst, ir::Instruction& retInst) {
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
        unsigned i_idx = 0, f_idx = 0;
        for (size_t j = 1; j < callInst.getOperands().size(); ++j) {
            ir::Value* a = callInst.getOperands()[j]->get();
            TypeInfo inf = getTypeInfo(a->getType());
            std::string op = cg.getValueAsOperand(a);
            if (inf.regClass == RegisterClass::Float) {
                if (f_idx < 8) {
                    std::string r = (inf.size == 32) ? "s" : "d";
                    r += std::to_string(f_idx++);
                    *os << "  ldr " << r << ", " << op << "\n";
                }
            } else {
                if (i_idx < 8) {
                    std::string r = getRegisterName("x" + std::to_string(i_idx++), a->getType());
                    *os << "  ldr " << r << ", " << op << "\n";
                }
            }
        }
        bool isDirect = (dynamic_cast<ir::Function*>(calleeVal) != nullptr ||
                        (dynamic_cast<ir::GlobalValue*>(calleeVal) != nullptr && dynamic_cast<ir::GlobalVariable*>(calleeVal) == nullptr));
        if (isDirect) {
            *os << "  b " << calleeVal->getName() << "\n";
        } else {
            *os << "  ldr x9, " << cg.getValueAsOperand(calleeVal) << "\n";
            *os << "  br x9\n";
        }
        return true;
    }
    return false;
}
void AArch64Architecture::emitFAdd(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) {
        std::string regKind = (l->getType() && l->getType()->getSize() == 4) ? "s" : "d";
        std::string r1 = regKind + "16", r2 = regKind + "17";
        *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n  fadd " << r1 << ", " << r1 << ", " << r2 << "\n  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n";
    }
}
void AArch64Architecture::emitFSub(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) {
        std::string regKind = (l->getType() && l->getType()->getSize() == 4) ? "s" : "d";
        std::string r1 = regKind + "16", r2 = regKind + "17";
        *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n  fsub " << r1 << ", " << r1 << ", " << r2 << "\n  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n";
    }
}
void AArch64Architecture::emitFMul(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) {
        std::string regKind = (l->getType() && l->getType()->getSize() == 4) ? "s" : "d";
        std::string r1 = regKind + "16", r2 = regKind + "17";
        *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n  fmul " << r1 << ", " << r1 << ", " << r2 << "\n  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n";
    }
}
void AArch64Architecture::emitFDiv(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) {
        std::string regKind = (l->getType() && l->getType()->getSize() == 4) ? "s" : "d";
        std::string r1 = regKind + "16", r2 = regKind + "17";
        *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n  fdiv " << r1 << ", " << r1 << ", " << r2 << "\n  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n";
    }
}
void AArch64Architecture::emitCmp(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    std::string cond = "eq";
    switch (i.getOpcode()) {
        case ir::Instruction::Ceq:  case ir::Instruction::Ceqf: cond = "eq"; break;
        case ir::Instruction::Cne:  case ir::Instruction::Cnef: cond = "ne"; break;
        case ir::Instruction::Cslt: cond = "lt"; break;
        case ir::Instruction::Cult: case ir::Instruction::Clt:  cond = "lo"; break;
        case ir::Instruction::Csle: cond = "le"; break;
        case ir::Instruction::Cule: case ir::Instruction::Cle:  cond = "ls"; break;
        case ir::Instruction::Csgt: cond = "gt"; break;
        case ir::Instruction::Cugt: case ir::Instruction::Cgt:  cond = "hi"; break;
        case ir::Instruction::Csge: cond = "ge"; break;
        case ir::Instruction::Cuge: case ir::Instruction::Cge:  cond = "hs"; break;
        default:                    cond = "eq"; break;
    }
    if (auto* os = cg.getTextStream()) {
        if (l->getType() && l->getType()->isFloatingPoint()) {
            std::string r1 = (l->getType()->getSize() == 4) ? "s16" : "d16";
            std::string r2 = (r->getType()->getSize() == 4) ? "s17" : "d17";
            *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n";
            *os << "  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n";
            *os << "  fcmp " << r1 << ", " << r2 << "\n";
            *os << "  cset w9, " << cond << "\n";
            *os << "  str w9, " << cg.getValueAsOperand(d) << "\n";
        } else {
            std::string r1 = getRegisterName("x10", l->getType());
            std::string r2 = getRegisterName("x11", r->getType());
            *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n";
            *os << "  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n";
            *os << "  cmp " << r1 << ", " << r2 << "\n";
            *os << "  cset w9, " << cond << "\n";
            *os << "  str w9, " << cg.getValueAsOperand(d) << "\n";
        }
    }
}
void AArch64Architecture::emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* f, const ir::Type* t) {
    if (auto* os = cg.getTextStream()) {
        ir::Value* src = i.getOperands()[0]->get();
        ir::Instruction::Opcode op = i.getOpcode();

        if (op == ir::Instruction::ExtUB) {
            *os << "  ldr w9, " << cg.getValueAsOperand(src) << "\n";
            *os << "  uxtb w9, w9\n";
            *os << "  str w9, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtUH) {
            *os << "  ldr w9, " << cg.getValueAsOperand(src) << "\n";
            *os << "  uxth w9, w9\n";
            *os << "  str w9, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtUW) {
            *os << "  ldr w9, " << cg.getValueAsOperand(src) << "\n";
            *os << "  uxtw x9, w9\n";
            *os << "  str x9, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtSB) {
            *os << "  ldr w9, " << cg.getValueAsOperand(src) << "\n";
            *os << "  sxtb x9, w9\n";
            *os << "  str x9, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtSH) {
            *os << "  ldr w9, " << cg.getValueAsOperand(src) << "\n";
            *os << "  sxth x9, w9\n";
            *os << "  str x9, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::ExtSW) {
            *os << "  ldr w9, " << cg.getValueAsOperand(src) << "\n";
            *os << "  sxtw x9, w9\n";
            *os << "  str x9, " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::UWtoF || op == ir::Instruction::Ultof) {
            std::string srcReg = getRegisterName("x9", f);
            std::string dstReg = (t->getSize() == 4) ? "s16" : "d16";
            *os << "  ldr " << srcReg << ", " << cg.getValueAsOperand(src) << "\n";
            *os << "  ucvtf " << dstReg << ", " << srcReg << "\n";
            *os << "  str " << dstReg << ", " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::SWtoF || op == ir::Instruction::Sltof) {
            std::string srcReg = getRegisterName("x9", f);
            std::string dstReg = (t->getSize() == 4) ? "s16" : "d16";
            *os << "  ldr " << srcReg << ", " << cg.getValueAsOperand(src) << "\n";
            *os << "  scvtf " << dstReg << ", " << srcReg << "\n";
            *os << "  str " << dstReg << ", " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::DToUI || op == ir::Instruction::SToUI) {
            std::string srcReg = (f->getSize() == 4) ? "s16" : "d16";
            std::string dstReg = getRegisterName("x9", t);
            *os << "  ldr " << srcReg << ", " << cg.getValueAsOperand(src) << "\n";
            *os << "  fcvtzu " << dstReg << ", " << srcReg << "\n";
            *os << "  str " << dstReg << ", " << cg.getValueAsOperand(&i) << "\n";
        } else if (op == ir::Instruction::TruncD) {
            *os << "  ldr w9, " << cg.getValueAsOperand(src) << "\n";
            *os << "  str w9, " << cg.getValueAsOperand(&i) << "\n";
        } else if (f->isIntegerTy() && t->isFloatingPoint()) {
            std::string srcReg = getRegisterName("x9", f);
            std::string dstReg = (t->getSize() == 4) ? "s16" : "d16";
            *os << "  ldr " << srcReg << ", " << cg.getValueAsOperand(src) << "\n  scvtf " << dstReg << ", " << srcReg << "\n  str " << dstReg << ", " << cg.getValueAsOperand(&i) << "\n";
        } else if (f->isFloatingPoint() && t->isIntegerTy()) {
            std::string srcReg = (f->getSize() == 4) ? "s16" : "d16";
            std::string dstReg = getRegisterName("x9", t);
            *os << "  ldr " << srcReg << ", " << cg.getValueAsOperand(src) << "\n  fcvtzs " << dstReg << ", " << srcReg << "\n  str " << dstReg << ", " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  ldr x9, " << cg.getValueAsOperand(src) << "\n  str x9, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}
void AArch64Architecture::emitVAStart(CodeGen& cg, ir::Instruction& i) {}
void AArch64Architecture::emitVAArg(CodeGen& cg, ir::Instruction& i) {}
AArch64ComplexAddress AArch64Architecture::matchComplexAddress(CodeGen& cg, ir::Value* val) const {
    AArch64ComplexAddress result;
    if (!val) return result;

    auto unwrapExt = [](ir::Value* v) -> ir::Value* {
        if (!v) return v;
        while (auto* inner = dynamic_cast<ir::Instruction*>(v)) {
            if (inner->getOpcode() == ir::Instruction::ExtSW || inner->getOpcode() == ir::Instruction::ExtUW) {
                if (!inner->getOperands().empty() && inner->getOperands()[0] && inner->getOperands()[0]->get()) {
                    v = inner->getOperands()[0]->get();
                } else break;
            } else break;
        }
        return v;
    };

    auto* inst = dynamic_cast<ir::Instruction*>(val);
    if (!inst) return result;

    int64_t disp = 0;
    ir::Value* coreAddr = val;

    if (inst->getOpcode() == ir::Instruction::Add && inst->getOperands().size() == 2) {
        ir::Value* op0 = inst->getOperands()[0]->get();
        ir::Value* op1 = inst->getOperands()[1]->get();
        if (auto* c1 = dynamic_cast<ir::ConstantInt*>(op1)) {
            disp = static_cast<int64_t>(c1->getValue());
            coreAddr = op0;
        } else if (auto* c0 = dynamic_cast<ir::ConstantInt*>(op0)) {
            disp = static_cast<int64_t>(c0->getValue());
            coreAddr = op1;
        }
    }

    ir::Value* baseVal = nullptr;
    ir::Value* indexVal = nullptr;
    int shift = 0;

    auto* coreInst = dynamic_cast<ir::Instruction*>(coreAddr);
    if (coreInst && coreInst->getOpcode() == ir::Instruction::Add && coreInst->getOperands().size() == 2) {
        ir::Value* op0 = coreInst->getOperands()[0]->get();
        ir::Value* op1 = coreInst->getOperands()[1]->get();

        auto* mul0 = dynamic_cast<ir::Instruction*>(unwrapExt(op0));
        auto* mul1 = dynamic_cast<ir::Instruction*>(unwrapExt(op1));

        if (mul1 && mul1->getOpcode() == ir::Instruction::Mul && mul1->getOperands().size() == 2) {
            baseVal = op0;
            ir::Value* m0 = mul1->getOperands()[0]->get();
            ir::Value* m1 = mul1->getOperands()[1]->get();
            if (auto* c1 = dynamic_cast<ir::ConstantInt*>(m1)) {
                indexVal = m0; int scale = static_cast<int>(c1->getValue());
                if (scale == 2) shift = 1; else if (scale == 4) shift = 2; else if (scale == 8) shift = 3; else if (scale == 1) shift = 0; else return result;
            } else if (auto* c0 = dynamic_cast<ir::ConstantInt*>(m0)) {
                indexVal = m1; int scale = static_cast<int>(c0->getValue());
                if (scale == 2) shift = 1; else if (scale == 4) shift = 2; else if (scale == 8) shift = 3; else if (scale == 1) shift = 0; else return result;
            }
        } else if (mul0 && mul0->getOpcode() == ir::Instruction::Mul && mul0->getOperands().size() == 2) {
            baseVal = op1;
            ir::Value* m0 = mul0->getOperands()[0]->get();
            ir::Value* m1 = mul0->getOperands()[1]->get();
            if (auto* c1 = dynamic_cast<ir::ConstantInt*>(m1)) {
                indexVal = m0; int scale = static_cast<int>(c1->getValue());
                if (scale == 2) shift = 1; else if (scale == 4) shift = 2; else if (scale == 8) shift = 3; else if (scale == 1) shift = 0; else return result;
            } else if (auto* c0 = dynamic_cast<ir::ConstantInt*>(m0)) {
                indexVal = m1; int scale = static_cast<int>(c0->getValue());
                if (scale == 2) shift = 1; else if (scale == 4) shift = 2; else if (scale == 8) shift = 3; else if (scale == 1) shift = 0; else return result;
            }
        } else {
            baseVal = op0;
            indexVal = op1;
            shift = 0;
        }
    } else if (coreInst && coreInst->getOpcode() == ir::Instruction::Mul && coreInst->getOperands().size() == 2) {
        ir::Value* m0 = coreInst->getOperands()[0]->get();
        ir::Value* m1 = coreInst->getOperands()[1]->get();
        if (auto* c1 = dynamic_cast<ir::ConstantInt*>(m1)) {
            indexVal = m0; int scale = static_cast<int>(c1->getValue());
            if (scale == 2) shift = 1; else if (scale == 4) shift = 2; else if (scale == 8) shift = 3; else if (scale == 1) shift = 0; else return result;
        } else if (auto* c0 = dynamic_cast<ir::ConstantInt*>(m0)) {
            indexVal = m1; int scale = static_cast<int>(c0->getValue());
            if (scale == 2) shift = 1; else if (scale == 4) shift = 2; else if (scale == 8) shift = 3; else if (scale == 1) shift = 0; else return result;
        }
    } else if (disp != 0 && coreInst) {
        baseVal = coreInst;
    }

    indexVal = unwrapExt(indexVal);
    baseVal = unwrapExt(baseVal);

    if (!baseVal && !indexVal) return result;

    if (disp != 0 && indexVal) return result;

    std::string bStr = baseVal ? cg.getValueAsOperand(baseVal) : "";
    std::string iStr = indexVal ? cg.getValueAsOperand(indexVal) : "";

    if (bStr.empty() && iStr.empty()) return result;

    auto isDirectReg = [](const std::string& s) {
        if (s.empty()) return true;
        return (s[0] == 'x' || s[0] == 'w') && s.find('[') == std::string::npos && s.find('#') == std::string::npos;
    };
    if (!isDirectReg(bStr) || !isDirectReg(iStr)) return result;

    result.base = bStr;
    result.index = iStr;
    result.shift = shift;
    result.disp = disp;
    result.isValid = true;
    return result;
}

void AArch64Architecture::emitLoad(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string loadMnemonic = "ldr";
        std::string regName = "x10";
        size_t size = i.getType() ? i.getType()->getSize() : 8;
        if (i.getType() && i.getType()->isFloatingPoint()) {
            regName = (size == 4) ? "s10" : "d10";
        } else {
            if (size == 1) { loadMnemonic = "ldrb"; regName = "w10"; }
            else if (size == 2) { loadMnemonic = "ldrh"; regName = "w10"; }
            else if (size == 4) { loadMnemonic = "ldr"; regName = "w10"; }
        }

        ir::Value* ptrVal = i.getOperands()[0]->get();
        AArch64ComplexAddress addr = matchComplexAddress(cg, ptrVal);
        if (addr.isValid) {
            *os << "  " << loadMnemonic << " " << regName << ", " << addr.format() << "\n";
            *os << "  str " << regName << ", " << cg.getValueAsOperand(&i) << "\n";
        } else {
            *os << "  ldr x9, " << cg.getValueAsOperand(ptrVal) << "\n";
            *os << "  " << loadMnemonic << " " << regName << ", [x9]\n";
            *os << "  str " << regName << ", " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}

void AArch64Architecture::emitStore(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string storeMnemonic = "str";
        std::string regName = "x10";
        ir::Value* val = i.getOperands()[0]->get();
        size_t size = val->getType() ? val->getType()->getSize() : 8;
        if (val->getType() && val->getType()->isFloatingPoint()) {
            regName = (size == 4) ? "s10" : "d10";
        } else {
            if (size == 1) { storeMnemonic = "strb"; regName = "w10"; }
            else if (size == 2) { storeMnemonic = "strh"; regName = "w10"; }
            else if (size == 4) { storeMnemonic = "str"; regName = "w10"; }
        }

        ir::Value* ptrVal = i.getOperands()[1]->get();
        AArch64ComplexAddress addr = matchComplexAddress(cg, ptrVal);
        if (addr.isValid) {
            *os << "  ldr " << regName << ", " << cg.getValueAsOperand(val) << "\n";
            *os << "  " << storeMnemonic << " " << regName << ", " << addr.format() << "\n";
        } else {
            *os << "  ldr x9, " << cg.getValueAsOperand(ptrVal) << "\n";
            *os << "  ldr " << regName << ", " << cg.getValueAsOperand(val) << "\n";
            *os << "  " << storeMnemonic << " " << regName << ", [x9]\n";
        }
    }
}
void AArch64Architecture::emitAlloc(CodeGen& cg, ir::Instruction& i) {
    uint64_t size = 8;
    if (i.getOpcode() == ir::Instruction::Alloc4) size = 4;
    else if (i.getOpcode() == ir::Instruction::Alloc16) size = 16;
    else if (!i.getOperands().empty()) {
        if (auto* sizeConst = dynamic_cast<ir::ConstantInt*>(i.getOperands()[0]->get()))
            size = sizeConst->getValue();
    }
    uint64_t alignedSize = (size + 7) & ~7;

    if (auto* os = cg.getTextStream()) {
        *os << "  # AArch64 Bump Allocation\n";
        *os << "  adrp x9, heap_ptr\n";
        *os << "  ldr x9, [x9, :lo12:heap_ptr]\n";
        *os << "  str x9, " << cg.getValueAsOperand(&i) << "\n";
        *os << "  add x9, x9, #" << alignedSize << "\n";
        *os << "  adrp x10, heap_ptr\n";
        *os << "  str x9, [x10, :lo12:heap_ptr]\n";
    }
}
void AArch64Architecture::emitPhiCopies(CodeGen& cg, ir::BasicBlock* source, ir::BasicBlock* target) {
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
            if (type && (type->isVectorTy() || dynamic_cast<const ir::VectorType*>(type) != nullptr)) {
                *os << "  ldr q16, " << srcOp << "\n";
                *os << "  str q16, " << destOp << "\n";
            } else if (type && type->isFloatingPoint()) {
                std::string reg = (type->getSize() == 4) ? "s16" : "d16";
                *os << "  ldr " << reg << ", " << srcOp << "\n";
                *os << "  str " << reg << ", " << destOp << "\n";
            } else {
                std::string reg = getRegisterName("x9", type);
                *os << "  ldr " << reg << ", " << srcOp << "\n";
                *os << "  str " << reg << ", " << destOp << "\n";
            }
        }
        return;
    }

    for (const auto& move : phiMoves) {
        ir::Value* incomingVal = move.first;
        std::string srcOp = cg.getValueAsOperand(incomingVal);
        const ir::Type* type = incomingVal->getType();
        if (type && (type->isVectorTy() || dynamic_cast<const ir::VectorType*>(type) != nullptr)) {
            *os << "  ldr q16, " << srcOp << "\n";
            *os << "  str q16, [sp, #-16]!\n";
        } else if (type && type->isFloatingPoint()) {
            std::string reg = (type->getSize() == 4) ? "s16" : "d16";
            *os << "  ldr " << reg << ", " << srcOp << "\n";
            *os << "  str " << reg << ", [sp, #-16]!\n";
        } else {
            std::string reg = getRegisterName("x9", type);
            *os << "  ldr " << reg << ", " << srcOp << "\n";
            *os << "  str x9, [sp, #-16]!\n";
        }
    }

    for (auto it = phiMoves.rbegin(); it != phiMoves.rend(); ++it) {
        ir::PhiNode* phi = it->second;
        std::string destOp = cg.getValueAsOperand(phi);
        const ir::Type* type = phi->getType();
        if (type && (type->isVectorTy() || dynamic_cast<const ir::VectorType*>(type) != nullptr)) {
            *os << "  ldr q16, [sp], #16\n";
            *os << "  str q16, " << destOp << "\n";
        } else if (type && type->isFloatingPoint()) {
            std::string reg = (type->getSize() == 4) ? "s16" : "d16";
            *os << "  ldr " << reg << ", [sp], #16\n";
            *os << "  str " << reg << ", " << destOp << "\n";
        } else {
            std::string reg = getRegisterName("x9", type);
            *os << "  ldr x9, [sp], #16\n";
            *os << "  str " << reg << ", " << destOp << "\n";
        }
    }
}

void AArch64Architecture::emitBr(CodeGen& cg, ir::Instruction& i) {
    if (i.getOperands().size() == 1) {
        auto* targetBB = dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get());
        emitPhiCopies(cg, i.getParent(), targetBB);
        if (auto* os = cg.getTextStream()) {
            *os << "  b " << cg.getTargetInfo()->getBBLabel(targetBB) << "\n";
        }
        return;
    }

    auto* targetTrue = dynamic_cast<ir::BasicBlock*>(i.getOperands()[1]->get());
    auto* targetFalse = dynamic_cast<ir::BasicBlock*>(i.getOperands()[2]->get());

    if (auto* os = cg.getTextStream()) {
        *os << "  ldr w9, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  cmp w9, #0\n";

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

            *os << "  b.ne " << labelTrueCopies << "\n";
            *os << "  b " << labelFalseCopies << "\n";

            *os << labelTrueCopies << ":\n";
            emitPhiCopies(cg, i.getParent(), targetTrue);
            *os << "  b " << trueLabel << "\n";

            *os << labelFalseCopies << ":\n";
            emitPhiCopies(cg, i.getParent(), targetFalse);
            *os << "  b " << falseLabel << "\n";
        } else {
            *os << "  b.ne " << trueLabel << "\n";
            *os << "  b " << falseLabel << "\n";
        }
    }
}

void AArch64Architecture::emitJmp(CodeGen& cg, ir::Instruction& i) {
    auto* targetBB = dynamic_cast<ir::BasicBlock*>(i.getOperands()[0]->get());
    emitPhiCopies(cg, i.getParent(), targetBB);
    if (auto* os = cg.getTextStream()) {
        *os << "  b " << cg.getTargetInfo()->getBBLabel(targetBB) << "\n";
    }
}

bool AArch64Architecture::emitMulAddFusion(CodeGen& cg, ir::Instruction& mul, ir::Instruction& add) {
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
        std::string r1 = getRegisterName("x9", m0->getType());
        std::string r2 = getRegisterName("x10", m1->getType());
        std::string r3 = getRegisterName("x11", addOther->getType());
        std::string rd = getRegisterName("x9", add.getType());

        *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(m0) << "\n";
        *os << "  ldr " << r2 << ", " << cg.getValueAsOperand(m1) << "\n";
        *os << "  ldr " << r3 << ", " << cg.getValueAsOperand(addOther) << "\n";
        *os << "  madd " << rd << ", " << r1 << ", " << r2 << ", " << r3 << "\n";
        *os << "  str " << rd << ", " << cg.getValueAsOperand(&add) << "\n";
        return true;
    }
    return false;
}

bool AArch64Architecture::emitCmpAndBranchFusion(CodeGen& cg, ir::Instruction& cmp, ir::Instruction& br) {
    if (br.getOperands().size() < 3) return false;
    auto* targetTrue = dynamic_cast<ir::BasicBlock*>(br.getOperands()[1]->get());
    auto* targetFalse = dynamic_cast<ir::BasicBlock*>(br.getOperands()[2]->get());
    if (!targetTrue || !targetFalse) return false;

    std::string cond = "eq";
    switch (cmp.getOpcode()) {
        case ir::Instruction::Ceq: case ir::Instruction::Ceqf: cond = "eq"; break;
        case ir::Instruction::Cne: case ir::Instruction::Cnef: cond = "ne"; break;
        case ir::Instruction::Cslt: cond = "lt"; break;
        case ir::Instruction::Cult: case ir::Instruction::Clt: cond = "lo"; break;
        case ir::Instruction::Csle: cond = "le"; break;
        case ir::Instruction::Cule: case ir::Instruction::Cle: cond = "ls"; break;
        case ir::Instruction::Csgt: cond = "gt"; break;
        case ir::Instruction::Cugt: case ir::Instruction::Cgt: cond = "hi"; break;
        case ir::Instruction::Csge: cond = "ge"; break;
        case ir::Instruction::Cuge: case ir::Instruction::Cge: cond = "hs"; break;
        default: cond = "ne"; break;
    }

    ir::Value* l = cmp.getOperands()[0]->get();
    ir::Value* r = cmp.getOperands()[1]->get();

    if (auto* os = cg.getTextStream()) {
        if (l->getType() && l->getType()->isFloatingPoint()) {
            std::string r1 = (l->getType()->getSize() == 4) ? "s16" : "d16";
            std::string r2 = (r->getType()->getSize() == 4) ? "s17" : "d17";
            *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n";
            *os << "  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n";
            *os << "  fcmp " << r1 << ", " << r2 << "\n";
        } else {
            std::string r1 = getRegisterName("x9", l->getType());
            std::string r2 = getRegisterName("x10", r->getType());
            *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n";
            *os << "  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n";
            *os << "  cmp " << r1 << ", " << r2 << "\n";
        }

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

            *os << "  b." << cond << " " << labelTrueCopies << "\n";
            *os << "  b " << labelFalseCopies << "\n";

            *os << labelTrueCopies << ":\n";
            emitPhiCopies(cg, br.getParent(), targetTrue);
            *os << "  b " << trueLabel << "\n";

            *os << labelFalseCopies << ":\n";
            emitPhiCopies(cg, br.getParent(), targetFalse);
            *os << "  b " << falseLabel << "\n";
        } else {
            *os << "  b." << cond << " " << trueLabel << "\n";
            *os << "  b " << falseLabel << "\n";
        }
        return true;
    }
    return false;
}

void AArch64Architecture::emitSyscall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {
    auto* si = dynamic_cast<ir::SyscallInstruction*>(&i); ir::SyscallId sid = si ? si->getSyscallId() : ir::SyscallId::None;
    if (auto* os = cg.getTextStream()) {
        if (sid != ir::SyscallId::None) *os << "  mov x8, #" << osInfo.getSyscallNumber(sid) << "\n"; else *os << "  mov x8, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        size_t sArg = (sid != ir::SyscallId::None) ? 0 : 1;
        for (size_t j = sArg; j < i.getOperands().size(); ++j) { size_t aIdx = (sid != ir::SyscallId::None) ? j : j - 1; if (aIdx < 6) *os << "  mov x" << aIdx << ", " << cg.getValueAsOperand(i.getOperands()[j]->get()) << "\n"; }
        *os << "  svc #0\n"; if (i.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  str x0, " << cg.getValueAsOperand(&i) << "\n";
    }
}

void AArch64Architecture::emitExternCall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {
    auto* ei = dynamic_cast<ir::ExternCallInstruction*>(&i); if (!ei) return;
    const auto* spec = cg.getTargetInfo()->findCapability(ei->getCapability());
    if (!spec || !cg.getTargetInfo()->validateCapability(i, *spec)) { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, spec); return; }
    cg.getTargetInfo()->emitDomainCapability(cg, i, *spec);
}

void AArch64Architecture::emitNativeSyscall(CodeGen& cg, uint64_t syscallNum, const std::vector<ir::Value*>& args) {
    if (auto* os = cg.getTextStream()) {
        *os << "  mov x8, #" << syscallNum << "\n";
        for (size_t i = 0; i < std::min(args.size(), (size_t)6); ++i) {
            *os << "  ldr " << getRegisterName("x" + std::to_string(i), args[i]->getType()) << ", " << cg.getValueAsOperand(args[i]) << "\n";
        }
        *os << "  svc #0\n";
    }
}

void AArch64Architecture::emitNativeLibraryCall(CodeGen& cg, const std::string& name, const std::vector<ir::Value*>& args) {
    if (auto* os = cg.getTextStream()) {
        for (size_t i = 0; i < std::min(args.size(), (size_t)8); ++i) {
            *os << "  ldr " << getRegisterName("x" + std::to_string(i), args[i]->getType()) << ", " << cg.getValueAsOperand(args[i]) << "\n";
        }
        *os << "  bl " << name << "\n";
    }
}

std::string AArch64Architecture::formatStackOperand(int o) const { return "[x29, #" + std::to_string(o) + "]"; }
std::string AArch64Architecture::formatGlobalOperand(const std::string& n) const { return n; }

std::string AArch64Architecture::formatConstant(const ir::ConstantInt* C) const {
    if (!C) return "#0";
    return "#" + std::to_string(C->getValue());
}

std::string AArch64Architecture::formatConstant(const ir::ConstantFP* C) const {
    if (!C) return "#0";
    uint64_t bits = 0;
    double val = C->getValue();
    std::memcpy(&bits, &val, sizeof(double));
    return "#" + std::to_string(bits);
}

bool AArch64Architecture::isCallerSaved(const std::string& r) const { static const std::set<std::string> cs = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"}; return cs.count(r); }
bool AArch64Architecture::isCalleeSaved(const std::string& r) const { static const std::set<std::string> cs = {"x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15"}; return cs.count(r); }
bool AArch64Architecture::isReserved(const std::string& reg) const { return reg == "sp" || reg == "x29" || reg == "x30" || reg == "w29" || reg == "w30"; }

std::string AArch64Architecture::getRegisterName(const std::string& b, const ir::Type* t) const {
    if (t && (t->isVectorTy() || dynamic_cast<const ir::VectorType*>(t) != nullptr)) {
        if (b[0] == 'x') return "v" + b.substr(1);
        if (b[0] == 'w') return "v" + b.substr(1);
    }
    if (auto* it = dynamic_cast<const ir::IntegerType*>(t)) {
        if (it->getBitwidth() <= 32 && b[0] == 'x') return "w" + b.substr(1);
    }
    return b;
}

void AArch64Architecture::emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) {
    if (auto* os = cg.getTextStream()) {
        if (type && type->isFloatingPoint()) {
            if (argIndex < 8) {
                std::string reg = (type->getSize() == 4) ? "s" : "d";
                reg += std::to_string(argIndex);
                *os << "  ldr " << reg << ", " << value << "\n";
            }
        } else {
            if (argIndex < 8) {
                std::string reg = getRegisterName("x" + std::to_string(argIndex), type);
                *os << "  ldr " << reg << ", " << value << "\n";
            }
        }
    }
}

void AArch64Architecture::emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) {
    if (auto* os = cg.getTextStream()) {
        if (type && type->isFloatingPoint()) {
            if (argIndex < 8) {
                std::string reg = (type->getSize() == 4) ? "s" : "d";
                reg += std::to_string(argIndex);
                *os << "  str " << reg << ", " << dest << "\n";
            }
        } else {
            if (argIndex < 8) {
                std::string reg = getRegisterName("x" + std::to_string(argIndex), type);
                *os << "  str " << reg << ", " << dest << "\n";
            }
        }
    }
}

std::string AArch64Architecture::getConditionCode(const std::string& op, bool isFloat, bool isUnsigned) const { return "eq"; }
std::string AArch64Architecture::getWRegister(const std::string& xReg) const { return xReg; }

VectorCapabilities AArch64Architecture::getVectorCapabilities() const {
    VectorCapabilities caps;
    caps.supportsNEON = true;
    caps.maxVectorWidth = 128;
    caps.supportedWidths = {64, 128};
    caps.supportsFloatVectors = true;
    caps.supportsIntegerVectors = true;
    caps.supportsDoubleVectors = true;
    caps.simdExtension = "NEON";
    return caps;
}

bool AArch64Architecture::supportsVectorWidth(unsigned width) const {
    return width == 64 || width == 128;
}

bool AArch64Architecture::supportsVectorType(const ir::VectorType* type) const {
    if (!type) return false;
    unsigned totalBits = static_cast<unsigned>(type->getSize() * 8);
    if (totalBits != 64 && totalBits != 128) return false;
    auto* elemTy = type->getElementType();
    if (!elemTy) return false;
    size_t elemBits = elemTy->getSize() * 8;
    return elemBits == 8 || elemBits == 16 || elemBits == 32 || elemBits == 64;
}

std::string AArch64Architecture::getNEONArrangement(const ir::VectorType* vecTy) const {
    if (!vecTy || !vecTy->getElementType()) return ".16b";
    unsigned elemBits = vecTy->getElementType()->getSize() * 8;
    unsigned numElems = vecTy->getNumElements();
    if (elemBits == 8 && numElems == 16) return ".16b";
    if (elemBits == 8 && numElems == 8) return ".8b";
    if (elemBits == 16 && numElems == 8) return ".8h";
    if (elemBits == 16 && numElems == 4) return ".4h";
    if (elemBits == 32 && numElems == 4) return ".4s";
    if (elemBits == 32 && numElems == 2) return ".2s";
    if (elemBits == 64 && numElems == 2) return ".2d";
    if (elemBits == 64 && numElems == 1) return ".1d";
    return ".16b";
}

bool AArch64Architecture::supportsVectorOperation(ir::Instruction::Opcode op, const ir::VectorType* type) const {
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
        case ir::Instruction::VHAdd:
        case ir::Instruction::FMA:
        case ir::Instruction::FMS:
        case ir::Instruction::FNMA:
        case ir::Instruction::FNMS:
        case ir::Instruction::VShuffle:
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

bool AArch64Architecture::supportsVectorConversion(ir::Instruction::Opcode op, const ir::VectorType* srcType, const ir::VectorType* dstType) const {
    if (!srcType || !dstType) return false;
    if (op == ir::Instruction::VSExt || op == ir::Instruction::VZExt || op == ir::Instruction::VTrunc) return true;
    return false;
}

void AArch64Architecture::emitVectorLoad(CodeGen& cg, ir::VectorInstruction& i) {
    auto* os = cg.getTextStream();
    if (!os) return;
    std::string ptr = cg.getValueAsOperand(i.getOperands()[0]->get());
    std::string dst = cg.getValueAsOperand(&i);
    *os << "  ldr x9, " << ptr << "\n";
    *os << "  ldr q16, [x9]\n";
    *os << "  str q16, " << dst << "\n";
}

void AArch64Architecture::emitVectorStore(CodeGen& cg, ir::VectorInstruction& i) {
    auto* os = cg.getTextStream();
    if (!os) return;
    std::string val = cg.getValueAsOperand(i.getOperands()[0]->get());
    std::string ptr = cg.getValueAsOperand(i.getOperands()[1]->get());
    *os << "  ldr q16, " << val << "\n";
    *os << "  ldr x9, " << ptr << "\n";
    *os << "  str q16, [x9]\n";
}

void AArch64Architecture::emitVectorReduction(CodeGen& cg, ir::VectorInstruction& i) {
    auto* os = cg.getTextStream();
    if (!os) return;

    auto* vecTy = dynamic_cast<const ir::VectorType*>(i.getOperands()[0]->get()->getType());
    std::string arrange = getNEONArrangement(vecTy);
    std::string val = cg.getValueAsOperand(i.getOperands()[0]->get());
    std::string dst = cg.getValueAsOperand(&i);

    std::string scalarReg = "s16";
    if (vecTy && vecTy->getElementType()) {
        unsigned bits = vecTy->getElementType()->getSize() * 8;
        if (bits == 8) scalarReg = "b16";
        else if (bits == 16) scalarReg = "h16";
        else if (bits == 32) scalarReg = "s16";
        else if (bits == 64) scalarReg = "d16";
    }

    *os << "  ldr q16, " << val << "\n";
    switch (i.getOpcode()) {
        case ir::Instruction::VAdd:
            *os << "  addv " << scalarReg << ", v16" << arrange << "\n";
            break;
        case ir::Instruction::VMin:
            *os << "  sminv " << scalarReg << ", v16" << arrange << "\n";
            break;
        case ir::Instruction::VMax:
            *os << "  smaxv " << scalarReg << ", v16" << arrange << "\n";
            break;
        case ir::Instruction::VFMin:
            *os << "  fminv " << scalarReg << ", v16" << arrange << "\n";
            break;
        case ir::Instruction::VFMax:
            *os << "  fmaxv " << scalarReg << ", v16" << arrange << "\n";
            break;
        default:
            *os << "  addv " << scalarReg << ", v16" << arrange << "\n";
            break;
    }
    *os << "  str " << scalarReg << ", " << dst << "\n";
}

void AArch64Architecture::emitVectorHorizontalOp(CodeGen& cg, ir::VectorInstruction& i) {
    emitVectorReduction(cg, i);
}

void AArch64Architecture::emitVectorArithmetic(CodeGen& cg, ir::VectorInstruction& i) {
    auto* os = cg.getTextStream();
    if (!os) return;

    auto* vecTy = dynamic_cast<const ir::VectorType*>(i.getType());
    if (!vecTy && !i.getOperands().empty() && i.getOperands()[0]->get()) {
        vecTy = dynamic_cast<const ir::VectorType*>(i.getOperands()[0]->get()->getType());
    }

    std::string arrange = getNEONArrangement(vecTy);
    std::string rawDst = cg.getValueAsOperand(&i);
    std::string dst = getRegisterName(rawDst, vecTy);

    switch (i.getOpcode()) {
        case ir::Instruction::VLoad: {
            std::string ptr = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string qDst = dst;
            if (!qDst.empty() && qDst[0] == 'v') qDst[0] = 'q';
            *os << "  ldr " << qDst << ", [" << ptr << "]\n";
            break;
        }
        case ir::Instruction::VStore: {
            std::string vec = getRegisterName(cg.getValueAsOperand(i.getOperands()[0]->get()), vecTy);
            std::string ptr = cg.getValueAsOperand(i.getOperands()[1]->get());
            std::string qVec = vec;
            if (!qVec.empty() && qVec[0] == 'v') qVec[0] = 'q';
            *os << "  str " << qVec << ", [" << ptr << "]\n";
            break;
        }
        case ir::Instruction::VAdd: {
            std::string op0 = getRegisterName(cg.getValueAsOperand(i.getOperands()[0]->get()), vecTy);
            std::string op1 = getRegisterName(cg.getValueAsOperand(i.getOperands()[1]->get()), vecTy);
            *os << "  add " << dst << arrange << ", " << op0 << arrange << ", " << op1 << arrange << "\n";
            break;
        }
        case ir::Instruction::VSub: {
            std::string op0 = getRegisterName(cg.getValueAsOperand(i.getOperands()[0]->get()), vecTy);
            std::string op1 = getRegisterName(cg.getValueAsOperand(i.getOperands()[1]->get()), vecTy);
            *os << "  sub " << dst << arrange << ", " << op0 << arrange << ", " << op1 << arrange << "\n";
            break;
        }
        case ir::Instruction::VMul: {
            std::string op0 = getRegisterName(cg.getValueAsOperand(i.getOperands()[0]->get()), vecTy);
            std::string op1 = getRegisterName(cg.getValueAsOperand(i.getOperands()[1]->get()), vecTy);
            *os << "  mul " << dst << arrange << ", " << op0 << arrange << ", " << op1 << arrange << "\n";
            break;
        }
        case ir::Instruction::VFAdd: {
            std::string op0 = getRegisterName(cg.getValueAsOperand(i.getOperands()[0]->get()), vecTy);
            std::string op1 = getRegisterName(cg.getValueAsOperand(i.getOperands()[1]->get()), vecTy);
            *os << "  fadd " << dst << arrange << ", " << op0 << arrange << ", " << op1 << arrange << "\n";
            break;
        }
        case ir::Instruction::VFSub: {
            std::string op0 = getRegisterName(cg.getValueAsOperand(i.getOperands()[0]->get()), vecTy);
            std::string op1 = getRegisterName(cg.getValueAsOperand(i.getOperands()[1]->get()), vecTy);
            *os << "  fsub " << dst << arrange << ", " << op0 << arrange << ", " << op1 << arrange << "\n";
            break;
        }
        case ir::Instruction::VFMul: {
            std::string op0 = getRegisterName(cg.getValueAsOperand(i.getOperands()[0]->get()), vecTy);
            std::string op1 = getRegisterName(cg.getValueAsOperand(i.getOperands()[1]->get()), vecTy);
            *os << "  fmul " << dst << arrange << ", " << op0 << arrange << ", " << op1 << arrange << "\n";
            break;
        }
        case ir::Instruction::VFDiv: {
            std::string op0 = getRegisterName(cg.getValueAsOperand(i.getOperands()[0]->get()), vecTy);
            std::string op1 = getRegisterName(cg.getValueAsOperand(i.getOperands()[1]->get()), vecTy);
            *os << "  fdiv " << dst << arrange << ", " << op0 << arrange << ", " << op1 << arrange << "\n";
            break;
        }
        case ir::Instruction::VAnd: {
            std::string op0 = getRegisterName(cg.getValueAsOperand(i.getOperands()[0]->get()), vecTy);
            std::string op1 = getRegisterName(cg.getValueAsOperand(i.getOperands()[1]->get()), vecTy);
            *os << "  and " << dst << ".16b, " << op0 << ".16b, " << op1 << ".16b\n";
            break;
        }
        case ir::Instruction::VOr: {
            std::string op0 = getRegisterName(cg.getValueAsOperand(i.getOperands()[0]->get()), vecTy);
            std::string op1 = getRegisterName(cg.getValueAsOperand(i.getOperands()[1]->get()), vecTy);
            *os << "  orr " << dst << ".16b, " << op0 << ".16b, " << op1 << ".16b\n";
            break;
        }
        case ir::Instruction::VXor: {
            std::string op0 = getRegisterName(cg.getValueAsOperand(i.getOperands()[0]->get()), vecTy);
            std::string op1 = getRegisterName(cg.getValueAsOperand(i.getOperands()[1]->get()), vecTy);
            *os << "  eor " << dst << ".16b, " << op0 << ".16b, " << op1 << ".16b\n";
            break;
        }
        case ir::Instruction::VBroadcast: {
            ir::Value* sVal = i.getOperands()[0]->get();
            std::string scalar = cg.getValueAsOperand(sVal);
            std::string srcReg = scalar;
            bool isFloat = sVal->getType() && sVal->getType()->isFloatingPoint();
            if (scalar.empty() || scalar[0] == '[' || scalar[0] == '#' || scalar.find('x') == std::string::npos) {
                if (isFloat) {
                    srcReg = (sVal->getType()->getSize() == 4) ? "s16" : "d16";
                    *os << "  ldr " << srcReg << ", " << scalar << "\n";
                } else {
                    srcReg = getRegisterName("x9", sVal->getType());
                    if (!scalar.empty() && scalar[0] == '#') *os << "  mov " << srcReg << ", " << scalar << "\n";
                    else *os << "  ldr " << srcReg << ", " << scalar << "\n";
                }
            }
            std::string realDst = (dst.empty() || dst[0] != 'v') ? "v16" : dst;
            *os << "  dup " << realDst << arrange << ", " << srcReg << "\n";
            if (dst != realDst) *os << "  str " << realDst << ", " << rawDst << "\n";
            break;
        }
        case ir::Instruction::VExtract: {
            std::string vecStr = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string vec = vecStr;
            if (vec.empty() || vec[0] != 'v') {
                *os << "  ldr q16, " << vecStr << "\n";
                vec = "v16";
            } else vec = getRegisterName(vec, vecTy);

            uint64_t idx = 0;
            if (i.getOperands().size() > 1 && i.getOperands()[1]->get()) {
                if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[1]->get())) {
                    idx = ci->getValue();
                }
            }

            unsigned bits = (vecTy && vecTy->getElementType()) ? vecTy->getElementType()->getSize() * 8 : 32;
            bool isFloat = vecTy && vecTy->getElementType() && vecTy->getElementType()->isFloatingPoint();
            std::string elementSpec = ".s[" + std::to_string(idx) + "]";
            if (bits == 8) elementSpec = ".b[" + std::to_string(idx) + "]";
            else if (bits == 16) elementSpec = ".h[" + std::to_string(idx) + "]";
            else if (bits == 32) elementSpec = ".s[" + std::to_string(idx) + "]";
            else if (bits == 64) elementSpec = ".d[" + std::to_string(idx) + "]";

            std::string targetReg;
            if (isFloat) {
                targetReg = (bits == 32) ? "s16" : "d16";
            } else {
                targetReg = (bits == 64) ? "x9" : "w9";
            }

            if (!rawDst.empty() && (rawDst[0] == 'x' || rawDst[0] == 'w' || rawDst[0] == 's' || rawDst[0] == 'd')) {
                targetReg = rawDst;
            }

            *os << "  mov " << targetReg << ", " << vec << elementSpec << "\n";
            if (targetReg != rawDst) {
                *os << "  str " << targetReg << ", " << rawDst << "\n";
            }
            break;
        }
        case ir::Instruction::VInsert: {
            std::string vecStr = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string vec = vecStr;
            if (vec.empty() || vec[0] != 'v') {
                *os << "  ldr q16, " << vecStr << "\n";
                vec = "v16";
            } else vec = getRegisterName(vec, vecTy);

            ir::Value* valVal = i.getOperands()[1]->get();
            std::string valStr = cg.getValueAsOperand(valVal);
            unsigned bits = (vecTy && vecTy->getElementType()) ? vecTy->getElementType()->getSize() * 8 : 32;
            bool isFloat = vecTy && vecTy->getElementType() && vecTy->getElementType()->isFloatingPoint();

            std::string valReg = valStr;
            if (valStr.empty() || valStr[0] == '[' || valStr[0] == '#' || valStr.find('x') == std::string::npos) {
                if (isFloat) {
                    valReg = (bits == 32) ? "s17" : "d17";
                    *os << "  ldr " << valReg << ", " << valStr << "\n";
                } else {
                    valReg = (bits == 64) ? "x10" : "w10";
                    if (!valStr.empty() && valStr[0] == '#') *os << "  mov " << valReg << ", " << valStr << "\n";
                    else *os << "  ldr " << valReg << ", " << valStr << "\n";
                }
            }

            uint64_t idx = 0;
            if (i.getOperands().size() > 2 && i.getOperands()[2]->get()) {
                if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[2]->get())) {
                    idx = ci->getValue();
                }
            }

            std::string elementSpec = ".s[" + std::to_string(idx) + "]";
            if (bits == 8) elementSpec = ".b[" + std::to_string(idx) + "]";
            else if (bits == 16) elementSpec = ".h[" + std::to_string(idx) + "]";
            else if (bits == 32) elementSpec = ".s[" + std::to_string(idx) + "]";
            else if (bits == 64) elementSpec = ".d[" + std::to_string(idx) + "]";

            std::string realDst = (dst.empty() || dst[0] != 'v') ? "v16" : dst;
            if (realDst != vec) *os << "  mov " << realDst << ".16b, " << vec << ".16b\n";
            *os << "  mov " << realDst << elementSpec << ", " << valReg << "\n";
            if (dst != realDst) *os << "  str " << realDst << ", " << rawDst << "\n";
            break;
        }
        case ir::Instruction::FMA: {
            std::string a = getRegisterName(cg.getValueAsOperand(i.getOperands()[0]->get()), vecTy);
            std::string b = getRegisterName(cg.getValueAsOperand(i.getOperands()[1]->get()), vecTy);
            std::string c = getRegisterName(cg.getValueAsOperand(i.getOperands()[2]->get()), vecTy);
            if (dst != c) *os << "  mov " << dst << ".16b, " << c << ".16b\n";
            *os << "  fmla " << dst << arrange << ", " << a << arrange << ", " << b << arrange << "\n";
            break;
        }
        case ir::Instruction::VCmp: {
            std::string op0 = getRegisterName(cg.getValueAsOperand(i.getOperands()[0]->get()), vecTy);
            std::string op1 = getRegisterName(cg.getValueAsOperand(i.getOperands()[1]->get()), vecTy);
            ir::VectorCompareOp pred = ir::VectorCompareOp::EQ;
            if (i.getOperands().size() > 2) {
                if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[2]->get())) {
                    pred = static_cast<ir::VectorCompareOp>(ci->getValue());
                }
            }
            std::string cmpMnemonic = "cmeq";
            switch (pred) {
                case ir::VectorCompareOp::EQ: cmpMnemonic = "cmeq"; break;
                case ir::VectorCompareOp::GT: case ir::VectorCompareOp::UGT: cmpMnemonic = "cmgt"; break;
                case ir::VectorCompareOp::GE: case ir::VectorCompareOp::UGE: cmpMnemonic = "cmge"; break;
                case ir::VectorCompareOp::LT: case ir::VectorCompareOp::ULT: cmpMnemonic = "cmlt"; break;
                case ir::VectorCompareOp::LE: case ir::VectorCompareOp::ULE: cmpMnemonic = "cmle"; break;
                default: cmpMnemonic = "cmeq"; break;
            }
            *os << "  " << cmpMnemonic << " " << dst << arrange << ", " << op0 << arrange << ", " << op1 << arrange << "\n";
            break;
        }
        case ir::Instruction::VSelect: {
            std::string mask = getRegisterName(cg.getValueAsOperand(i.getOperands()[0]->get()), vecTy);
            std::string trueVal = getRegisterName(cg.getValueAsOperand(i.getOperands()[1]->get()), vecTy);
            std::string falseVal = getRegisterName(cg.getValueAsOperand(i.getOperands()[2]->get()), vecTy);
            if (dst != trueVal) *os << "  mov " << dst << ".16b, " << trueVal << ".16b\n";
            *os << "  bsl " << mask << ".16b, " << trueVal << ".16b, " << falseVal << ".16b\n";
            break;
        }
        case ir::Instruction::VMin: {
            std::string op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
            if (op0.empty() || op0[0] != 'v') { *os << "  ldr q16, " << op0 << "\n"; op0 = "v16"; } else op0 = getRegisterName(op0, vecTy);
            if (op1.empty() || op1[0] != 'v') { *os << "  ldr q17, " << op1 << "\n"; op1 = "v17"; } else op1 = getRegisterName(op1, vecTy);
            std::string realDst = (dst.empty() || dst[0] != 'v') ? "v16" : dst;
            *os << "  smin " << realDst << arrange << ", " << op0 << arrange << ", " << op1 << arrange << "\n";
            if (dst != realDst) *os << "  str " << realDst << ", " << rawDst << "\n";
            break;
        }
        case ir::Instruction::VMax: {
            std::string op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
            if (op0.empty() || op0[0] != 'v') { *os << "  ldr q16, " << op0 << "\n"; op0 = "v16"; } else op0 = getRegisterName(op0, vecTy);
            if (op1.empty() || op1[0] != 'v') { *os << "  ldr q17, " << op1 << "\n"; op1 = "v17"; } else op1 = getRegisterName(op1, vecTy);
            std::string realDst = (dst.empty() || dst[0] != 'v') ? "v16" : dst;
            *os << "  smax " << realDst << arrange << ", " << op0 << arrange << ", " << op1 << arrange << "\n";
            if (dst != realDst) *os << "  str " << realDst << ", " << rawDst << "\n";
            break;
        }
        case ir::Instruction::VFMin: {
            std::string op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
            if (op0.empty() || op0[0] != 'v') { *os << "  ldr q16, " << op0 << "\n"; op0 = "v16"; } else op0 = getRegisterName(op0, vecTy);
            if (op1.empty() || op1[0] != 'v') { *os << "  ldr q17, " << op1 << "\n"; op1 = "v17"; } else op1 = getRegisterName(op1, vecTy);
            std::string realDst = (dst.empty() || dst[0] != 'v') ? "v16" : dst;
            *os << "  fmin " << realDst << arrange << ", " << op0 << arrange << ", " << op1 << arrange << "\n";
            if (dst != realDst) *os << "  str " << realDst << ", " << rawDst << "\n";
            break;
        }
        case ir::Instruction::VFMax: {
            std::string op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
            if (op0.empty() || op0[0] != 'v') { *os << "  ldr q16, " << op0 << "\n"; op0 = "v16"; } else op0 = getRegisterName(op0, vecTy);
            if (op1.empty() || op1[0] != 'v') { *os << "  ldr q17, " << op1 << "\n"; op1 = "v17"; } else op1 = getRegisterName(op1, vecTy);
            std::string realDst = (dst.empty() || dst[0] != 'v') ? "v16" : dst;
            *os << "  fmax " << realDst << arrange << ", " << op0 << arrange << ", " << op1 << arrange << "\n";
            if (dst != realDst) *os << "  str " << realDst << ", " << rawDst << "\n";
            break;
        }
        case ir::Instruction::VHAdd: {
            std::string op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string op1 = cg.getValueAsOperand(i.getOperands()[1]->get());
            if (op0.empty() || op0[0] != 'v') { *os << "  ldr q16, " << op0 << "\n"; op0 = "v16"; } else op0 = getRegisterName(op0, vecTy);
            if (op1.empty() || op1[0] != 'v') { *os << "  ldr q17, " << op1 << "\n"; op1 = "v17"; } else op1 = getRegisterName(op1, vecTy);
            std::string realDst = (dst.empty() || dst[0] != 'v') ? "v16" : dst;
            *os << "  addp " << realDst << arrange << ", " << op0 << arrange << ", " << op1 << arrange << "\n";
            if (dst != realDst) *os << "  str " << realDst << ", " << rawDst << "\n";
            break;
        }
        case ir::Instruction::FMS: {
            std::string a = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string b = cg.getValueAsOperand(i.getOperands()[1]->get());
            std::string c = cg.getValueAsOperand(i.getOperands()[2]->get());
            if (a.empty() || a[0] != 'v') { *os << "  ldr q16, " << a << "\n"; a = "v16"; } else a = getRegisterName(a, vecTy);
            if (b.empty() || b[0] != 'v') { *os << "  ldr q17, " << b << "\n"; b = "v17"; } else b = getRegisterName(b, vecTy);
            if (c.empty() || c[0] != 'v') { *os << "  ldr q18, " << c << "\n"; c = "v18"; } else c = getRegisterName(c, vecTy);
            std::string realDst = (dst.empty() || dst[0] != 'v') ? "v18" : dst;
            if (realDst != c) *os << "  mov " << realDst << ".16b, " << c << ".16b\n";
            *os << "  fmls " << realDst << arrange << ", " << a << arrange << ", " << b << arrange << "\n";
            if (dst != realDst) *os << "  str " << realDst << ", " << rawDst << "\n";
            break;
        }
        case ir::Instruction::FNMA: {
            std::string a = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string b = cg.getValueAsOperand(i.getOperands()[1]->get());
            std::string c = cg.getValueAsOperand(i.getOperands()[2]->get());
            if (a.empty() || a[0] != 'v') { *os << "  ldr q16, " << a << "\n"; a = "v16"; } else a = getRegisterName(a, vecTy);
            if (b.empty() || b[0] != 'v') { *os << "  ldr q17, " << b << "\n"; b = "v17"; } else b = getRegisterName(b, vecTy);
            if (c.empty() || c[0] != 'v') { *os << "  ldr q18, " << c << "\n"; c = "v18"; } else c = getRegisterName(c, vecTy);
            std::string realDst = (dst.empty() || dst[0] != 'v') ? "v19" : dst;
            *os << "  fmul v19" << arrange << ", " << a << arrange << ", " << b << arrange << "\n";
            *os << "  fneg v19" << arrange << ", v19" << arrange << "\n";
            *os << "  fsub " << realDst << arrange << ", v19" << arrange << ", " << c << arrange << "\n";
            if (dst != realDst) *os << "  str " << realDst << ", " << rawDst << "\n";
            break;
        }
        case ir::Instruction::FNMS: {
            std::string a = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string b = cg.getValueAsOperand(i.getOperands()[1]->get());
            std::string c = cg.getValueAsOperand(i.getOperands()[2]->get());
            if (a.empty() || a[0] != 'v') { *os << "  ldr q16, " << a << "\n"; a = "v16"; } else a = getRegisterName(a, vecTy);
            if (b.empty() || b[0] != 'v') { *os << "  ldr q17, " << b << "\n"; b = "v17"; } else b = getRegisterName(b, vecTy);
            if (c.empty() || c[0] != 'v') { *os << "  ldr q18, " << c << "\n"; c = "v18"; } else c = getRegisterName(c, vecTy);
            std::string realDst = (dst.empty() || dst[0] != 'v') ? "v18" : dst;
            if (realDst != c) *os << "  mov " << realDst << ".16b, " << c << ".16b\n";
            *os << "  fmls " << realDst << arrange << ", " << a << arrange << ", " << b << arrange << "\n";
            if (dst != realDst) *os << "  str " << realDst << ", " << rawDst << "\n";
            break;
        }
        case ir::Instruction::VShuffle: {
            std::string op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string op1 = (i.getOperands().size() > 1 && i.getOperands()[1]->get()) ? cg.getValueAsOperand(i.getOperands()[1]->get()) : op0;
            if (op0.empty() || op0[0] != 'v') { *os << "  ldr q16, " << op0 << "\n"; op0 = "v16"; } else op0 = getRegisterName(op0, vecTy);
            if (op1.empty() || op1[0] != 'v') { *os << "  ldr q17, " << op1 << "\n"; op1 = "v17"; } else op1 = getRegisterName(op1, vecTy);
            std::string realDst = (dst.empty() || dst[0] != 'v') ? "v16" : dst;
            *os << "  tbl " << realDst << ".16b, {" << op0 << ".16b}, " << op1 << ".16b\n";
            if (dst != realDst) *os << "  str " << realDst << ", " << rawDst << "\n";
            break;
        }
        case ir::Instruction::VSExt: {
            std::string op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
            if (op0.empty() || op0[0] != 'v') { *os << "  ldr q16, " << op0 << "\n"; op0 = "v16"; } else op0 = getRegisterName(op0, vecTy);
            auto* srcTy = dynamic_cast<const ir::VectorType*>(i.getOperands()[0]->get()->getType());
            std::string srcArrange = ".4h";
            std::string dstArrange = ".4s";
            if (srcTy && srcTy->getElementType()) {
                unsigned bits = srcTy->getElementType()->getSize() * 8;
                unsigned num = srcTy->getNumElements();
                if (bits == 8 && num == 8) { srcArrange = ".8b"; dstArrange = ".8h"; }
                else if (bits == 16 && num == 4) { srcArrange = ".4h"; dstArrange = ".4s"; }
                else if (bits == 32 && num == 2) { srcArrange = ".2s"; dstArrange = ".2d"; }
            }
            std::string realDst = (dst.empty() || dst[0] != 'v') ? "v16" : dst;
            *os << "  sxtl " << realDst << dstArrange << ", " << op0 << srcArrange << "\n";
            if (dst != realDst) *os << "  str " << realDst << ", " << rawDst << "\n";
            break;
        }
        case ir::Instruction::VZExt: {
            std::string op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
            if (op0.empty() || op0[0] != 'v') { *os << "  ldr q16, " << op0 << "\n"; op0 = "v16"; } else op0 = getRegisterName(op0, vecTy);
            auto* srcTy = dynamic_cast<const ir::VectorType*>(i.getOperands()[0]->get()->getType());
            std::string srcArrange = ".4h";
            std::string dstArrange = ".4s";
            if (srcTy && srcTy->getElementType()) {
                unsigned bits = srcTy->getElementType()->getSize() * 8;
                unsigned num = srcTy->getNumElements();
                if (bits == 8 && num == 8) { srcArrange = ".8b"; dstArrange = ".8h"; }
                else if (bits == 16 && num == 4) { srcArrange = ".4h"; dstArrange = ".4s"; }
                else if (bits == 32 && num == 2) { srcArrange = ".2s"; dstArrange = ".2d"; }
            }
            std::string realDst = (dst.empty() || dst[0] != 'v') ? "v16" : dst;
            *os << "  uxtl " << realDst << dstArrange << ", " << op0 << srcArrange << "\n";
            if (dst != realDst) *os << "  str " << realDst << ", " << rawDst << "\n";
            break;
        }
        case ir::Instruction::VTrunc: {
            std::string op0 = cg.getValueAsOperand(i.getOperands()[0]->get());
            if (op0.empty() || op0[0] != 'v') { *os << "  ldr q16, " << op0 << "\n"; op0 = "v16"; } else op0 = getRegisterName(op0, vecTy);
            auto* srcTy = dynamic_cast<const ir::VectorType*>(i.getOperands()[0]->get()->getType());
            std::string srcArrange = ".4s";
            std::string dstArrange = ".4h";
            if (srcTy && srcTy->getElementType()) {
                unsigned bits = srcTy->getElementType()->getSize() * 8;
                unsigned num = srcTy->getNumElements();
                if (bits == 16 && num == 8) { srcArrange = ".8h"; dstArrange = ".8b"; }
                else if (bits == 32 && num == 4) { srcArrange = ".4s"; dstArrange = ".4h"; }
                else if (bits == 64 && num == 2) { srcArrange = ".2d"; dstArrange = ".2s"; }
            }
            std::string realDst = (dst.empty() || dst[0] != 'v') ? "v16" : dst;
            *os << "  xtn " << realDst << dstArrange << ", " << op0 << srcArrange << "\n";
            if (dst != realDst) *os << "  str " << realDst << ", " << rawDst << "\n";
            break;
        }
        case ir::Instruction::VGather: {
            std::string basePtr = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string indexVecStr = cg.getValueAsOperand(i.getOperands()[1]->get());
            std::string indexVec = indexVecStr;
            if (indexVec.empty() || indexVec[0] != 'v') {
                *os << "  ldr q17, " << indexVecStr << "\n";
                indexVec = "v17";
            } else indexVec = getRegisterName(indexVec, vecTy);

            std::string realDst = (dst.empty() || dst[0] != 'v') ? "v16" : dst;
            unsigned numElems = vecTy ? vecTy->getNumElements() : 4;
            unsigned elemBits = (vecTy && vecTy->getElementType()) ? vecTy->getElementType()->getSize() * 8 : 32;
            std::string spec = (elemBits == 64) ? ".d" : ".s";
            std::string ldrInst = "ldr";
            std::string wReg = (elemBits == 64) ? "x10" : "w10";
            *os << "  ldr x9, " << basePtr << "\n";
            for (unsigned k = 0; k < numElems; ++k) {
                *os << "  mov " << wReg << ", " << indexVec << spec << "[" << k << "]\n";
                if (elemBits == 64) *os << "  lsl x10, x10, #3\n";
                else *os << "  lsl x10, x10, #2\n";
                *os << "  add x10, x9, x10\n";
                *os << "  " << ldrInst << " " << wReg << ", [x10]\n";
                *os << "  mov " << realDst << spec << "[" << k << "], " << wReg << "\n";
            }
            if (dst != realDst) *os << "  str " << realDst << ", " << rawDst << "\n";
            break;
        }
        case ir::Instruction::VScatter: {
            std::string valVecStr = cg.getValueAsOperand(i.getOperands()[0]->get());
            std::string valVec = valVecStr;
            if (valVec.empty() || valVec[0] != 'v') {
                *os << "  ldr q16, " << valVecStr << "\n";
                valVec = "v16";
            } else valVec = getRegisterName(valVec, vecTy);

            std::string basePtr = cg.getValueAsOperand(i.getOperands()[1]->get());
            std::string indexVecStr = (i.getOperands().size() > 2 && i.getOperands()[2]->get()) ? cg.getValueAsOperand(i.getOperands()[2]->get()) : "";
            std::string indexVec = indexVecStr;
            if (indexVec.empty() || indexVec[0] != 'v') {
                *os << "  ldr q17, " << indexVecStr << "\n";
                indexVec = "v17";
            } else indexVec = getRegisterName(indexVec, vecTy);

            unsigned numElems = vecTy ? vecTy->getNumElements() : 4;
            unsigned elemBits = (vecTy && vecTy->getElementType()) ? vecTy->getElementType()->getSize() * 8 : 32;
            std::string spec = (elemBits == 64) ? ".d" : ".s";
            std::string strInst = "str";
            std::string wReg1 = (elemBits == 64) ? "x10" : "w10";
            std::string wReg2 = (elemBits == 64) ? "x11" : "w11";
            *os << "  ldr x9, " << basePtr << "\n";
            for (unsigned k = 0; k < numElems; ++k) {
                *os << "  mov " << wReg1 << ", " << indexVec << spec << "[" << k << "]\n";
                if (elemBits == 64) *os << "  lsl x10, x10, #3\n";
                else *os << "  lsl x10, x10, #2\n";
                *os << "  add x10, x9, x10\n";
                *os << "  mov " << wReg2 << ", " << valVec << spec << "[" << k << "]\n";
                *os << "  " << strInst << " " << wReg2 << ", [x10]\n";
            }
            break;
        }
        default:
            *os << "  # Unsupported AArch64 vector opcode: " << i.getOpcode() << "\n";
            break;
    }
}

}
