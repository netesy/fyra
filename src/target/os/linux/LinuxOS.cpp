#include "target/os/linux/LinuxOS.h"
#include "codegen/CodeGen.h"
#include "target/core/ArchitectureInfo.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include <ostream>

namespace target {

uint64_t LinuxOS::getSyscallNumber(ir::SyscallId id) const {
    switch(id) {
        case ir::SyscallId::Exit: return 60; case ir::SyscallId::Write: return 1; case ir::SyscallId::Read: return 0; case ir::SyscallId::OpenAt: return 2; case ir::SyscallId::Close: return 3; default: return 0;
    }
}

namespace {
std::vector<ir::Value*> getArgValues(ir::Instruction& instr) {
    std::vector<ir::Value*> args;
    for (auto& op : instr.getOperands()) args.push_back(op->get());
    return args;
}
void emitStoreExternResult(CodeGen& cg, ir::Instruction& instr, const ArchitectureInfo& arch) {
    if (auto* os = cg.getTextStream()) {
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  # result in rax/x0/a0 handled by architecture or calling convention\n";
            // For now, assume it's moved by the caller of emitXCapability or handled in emitNativeSyscall
        }
    }
}
}

bool LinuxOS::supportsCapability(const CapabilitySpec& spec) const {
    switch (spec.id) {
        case CapabilityId::IO_READ:
        case CapabilityId::IO_WRITE:
        case CapabilityId::IO_OPEN:
        case CapabilityId::IO_CLOSE:
        case CapabilityId::IO_SEEK:
        case CapabilityId::IO_STAT:
        case CapabilityId::IO_FLUSH:
        case CapabilityId::FS_OPEN:
        case CapabilityId::FS_CREATE:
        case CapabilityId::FS_STAT:
        case CapabilityId::FS_REMOVE:
        case CapabilityId::FS_RENAME:
        case CapabilityId::FS_MKDIR:
        case CapabilityId::FS_RMDIR:
        case CapabilityId::MEMORY_ALLOC:
        case CapabilityId::MEMORY_FREE:
        case CapabilityId::MEMORY_MAP:
        case CapabilityId::MEMORY_PROTECT:
        case CapabilityId::MEMORY_USAGE:
        case CapabilityId::PROCESS_EXIT:
        case CapabilityId::PROCESS_ABORT:
        case CapabilityId::PROCESS_SLEEP:
        case CapabilityId::PROCESS_SPAWN:
        case CapabilityId::PROCESS_ARGS:
        case CapabilityId::PROCESS_GETPID:
        case CapabilityId::THREAD_SPAWN:
        case CapabilityId::THREAD_JOIN:
        case CapabilityId::THREAD_DETACH:
        case CapabilityId::THREAD_YIELD:
        case CapabilityId::THREAD_GETID:
        case CapabilityId::SYNC_MUTEX_LOCK:
        case CapabilityId::SYNC_MUTEX_UNLOCK:
        case CapabilityId::SYNC_ATOMIC_ADD:
        case CapabilityId::SYNC_ATOMIC_SUB:
        case CapabilityId::SYNC_ATOMIC_CAS:
        case CapabilityId::TIME_NOW:
        case CapabilityId::TIME_MONOTONIC:
        case CapabilityId::TIME_SLEEP:
        case CapabilityId::EVENT_POLL:
        case CapabilityId::EVENT_CREATE:
        case CapabilityId::EVENT_MODIFY:
        case CapabilityId::EVENT_CLOSE:
        case CapabilityId::NET_SOCKET:
        case CapabilityId::NET_CONNECT:
        case CapabilityId::NET_LISTEN:
        case CapabilityId::NET_ACCEPT:
        case CapabilityId::NET_SEND:
        case CapabilityId::NET_RECV:
        case CapabilityId::NET_CLOSE:
        case CapabilityId::NET_BIND:
        case CapabilityId::IPC_SEND:
        case CapabilityId::IPC_RECV:
        case CapabilityId::IPC_CONNECT:
        case CapabilityId::IPC_LISTEN:
        case CapabilityId::ENV_GET:
        case CapabilityId::ENV_SET:
        case CapabilityId::ENV_LIST:
        case CapabilityId::SYSTEM_INFO:
        case CapabilityId::SYSTEM_REBOOT:
        case CapabilityId::SYSTEM_SHUTDOWN:
        case CapabilityId::SIGNAL_SEND:
        case CapabilityId::SIGNAL_REGISTER:
        case CapabilityId::SIGNAL_WAIT:
        case CapabilityId::RANDOM_U64:
        case CapabilityId::RANDOM_BYTES:
        case CapabilityId::ERROR_GET:
        case CapabilityId::ERROR_STR:
        case CapabilityId::DEBUG_LOG:
        case CapabilityId::DEBUG_BREAK:
        case CapabilityId::DEBUG_TRACE:
        case CapabilityId::MODULE_LOAD:
        case CapabilityId::MODULE_UNLOAD:
        case CapabilityId::MODULE_GETSYM:
        case CapabilityId::TTY_ISATTY:
        case CapabilityId::TTY_GETSIZE:
        case CapabilityId::TTY_SETMODE:
        case CapabilityId::SECURITY_CHMOD:
        case CapabilityId::SECURITY_CHOWN:
        case CapabilityId::SECURITY_GETUID:
        case CapabilityId::GPU_COMPUTE:
        case CapabilityId::GPU_MALLOC:
        case CapabilityId::GPU_MEMCPY:
            return true;
        default:
            return false;
    }
}

