#include "ir/Instruction.h"
#include "ir/Use.h"
#include "target/architecture/riscv64/RiscV64Architecture.h"
#include "codegen/CodeGen.h"
#include "target/core/OperatingSystemInfo.h"
#include <ostream>

namespace codegen {
namespace target {

RiscV64Architecture::RiscV64Architecture() {}

TypeInfo RiscV64Architecture::getTypeInfo(const ir::Type* type) const {
    return {type->getSize() * 8, type->getAlignment() * 8, type->isFloatingPoint() ? RegisterClass::Float : RegisterClass::Integer, type->isFloatingPoint(), true};
}

const std::vector<std::string>& RiscV64Architecture::getRegisters(RegisterClass regClass) const {
    static const std::vector<std::string> intRegs = {"x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x5", "x6", "x7", "x28", "x29", "x30", "x31"};
    static const std::vector<std::string> floatRegs = {"f10", "f11", "f12", "f13", "f14", "f15", "f16", "f17", "f5", "f6", "f7", "f28", "f29", "f30", "f31"};
    return (regClass == RegisterClass::Float) ? floatRegs : intRegs;
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
    if (auto* os = cg.getTextStream()) { *os << "  addi sp, sp, -" << stack_size << "\n  sd ra, " << stack_size - 8 << "(sp)\n  sd s0, " << stack_size - 16 << "(sp)\n  addi s0, sp, " << stack_size << "\n"; }
}

void RiscV64Architecture::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) { *os << func.getName() << "_epilogue:\n  ld ra, -8(s0)\n  ld s0, -16(s0)\n  addi sp, s0, 0\n  jr ra\n"; }
}

void RiscV64Architecture::emitStartFunction(CodeGen& cg) {}

void RiscV64Architecture::emitRet(CodeGen& cg, ir::Instruction& i) {
    if (!i.getOperands().empty()) { if (auto* os = cg.getTextStream()) *os << "  ld a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n"; }
    if (auto* os = cg.getTextStream()) *os << "  j " << i.getParent()->getParent()->getName() << "_epilogue\n";
}

void RiscV64Architecture::emitAdd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  ld a1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  add a0, a0, a1\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitSub(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  ld a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  ld a1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
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
    if (auto* os = cg.getTextStream()) {
        *os << "  ld a0, " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  ld a1, " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  div a0, a0, a1\n";
        *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
    }
}
void RiscV64Architecture::emitRem(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitAnd(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitOr(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitXor(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitShl(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitShr(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitSar(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitNeg(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitNot(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitCopy(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitCall(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  call " << i.getOperands()[0]->get()->getName() << "\n";
        if (i.getType() && !i.getType()->isVoidTy()) {
            *os << "  sd a0, " << cg.getValueAsOperand(&i) << "\n";
        }
    }
}
void RiscV64Architecture::emitFAdd(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitFSub(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitFMul(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitFDiv(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitCmp(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* f, const ir::Type* t) {}
void RiscV64Architecture::emitVAStart(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitVAArg(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitLoad(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitStore(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitAlloc(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitBr(CodeGen& cg, ir::Instruction& i) {}
void RiscV64Architecture::emitJmp(CodeGen& cg, ir::Instruction& i) {}

void RiscV64Architecture::emitSyscall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {
    if (auto* os = cg.getTextStream()) {
        *os << "  li a7, " << osInfo.getSyscallNumber(dynamic_cast<ir::SyscallInstruction*>(&i)->getSyscallId()) << "\n";
        for (size_t j = 0; j < std::min(i.getOperands().size(), (size_t)6); ++j) *os << "  ld a" << j << ", " << cg.getValueAsOperand(i.getOperands()[j]->get()) << "\n";
        *os << "  ecall\n";
    }
}

void RiscV64Architecture::emitExternCall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {}

std::string RiscV64Architecture::formatStackOperand(int o) const { return std::to_string(o) + "(s0)"; }
std::string RiscV64Architecture::formatGlobalOperand(const std::string& n) const { return n; }
bool RiscV64Architecture::isCallerSaved(const std::string& r) const { return (r[0] == 't' || r[0] == 'a' || r[0] == 'f'); }
bool RiscV64Architecture::isCalleeSaved(const std::string& r) const { return (r[0] == 's' || r == "gp" || r == "tp" || r == "fp"); }

void RiscV64Architecture::emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) {}
void RiscV64Architecture::emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) {}

}
}
