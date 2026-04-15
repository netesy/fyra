#include "codegen/target/MacOS_AArch64.h"
#include "codegen/CodeGen.h"
#include "ir/Function.h"
#include "ir/Use.h"
#include <ostream>

namespace codegen {
namespace target {


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

void MacOS_AArch64::emitExternCall(CodeGen& cg, ir::Instruction& instr) {
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

void MacOS_AArch64::emitIOCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "io.write") {
        *os << "  mov x16, #" << (unix_class | 4) << "\n"; // write
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  svc #0x80\n";
    } else if (cap == "io.read") {
        *os << "  mov x16, #" << (unix_class | 3) << "\n"; // read
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  svc #0x80\n";
    } else if (cap == "io.open") {
        *os << "  mov x16, #" << (unix_class | 5) << "\n"; // open
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  svc #0x80\n";
    } else if (cap == "io.close") {
        *os << "  mov x16, #" << (unix_class | 6) << "\n"; // close
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  svc #0x80\n";
    } else if (cap == "io.seek") {
        *os << "  mov x16, #" << (unix_class | 199) << "\n"; // lseek
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
        *os << "  svc #0x80\n";
    } else if (cap == "io.flush") {
        *os << "  mov x16, #" << (unix_class | 95) << "\n"; // fsync
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  svc #0x80\n";
    } else if (cap == "io.stat") {
        *os << "  mov x16, #" << (unix_class | 189) << "\n"; // fstat64
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  svc #0x80\n";
    void MacOS_AArch64::emitProcessCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "process.exit") {
        *os << "  mov x16, #" << (unix_class | 1) << "\n";
        if (!instr.getOperands().empty()) *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  svc #0x80\n";
    } else if (cap == "process.abort") {
        *os << "  mov x16, #" << (unix_class | 20) << "\n"; // getpid
        *os << "  svc #0x80\n";
        *os << "  mov x1, #6\n"; // SIGABRT
        *os << "  mov x16, #" << (unix_class | 37) << "\n"; // kill
        *os << "  svc #0x80\n";
    } else if (cap == "process.spawn") {
        *os << "  mov x16, #" << (unix_class | 2) << "\n"; // fork
        *os << "  svc #0x80\n";
        *os << "  cmp x0, #0\n";
        *os << "  b.ne .Lspawn_parent_" << cg.labelCounter << "\n";
        *os << "  mov x16, #" << (unix_class | 59) << "\n"; // execve
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0x80\n";
        *os << "  mov x16, #" << (unix_class | 1) << "\n"; // exit(1) if failed
        *os << "  mov x0, #1\n";
        *os << "  svc #0x80\n";
        *os << ".Lspawn_parent_" << cg.labelCounter << ":\n";
        cg.labelCounter++;
    } else if (cap == "process.info") {
        *os << "  mov x16, #" << (unix_class | 20) << "\n"; // getpid
        *os << "  svc #0x80\n";
    void MacOS_AArch64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "sync.mutex.lock") {
         *os << "  mov x1, #1\n";
         *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
         *os << ".Lmutex_retry_" << cg.labelCounter << ":\n";
         *os << "  ldxr x0, [x2]\n";
         *os << "  stxr w3, x1, [x2]\n";
         *os << "  cbnz w3, .Lmutex_retry_" << cg.labelCounter << "\n";
         *os << "  cbnz x0, .Lmutex_retry_" << cg.labelCounter << "\n";
         cg.labelCounter++;
    } else if (cap == "sync.mutex.unlock") {
         *os << "  mov x1, #0\n";
         *os << "  str x1, [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    void MacOS_AArch64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "net.connect") {
        *os << "  mov x16, #" << (unix_class | 97) << "\n"; // socket
        *os << "  mov x0, #2\n";
        *os << "  mov x1, #1\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0x80\n";
        *os << "  mov x9, x0\n";
        *os << "  mov x16, #" << (unix_class | 98) << "\n"; // connect
        *os << "  mov x0, x9\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #16\n";
        *os << "  svc #0x80\n";
        *os << "  mov x0, x9\n";
    void MacOS_AArch64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "random.u64") {
        *os << "  sub sp, sp, #16\n";
        *os << "  mov x16, #" << (unix_class | 318) << "\n"; // getentropy
        *os << "  mov x0, sp\n";
        *os << "  mov x1, #8\n";
        *os << "  svc #0x80\n";
        *os << "  ldr x0, [sp]\n";
        *os << "  add sp, sp, #16\n";
    void MacOS_AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void MacOS_AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitFSCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitMemoryCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitProcessCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "process.exit") {
        *os << "  mov x16, #" << (unix_class | 1) << "\n";
        if (!instr.getOperands().empty()) *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  svc #0x80\n";
    } else if (cap == "process.abort") {
        *os << "  mov x16, #" << (unix_class | 20) << "\n"; // getpid
        *os << "  svc #0x80\n";
        *os << "  mov x1, #6\n"; // SIGABRT
        *os << "  mov x16, #" << (unix_class | 37) << "\n"; // kill
        *os << "  svc #0x80\n";
    } else if (cap == "process.spawn") {
        *os << "  mov x16, #" << (unix_class | 2) << "\n"; // fork
        *os << "  svc #0x80\n";
        *os << "  cmp x0, #0\n";
        *os << "  b.ne .Lspawn_parent_" << cg.labelCounter << "\n";
        *os << "  mov x16, #" << (unix_class | 59) << "\n"; // execve
        *os << "  mov x0, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0x80\n";
        *os << "  mov x16, #" << (unix_class | 1) << "\n"; // exit(1) if failed
        *os << "  mov x0, #1\n";
        *os << "  svc #0x80\n";
        *os << ".Lspawn_parent_" << cg.labelCounter << ":\n";
        cg.labelCounter++;
    } else if (cap == "process.info") {
        *os << "  mov x16, #" << (unix_class | 20) << "\n"; // getpid
        *os << "  svc #0x80\n";
    void MacOS_AArch64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "sync.mutex.lock") {
         *os << "  mov x1, #1\n";
         *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
         *os << ".Lmutex_retry_" << cg.labelCounter << ":\n";
         *os << "  ldxr x0, [x2]\n";
         *os << "  stxr w3, x1, [x2]\n";
         *os << "  cbnz w3, .Lmutex_retry_" << cg.labelCounter << "\n";
         *os << "  cbnz x0, .Lmutex_retry_" << cg.labelCounter << "\n";
         cg.labelCounter++;
    } else if (cap == "sync.mutex.unlock") {
         *os << "  mov x1, #0\n";
         *os << "  str x1, [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    void MacOS_AArch64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "net.connect") {
        *os << "  mov x16, #" << (unix_class | 97) << "\n"; // socket
        *os << "  mov x0, #2\n";
        *os << "  mov x1, #1\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0x80\n";
        *os << "  mov x9, x0\n";
        *os << "  mov x16, #" << (unix_class | 98) << "\n"; // connect
        *os << "  mov x0, x9\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #16\n";
        *os << "  svc #0x80\n";
        *os << "  mov x0, x9\n";
    void MacOS_AArch64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "random.u64") {
        *os << "  sub sp, sp, #16\n";
        *os << "  mov x16, #" << (unix_class | 318) << "\n"; // getentropy
        *os << "  mov x0, sp\n";
        *os << "  mov x1, #8\n";
        *os << "  svc #0x80\n";
        *os << "  ldr x0, [sp]\n";
        *os << "  add sp, sp, #16\n";
    void MacOS_AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void MacOS_AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitThreadCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "sync.mutex.lock") {
         *os << "  mov x1, #1\n";
         *os << "  mov x2, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
         *os << ".Lmutex_retry_" << cg.labelCounter << ":\n";
         *os << "  ldxr x0, [x2]\n";
         *os << "  stxr w3, x1, [x2]\n";
         *os << "  cbnz w3, .Lmutex_retry_" << cg.labelCounter << "\n";
         *os << "  cbnz x0, .Lmutex_retry_" << cg.labelCounter << "\n";
         cg.labelCounter++;
    } else if (cap == "sync.mutex.unlock") {
         *os << "  mov x1, #0\n";
         *os << "  str x1, [" << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "]\n";
    void MacOS_AArch64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "net.connect") {
        *os << "  mov x16, #" << (unix_class | 97) << "\n"; // socket
        *os << "  mov x0, #2\n";
        *os << "  mov x1, #1\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0x80\n";
        *os << "  mov x9, x0\n";
        *os << "  mov x16, #" << (unix_class | 98) << "\n"; // connect
        *os << "  mov x0, x9\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #16\n";
        *os << "  svc #0x80\n";
        *os << "  mov x0, x9\n";
    void MacOS_AArch64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "random.u64") {
        *os << "  sub sp, sp, #16\n";
        *os << "  mov x16, #" << (unix_class | 318) << "\n"; // getentropy
        *os << "  mov x0, sp\n";
        *os << "  mov x1, #8\n";
        *os << "  svc #0x80\n";
        *os << "  ldr x0, [sp]\n";
        *os << "  add sp, sp, #16\n";
    void MacOS_AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void MacOS_AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitTimeCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitEventCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "net.connect") {
        *os << "  mov x16, #" << (unix_class | 97) << "\n"; // socket
        *os << "  mov x0, #2\n";
        *os << "  mov x1, #1\n";
        *os << "  mov x2, #0\n";
        *os << "  svc #0x80\n";
        *os << "  mov x9, x0\n";
        *os << "  mov x16, #" << (unix_class | 98) << "\n"; // connect
        *os << "  mov x0, x9\n";
        *os << "  mov x1, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov x2, #16\n";
        *os << "  svc #0x80\n";
        *os << "  mov x0, x9\n";
    void MacOS_AArch64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "random.u64") {
        *os << "  sub sp, sp, #16\n";
        *os << "  mov x16, #" << (unix_class | 318) << "\n"; // getentropy
        *os << "  mov x0, sp\n";
        *os << "  mov x1, #8\n";
        *os << "  svc #0x80\n";
        *os << "  ldr x0, [sp]\n";
        *os << "  add sp, sp, #16\n";
    void MacOS_AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void MacOS_AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitIPCCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitEnvCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitSystemCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitSignalCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "random.u64") {
        *os << "  sub sp, sp, #16\n";
        *os << "  mov x16, #" << (unix_class | 318) << "\n"; // getentropy
        *os << "  mov x0, sp\n";
        *os << "  mov x1, #8\n";
        *os << "  svc #0x80\n";
        *os << "  ldr x0, [sp]\n";
        *os << "  add sp, sp, #16\n";
    void MacOS_AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void MacOS_AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void MacOS_AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitTTYCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitSecurityCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_AArch64::emitGPUCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}


} // namespace target
} // namespace codegen