void LinuxOS::emitIOCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::IO_WRITE: num = 1; break;
        case CapabilityId::IO_READ: num = 0; break;
        case CapabilityId::IO_OPEN: num = 2; break;
        case CapabilityId::IO_CLOSE: num = 3; break;
        case CapabilityId::IO_SEEK: num = 8; break;
        case CapabilityId::IO_STAT: num = 4; break;
        case CapabilityId::IO_FLUSH: num = 74; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void LinuxOS::emitFSCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::FS_OPEN:
        case CapabilityId::FS_CREATE: num = 2; break;
        case CapabilityId::FS_STAT: num = 4; break;
        case CapabilityId::FS_REMOVE: num = 87; break;
        case CapabilityId::FS_RENAME: num = 82; break;
        case CapabilityId::FS_MKDIR: num = 83; break;
        case CapabilityId::FS_RMDIR: num = 84; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void LinuxOS::emitMemoryCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::MEMORY_ALLOC:
            num = 9;
            if (instr.getOperands().size() == 1) {
                auto* zero = ir::ConstantInt::get(ir::IntegerType::get(64), 0);
                auto* prot = ir::ConstantInt::get(ir::IntegerType::get(64), 3);
                auto* flags = ir::ConstantInt::get(ir::IntegerType::get(64), 34);
                auto* fd = ir::ConstantInt::get(ir::IntegerType::get(64), -1);
                arch.emitNativeSyscall(cg, 9, {zero, instr.getOperands()[0]->get(), prot, flags, fd, zero});
                return;
            }
            break;
        case CapabilityId::MEMORY_MAP: num = 9; break;
        case CapabilityId::MEMORY_FREE: num = 11; break;
        case CapabilityId::MEMORY_PROTECT: num = 10; break;
        case CapabilityId::MEMORY_USAGE:
            arch.emitNativeSyscall(cg, 99, getArgValues(instr)); // sysinfo
            return;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void LinuxOS::emitProcessCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::PROCESS_SLEEP) {
        arch.emitNativeLibraryCall(cg, "sleep", getArgValues(instr));
        return;
    }
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::PROCESS_EXIT: num = 60; break;
        case CapabilityId::PROCESS_ABORT: num = 62; break; // Simplified
        case CapabilityId::PROCESS_GETPID: num = 39; break;
        case CapabilityId::PROCESS_SPAWN: num = 57; break;
        case CapabilityId::PROCESS_ARGS:
            arch.emitNativeSyscall(cg, 2, getArgValues(instr)); // open /proc/self/cmdline
            return;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void LinuxOS::emitThreadCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::THREAD_SPAWN: num = 56; break;
        case CapabilityId::THREAD_JOIN: num = 61; break;
        case CapabilityId::THREAD_DETACH:
            arch.emitNativeSyscall(cg, 202, getArgValues(instr)); // futex
            return;
        case CapabilityId::THREAD_YIELD: num = 24; break;
        case CapabilityId::THREAD_GETID: num = 186; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void LinuxOS::emitSyncCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 202; // futex
    switch (spec.id) {
        case CapabilityId::SYNC_MUTEX_LOCK:
        case CapabilityId::SYNC_MUTEX_UNLOCK:
        case CapabilityId::SYNC_ATOMIC_ADD:
        case CapabilityId::SYNC_ATOMIC_SUB:
        case CapabilityId::SYNC_ATOMIC_CAS:
            arch.emitNativeSyscall(cg, num, getArgValues(instr));
            break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
}

void LinuxOS::emitTimeCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::TIME_SLEEP) {
        arch.emitNativeLibraryCall(cg, "sleep", getArgValues(instr));
        return;
    }
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::TIME_NOW: num = 228; break;
        case CapabilityId::TIME_MONOTONIC: num = 228; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void LinuxOS::emitEventCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::EVENT_POLL: num = 232; break;
        case CapabilityId::EVENT_CREATE: num = 213; break;
        case CapabilityId::EVENT_MODIFY: num = 233; break;
        case CapabilityId::EVENT_CLOSE: num = 3; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void LinuxOS::emitNetCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::NET_SOCKET: num = 41; break;
        case CapabilityId::NET_CONNECT: num = 42; break;
        case CapabilityId::NET_LISTEN: num = 50; break;
        case CapabilityId::NET_ACCEPT: num = 43; break;
        case CapabilityId::NET_SEND: num = 44; break;
        case CapabilityId::NET_RECV: num = 45; break;
        case CapabilityId::NET_CLOSE: num = 3; break;
        case CapabilityId::NET_BIND: num = 49; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void LinuxOS::emitIPCCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::IPC_SEND) { emitIOCapability(cg, instr, CapabilitySpec{CapabilityId::IO_WRITE, "io.write", CapabilityDomain::IO, 3, 3, true, true}, arch); return; }
    if (spec.id == CapabilityId::IPC_RECV) { emitIOCapability(cg, instr, CapabilitySpec{CapabilityId::IO_READ, "io.read", CapabilityDomain::IO, 3, 3, true, true}, arch); return; }
    if (spec.id == CapabilityId::IPC_CONNECT) { arch.emitNativeSyscall(cg, 42, getArgValues(instr)); return; }
    if (spec.id == CapabilityId::IPC_LISTEN) { arch.emitNativeSyscall(cg, 50, getArgValues(instr)); return; }
    cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
}

