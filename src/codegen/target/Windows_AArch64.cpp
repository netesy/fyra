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

uint64_t Windows_AArch64::getSyscallNumber(ir::SyscallId id) const {
    // Windows ARM64 syscall numbers (similarly unstable)
    switch (id) {
        case ir::SyscallId::Exit: return 0x002C;
        case ir::SyscallId::Read: return 0x0006;
        case ir::SyscallId::Write: return 0x0008;
        case ir::SyscallId::Close: return 0x000F;
        default: return 0;
    }
}

void Windows_AArch64::emitSyscall(CodeGen& cg, ir::Instruction& instr) {
    auto* syscallInstr = dynamic_cast<ir::SyscallInstruction*>(&instr);
    ir::SyscallId sid = syscallInstr ? syscallInstr->getSyscallId() : ir::SyscallId::None;

    // Direct syscalls on Windows ARM64 are very rare and discouraged.
    // However, for consistency:
    // x16: syscall number, x0-x7: arguments
    if (auto* os = cg.getTextStream()) {
        *os << "  # Windows ARM64 Syscall (Highly unstable)\n";
        if (sid != ir::SyscallId::None) {
            *os << "  mov x16, #" << getSyscallNumber(sid) << "\n";
        } else {
            *os << "  mov x16, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        }

        size_t startArg = (sid != ir::SyscallId::None) ? 0 : 1;
        for (size_t i = startArg; i < instr.getOperands().size(); ++i) {
            size_t argIdx = (sid != ir::SyscallId::None) ? i : i - 1;
            if (argIdx < 8) {
                *os << "  mov x" << argIdx << ", " << cg.getValueAsOperand(instr.getOperands()[i]->get()) << "\n";
            }
        }
        *os << "  svc #0\n";
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  str x0, " << cg.getValueAsOperand(&instr) << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        // mov x16, num
        if (sid != ir::SyscallId::None) {
            uint64_t num = getSyscallNumber(sid);
            assembler.emitDWord(0xD2800010 | ((num & 0xFFFF) << 5));
        } else {
            emitLoadValue(cg, assembler, instr.getOperands()[0]->get(), 16);
        }

        size_t startArg = (sid != ir::SyscallId::None) ? 0 : 1;
        for (size_t i = startArg; i < instr.getOperands().size(); ++i) {
            size_t argIdx = (sid != ir::SyscallId::None) ? i : i - 1;
            if (argIdx < 8) {
                emitLoadValue(cg, assembler, instr.getOperands()[i]->get(), argIdx);
            }
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
