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
    static const std::vector<std::string> regs = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"}; return regs;
}
const std::vector<std::string>& AArch64Architecture::getFloatArgumentRegisters() const {
    static const std::vector<std::string> regs = {"v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7"}; return regs;
}
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
        *os << "  stp x29, x30, [sp, #-16]!\n  mov x29, sp\n";
        if (total_frame_size > 0) { if (total_frame_size <= 4095) *os << "  sub sp, sp, #" << total_frame_size << "\n"; else { *os << "  mov x9, #" << total_frame_size << "\n  sub sp, sp, x9\n"; } }
        int cs_off = isLeaf ? 0 : 16; auto it = usedCS.begin();
        while (it != usedCS.end()) { std::string r1 = *it++; if (it != usedCS.end()) { std::string r2 = *it++; *os << "  stp " << r1 << ", " << r2 << ", [sp, #" << cs_off << "]\n"; cs_off += 16; } else { *os << "  str " << r1 << ", [sp, #" << cs_off << "]\n"; cs_off += 8; } }
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
    if (!needsFrame) { if (auto* os = cg.getTextStream()) *os << "  ret\n"; else cg.getAssembler().emitDWord(0xD65F03C0); return; }
    size_t total_frame_size = align_to_16(local_area_size + (usedCS.size() * 8) + (isLeaf ? 0 : 16));
    if (auto* os = cg.getTextStream()) {
        int cs_off = isLeaf ? 0 : 16; auto it = usedCS.begin();
        while (it != usedCS.end()) { std::string r1 = *it++; if (it != usedCS.end()) { std::string r2 = *it++; *os << "  ldp " << r1 << ", " << r2 << ", [sp, #" << cs_off << "]\n"; cs_off += 16; } else { *os << "  ldr " << r1 << ", [sp, #" << cs_off << "]\n"; cs_off += 8; } }
        if (!isLeaf) { *os << "  mov sp, x29\n  ldp x29, x30, [sp], #16\n"; } else if (total_frame_size > 0) *os << "  add sp, sp, #" << total_frame_size << "\n";
        *os << "  ret\n";
    }
}

void AArch64Architecture::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << ".section .rodata\n.Lproc_environ:\n  .string \"/proc/self/environ\"\n.Lproc_cmdline:\n  .string \"/proc/self/cmdline\"\n.text\n.globl _start\n_start:\n  bl main\n  mov x8, #93\n  svc #0\n";
    }
}

void AArch64Architecture::emitRet(CodeGen& cg, ir::Instruction& i) {
    if (!i.getOperands().empty()) { ir::Value* rv = i.getOperands()[0]->get(); if (auto* os = cg.getTextStream()) *os << "  ldr " << getRegisterName("x0", rv->getType()) << ", " << cg.getValueAsOperand(rv) << "\n"; else emitLoadValue(cg, cg.getAssembler(), rv, 0); }
    if (auto* os = cg.getTextStream()) *os << "  b " << i.getParent()->getParent()->getName() << "_epilogue\n";
    else emitFunctionEpilogue(cg, *i.getParent()->getParent());
}

