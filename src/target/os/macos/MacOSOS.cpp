#include "target/os/macos/MacOSOS.h"
#include "codegen/CodeGen.h"
#include "target/core/ArchitectureInfo.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include <ostream>

namespace codegen {
namespace target {

uint64_t MacOSOS::getSyscallNumber(ir::SyscallId id) const {
    switch(id) {
        case ir::SyscallId::Exit: return 0x02000001;
        case ir::SyscallId::Write: return 0x02000004;
        case ir::SyscallId::Read: return 0x02000003;
        default: return 0;
    }
}

namespace {
void emitMacOSSyscallArgs(CodeGen& cg, ir::Instruction& instr, const ArchitectureInfo& arch, size_t maxArgs = 6) {
    static const char* regs[] = {"rdi", "rsi", "rdx", "r10", "r8", "r9"};
    if (auto* os = cg.getTextStream()) {
        for (size_t i = 0; i < std::min(instr.getOperands().size(), maxArgs); ++i) {
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[i]->get()) << ", " << arch.getRegisterName(regs[i], instr.getOperands()[i]->get()->getType()) << "\n";
        }
    }
}
void emitMacOSStoreResult(CodeGen& cg, ir::Instruction& instr, const ArchitectureInfo& arch) {
    if (auto* os = cg.getTextStream()) {
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  movq " << arch.getRegisterName("rax", instr.getType()) << ", " << cg.getValueAsOperand(&instr) << "\n";
        }
    }
}
}

void MacOSOS::emitIOCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        uint64_t num = 0;
        switch (spec.id) {
            case CapabilityId::IO_READ: num = 0x02000003; break;
            case CapabilityId::IO_WRITE: num = 0x02000004; break;
            case CapabilityId::IO_OPEN: num = 0x02000005; break;
            case CapabilityId::IO_CLOSE: num = 0x02000006; break;
            case CapabilityId::IO_SEEK: num = 0x020000c7; break;
            case CapabilityId::IO_STAT: num = 0x020000bc; break;
            case CapabilityId::IO_FLUSH: num = 0x0200005f; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
        *os << "  movq $" << num << ", " << arch.getRegisterName("rax", nullptr) << "\n";
        emitMacOSSyscallArgs(cg, instr, arch, 3);
        *os << "  syscall\n";
    }
    emitMacOSStoreResult(cg, instr, arch);
}

void MacOSOS::emitFSCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        uint64_t num = 0;
        switch (spec.id) {
            case CapabilityId::FS_OPEN:
            case CapabilityId::FS_CREATE: num = 0x02000005; break;
            case CapabilityId::FS_STAT: num = 0x020000bc; break;
            case CapabilityId::FS_REMOVE: num = 0x0200000a; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
        *os << "  movq $" << num << ", " << arch.getRegisterName("rax", nullptr) << "\n";
        emitMacOSSyscallArgs(cg, instr, arch, 3);
        *os << "  syscall\n";
    }
    emitMacOSStoreResult(cg, instr, arch);
}

void MacOSOS::emitMemoryCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        uint64_t num = 0;
        switch (spec.id) {
            case CapabilityId::MEMORY_ALLOC:
            case CapabilityId::MEMORY_MAP: num = 0x020000c5; break;
            case CapabilityId::MEMORY_FREE: num = 0x02000049; break;
            case CapabilityId::MEMORY_PROTECT: num = 0x0200004a; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
        *os << "  movq $" << num << ", " << arch.getRegisterName("rax", nullptr) << "\n";
        emitMacOSSyscallArgs(cg, instr, arch, 6);
        *os << "  syscall\n";
    }
    emitMacOSStoreResult(cg, instr, arch);
}

