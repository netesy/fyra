#include "target/os/linux/LinuxOS.h"
#include "codegen/CodeGen.h"
#include "target/core/ArchitectureInfo.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include <ostream>

namespace codegen {
namespace target {

uint64_t LinuxOS::getSyscallNumber(ir::SyscallId id) const {
    switch(id) {
        case ir::SyscallId::Exit: return 60; case ir::SyscallId::Write: return 1; case ir::SyscallId::Read: return 0; case ir::SyscallId::OpenAt: return 2; case ir::SyscallId::Close: return 3; default: return 0;
    }
}

namespace {
void emitLinuxSyscallArgs(CodeGen& cg, ir::Instruction& instr, const ArchitectureInfo& arch, size_t maxArgs = 6) {
    static const char* regs[] = {"rdi", "rsi", "rdx", "r10", "r8", "r9"};
    if (auto* os = cg.getTextStream()) {
        for (size_t i = 0; i < std::min(instr.getOperands().size(), maxArgs); ++i) {
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[i]->get()) << ", " << arch.getRegisterName(regs[i], instr.getOperands()[i]->get()->getType()) << "\n";
        }
    }
}
void emitStoreExternResult(CodeGen& cg, ir::Instruction& instr, const ArchitectureInfo& arch) {
    if (auto* os = cg.getTextStream()) {
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  movq " << arch.getRegisterName("rax", instr.getType()) << ", " << cg.getValueAsOperand(&instr) << "\n";
        }
    }
}
}

bool LinuxOS::supportsCapability(const CapabilitySpec& spec) const {
    return true;
}

