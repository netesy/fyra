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
    return true;
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
            if (auto* os = cg.getTextStream()) *os << "  # memory.usage stub\n";
            return;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void LinuxOS::emitProcessCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::PROCESS_EXIT: num = 60; break;
        case CapabilityId::PROCESS_ABORT: num = 62; break; // Simplified
        case CapabilityId::PROCESS_SLEEP: num = 35; break;
        case CapabilityId::PROCESS_GETPID: num = 39; break;
        case CapabilityId::PROCESS_SPAWN: num = 57; break;
        case CapabilityId::PROCESS_ARGS:
            if (auto* os = cg.getTextStream()) *os << "  # process.args stub\n";
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
            if (auto* os = cg.getTextStream()) *os << "  # thread.detach stub\n";
            return;
        case CapabilityId::THREAD_YIELD: num = 24; break;
        case CapabilityId::THREAD_GETID: num = 186; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void LinuxOS::emitSyncCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::SYNC_MUTEX_LOCK:
                *os << "  # mutex.lock portable stub\n";
                break;
            case CapabilityId::SYNC_MUTEX_UNLOCK:
                *os << "  # mutex.unlock portable stub\n";
                break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
}

void LinuxOS::emitTimeCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::TIME_NOW: num = 228; break;
        case CapabilityId::TIME_MONOTONIC: num = 228; break;
        case CapabilityId::TIME_SLEEP: num = 35; break;
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
    cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
}

void LinuxOS::emitEnvCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::ENV_GET:
            case CapabilityId::ENV_LIST:
            case CapabilityId::ENV_SET:
                *os << "  # env portable stub\n";
                break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
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
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::ERROR_GET:
            case CapabilityId::ERROR_STR:
                *os << "  # error portable stub\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
}

void LinuxOS::emitDebugCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::DEBUG_LOG: {
                auto* two = ir::ConstantInt::get(ir::IntegerType::get(64), 2);
                auto* len = ir::ConstantInt::get(ir::IntegerType::get(64), 128);
                arch.emitNativeSyscall(cg, 1, {two, instr.getOperands()[0]->get(), len});
                break;
            }
            case CapabilityId::DEBUG_BREAK: *os << "  # debug.break stub\n"; break;
            case CapabilityId::DEBUG_TRACE: *os << "  # debug.trace stub\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
}

void LinuxOS::emitModuleCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::MODULE_LOAD: num = 313; break;
        case CapabilityId::MODULE_UNLOAD: num = 176; break;
        case CapabilityId::MODULE_GETSYM:
            if (auto* os = cg.getTextStream()) *os << "  # module.getsym stub\n";
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
        *os << ".globl _start\n_start:\n";
        auto* zero = ir::ConstantInt::get(ir::IntegerType::get(64), 0);
        const_cast<ArchitectureInfo&>(arch).emitNativeLibraryCall(cg, "main", {});
        const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, 60, {zero});
    }
}

}
