#include "target/architecture/ebpf/EBPFArchitecture.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include "ir/Constant.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "codegen/CodeGen.h"
#include "codegen/asm/Assembler.h"
#include "target/core/OperatingSystemInfo.h"
#include <ostream>
#include <vector>

namespace target {

EBPFArchitecture::EBPFArchitecture() {}

TypeInfo EBPFArchitecture::getTypeInfo(const ir::Type* type) const {
    return {type->getSize() * 8, type->getAlignment() * 8, RegisterClass::Integer, false, true};
}

const std::vector<std::string>& EBPFArchitecture::getRegisters(RegisterClass regClass) const {
    static const std::vector<std::string> intRegs = {"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9"};
    static const std::vector<std::string> emptyRegs = {};
    return regClass == RegisterClass::Integer ? intRegs : emptyRegs;
}

const std::string& EBPFArchitecture::getReturnRegister(const ir::Type* type) const {
    static const std::string r = "r0"; return r;
}

const std::vector<std::string>& EBPFArchitecture::getIntegerArgumentRegisters() const {
    static const std::vector<std::string> r = {"r1", "r2", "r3", "r4", "r5"}; return r;
}

const std::vector<std::string>& EBPFArchitecture::getFloatArgumentRegisters() const {
    static const std::vector<std::string> r = {}; return r;
}

const std::string& EBPFArchitecture::getIntegerReturnRegister() const {
    static const std::string r = "r0"; return r;
}

const std::string& EBPFArchitecture::getFloatReturnRegister() const {
    static const std::string r = "r0"; return r;
}

bool EBPFArchitecture::validateLegality(ir::Function& func, std::string& errorMsg) const {
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getType() && instr->getType()->isFloatingPoint()) {
                errorMsg = "eBPF target does not support floating-point type: " + instr->getName();
                return false;
            }
            if (instr->getOpcode() == ir::Instruction::Alloc4 ||
                instr->getOpcode() == ir::Instruction::Alloc16) {
                errorMsg = "eBPF target does not support dynamic stack allocation instructions";
                return false;
            }
        }
    }
    return true;
}

void EBPFArchitecture::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    std::string err;
    if (!validateLegality(func, err)) {
        if (auto* os = cg.getTextStream()) {
            *os << "  # ERROR: " << err << "\n";
        }
    }

    currentStackOffset = 0;
    for (auto& param : func.getParameters()) {
        currentStackOffset -= 8;
        cg.getStackOffsets()[param.get()] = currentStackOffset;
    }
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getType()->getTypeID() != ir::Type::VoidTyID) {
                currentStackOffset -= 8;
                cg.getStackOffsets()[instr.get()] = currentStackOffset;
            }
        }
    }

    if (auto* os = cg.getTextStream()) {
        *os << "  # eBPF function entry point: " << func.getName() << "\n";
        *os << "  # Stack frame size: " << -currentStackOffset << " bytes (Limit: 512 bytes)\n";
    }
}

void EBPFArchitecture::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << "  exit\n";
    }
}

void EBPFArchitecture::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << ".text\n.globl main\nmain:\n";
    }
}

void EBPFArchitecture::emitRet(CodeGen& cg, ir::Instruction& i) {
    if (!i.getOperands().empty() && i.getOperands()[0] && i.getOperands()[0]->get() != nullptr) {
        ir::Value* rv = i.getOperands()[0]->get();
        if (auto* os = cg.getTextStream()) {
            *os << "  r0 = " << cg.getValueAsOperand(rv) << "\n";
        }
    }
    if (auto* os = cg.getTextStream()) {
        *os << "  exit\n";
    }
}

void EBPFArchitecture::emitAdd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  r2 = " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  r1 += r2\n";
        *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r1\n";
    }
}

void EBPFArchitecture::emitSMin(CodeGen& cg, ir::Instruction& i) {}
void EBPFArchitecture::emitSMax(CodeGen& cg, ir::Instruction& i) {}

void EBPFArchitecture::emitSub(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  r2 = " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  r1 -= r2\n";
        *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r1\n";
    }
}

void EBPFArchitecture::emitMul(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  r2 = " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  r1 *= r2\n";
        *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r1\n";
    }
}

void EBPFArchitecture::emitDiv(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  r2 = " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  r1 /= r2\n";
        *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r1\n";
    }
}

void EBPFArchitecture::emitRem(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  r2 = " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  r1 %= r2\n";
        *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r1\n";
    }
}

void EBPFArchitecture::emitAnd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  r2 = " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  r1 &= r2\n";
        *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r1\n";
    }
}