void MacOSOS::emitProcessCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        uint64_t num = 0;
        switch (spec.id) {
            case CapabilityId::PROCESS_EXIT: num = 0x02000001; break;
            case CapabilityId::PROCESS_ABORT: num = 0x02000025; break;
            case CapabilityId::PROCESS_SLEEP: num = 0x02000181; break;
            case CapabilityId::PROCESS_SPAWN: num = 0x02000002; break;
            case CapabilityId::PROCESS_ARGS: num = 0x0200003b; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
        }
        *os << "  movq $" << num << ", " << arch.getRegisterName("rax", nullptr) << "\n";
        emitMacOSSyscallArgs(cg, i, arch, 1);
        *os << "  syscall\n";
    }
    emitMacOSStoreResult(cg, i, arch);
}

void MacOSOS::emitThreadCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        uint64_t num = 0;
        switch (spec.id) {
            case CapabilityId::THREAD_SPAWN: num = 0x0200018d; break;
            case CapabilityId::THREAD_JOIN: num = 0x0200018e; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
        }
        *os << "  movq $" << num << ", " << arch.getRegisterName("rax", nullptr) << "\n";
        emitMacOSSyscallArgs(cg, i, arch, 3);
        *os << "  syscall\n";
    }
    emitMacOSStoreResult(cg, i, arch);
}

void MacOSOS::emitSyncCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::SYNC_MUTEX_LOCK:
                *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rdi\n";
                *os << "  movq $1, %rax\n  .Lmutex_retry_mac_" << cg.labelCounter << ":\n  lock xchgq %rax, (%rdi)\n  testq %rax, %rax\n  jnz .Lmutex_retry_mac_" << cg.labelCounter << "\n";
                cg.labelCounter++;
                break;
            case CapabilityId::SYNC_MUTEX_UNLOCK:
                *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rdi\n  movq $0, (%rdi)\n";
                break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
        }
    }
    emitMacOSStoreResult(cg, i, arch);
}

void MacOSOS::emitTimeCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        uint64_t num = 0;
        switch (spec.id) {
            case CapabilityId::TIME_NOW: num = 0x02000074; break;
            case CapabilityId::TIME_MONOTONIC: num = 0x02000155; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
        }
        *os << "  movq $" << num << ", " << arch.getRegisterName("rax", nullptr) << "\n";
        *os << "  syscall\n";
    }
    emitMacOSStoreResult(cg, i, arch);
}

void MacOSOS::emitEventCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        uint64_t num = 0;
        switch (spec.id) {
            case CapabilityId::EVENT_POLL: num = 0x020000e8; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
        }
        *os << "  movq $" << num << ", " << arch.getRegisterName("rax", nullptr) << "\n";
        emitMacOSSyscallArgs(cg, i, arch, 4);
        *os << "  syscall\n";
    }
    emitMacOSStoreResult(cg, i, arch);
}

void MacOSOS::emitNetCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        uint64_t num = 0;
        switch (spec.id) {
            case CapabilityId::NET_SOCKET: num = 0x02000061; break;
            case CapabilityId::NET_CONNECT: num = 0x02000062; break;
            case CapabilityId::NET_LISTEN: num = 0x0200006a; break;
            case CapabilityId::NET_ACCEPT: num = 0x0200001e; break;
            case CapabilityId::NET_SEND: num = 0x02000085; break;
            case CapabilityId::NET_RECV: num = 0x02000066; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
        }
        *os << "  movq $" << num << ", " << arch.getRegisterName("rax", nullptr) << "\n";
        emitMacOSSyscallArgs(cg, i, arch, 4);
        *os << "  syscall\n";
    }
    emitMacOSStoreResult(cg, i, arch);
}

void MacOSOS::emitIPCCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::IPC_SEND) { emitIOCapability(cg, i, CapabilitySpec{CapabilityId::IO_WRITE, "io.write", CapabilityDomain::IO, 3, 3, true, true}, arch); return; }
    if (spec.id == CapabilityId::IPC_RECV) { emitIOCapability(cg, i, CapabilitySpec{CapabilityId::IO_READ, "io.read", CapabilityDomain::IO, 3, 3, true, true}, arch); return; }
    cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec);
}

