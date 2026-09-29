#include "target/os/freebsd/FreeBSDOS.h"
#include "codegen/CodeGen.h"
#include "target/core/ArchitectureInfo.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include <ostream>

namespace target {

uint64_t FreeBSDOS::getSyscallNumber(ir::SyscallId id) const {
    switch (id) {
        case ir::SyscallId::Read: return 3;          // sys_read
        case ir::SyscallId::Write: return 4;         // sys_write
        case ir::SyscallId::OpenAt: return 499;      // sys_openat
        case ir::SyscallId::Close: return 6;         // sys_close
        case ir::SyscallId::Exit: return 1;          // sys_exit
        case ir::SyscallId::MMap: return 477;        // sys_mmap
        case ir::SyscallId::MUnmap: return 73;       // sys_munmap
        case ir::SyscallId::GetPid: return 20;       // sys_getpid
        case ir::SyscallId::Fork: return 2;          // sys_fork
        case ir::SyscallId::Execve: return 59;       // sys_execve
        case ir::SyscallId::NanoSleep: return 240;   // sys_nanosleep
        case ir::SyscallId::ClockGetTime: return 232;// sys_clock_gettime
        default: return 0;
    }
}

std::string FreeBSDOS::formatFunctionTypeDirective(const std::string& name, const ArchitectureInfo& arch) const {
    return ".type " + name + ", " + arch.getFunctionTypeSpecifier();
}

std::string FreeBSDOS::formatFunctionSizeDirective(const std::string& name) const {
    return ".size " + name + ", .-" + name;
}

void FreeBSDOS::emitHeader(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "  # FreeBSD OS Target Header\n";
    }
}

void FreeBSDOS::emitStartFunction(CodeGen& cg, const ArchitectureInfo& arch) {
    std::vector<ir::Value*> emptyArgs;
    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, getSyscallNumber(ir::SyscallId::Exit), emptyArgs);
}

void FreeBSDOS::emitIOCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    uint64_t sysNum = 4; // sys_write
    if (spec.id == CapabilityId::IO_READ) sysNum = 3;       // sys_read
    else if (spec.id == CapabilityId::IO_WRITE) sysNum = 4;  // sys_write
    else if (spec.id == CapabilityId::IO_OPEN) sysNum = 5;   // sys_open
    else if (spec.id == CapabilityId::IO_CLOSE) sysNum = 6;  // sys_close
    else if (spec.id == CapabilityId::IO_SEEK) sysNum = 478; // sys_lseek
    else if (spec.id == CapabilityId::IO_STAT) sysNum = 551; // sys_fstatat

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, sysNum, args);
}

void FreeBSDOS::emitFSCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    uint64_t sysNum = 5; // sys_open
    if (spec.id == CapabilityId::FS_OPEN) sysNum = 5;
    else if (spec.id == CapabilityId::FS_CREATE) sysNum = 5;
    else if (spec.id == CapabilityId::FS_STAT) sysNum = 551;
    else if (spec.id == CapabilityId::FS_REMOVE) sysNum = 10; // sys_unlink
    else if (spec.id == CapabilityId::FS_RENAME) sysNum = 128; // sys_rename
    else if (spec.id == CapabilityId::FS_MKDIR) sysNum = 136;  // sys_mkdir
    else if (spec.id == CapabilityId::FS_RMDIR) sysNum = 137;  // sys_rmdir

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, sysNum, args);
}

void FreeBSDOS::emitMemoryCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    uint64_t sysNum = 477; // sys_mmap
    if (spec.id == CapabilityId::MEMORY_FREE) sysNum = 73; // sys_munmap
    else if (spec.id == CapabilityId::MEMORY_PROTECT) sysNum = 74; // sys_mprotect

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, sysNum, args);
}

void FreeBSDOS::emitProcessCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    uint64_t sysNum = 1; // sys_exit
    if (spec.id == CapabilityId::PROCESS_EXIT) sysNum = 1;
    else if (spec.id == CapabilityId::PROCESS_ABORT) sysNum = 1;
    else if (spec.id == CapabilityId::PROCESS_SLEEP) sysNum = 240; // sys_nanosleep
    else if (spec.id == CapabilityId::PROCESS_SPAWN) sysNum = 59;  // sys_execve
    else if (spec.id == CapabilityId::PROCESS_GETPID) sysNum = 20; // sys_getpid

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, sysNum, args);
}