void AArch64Architecture::emitAdd(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) { std::string r1 = getRegisterName("x9", l->getType()), r2 = getRegisterName("x10", r->getType()); *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n  add " << r1 << ", " << r1 << ", " << r2 << "\n  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n"; }
}
void AArch64Architecture::emitSub(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) { std::string r1 = getRegisterName("x9", l->getType()), r2 = getRegisterName("x10", r->getType()); *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n  sub " << r1 << ", " << r1 << ", " << r2 << "\n  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n"; }
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
    if (auto* os = cg.getTextStream()) { std::string r1 = getRegisterName("x9", l->getType()), r2 = getRegisterName("x10", r->getType()); *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n  lsl " << r1 << ", " << r1 << ", " << r2 << "\n  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n"; }
}
void AArch64Architecture::emitShr(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) { std::string r1 = getRegisterName("x9", l->getType()), r2 = getRegisterName("x10", r->getType()); *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n  lsr " << r1 << ", " << r1 << ", " << r2 << "\n  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n"; }
}
void AArch64Architecture::emitSar(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    if (auto* os = cg.getTextStream()) { std::string r1 = getRegisterName("x9", l->getType()), r2 = getRegisterName("x10", r->getType()); *os << "  ldr " << r1 << ", " << cg.getValueAsOperand(l) << "\n  ldr " << r2 << ", " << cg.getValueAsOperand(r) << "\n  asr " << r1 << ", " << r1 << ", " << r2 << "\n  str " << r1 << ", " << cg.getValueAsOperand(d) << "\n"; }
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
    if (auto* os = cg.getTextStream()) { *os << "  ldr x9, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  str x9, " << cg.getValueAsOperand(&i) << "\n"; }
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
void AArch64Architecture::emitFAdd(CodeGen& cg, ir::Instruction& i) {}
void AArch64Architecture::emitFSub(CodeGen& cg, ir::Instruction& i) {}
void AArch64Architecture::emitFMul(CodeGen& cg, ir::Instruction& i) {}
void AArch64Architecture::emitFDiv(CodeGen& cg, ir::Instruction& i) {}
void AArch64Architecture::emitCmp(CodeGen& cg, ir::Instruction& i) {
    ir::Value *d = &i, *l = i.getOperands()[0]->get(), *r = i.getOperands()[1]->get();
    std::string cond = "eq"; switch(i.getOpcode()){case ir::Instruction::Ceq:cond="eq";break;case ir::Instruction::Cne:cond="ne";break;case ir::Instruction::Cslt:cond="lt";break;case ir::Instruction::Csle:cond="le";break;case ir::Instruction::Csgt:cond="gt";break;case ir::Instruction::Csge:cond="ge";break;default:cond="eq";break;}
    if (auto* os = cg.getTextStream()) { *os << "  ldr " << getRegisterName("x10", l->getType()) << ", " << cg.getValueAsOperand(l) << "\n  ldr " << getRegisterName("x11", r->getType()) << ", " << cg.getValueAsOperand(r) << "\n  cmp " << getRegisterName("x10", l->getType()) << ", " << getRegisterName("x11", r->getType()) << "\n  cset w9, " << cond << "\n  str w9, " << cg.getValueAsOperand(d) << "\n"; }
}
void AArch64Architecture::emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* f, const ir::Type* t) {}
void AArch64Architecture::emitVAStart(CodeGen& cg, ir::Instruction& i) {}
void AArch64Architecture::emitVAArg(CodeGen& cg, ir::Instruction& i) {}
void AArch64Architecture::emitLoad(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  ldr x9, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  ldr x10, [x9]\n  str x10, " << cg.getValueAsOperand(&i) << "\n"; }
}
void AArch64Architecture::emitStore(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) { *os << "  ldr x9, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  ldr x10, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  str x10, [x9]\n"; }
}
void AArch64Architecture::emitAlloc(CodeGen& cg, ir::Instruction& i) {
    uint64_t size = dynamic_cast<ir::ConstantInt*>(i.getOperands()[0]->get())->getValue(); size = (size + 7) & ~7;
    if (auto* os = cg.getTextStream()) { *os << "  adrp x9, heap_ptr\n  ldr x9, [x9, :lo12:heap_ptr]\n  str x9, [x29, #" << cg.getStackOffset(&i) << "]\n  add x9, x9, #" << size << "\n  adrp x10, heap_ptr\n  str x9, [x10, :lo12:heap_ptr]\n"; }
}
void AArch64Architecture::emitBr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        if (i.getOperands().size() == 1) *os << "  b " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        else { *os << "  ldr w9, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n  cmp w9, #0\n  b.ne " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n  b " << cg.getValueAsOperand(i.getOperands()[2]->get()) << "\n"; }
    }
}
void AArch64Architecture::emitJmp(CodeGen& cg, ir::Instruction& i) { if (auto* os = cg.getTextStream()) *os << "  b " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n"; }

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
bool AArch64Architecture::isCallerSaved(const std::string& r) const { static const std::set<std::string> cs = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"}; return cs.count(r); }
bool AArch64Architecture::isCalleeSaved(const std::string& r) const { static const std::set<std::string> cs = {"x19", "x20", "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15"}; return cs.count(r); }
std::string AArch64Architecture::getRegisterName(const std::string& b, const ir::Type* t) const { if (auto* it = dynamic_cast<const ir::IntegerType*>(t)) { if (it->getBitwidth() <= 32 && b[0] == 'x') return "w" + b.substr(1); } return b; }

void AArch64Architecture::emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) {}
void AArch64Architecture::emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) {}

std::string AArch64Architecture::getConditionCode(const std::string& op, bool isFloat, bool isUnsigned) const { return "eq"; }
std::string AArch64Architecture::getWRegister(const std::string& xReg) const { return xReg; }

}
}