void EBPFArchitecture::emitOr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  r2 = " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  r1 |= r2\n";
        *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r1\n";
    }
}

void EBPFArchitecture::emitXor(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  r2 = " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  r1 ^= r2\n";
        *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r1\n";
    }
}

void EBPFArchitecture::emitShl(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  r2 = " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  r1 <<= r2\n";
        *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r1\n";
    }
}

void EBPFArchitecture::emitShr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  r2 = " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  r1 >>= r2\n";
        *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r1\n";
    }
}

void EBPFArchitecture::emitSar(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  r2 = " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  r1 s>>= r2\n";
        *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r1\n";
    }
}

void EBPFArchitecture::emitNeg(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  r1 = -r1\n";
        *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r1\n";
    }
}

void EBPFArchitecture::emitNot(CodeGen& cg, ir::Instruction& i) {}

void EBPFArchitecture::emitCopy(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r1\n";
    }
}

void EBPFArchitecture::emitCall(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        for (size_t j = 1; j < std::min(i.getOperands().size(), (size_t)6); ++j) {
            *os << "  r" << j << " = " << cg.getValueAsOperand(i.getOperands()[j]->get()) << "\n";
        }
        if (!i.getOperands().empty() && i.getOperands()[0]->get()) {
            *os << "  call " << i.getOperands()[0]->get()->getName() << "\n";
        }
        if (i.getType() && !i.getType()->isVoidTy()) {
            *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r0\n";
        }
    }
}

bool EBPFArchitecture::emitTailCall(CodeGen& cg, ir::Instruction& callInst, ir::Instruction& retInst) { return false; }
void EBPFArchitecture::emitFAdd(CodeGen& cg, ir::Instruction& i) {}
void EBPFArchitecture::emitFSub(CodeGen& cg, ir::Instruction& i) {}
void EBPFArchitecture::emitFMul(CodeGen& cg, ir::Instruction& i) {}
void EBPFArchitecture::emitFDiv(CodeGen& cg, ir::Instruction& i) {}

void EBPFArchitecture::emitCmp(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  r2 = " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  # eBPF cmp r1, r2\n";
        *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r1\n";
    }
}

void EBPFArchitecture::emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* from, const ir::Type* to) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r1\n";
    }
}

void EBPFArchitecture::emitVAStart(CodeGen& cg, ir::Instruction& i) {}
void EBPFArchitecture::emitVAArg(CodeGen& cg, ir::Instruction& i) {}

void EBPFArchitecture::emitLoad(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  r1 = *(u64*)(r1 + 0)\n";
        *os << "  *(u64*)(r10" << formatStackOperand(cg.getStackOffset(&i)) << ") = r1\n";
    }
}

void EBPFArchitecture::emitStore(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        *os << "  r2 = " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
        *os << "  *(u64*)(r2 + 0) = r1\n";
    }
}

void EBPFArchitecture::emitAlloc(CodeGen& cg, ir::Instruction& i) {}

void EBPFArchitecture::emitBr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        if (i.getOperands().size() == 1) {
            *os << "  goto " << i.getOperands()[0]->get()->getName() << "\n";
        } else if (i.getOperands().size() >= 3) {
            *os << "  r1 = " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
            *os << "  if r1 != 0 goto " << i.getOperands()[1]->get()->getName() << "\n";
            *os << "  goto " << i.getOperands()[2]->get()->getName() << "\n";
        }
    }
}

void EBPFArchitecture::emitJmp(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "  goto " << i.getOperands()[0]->get()->getName() << "\n";
    }
}

void EBPFArchitecture::emitSyscall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {}
void EBPFArchitecture::emitExternCall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {}
void EBPFArchitecture::emitNativeSyscall(CodeGen& cg, uint64_t syscallNum, const std::vector<ir::Value*>& args) {}
void EBPFArchitecture::emitNativeLibraryCall(CodeGen& cg, const std::string& name, const std::vector<ir::Value*>& args) {}

std::string EBPFArchitecture::formatStackOperand(int offset) const {
    return std::to_string(offset);
}

std::string EBPFArchitecture::formatGlobalOperand(const std::string& name) const { return name; }
bool EBPFArchitecture::isCallerSaved(const std::string& reg) const { return reg[0] == 'r' && (reg[1] >= '1' && reg[1] <= '5'); }
bool EBPFArchitecture::isCalleeSaved(const std::string& reg) const { return reg[0] == 'r' && (reg[1] >= '6' && reg[1] <= '9'); }

void EBPFArchitecture::emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) {}
void EBPFArchitecture::emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) {}

}
