#include "codegen/target/MacOS_x64.h"
#include "codegen/target/SystemV_x64.h"
#include "codegen/CodeGen.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include <ostream>

namespace codegen { namespace target {


void MacOS_x64::initRegisters() {
    SystemV_x64::initRegisters(); }
MacOS_x64::MacOS_x64() : SystemV_x64() { initRegisters();
}

void MacOS_x64::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) { *os << "_" << func.getName() << ":\n"; }
    SystemV_x64::emitFunctionPrologue(cg, func);
}

void MacOS_x64::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) { *os << "\n.globl _main\n_main:\n  call _main_user\n  ret\n"; }
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

void MacOS_x64::emitExternCall(CodeGen& cg, ir::Instruction& instr) {
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
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  movq %rax, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void MacOS_x64::emitIOCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "io.write") {
        *os << "  movq $" << (unix_class | 4) << ", %rax\n"; // write
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << ", %rdx\n";
        *os << "  syscall\n";
    } else if (cap == "io.read") {
        *os << "  movq $" << (unix_class | 3) << ", %rax\n"; // read
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << ", %rdx\n";
        *os << "  syscall\n";
    } else if (cap == "io.open") {
        *os << "  movq $" << (unix_class | 5) << ", %rax\n"; // open
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << ", %rdx\n";
        *os << "  syscall\n";
    } else if (cap == "io.close") {
        *os << "  movq $" << (unix_class | 6) << ", %rax\n"; // close
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
        *os << "  syscall\n";
    } else if (cap == "io.seek") {
        *os << "  movq $" << (unix_class | 199) << ", %rax\n"; // lseek
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << ", %rdx\n";
        *os << "  syscall\n";
    } else if (cap == "io.flush") {
        *os << "  movq $" << (unix_class | 95) << ", %rax\n"; // fsync
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
        *os << "  syscall\n";
    } else if (cap == "io.stat") {
        *os << "  movq $" << (unix_class | 189) << ", %rax\n"; // fstat64
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n";
        *os << "  syscall\n";
    void MacOS_x64::emitProcessCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "process.exit") {
        *os << "  movq $" << (unix_class | 1) << ", %rax\n";
        if (!instr.getOperands().empty()) *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
        *os << "  syscall\n";
    } else if (cap == "process.abort") {
        *os << "  movq $" << (unix_class | 20) << ", %rax\n"; // getpid
        *os << "  syscall\n";
        *os << "  movq %rax, %rdi\n";
        *os << "  movq $6, %rsi\n"; // SIGABRT
        *os << "  movq $" << (unix_class | 37) << ", %rax\n"; // kill
        *os << "  syscall\n";
    } else if (cap == "process.spawn") {
        *os << "  movq $" << (unix_class | 2) << ", %rax\n"; // fork
        *os << "  syscall\n";
        *os << "  testq %rax, %rax\n";
        *os << "  jnz .Lspawn_parent_" << cg.labelCounter << "\n";
        *os << "  movq $" << (unix_class | 59) << ", %rax\n"; // execve
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n";
        *os << "  xorq %rdx, %rdx\n";
        *os << "  syscall\n";
        *os << "  movq $" << (unix_class | 1) << ", %rax\n"; // exit(1) if failed
        *os << "  movq $1, %rdi\n";
        *os << "  syscall\n";
        *os << ".Lspawn_parent_" << cg.labelCounter << ":\n";
        cg.labelCounter++;
    } else if (cap == "process.info") {
        *os << "  movq $" << (unix_class | 20) << ", %rax\n"; // getpid
        *os << "  syscall\n";
    void MacOS_x64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "sync.mutex.lock") {
         *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
         *os << "  movq $1, %rax\n";
         *os << "  .Lmutex_retry_" << cg.labelCounter << ":\n";
         *os << "  xchgq %rax, (%rdi)\n";
         *os << "  testq %rax, %rax\n";
         *os << "  jnz .Lmutex_retry_" << cg.labelCounter << "\n";
         cg.labelCounter++;
    } else if (cap == "sync.mutex.unlock") {
         *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
         *os << "  movq $0, (%rdi)\n";
    void MacOS_x64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "net.connect") {
        *os << "  movq $" << (unix_class | 97) << ", %rax\n"; // socket
        *os << "  movq $2, %rdi\n";
        *os << "  movq $1, %rsi\n";
        *os << "  xorq %rdx, %rdx\n";
        *os << "  syscall\n";
        *os << "  pushq %rax\n";
        *os << "  movq %rax, %rdi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rsi\n";
        *os << "  movq $16, %rdx\n";
        *os << "  movq $" << (unix_class | 98) << ", %rax\n"; // connect
        *os << "  syscall\n";
        *os << "  popq %rax\n";
    void MacOS_x64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "random.u64") {
        *os << "  subq $8, %rsp\n";
        *os << "  movq $" << (unix_class | 318) << ", %rax\n"; // getentropy
        *os << "  movq %rsp, %rdi\n";
        *os << "  movq $8, %rsi\n";
        *os << "  syscall\n";
        *os << "  popq %rax\n";
    void MacOS_x64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void MacOS_x64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

}}

}} // namespace codegen::target