void LinuxOS::emitIOCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::IO_WRITE: *os << "  movq $1, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 3); *os << "  syscall\n"; break;
            case CapabilityId::IO_READ: *os << "  movq $0, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 3); *os << "  syscall\n"; break;
            case CapabilityId::IO_OPEN: *os << "  movq $2, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 3); *os << "  syscall\n"; break;
            case CapabilityId::IO_CLOSE: *os << "  movq $3, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 1); *os << "  syscall\n"; break;
            case CapabilityId::IO_SEEK: *os << "  movq $8, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 3); *os << "  syscall\n"; break;
            case CapabilityId::IO_STAT: *os << "  movq $4, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 2); *os << "  syscall\n"; break;
            case CapabilityId::IO_FLUSH: *os << "  movq $74, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 1); *os << "  syscall\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitFSCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::FS_OPEN:
            case CapabilityId::FS_CREATE: { *os << "  movq $2, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 3); *os << "  syscall\n"; break; }
            case CapabilityId::FS_STAT: *os << "  movq $4, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 2); *os << "  syscall\n"; break;
            case CapabilityId::FS_REMOVE: *os << "  movq $87, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 1); *os << "  syscall\n"; break;
            case CapabilityId::FS_RENAME: *os << "  movq $82, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 2); *os << "  syscall\n"; break;
            case CapabilityId::FS_MKDIR: *os << "  movq $83, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 2); *os << "  syscall\n"; break;
            case CapabilityId::FS_RMDIR: *os << "  movq $84, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 1); *os << "  syscall\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitMemoryCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::MEMORY_ALLOC:
                *os << "  movq $9, " << arch.getRegisterName("rax", nullptr) << "\n";
                if (instr.getOperands().size() == 1) {
                    *os << "  xorq " << arch.getRegisterName("rdi", nullptr) << ", " << arch.getRegisterName("rdi", nullptr) << "\n";
                    *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", " << arch.getRegisterName("rsi", nullptr) << "\n";
                    *os << "  movq $3, " << arch.getRegisterName("rdx", nullptr) << "\n  movq $34, " << arch.getRegisterName("r10", nullptr) << "\n  movq $-1, " << arch.getRegisterName("r8", nullptr) << "\n  xorq " << arch.getRegisterName("r9", nullptr) << ", " << arch.getRegisterName("r9", nullptr) << "\n";
                } else emitLinuxSyscallArgs(cg, instr, arch, 6);
                *os << "  syscall\n";
                break;
            case CapabilityId::MEMORY_MAP: *os << "  movq $9, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 6); *os << "  syscall\n"; break;
            case CapabilityId::MEMORY_FREE: *os << "  movq $11, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 2); *os << "  syscall\n"; break;
            case CapabilityId::MEMORY_PROTECT: *os << "  movq $10, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 3); *os << "  syscall\n"; break;
            case CapabilityId::MEMORY_USAGE: *os << "  xorq " << arch.getRegisterName("rax", nullptr) << ", " << arch.getRegisterName("rax", nullptr) << "\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitProcessCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::PROCESS_EXIT: *os << "  movq $60, " << arch.getRegisterName("rax", nullptr) << "\n"; emitLinuxSyscallArgs(cg, instr, arch, 1); *os << "  syscall\n"; break;
            case CapabilityId::PROCESS_ABORT:
                *os << "  movq $39, " << arch.getRegisterName("rax", nullptr) << "\n  syscall\n  movq " << arch.getRegisterName("rax", nullptr) << ", " << arch.getRegisterName("rdi", nullptr) << "\n  movq $6, " << arch.getRegisterName("rsi", nullptr) << "\n  movq $62, " << arch.getRegisterName("rax", nullptr) << "\n  syscall\n";
                break;
            case CapabilityId::PROCESS_SLEEP:
                *os << "  subq $16, %rsp\n  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rax\n";
                *os << "  movq $1000, %rcx\n  xorq %rdx, %rdx\n  divq %rcx\n  movq %rax, (%rsp)\n";
                *os << "  imulq $1000000, %rdx, %rdx\n  movq %rdx, 8(%rsp)\n  movq $35, %rax\n";
                *os << "  movq %rsp, %rdi\n  xorq %rsi, %rsi\n  syscall\n  addq $16, %rsp\n";
                break;
            case CapabilityId::PROCESS_GETPID:
                *os << "  movq $39, %rax\n  syscall\n";
                break;
            case CapabilityId::PROCESS_SPAWN:
                *os << "  movq $57, %rax\n  syscall\n";
                break;
            case CapabilityId::PROCESS_ARGS:
                *os << "  movq $257, %rax\n";
                break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitThreadCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::THREAD_SPAWN: *os << "  movq $56, %rax\n"; emitLinuxSyscallArgs(cg, instr, arch, 5); *os << "  syscall\n"; break;
            case CapabilityId::THREAD_JOIN: *os << "  movq $61, %rax\n"; emitLinuxSyscallArgs(cg, instr, arch, 1); *os << "  xorq %rsi, %rsi\n  xorq %rdx, %rdx\n  xorq %r10, %r10\n"; *os << "  syscall\n"; break;
            case CapabilityId::THREAD_DETACH: *os << "  xorq %rax, %rax\n"; break;
            case CapabilityId::THREAD_YIELD: *os << "  movq $24, %rax\n  syscall\n"; break;
            case CapabilityId::THREAD_GETID: *os << "  movq $186, %rax\n  syscall\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitSyncCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::SYNC_MUTEX_LOCK:
                *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n";
                *os << "  movq $1, %rax\n  .Lmutex_retry_" << cg.labelCounter << ":\n  xchgq %rax, (%rdi)\n  testq %rax, %rax\n  jnz .Lmutex_retry_" << cg.labelCounter << "\n";
                cg.labelCounter++;
                break;
            case CapabilityId::SYNC_MUTEX_UNLOCK:
                *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n  movq $0, (%rdi)\n";
                break;
            case CapabilityId::SYNC_ATOMIC_ADD:
            case CapabilityId::SYNC_ATOMIC_SUB:
            case CapabilityId::SYNC_ATOMIC_CAS:
                *os << "  xor rax, rax\n";
                break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitTimeCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::TIME_NOW:
            case CapabilityId::TIME_MONOTONIC:
                *os << "  subq $16, %rsp\n  movq $228, %rax\n";
                *os << "  movq $" << (spec.id == CapabilityId::TIME_NOW ? 0 : 1) << ", %rdi\n";
                *os << "  movq %rsp, %rsi\n  syscall\n  movq (%rsp), %rax\n";
                if (spec.id == CapabilityId::TIME_MONOTONIC) *os << "  imulq $1000000000, %rax, %rax\n  addq 8(%rsp), %rax\n";
                *os << "  addq $16, %rsp\n";
                break;
            case CapabilityId::TIME_SLEEP:
                 emitProcessCapability(cg, instr, CapabilitySpec{CapabilityId::PROCESS_SLEEP, "process.sleep", CapabilityDomain::PROCESS, 1, 1, true, true}, arch);
                 return;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitEventCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::EVENT_POLL: *os << "  movq $232, %rax\n"; emitLinuxSyscallArgs(cg, instr, arch, 4); *os << "  syscall\n"; break;
            case CapabilityId::EVENT_CREATE: *os << "  movq $213, %rax\n  movq $0, %rdi\n  syscall\n"; break;
            case CapabilityId::EVENT_MODIFY: *os << "  movq $233, %rax\n"; emitLinuxSyscallArgs(cg, instr, arch, 4); *os << "  syscall\n"; break;
            case CapabilityId::EVENT_CLOSE: *os << "  movq $3, %rax\n"; emitLinuxSyscallArgs(cg, instr, arch, 1); *os << "  syscall\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitNetCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::NET_SOCKET: *os << "  movq $41, %rax\n"; break;
            case CapabilityId::NET_CONNECT: *os << "  movq $42, %rax\n"; break;
            case CapabilityId::NET_LISTEN: *os << "  movq $50, %rax\n"; break;
            case CapabilityId::NET_ACCEPT: *os << "  movq $43, %rax\n"; break;
            case CapabilityId::NET_SEND: *os << "  movq $44, %rax\n"; break;
            case CapabilityId::NET_RECV: *os << "  movq $45, %rax\n"; break;
            case CapabilityId::NET_CLOSE: *os << "  movq $3, %rax\n"; break;
            case CapabilityId::NET_BIND: *os << "  movq $49, %rax\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
        emitLinuxSyscallArgs(cg, instr, arch, 4); *os << "  syscall\n";
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitIPCCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::IPC_SEND) { emitIOCapability(cg, instr, CapabilitySpec{CapabilityId::IO_WRITE, "io.write", CapabilityDomain::IO, 3, 3, true, true}, arch); return; }
    if (spec.id == CapabilityId::IPC_RECV) { emitIOCapability(cg, instr, CapabilitySpec{CapabilityId::IO_READ, "io.read", CapabilityDomain::IO, 3, 3, true, true}, arch); return; }
    cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
}