void MacOSOS::emitEnvCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        uint64_t num = 0;
        switch (spec.id) {
            case CapabilityId::ENV_GET: num = 0x0200003b; break;
            case CapabilityId::ENV_LIST: num = 0x0200003b; break;
            case CapabilityId::ENV_SET: num = 0x0200003b; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
        }
        *os << "  movq $" << num << ", " << arch.getRegisterName("rax", nullptr) << "\n";
        *os << "  syscall\n";
    }
    emitMacOSStoreResult(cg, i, arch);
}

void MacOSOS::emitSystemCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        uint64_t num = 0;
        switch (spec.id) {
            case CapabilityId::SYSTEM_INFO: num = 0x02000074; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
        }
        *os << "  movq $" << num << ", " << arch.getRegisterName("rax", nullptr) << "\n";
        *os << "  syscall\n";
    }
    emitMacOSStoreResult(cg, i, arch);
}

void MacOSOS::emitSignalCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        uint64_t num = 0;
        switch (spec.id) {
            case CapabilityId::SIGNAL_SEND: num = 0x02000025; break;
            case CapabilityId::SIGNAL_REGISTER: num = 0x02000030; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
        }
        *os << "  movq $" << num << ", " << arch.getRegisterName("rax", nullptr) << "\n";
        emitMacOSSyscallArgs(cg, i, arch, 3);
        *os << "  syscall\n";
    }
    emitMacOSStoreResult(cg, i, arch);
}

void MacOSOS::emitRandomCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        uint64_t num = 0;
        switch (spec.id) {
            case CapabilityId::RANDOM_U64: num = 0x020001a1; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
        }
        *os << "  movq $" << num << ", " << arch.getRegisterName("rax", nullptr) << "\n";
        *os << "  syscall\n";
    }
    emitMacOSStoreResult(cg, i, arch);
}

void MacOSOS::emitErrorCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        *os << "  movq $0, %rax\n";
    }
    emitMacOSStoreResult(cg, i, arch);
}

void MacOSOS::emitDebugCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::DEBUG_LOG) {
            *os << "  movq $0x02000004, %rax\n  movq $2, %rdi\n";
            *os << "  movq " << cg.getValueAsOperand(i.getOperands()[0]->get()) << ", %rsi\n  movq $128, %rdx\n  syscall\n";
        } else {
            cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec);
        }
    }
    emitMacOSStoreResult(cg, i, arch);
}

void MacOSOS::emitModuleCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        uint64_t num = 0;
        switch (spec.id) {
            case CapabilityId::MODULE_LOAD: num = 0x0200003b; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
        }
        *os << "  movq $" << num << ", " << arch.getRegisterName("rax", nullptr) << "\n";
        *os << "  syscall\n";
    }
    emitMacOSStoreResult(cg, i, arch);
}

void MacOSOS::emitTTYCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        uint64_t num = 0;
        switch (spec.id) {
            case CapabilityId::TTY_ISATTY: num = 0x02000036; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
        }
        *os << "  movq $" << num << ", " << arch.getRegisterName("rax", nullptr) << "\n";
        emitMacOSSyscallArgs(cg, i, arch, 3);
        *os << "  syscall\n";
    }
    emitMacOSStoreResult(cg, i, arch);
}

void MacOSOS::emitSecurityCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        uint64_t num = 0;
        switch (spec.id) {
            case CapabilityId::SECURITY_CHMOD: num = 0x0200000f; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
        }
        *os << "  movq $" << num << ", " << arch.getRegisterName("rax", nullptr) << "\n";
        emitMacOSSyscallArgs(cg, i, arch, 3);
        *os << "  syscall\n";
    }
    emitMacOSStoreResult(cg, i, arch);
}

void MacOSOS::emitGPUCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const {
     cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s);
}

void MacOSOS::emitStartFunction(CodeGen& cg, const ArchitectureInfo& arch) {
    if (auto* os = cg.getTextStream()) {
        *os << ".globl _main\n_main:\n  pushq %rbp\n  movq %rsp, %rbp\n  call main\n  popq %rbp\n  ret\n";
    }
}

}
}
