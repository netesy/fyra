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
std::vector<ir::Value*> getArgValues(ir::Instruction& instr) {
    std::vector<ir::Value*> args;
    for (auto& op : instr.getOperands()) args.push_back(op->get());
    return args;
}
}

void MacOSOS::emitIOCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
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
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void MacOSOS::emitFSCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::FS_OPEN:
        case CapabilityId::FS_CREATE: num = 0x02000005; break;
        case CapabilityId::FS_STAT: num = 0x020000bc; break;
        case CapabilityId::FS_REMOVE: num = 0x0200000a; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void MacOSOS::emitMemoryCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::MEMORY_ALLOC:
        case CapabilityId::MEMORY_MAP: num = 0x020000c5; break;
        case CapabilityId::MEMORY_FREE: num = 0x02000049; break;
        case CapabilityId::MEMORY_PROTECT: num = 0x0200004a; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(instr));
}

void MacOSOS::emitProcessCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::PROCESS_EXIT: num = 0x02000001; break;
        case CapabilityId::PROCESS_ABORT: num = 0x02000025; break;
        case CapabilityId::PROCESS_SLEEP: num = 0x02000181; break;
        case CapabilityId::PROCESS_SPAWN: num = 0x02000002; break;
        case CapabilityId::PROCESS_ARGS: num = 0x0200003b; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(i));
}

void MacOSOS::emitThreadCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::THREAD_SPAWN: num = 0x0200018d; break;
        case CapabilityId::THREAD_JOIN: num = 0x0200018e; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(i));
}

void MacOSOS::emitSyncCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::SYNC_MUTEX_LOCK:
                *os << "  # mutex.lock portable stub\n";
                break;
            case CapabilityId::SYNC_MUTEX_UNLOCK:
                *os << "  # mutex.unlock portable stub\n";
                break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
        }
    }
}

void MacOSOS::emitTimeCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::TIME_NOW: num = 0x02000074; break;
        case CapabilityId::TIME_MONOTONIC: num = 0x02000155; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(i));
}

void MacOSOS::emitEventCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::EVENT_POLL: num = 0x020000e8; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(i));
}

void MacOSOS::emitNetCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
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
    arch.emitNativeSyscall(cg, num, getArgValues(i));
}

void MacOSOS::emitIPCCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::IPC_SEND) { emitIOCapability(cg, i, CapabilitySpec{CapabilityId::IO_WRITE, "io.write", CapabilityDomain::IO, 3, 3, true, true}, arch); return; }
    if (spec.id == CapabilityId::IPC_RECV) { emitIOCapability(cg, i, CapabilitySpec{CapabilityId::IO_READ, "io.read", CapabilityDomain::IO, 3, 3, true, true}, arch); return; }
    cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec);
}

void MacOSOS::emitEnvCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::ENV_GET: num = 0x0200003b; break;
        case CapabilityId::ENV_LIST: num = 0x0200003b; break;
        case CapabilityId::ENV_SET: num = 0x0200003b; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(i));
}

void MacOSOS::emitSystemCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::SYSTEM_INFO: num = 0x02000074; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(i));
}

void MacOSOS::emitSignalCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::SIGNAL_SEND: num = 0x02000025; break;
        case CapabilityId::SIGNAL_REGISTER: num = 0x02000030; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(i));
}

void MacOSOS::emitRandomCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::RANDOM_U64: num = 0x020001a1; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(i));
}

void MacOSOS::emitErrorCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) *os << "  # error portable stub\n";
}

void MacOSOS::emitDebugCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::DEBUG_LOG) {
        auto* two = ir::ConstantInt::get(ir::IntegerType::get(64), 2);
        auto* len = ir::ConstantInt::get(ir::IntegerType::get(64), 128);
        arch.emitNativeSyscall(cg, 0x02000004, {two, i.getOperands()[0]->get(), len});
    } else {
        cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec);
    }
}

void MacOSOS::emitModuleCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::MODULE_LOAD: num = 0x0200003b; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(i));
}

void MacOSOS::emitTTYCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::TTY_ISATTY: num = 0x02000036; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(i));
}

void MacOSOS::emitSecurityCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    uint64_t num = 0;
    switch (spec.id) {
        case CapabilityId::SECURITY_CHMOD: num = 0x0200000f; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec); return;
    }
    arch.emitNativeSyscall(cg, num, getArgValues(i));
}

void MacOSOS::emitGPUCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, ArchitectureInfo& a) const {
     cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s);
}

void MacOSOS::emitStartFunction(CodeGen& cg, const ArchitectureInfo& arch) {
    if (auto* os = cg.getTextStream()) {
        *os << ".globl _main\n_main:\n";
        const_cast<ArchitectureInfo&>(arch).emitNativeLibraryCall(cg, "main", {});
        *os << "  ret\n";
    }
}

}
}
