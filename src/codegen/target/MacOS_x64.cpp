#include "codegen/target/MacOS_x64.h"
#include "codegen/target/SystemV_x64.h"
#include "codegen/CodeGen.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include <ostream>

namespace codegen {
namespace target {

void MacOS_x64::initRegisters() {
    SystemV_x64::initRegisters();
}

MacOS_x64::MacOS_x64() : SystemV_x64() {
    initRegisters();
}

void MacOS_x64::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << "_" << func.getName() << ":\n";
    }
    SystemV_x64::emitFunctionPrologue(cg, func);
}

void MacOS_x64::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "\n.globl _main\n_main:\n  call _main_user\n  ret\n";
    }
}

uint64_t MacOS_x64::getSyscallNumber(ir::SyscallId id) const {
    const uint64_t unix_class = 0x02000000;
    switch (id) {
        case ir::SyscallId::Exit: return unix_class | 1;
        case ir::SyscallId::Read: return unix_class | 3;
        case ir::SyscallId::Write: return unix_class | 4;
        default: return 0;
    }
}

void MacOS_x64::emitSyscall(CodeGen& cg, ir::Instruction& instr) {
    if (auto* os = cg.getTextStream()) {
        *os << "  movq $" << getSyscallNumber(static_cast<ir::SyscallInstruction&>(instr).getSyscallId()) << ", %rax\n  syscall\n";
    }
}

}
}
