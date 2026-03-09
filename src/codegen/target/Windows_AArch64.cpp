#include "codegen/target/Windows_AArch64.h"
#include "codegen/CodeGen.h"
#include "ir/Use.h"
#include "ir/Instruction.h"

namespace codegen {
namespace target {

void Windows_AArch64::emitStartFunction(CodeGen& cg) {
    // Windows entry point is typically 'mainCRTStartup' or 'WinMainCRTStartup'
    // which calls 'main' or 'WinMain'.
    // For now, we'll just emit a simple entry point that calls main if needed,
    // or leave it to the linker/CRT.
    if (auto* os = cg.getTextStream()) {
        *os << "\n; Windows ARM64 Entry Point\n";
        *os << ".globl mainCRTStartup\n";
        *os << "mainCRTStartup:\n";
        *os << "  bl main\n";
        *os << "  ret\n";
    }
}

bool Windows_AArch64::isReserved(const std::string& reg) const {
    // x18 is reserved for the TEB on Windows ARM64
    if (reg == "x18") return true;
    return false;
}

void Windows_AArch64::emitRem(CodeGen& cg, ir::Instruction& instr) {
    AArch64::emitRem(cg, instr);
}

void Windows_AArch64::emitSyscall(CodeGen& cg, ir::Instruction& instr) {
    // Direct syscalls on Windows ARM64 are very rare and discouraged.
    // However, for consistency:
    // x16: syscall number, x0-x7: arguments
    if (auto* os = cg.getTextStream()) {
        *os << "  # Windows ARM64 Syscall (Highly unstable)\n";
        *os << "  mov x16, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        for (size_t i = 1; i < instr.getOperands().size(); ++i) {
            *os << "  mov x" << (i-1) << ", " << cg.getValueAsOperand(instr.getOperands()[i]->get()) << "\n";
        }
        *os << "  svc #0\n";
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  str x0, " << cg.getValueAsOperand(&instr) << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        // mov x16, num
        emitLoadValue(cg, assembler, instr.getOperands()[0]->get(), 16);
        for (size_t i = 1; i < instr.getOperands().size(); ++i) {
            emitLoadValue(cg, assembler, instr.getOperands()[i]->get(), i-1);
        }
        assembler.emitDWord(0xD4000001); // svc #0
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            int32_t destOffset = cg.getStackOffset(&instr);
            assembler.emitDWord(0xF8000BA0 | ((destOffset & 0x1FF) << 12));
        }
    }
}

} // namespace target
} // namespace codegen