void FreeBSDOS::emitThreadCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    uint64_t sysNum = 455; // sys_thr_new
    if (spec.id == CapabilityId::THREAD_SPAWN) sysNum = 455;
    else if (spec.id == CapabilityId::THREAD_JOIN) sysNum = 431;  // sys_thr_exit
    else if (spec.id == CapabilityId::THREAD_GETID) sysNum = 432; // sys_thr_self
    else if (spec.id == CapabilityId::THREAD_YIELD) sysNum = 331; // sys_sched_yield

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, sysNum, args);
}

void FreeBSDOS::emitSyncCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, 454, args); // sys_umtx_op
}

void FreeBSDOS::emitTimeCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    uint64_t sysNum = 232; // sys_clock_gettime
    if (spec.id == CapabilityId::TIME_SLEEP) sysNum = 240; // sys_nanosleep

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, sysNum, args);
}

void FreeBSDOS::emitEventCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    uint64_t sysNum = 362; // sys_kqueue
    if (spec.id == CapabilityId::EVENT_POLL || spec.id == CapabilityId::EVENT_MODIFY) sysNum = 363; // sys_kevent
    else if (spec.id == CapabilityId::EVENT_CLOSE) sysNum = 6; // sys_close

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, sysNum, args);
}

void FreeBSDOS::emitNetCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    uint64_t sysNum = 97; // sys_socket
    if (spec.id == CapabilityId::NET_SOCKET) sysNum = 97;
    else if (spec.id == CapabilityId::NET_CONNECT) sysNum = 98;
    else if (spec.id == CapabilityId::NET_LISTEN) sysNum = 106;
    else if (spec.id == CapabilityId::NET_ACCEPT) sysNum = 30;
    else if (spec.id == CapabilityId::NET_BIND) sysNum = 104;
    else if (spec.id == CapabilityId::NET_SEND) sysNum = 4; // sys_write
    else if (spec.id == CapabilityId::NET_RECV) sysNum = 3; // sys_read
    else if (spec.id == CapabilityId::NET_CLOSE) sysNum = 6; // sys_close

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, sysNum, args);
}

void FreeBSDOS::emitIPCCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, 255, args); // sys_minherit / sysvipc
}

void FreeBSDOS::emitEnvCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, 202, args); // sys___sysctl
}

void FreeBSDOS::emitSystemCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    if (spec.id == CapabilityId::SYSTEM_REBOOT || spec.id == CapabilityId::SYSTEM_SHUTDOWN) {
        const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, 55, args); // sys_reboot
    } else {
        const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, 202, args); // sys___sysctl
    }
}

void FreeBSDOS::emitSignalCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    uint64_t sysNum = 416; // sys_sigaction
    if (spec.id == CapabilityId::SIGNAL_SEND) sysNum = 37; // sys_kill
    else if (spec.id == CapabilityId::SIGNAL_WAIT) sysNum = 345; // sys_sigtimedwait

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, sysNum, args);
}

void FreeBSDOS::emitRandomCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, 563, args); // sys_getrandom
}

void FreeBSDOS::emitErrorCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, 20, args); // sys_getpid
}

void FreeBSDOS::emitDebugCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, 26, args); // sys_ptrace
}

void FreeBSDOS::emitModuleCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, 304, args); // sys_modnext
}

void FreeBSDOS::emitTTYCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, 54, args); // sys_ioctl
}

void FreeBSDOS::emitSecurityCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    uint64_t sysNum = 24; // sys_getuid
    if (spec.id == CapabilityId::SECURITY_CHMOD) sysNum = 15; // sys_chmod
    else if (spec.id == CapabilityId::SECURITY_CHOWN) sysNum = 16; // sys_chown

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, sysNum, args);
}

void FreeBSDOS::emitGPUCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const {
    std::vector<ir::Value*> args;
    for (const auto& op : i.getOperands()) {
        if (op) args.push_back(op->get());
    }

    const_cast<ArchitectureInfo&>(arch).emitNativeSyscall(cg, 54, args); // sys_ioctl (DRM/KMS)
}

}