void MacOS_x64::emitExternCall(CodeGen& cg, ir::Instruction& instr) {
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
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  movq %rax, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void MacOS_x64::emitFSCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_x64::emitMemoryCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_x64::emitProcessCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "process.exit") {
        *os << "  movq $" << (unix_class | 1) << ", %rax\n";
        if (!instr.getOperands().empty()) *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
        *os << "  syscall\n";
    } else if (cap == "process.abort") {
        *os << "  movq $" << (unix_class | 20) << ", %rax\n"; // getpid
        *os << "  syscall\n";
        *os << "  movq %rax, %rdi\n";
        *os << "  movq $6, %rsi\n"; // SIGABRT
        *os << "  movq $" << (unix_class | 37) << ", %rax\n"; // kill
        *os << "  syscall\n";
    } else if (cap == "process.spawn") {
        *os << "  movq $" << (unix_class | 2) << ", %rax\n"; // fork
        *os << "  syscall\n";
        *os << "  testq %rax, %rax\n";
        *os << "  jnz .Lspawn_parent_" << cg.labelCounter << "\n";
        *os << "  movq $" << (unix_class | 59) << ", %rax\n"; // execve
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n";
        *os << "  xorq %rdx, %rdx\n";
        *os << "  syscall\n";
        *os << "  movq $" << (unix_class | 1) << ", %rax\n"; // exit(1) if failed
        *os << "  movq $1, %rdi\n";
        *os << "  syscall\n";
        *os << ".Lspawn_parent_" << cg.labelCounter << ":\n";
        cg.labelCounter++;
    } else if (cap == "process.info") {
        *os << "  movq $" << (unix_class | 20) << ", %rax\n"; // getpid
        *os << "  syscall\n";
    void MacOS_x64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "sync.mutex.lock") {
         *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
         *os << "  movq $1, %rax\n";
         *os << "  .Lmutex_retry_" << cg.labelCounter << ":\n";
         *os << "  xchgq %rax, (%rdi)\n";
         *os << "  testq %rax, %rax\n";
         *os << "  jnz .Lmutex_retry_" << cg.labelCounter << "\n";
         cg.labelCounter++;
    } else if (cap == "sync.mutex.unlock") {
         *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
         *os << "  movq $0, (%rdi)\n";
    void MacOS_x64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "net.connect") {
        *os << "  movq $" << (unix_class | 97) << ", %rax\n"; // socket
        *os << "  movq $2, %rdi\n";
        *os << "  movq $1, %rsi\n";
        *os << "  xorq %rdx, %rdx\n";
        *os << "  syscall\n";
        *os << "  pushq %rax\n";
        *os << "  movq %rax, %rdi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rsi\n";
        *os << "  movq $16, %rdx\n";
        *os << "  movq $" << (unix_class | 98) << ", %rax\n"; // connect
        *os << "  syscall\n";
        *os << "  popq %rax\n";
    void MacOS_x64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "random.u64") {
        *os << "  subq $8, %rsp\n";
        *os << "  movq $" << (unix_class | 318) << ", %rax\n"; // getentropy
        *os << "  movq %rsp, %rdi\n";
        *os << "  movq $8, %rsi\n";
        *os << "  syscall\n";
        *os << "  popq %rax\n";
    void MacOS_x64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void MacOS_x64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

}}

}} // namespace codegen::target

