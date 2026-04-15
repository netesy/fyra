#include "codegen/target/Windows_AArch64.h"
#include "codegen/CodeGen.h"
#include "ir/Use.h"
#include "ir/Instruction.h"
#include <algorithm>
#include <ostream>

namespace codegen {
namespace target {


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

void Windows_AArch64::emitStartFunction(CodeGen& cg) {
    // Windows entry point is typically 'mainCRTStartup' or 'WinMainCRTStartup'
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

bool Windows_AArch64::isReserved(const std::string& reg) const {
    // x18 is reserved for the TEB on Windows ARM64
    if (reg == "x18") return true;
    return false;
}

void Windows_AArch64::emitExternCall(CodeGen& cg, ir::Instruction& instr) {
    auto* ei = dynamic_cast<ir::ExternCallInstruction*>(&instr);
    if (!ei) return;
    const std::string& cap = ei->getCapability();

    if (cap.compare(0, 3, "io.") == 0) emitIOCall(cg, instr, cap);
    else if (cap.compare(0, 3, "fs.") == 0) emitFSCall(cg, instr, cap);
    else if (cap.compare(0, 7, "memory.") == 0) emitMemoryCall(cg, instr, cap);
    else if (cap.compare(0, 8, "process.") == 0) emitProcessCall(cg, instr, cap);
    else if (cap.compare(0, 7, "thread.") == 0) emitThreadCall(cg, instr, cap);
    else if (cap.compare(0, 5, "sync.") == 0) emitSyncCall(cg, instr, cap);
    else if (cap.compare(0, 5, "time.") == 0) emitTimeCall(cg, instr, cap);
    else if (cap.compare(0, 6, "event.") == 0) emitEventCall(cg, instr, cap);
    else if (cap.compare(0, 4, "net.") == 0) emitNetCall(cg, instr, cap);
    else if (cap.compare(0, 4, "ipc.") == 0) emitIPCCall(cg, instr, cap);
    else if (cap.compare(0, 4, "env.") == 0) emitEnvCall(cg, instr, cap);
    else if (cap.compare(0, 7, "system.") == 0) emitSystemCall(cg, instr, cap);
    else if (cap.compare(0, 7, "signal.") == 0) emitSignalCall(cg, instr, cap);
    else if (cap.compare(0, 7, "random.") == 0) emitRandomCall(cg, instr, cap);
    else if (cap.compare(0, 6, "error.") == 0) emitErrorCall(cg, instr, cap);
    else if (cap.compare(0, 6, "debug.") == 0) emitDebugCall(cg, instr, cap);
    else if (cap.compare(0, 7, "module.") == 0) emitModuleCall(cg, instr, cap);
    else if (cap.compare(0, 4, "tty.") == 0) emitTTYCall(cg, instr, cap);
    else if (cap.compare(0, 9, "security.") == 0) emitSecurityCall(cg, instr, cap);
    else if (cap.compare(0, 4, "gpu.") == 0) emitGPUCall(cg, instr, cap);

    if (auto* os = cg.getTextStream()) {
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  str x0, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void Windows_AArch64::emitIOCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "io.write") {
        *os << "  mov x0, #-11\n"; // STD_OUTPUT_HANDLE
        *os << "  bl GetStdHandle\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  bl WriteFile\n";
    } else if (cap == "io.read") {
        *os << "  mov x0, #-10\n"; // STD_INPUT_HANDLE
        *os << "  bl GetStdHandle\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  bl ReadFile\n";
    } else if (cap == "io.open") {
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  bl CreateFileA\n";
    } else if (cap == "io.close") {
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  bl CloseHandle\n";
    } else if (cap == "io.stat") {
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  bl GetFileInformationByHandle\n";
    void Windows_AArch64::emitProcessCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "process.exit") {
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  bl ExitProcess\n";
    } else if (cap == "time.now") {
        *os << "  bl GetSystemTimeAsFileTime\n";
    } else if (cap == "thread.spawn") {
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x3, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  bl CreateThread\n";
    } else if (cap == "thread.join") {
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, #-1\n";
        *os << "  bl WaitForSingleObject\n";
    } else if (cap == "thread.exit") {
        *os << "  bl ExitThread\n";
    } else if (cap == "sync.mutex.lock") {
        *os << "  mov x1, #1\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << ".Lmutex_retry_" << cg.labelCounter << ":\n";
        *os << "  ldaxr x0, [x2]\n";
        *os << "  stlxr w3, x1, [x2]\n";
        *os << "  cbnz w3, .Lmutex_retry_" << cg.labelCounter << "\n";
        *os << "  cbnz x0, .Lmutex_retry_" << cg.labelCounter << "\n";
        cg.labelCounter++;
    } else if (cap == "sync.mutex.unlock") {
        *os << "  mov x1, #0\n";
        *os << "  str x1, [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    } else if (cap == "memory.alloc") {
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #0x3000\n";
        *os << "  mov x3, #4\n";
        *os << "  bl VirtualAlloc\n";
    } else if (cap == "memory.free") {
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, #0\n";
        *os << "  mov x2, #0x8000\n";
        *os << "  bl VirtualFree\n";
    } else if (cap == "net.connect") {
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  bl connect\n";
    void Windows_AArch64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitFSCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitMemoryCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitProcessCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "process.exit") {
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  bl ExitProcess\n";
    } else if (cap == "time.now") {
        *os << "  bl GetSystemTimeAsFileTime\n";
    } else if (cap == "thread.spawn") {
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x3, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  bl CreateThread\n";
    } else if (cap == "thread.join") {
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, #-1\n";
        *os << "  bl WaitForSingleObject\n";
    } else if (cap == "thread.exit") {
        *os << "  bl ExitThread\n";
    } else if (cap == "sync.mutex.lock") {
        *os << "  mov x1, #1\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << ".Lmutex_retry_" << cg.labelCounter << ":\n";
        *os << "  ldaxr x0, [x2]\n";
        *os << "  stlxr w3, x1, [x2]\n";
        *os << "  cbnz w3, .Lmutex_retry_" << cg.labelCounter << "\n";
        *os << "  cbnz x0, .Lmutex_retry_" << cg.labelCounter << "\n";
        cg.labelCounter++;
    } else if (cap == "sync.mutex.unlock") {
        *os << "  mov x1, #0\n";
        *os << "  str x1, [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    } else if (cap == "memory.alloc") {
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #0x3000\n";
        *os << "  mov x3, #4\n";
        *os << "  bl VirtualAlloc\n";
    } else if (cap == "memory.free") {
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, #0\n";
        *os << "  mov x2, #0x8000\n";
        *os << "  bl VirtualFree\n";
    } else if (cap == "net.connect") {
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  bl connect\n";
    void Windows_AArch64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitThreadCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitTimeCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitEventCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitIPCCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitEnvCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitSystemCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitSignalCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void Windows_AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitTTYCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitSecurityCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void Windows_AArch64::emitGPUCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}


} // namespace target
} // namespace codegen