void LinuxOS::emitEnvCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::ENV_GET:
            case CapabilityId::ENV_LIST:
                *os << "  movq $257, %rax\n  movq $-100, %rdi\n  leaq .Lproc_environ(%rip), %rsi\n  xorq %rdx, %rdx\n  xorq %r10, %r10\n  syscall\n  movq %rax, %r12\n  movq $0, %rax\n  movq %r12, %rdi\n";
                if (!instr.getOperands().empty()) *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rsi\n";
                else *os << "  subq $4096, %rsp\n  movq %rsp, %rsi\n";
                *os << "  movq $4096, %rdx\n  syscall\n  movq %rax, %r13\n  movq $3, %rax\n  movq %r12, %rdi\n  syscall\n  movq %r13, %rax\n";
                if (instr.getOperands().empty()) *os << "  addq $4096, %rsp\n";
                break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitSystemCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::SYSTEM_INFO: *os << "  movq $63, %rax\n"; emitLinuxSyscallArgs(cg, instr, arch, 1); *os << "  syscall\n"; break;
            case CapabilityId::SYSTEM_REBOOT: *os << "  movq $169, %rax\n"; break;
            case CapabilityId::SYSTEM_SHUTDOWN: *os << "  movq $169, %rax\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitSignalCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::SIGNAL_SEND: *os << "  movq $62, %rax\n"; emitLinuxSyscallArgs(cg, instr, arch, 2); *os << "  syscall\n"; break;
            case CapabilityId::SIGNAL_REGISTER: *os << "  movq $13, %rax\n"; *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n  xorq %rdx, %rdx\n  movq $8, %r10\n  syscall\n"; break;
            case CapabilityId::SIGNAL_WAIT: *os << "  movq $34, %rax\n  syscall\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitRandomCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::RANDOM_U64: *os << "  subq $8, %rsp\n  movq $318, %rax\n  movq %rsp, %rdi\n  movq $8, %rsi\n  xorq %rdx, %rdx\n  syscall\n  popq %rax\n"; break;
            case CapabilityId::RANDOM_BYTES: *os << "  movq $318, %rax\n"; emitLinuxSyscallArgs(cg, instr, arch, 2); *os << "  xorq %rdx, %rdx\n  syscall\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitErrorCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::ERROR_GET:
            case CapabilityId::ERROR_STR:
                *os << "  xorq %rax, %rax\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitDebugCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::DEBUG_LOG: *os << "  movq $1, %rax\n  movq $2, %rdi\n"; *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rsi\n  movq $128, %rdx\n  syscall\n"; break;
            case CapabilityId::DEBUG_BREAK: *os << "  int3\n"; break;
            case CapabilityId::DEBUG_TRACE: *os << "  xor rax, rax\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitModuleCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::MODULE_LOAD: *os << "  movq $257, %rax\n  movq $-100, %rdi\n  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rsi\n  xorq %rdx, %rdx\n  xorq %r10, %r10\n  syscall\n  movq %rax, %rdi\n  movq $313, %rax\n"; if (instr.getOperands().size() > 1) *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << ", %rsi\n"; else *os << "  xorq %rsi, %rsi\n"; *os << "  xorq %rdx, %rdx\n  syscall\n"; break;
            case CapabilityId::MODULE_UNLOAD: *os << "  movq $176, %rax\n"; emitLinuxSyscallArgs(cg, instr, arch, 1); *os << "  xorq %rsi, %rsi\n  syscall\n"; break;
            case CapabilityId::MODULE_GETSYM: *os << "  xor rax, rax\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitTTYCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::TTY_ISATTY: *os << "  subq $64, %rsp\n  movq $16, %rax\n  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n  movq $0x5401, %rsi\n  movq %rsp, %rdx\n  syscall\n  cmpq $0, %rax\n  sete %al\n  movzbq %al, %rax\n  addq $64, %rsp\n"; break;
            case CapabilityId::TTY_GETSIZE: *os << "  subq $8, %rsp\n  movq $16, %rax\n  movq " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << ", %rdi\n  movq $0x5413, %rsi\n  movq %rsp, %rdx\n  syscall\n  movq (%rsp), %rax\n  addq $8, %rsp\n"; break;
            case CapabilityId::TTY_SETMODE: *os << "  movq $16, %rax\n"; emitLinuxSyscallArgs(cg, instr, arch, 3); *os << "  syscall\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitSecurityCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::SECURITY_CHMOD: *os << "  movq $90, %rax\n"; emitLinuxSyscallArgs(cg, instr, arch, 2); *os << "  syscall\n"; break;
            case CapabilityId::SECURITY_CHOWN: *os << "  movq $92, %rax\n"; emitLinuxSyscallArgs(cg, instr, arch, 3); *os << "  syscall\n"; break;
            case CapabilityId::SECURITY_GETUID: *os << "  movq $102, %rax\n  syscall\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitStoreExternResult(cg, instr, arch);
}

void LinuxOS::emitGPUCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const {
     cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s);
}

void LinuxOS::emitHeader(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << ".section .rodata\n.Lproc_environ:\n  .string \"/proc/self/environ\"\n";
        *os << ".Lproc_cmdline:\n  .string \"/proc/self/cmdline\"\n";
        *os << ".section .data\n.align 8\nheap_ptr:\n  .quad __fyra_heap\n";
        *os << ".section .bss\n.align 16\n__fyra_heap:\n  .zero 1048576\n";
        *os << ".text\n";
    }
}

void LinuxOS::emitStartFunction(CodeGen& cg, const ArchitectureInfo& arch) {
    if (auto* os = cg.getTextStream()) {
        *os << ".globl _start\n_start:\n  call main\n  movq " << arch.getRegisterName("rax", nullptr) << ", " << arch.getRegisterName("rdi", nullptr) << "\n  movq $60, " << arch.getRegisterName("rax", nullptr) << "\n  syscall\n";
    }
}

}
}