void MacOS_x64::emitExternCall(CodeGen& cg, ir::Instruction& instr) {
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
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  movq %rax, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void MacOS_x64::emitThreadCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_x64::emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    if (cap == "sync.mutex.lock") {
         *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
         *os << "  movq $1, %rax\n";
         *os << "  .Lmutex_retry_" << cg.labelCounter << ":\n";
         *os << "  xchgq %rax, (%rdi)\n";
         *os << "  testq %rax, %rax\n";
         *os << "  jnz .Lmutex_retry_" << cg.labelCounter << "\n";
         cg.labelCounter++;
    } else if (cap == "sync.mutex.unlock") {
         *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
         *os << "  movq $0, (%rdi)\n";
    void MacOS_x64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "net.connect") {
        *os << "  movq $" << (unix_class | 97) << ", %rax\n"; // socket
        *os << "  movq $2, %rdi\n";
        *os << "  movq $1, %rsi\n";
        *os << "  xorq %rdx, %rdx\n";
        *os << "  syscall\n";
        *os << "  pushq %rax\n";
        *os << "  movq %rax, %rdi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rsi\n";
        *os << "  movq $16, %rdx\n";
        *os << "  movq $" << (unix_class | 98) << ", %rax\n"; // connect
        *os << "  syscall\n";
        *os << "  popq %rax\n";
    void MacOS_x64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "random.u64") {
        *os << "  subq $8, %rsp\n";
        *os << "  movq $" << (unix_class | 318) << ", %rax\n"; // getentropy
        *os << "  movq %rsp, %rdi\n";
        *os << "  movq $8, %rsi\n";
        *os << "  syscall\n";
        *os << "  popq %rax\n";
    void MacOS_x64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void MacOS_x64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

}}

}} // namespace codegen::target

void MacOS_x64::emitExternCall(CodeGen& cg, ir::Instruction& instr) {
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
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  movq %rax, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void MacOS_x64::emitTimeCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_x64::emitEventCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_x64::emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "net.connect") {
        *os << "  movq $" << (unix_class | 97) << ", %rax\n"; // socket
        *os << "  movq $2, %rdi\n";
        *os << "  movq $1, %rsi\n";
        *os << "  xorq %rdx, %rdx\n";
        *os << "  syscall\n";
        *os << "  pushq %rax\n";
        *os << "  movq %rax, %rdi\n";
        *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rsi\n";
        *os << "  movq $16, %rdx\n";
        *os << "  movq $" << (unix_class | 98) << ", %rax\n"; // connect
        *os << "  syscall\n";
        *os << "  popq %rax\n";
    void MacOS_x64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "random.u64") {
        *os << "  subq $8, %rsp\n";
        *os << "  movq $" << (unix_class | 318) << ", %rax\n"; // getentropy
        *os << "  movq %rsp, %rdi\n";
        *os << "  movq $8, %rsi\n";
        *os << "  syscall\n";
        *os << "  popq %rax\n";
    void MacOS_x64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void MacOS_x64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

}}

}} // namespace codegen::target

void MacOS_x64::emitExternCall(CodeGen& cg, ir::Instruction& instr) {
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
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  movq %rax, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void MacOS_x64::emitIPCCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_x64::emitEnvCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_x64::emitSystemCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_x64::emitSignalCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_x64::emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    auto* os = cg.getTextStream(); if (!os) return;
    uint64_t unix_class = 0x02000000;
    if (cap == "random.u64") {
        *os << "  subq $8, %rsp\n";
        *os << "  movq $" << (unix_class | 318) << ", %rax\n"; // getentropy
        *os << "  movq %rsp, %rdi\n";
        *os << "  movq $8, %rsi\n";
        *os << "  syscall\n";
        *os << "  popq %rax\n";
    void MacOS_x64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void MacOS_x64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

}}

}} // namespace codegen::target

void MacOS_x64::emitExternCall(CodeGen& cg, ir::Instruction& instr) {
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
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  movq %rax, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void MacOS_x64::emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_x64::emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
void MacOS_x64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

}}

}} // namespace codegen::target

void MacOS_x64::emitExternCall(CodeGen& cg, ir::Instruction& instr) {
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
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  movq %rax, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void MacOS_x64::emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

}}

}} // namespace codegen::target

void MacOS_x64::emitExternCall(CodeGen& cg, ir::Instruction& instr) {
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
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) *os << "  movq %rax, " << cg.getValueAsOperand(&instr) << "\n";
    }
}

void MacOS_x64::emitTTYCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_x64::emitSecurityCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}

void MacOS_x64::emitGPUCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap) {
    (void)cg; (void)instr; (void)cap;
}


}} // namespace codegen::target