void LinuxOS::emitEnvCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    switch (spec.id) {
        case CapabilityId::ENV_GET:
        case CapabilityId::ENV_LIST:
        case CapabilityId::ENV_SET:
            arch.emitNativeSyscall(cg, 2, getArgValues(instr)); // open /proc/self/environ
            break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
}

void LinuxOS::emitSystemCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::SYSTEM_INFO: num = 63; break;
        case CapabilityId::SYSTEM_REBOOT: num = 169; break;
        case CapabilityId::SYSTEM_SHUTDOWN: num = 169; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void LinuxOS::emitSignalCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::SIGNAL_SEND: num = 62; break;
        case CapabilityId::SIGNAL_REGISTER: num = 13; break;
        case CapabilityId::SIGNAL_WAIT: num = 34; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void LinuxOS::emitRandomCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::RANDOM_U64: num = 318; break;
        case CapabilityId::RANDOM_BYTES: num = 318; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void LinuxOS::emitErrorCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    switch (spec.id) {
        case CapabilityId::ERROR_GET:
        case CapabilityId::ERROR_STR:
            arch.emitNativeSyscall(cg, 39, getArgValues(instr)); // getpid / errno representation
            break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
}

void LinuxOS::emitDebugCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    switch (spec.id) {
        case CapabilityId::DEBUG_LOG: {
            auto* two = ir::ConstantInt::get(ir::IntegerType::get(64), 2);
            auto* len = ir::ConstantInt::get(ir::IntegerType::get(64), 128);
            arch.emitNativeSyscall(cg, 1, {two, instr.getOperands()[0]->get(), len});
            break;
        }
        case CapabilityId::DEBUG_BREAK:
            arch.emitNativeSyscall(cg, 62, getArgValues(instr)); // kill SIGTRAP
            break;
        case CapabilityId::DEBUG_TRACE:
            arch.emitNativeSyscall(cg, 101, getArgValues(instr)); // ptrace
            break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
}

void LinuxOS::emitModuleCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::MODULE_LOAD: num = 313; break;
        case CapabilityId::MODULE_UNLOAD: num = 176; break;
        case CapabilityId::MODULE_GETSYM:
            arch.emitNativeSyscall(cg, 313, getArgValues(instr)); // init_module/finit_module
            return;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void LinuxOS::emitTTYCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 16; // ioctl
    switch (spec.id) {
        case CapabilityId::TTY_ISATTY: break;
        case CapabilityId::TTY_GETSIZE: break;
        case CapabilityId::TTY_SETMODE: break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void LinuxOS::emitSecurityCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::SECURITY_CHMOD: num = 90; break;
        case CapabilityId::SECURITY_CHOWN: num = 92; break;
        case CapabilityId::SECURITY_GETUID: num = 102; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void LinuxOS::emitGPUCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, ArchitectureInfo& a) const {
    switch (s.id) {
        case CapabilityId::GPU_COMPUTE:
        case CapabilityId::GPU_MALLOC:
        case CapabilityId::GPU_MEMCPY:
            a.emitNativeSyscall(cg, 16, getArgValues(i)); // ioctl DRM/NVIDIA driver
            break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); return;
    }
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
        *os << ".globl _start\n_start:\n";
        *os << "  call main\n";
        *os << "  movq %rax, %rdi\n";
        *os << "  movq $60, %rax\n";
        *os << "  syscall\n";
    } else {
        auto& as = cg.getAssembler();
        CodeGen::SymbolInfo start_sym;
        start_sym.name = "_start";
        start_sym.sectionName = ".text";
        start_sym.value = as.getCodeSize();
        start_sym.type = 2; // STT_FUNC
        start_sym.binding = 1; // STB_GLOBAL
        cg.addSymbol(start_sym);

        // call main
        as.emitByte(0xE8);
        uint64_t off = as.getCodeSize(); as.emitDWord(0);
        cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, "main", ".text"});

        // movq %rax, %rdi
        as.emitBytes({0x48, 0x89, 0xC7});

        // movq $60, %rax
        as.emitBytes({0x48, 0xC7, 0xC0}); as.emitDWord(60);

        // syscall
        as.emitBytes({0x0F, 0x05});
    }
}

}
