#include "codegen/target/MacOS_AArch64.h"
#include "codegen/CodeGen.h"
#include "ir/Function.h"
#include "ir/Use.h"
#include <ostream>

namespace codegen {
namespace target {

MacOS_AArch64::MacOS_AArch64() : AArch64() {}

void MacOS_AArch64::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        // macOS standard: prefix symbols with underscore
        *os << "_" << func.getName() << ":\n";
    }

    // Use base AArch64 class for stack frame setup
    AArch64::emitFunctionPrologue(cg, func);
}

void MacOS_AArch64::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "\n.globl _main\n";
        *os << "_main:\n";
        *os << "  bl _main_user\n";
        *os << "  ret\n";
    }
}

uint64_t MacOS_AArch64::getSyscallNumber(ir::SyscallId id) const {
    const uint64_t unix_class = 0x02000000;
    switch (id) {
        case ir::SyscallId::Exit: return unix_class | 1;
        case ir::SyscallId::Read: return unix_class | 3;
        case ir::SyscallId::Write: return unix_class | 4;
        case ir::SyscallId::OpenAt: return unix_class | 463;
        case ir::SyscallId::Close: return unix_class | 6;
        case ir::SyscallId::LSeek: return unix_class | 199;
        case ir::SyscallId::MMap: return unix_class | 197;
        case ir::SyscallId::MUnmap: return unix_class | 73;
        case ir::SyscallId::MProtect: return unix_class | 74;
        case ir::SyscallId::MkDirAt: return unix_class | 461;
        case ir::SyscallId::UnlinkAt: return unix_class | 462;
        case ir::SyscallId::RenameAt: return unix_class | 464;
        case ir::SyscallId::GetDents64: return unix_class | 344;
        case ir::SyscallId::ClockGetTime: return unix_class | 427;
        case ir::SyscallId::NanoSleep: return unix_class | 240;
        case ir::SyscallId::Uname: return unix_class | 116;
        case ir::SyscallId::GetPid: return unix_class | 20;
        case ir::SyscallId::GetTid: return unix_class | 286;
        case ir::SyscallId::Fork: return unix_class | 2;
        case ir::SyscallId::Execve: return unix_class | 59;
        case ir::SyscallId::Wait4: return unix_class | 7;
        case ir::SyscallId::Kill: return unix_class | 37;
        default: return 0;
    }
}

void MacOS_AArch64::emitSyscall(CodeGen& cg, ir::Instruction& instr) {
    auto* syscallInstr = dynamic_cast<ir::SyscallInstruction*>(&instr);
    ir::SyscallId sid = syscallInstr ? syscallInstr->getSyscallId() : ir::SyscallId::None;

    if (auto* os = cg.getTextStream()) {
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
        *os << "  svc #0x80\n";
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  str x0, " << cg.getValueAsOperand(&instr) << "\n";
        }
    }
}

} // namespace target
} // namespace codegen
