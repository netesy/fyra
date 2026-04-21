#include "codegen/target/TargetInfo.h"
#include "codegen/CodeGen.h"
#include "ir/Type.h"
#include "ir/Instruction.h"
#include <iostream>
#include <array>

namespace codegen {
namespace target {
namespace {
constexpr uint64_t kCapabilityUnsupported = 0x01100001ULL;
constexpr std::array<CapabilitySpec, 84> kCapabilityRegistry{{
    {CapabilityId::IO_READ, "io.read", CapabilityDomain::IO, 3, 3, true, true},
    {CapabilityId::IO_WRITE, "io.write", CapabilityDomain::IO, 3, 3, true, true},
    {CapabilityId::IO_OPEN, "io.open", CapabilityDomain::IO, 2, 3, true, true},
    {CapabilityId::IO_CLOSE, "io.close", CapabilityDomain::IO, 1, 1, true, true},
    {CapabilityId::IO_SEEK, "io.seek", CapabilityDomain::IO, 3, 3, true, true},
    {CapabilityId::IO_STAT, "io.stat", CapabilityDomain::IO, 2, 2, true, true},
    {CapabilityId::IO_FLUSH, "io.flush", CapabilityDomain::IO, 1, 1, true, true},
    {CapabilityId::FS_OPEN, "fs.open", CapabilityDomain::FS, 2, 3, true, true},
    {CapabilityId::FS_CREATE, "fs.create", CapabilityDomain::FS, 2, 3, true, true},
    {CapabilityId::FS_STAT, "fs.stat", CapabilityDomain::FS, 2, 2, true, true},
    {CapabilityId::FS_REMOVE, "fs.remove", CapabilityDomain::FS, 1, 1, true, true},
    {CapabilityId::FS_RENAME, "fs.rename", CapabilityDomain::FS, 2, 2, true, true},
    {CapabilityId::FS_MKDIR, "fs.mkdir", CapabilityDomain::FS, 2, 2, true, true},
    {CapabilityId::FS_RMDIR, "fs.rmdir", CapabilityDomain::FS, 1, 1, true, true},
    {CapabilityId::MEMORY_ALLOC, "memory.alloc", CapabilityDomain::MEMORY, 1, 6, true, true},
    {CapabilityId::MEMORY_FREE, "memory.free", CapabilityDomain::MEMORY, 2, 2, true, true},
    {CapabilityId::MEMORY_MAP, "memory.map", CapabilityDomain::MEMORY, 6, 6, true, true},
    {CapabilityId::MEMORY_PROTECT, "memory.protect", CapabilityDomain::MEMORY, 3, 3, true, true},
    {CapabilityId::MEMORY_USAGE, "memory.usage", CapabilityDomain::MEMORY, 0, 1, true, true},
    {CapabilityId::PROCESS_EXIT, "process.exit", CapabilityDomain::PROCESS, 0, 1, false, false},
    {CapabilityId::PROCESS_ABORT, "process.abort", CapabilityDomain::PROCESS, 0, 0, false, false},
    {CapabilityId::PROCESS_SLEEP, "process.sleep", CapabilityDomain::PROCESS, 1, 1, true, true},
    {CapabilityId::PROCESS_SPAWN, "process.spawn", CapabilityDomain::PROCESS, 2, 4, true, true},
    {CapabilityId::PROCESS_ARGS, "process.args", CapabilityDomain::PROCESS, 0, 2, true, true},
    {CapabilityId::PROCESS_GETPID, "process.getpid", CapabilityDomain::PROCESS, 0, 0, true, false},
    {CapabilityId::THREAD_SPAWN, "thread.spawn", CapabilityDomain::THREAD, 2, 4, true, true},
    {CapabilityId::THREAD_JOIN, "thread.join", CapabilityDomain::THREAD, 1, 1, true, true},
    {CapabilityId::THREAD_DETACH, "thread.detach", CapabilityDomain::THREAD, 1, 1, true, true},
    {CapabilityId::THREAD_YIELD, "thread.yield", CapabilityDomain::THREAD, 0, 0, false, false},
    {CapabilityId::THREAD_GETID, "thread.getid", CapabilityDomain::THREAD, 0, 0, true, false},
    {CapabilityId::SYNC_MUTEX_LOCK, "sync.mutex.lock", CapabilityDomain::SYNC, 1, 1, true, true},
    {CapabilityId::SYNC_MUTEX_UNLOCK, "sync.mutex.unlock", CapabilityDomain::SYNC, 1, 1, true, true},
    {CapabilityId::SYNC_ATOMIC_ADD, "sync.atomic.add", CapabilityDomain::SYNC, 2, 2, true, false},
    {CapabilityId::SYNC_ATOMIC_SUB, "sync.atomic.sub", CapabilityDomain::SYNC, 2, 2, true, false},
    {CapabilityId::SYNC_ATOMIC_CAS, "sync.atomic.cas", CapabilityDomain::SYNC, 3, 3, true, false},
    {CapabilityId::TIME_NOW, "time.now", CapabilityDomain::TIME, 0, 0, true, true},
    {CapabilityId::TIME_MONOTONIC, "time.monotonic", CapabilityDomain::TIME, 0, 0, true, true},
    {CapabilityId::TIME_SLEEP, "time.sleep", CapabilityDomain::TIME, 1, 1, true, true},
    {CapabilityId::EVENT_POLL, "event.poll", CapabilityDomain::EVENT, 1, 4, true, true},
    {CapabilityId::EVENT_CREATE, "event.create", CapabilityDomain::EVENT, 0, 1, true, true},
    {CapabilityId::EVENT_MODIFY, "event.modify", CapabilityDomain::EVENT, 3, 3, true, true},
    {CapabilityId::EVENT_CLOSE, "event.close", CapabilityDomain::EVENT, 1, 1, true, true},
    {CapabilityId::NET_SOCKET, "net.socket", CapabilityDomain::NET, 3, 3, true, true},
    {CapabilityId::NET_CONNECT, "net.connect", CapabilityDomain::NET, 3, 3, true, true},
    {CapabilityId::NET_LISTEN, "net.listen", CapabilityDomain::NET, 2, 2, true, true},
    {CapabilityId::NET_ACCEPT, "net.accept", CapabilityDomain::NET, 3, 3, true, true},
    {CapabilityId::NET_SEND, "net.send", CapabilityDomain::NET, 4, 4, true, true},
    {CapabilityId::NET_RECV, "net.recv", CapabilityDomain::NET, 4, 4, true, true},
    {CapabilityId::NET_CLOSE, "net.close", CapabilityDomain::NET, 1, 1, true, true},
    {CapabilityId::NET_BIND, "net.bind", CapabilityDomain::NET, 2, 2, true, true},
    {CapabilityId::IPC_SEND, "ipc.send", CapabilityDomain::IPC, 2, 4, true, true},
    {CapabilityId::IPC_RECV, "ipc.recv", CapabilityDomain::IPC, 2, 4, true, true},
    {CapabilityId::IPC_CONNECT, "ipc.connect", CapabilityDomain::IPC, 1, 1, true, true},
    {CapabilityId::IPC_LISTEN, "ipc.listen", CapabilityDomain::IPC, 1, 1, true, true},
    {CapabilityId::ENV_GET, "env.get", CapabilityDomain::ENV, 1, 2, true, true},
    {CapabilityId::ENV_SET, "env.set", CapabilityDomain::ENV, 2, 2, true, true},
    {CapabilityId::ENV_LIST, "env.list", CapabilityDomain::ENV, 0, 1, true, true},
    {CapabilityId::SYSTEM_INFO, "system.info", CapabilityDomain::SYSTEM, 1, 2, true, true},
    {CapabilityId::SYSTEM_REBOOT, "system.reboot", CapabilityDomain::SYSTEM, 0, 0, false, true},
    {CapabilityId::SYSTEM_SHUTDOWN, "system.shutdown", CapabilityDomain::SYSTEM, 0, 0, false, true},
    {CapabilityId::SIGNAL_SEND, "signal.send", CapabilityDomain::SIGNAL, 2, 2, true, true},
    {CapabilityId::SIGNAL_REGISTER, "signal.register", CapabilityDomain::SIGNAL, 2, 2, true, true},
    {CapabilityId::SIGNAL_WAIT, "signal.wait", CapabilityDomain::SIGNAL, 1, 1, true, true},
    {CapabilityId::RANDOM_U64, "random.u64", CapabilityDomain::RANDOM, 0, 0, true, true},
    {CapabilityId::RANDOM_BYTES, "random.bytes", CapabilityDomain::RANDOM, 2, 2, true, true},
    {CapabilityId::ERROR_GET, "error.get", CapabilityDomain::ERROR, 0, 0, true, true},
    {CapabilityId::ERROR_STR, "error.str", CapabilityDomain::ERROR, 1, 2, true, true},
    {CapabilityId::DEBUG_LOG, "debug.log", CapabilityDomain::DEBUG, 1, 2, true, true},
    {CapabilityId::DEBUG_BREAK, "debug.break", CapabilityDomain::DEBUG, 0, 0, false, false},
    {CapabilityId::DEBUG_TRACE, "debug.trace", CapabilityDomain::DEBUG, 0, 1, true, true},
    {CapabilityId::MODULE_LOAD, "module.load", CapabilityDomain::MODULE, 1, 2, true, true},
    {CapabilityId::MODULE_UNLOAD, "module.unload", CapabilityDomain::MODULE, 1, 1, true, true},
    {CapabilityId::MODULE_GETSYM, "module.getsym", CapabilityDomain::MODULE, 2, 2, true, true},
    {CapabilityId::TTY_ISATTY, "tty.isatty", CapabilityDomain::TTY, 1, 1, true, true},
    {CapabilityId::TTY_GETSIZE, "tty.getsize", CapabilityDomain::TTY, 1, 2, true, true},
    {CapabilityId::TTY_SETMODE, "tty.setmode", CapabilityDomain::TTY, 2, 2, true, true},
    {CapabilityId::SECURITY_CHMOD, "security.chmod", CapabilityDomain::SECURITY, 2, 2, true, true},
    {CapabilityId::SECURITY_CHOWN, "security.chown", CapabilityDomain::SECURITY, 3, 3, true, true},
    {CapabilityId::SECURITY_GETUID, "security.getuid", CapabilityDomain::SECURITY, 0, 0, true, false},
    {CapabilityId::GPU_COMPUTE, "gpu.compute", CapabilityDomain::GPU, 2, 8, true, true},
    {CapabilityId::GPU_MALLOC, "gpu.malloc", CapabilityDomain::GPU, 1, 2, true, true},
    {CapabilityId::GPU_MEMCPY, "gpu.memcpy", CapabilityDomain::GPU, 3, 3, true, true}
}};
}

const CapabilitySpec* TargetInfo::findCapability(std::string_view name) const {
    for (const auto& spec : kCapabilityRegistry) {
        if (name == spec.name) return &spec;
    }
    return nullptr;
}

bool TargetInfo::supportsCapability(const CapabilitySpec&) const {
    return false;
}

bool TargetInfo::validateCapability(ir::Instruction& instr, const CapabilitySpec& spec) const {
    const auto argc = static_cast<int>(instr.getOperands().size());
    if (argc < spec.minArgs || argc > spec.maxArgs) return false;
    const bool hasReturn = instr.getType() && instr.getType()->getTypeID() != ir::Type::VoidTyID;
    if (spec.returnsValue != hasReturn) return false;
    return supportsCapability(spec);
}

void TargetInfo::emitUnsupportedCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec* spec) const {
    uint64_t domain = spec ? static_cast<uint64_t>(spec->domain) : static_cast<uint64_t>(CapabilityDomain::SYSTEM);
    uint64_t category_unsupported = 0x01;
    uint64_t code = spec ? static_cast<uint64_t>(spec->id) : 0;
    uint64_t error = (domain << 24) | (category_unsupported << 16) | code;

    if (auto* os = cg.getTextStream()) {
        std::string rax = getRegisterName("rax", instr.getType());
        *os << "  movq $" << error << ", " << rax << "\n";
        if (instr.getType() && instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  movq " << rax << ", " << cg.getValueAsOperand(&instr) << "\n";
        }
    }
}

void TargetInfo::emitDomainCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    switch (spec.domain) {
        case CapabilityDomain::IO: emitIOCapability(cg, instr, spec); break;
        case CapabilityDomain::FS: emitFSCapability(cg, instr, spec); break;
        case CapabilityDomain::MEMORY: emitMemoryCapability(cg, instr, spec); break;
        case CapabilityDomain::PROCESS: emitProcessCapability(cg, instr, spec); break;
        case CapabilityDomain::THREAD: emitThreadCapability(cg, instr, spec); break;
        case CapabilityDomain::SYNC: emitSyncCapability(cg, instr, spec); break;
        case CapabilityDomain::TIME: emitTimeCapability(cg, instr, spec); break;
        case CapabilityDomain::EVENT: emitEventCapability(cg, instr, spec); break;
        case CapabilityDomain::NET: emitNetCapability(cg, instr, spec); break;
        case CapabilityDomain::IPC: emitIPCCapability(cg, instr, spec); break;
        case CapabilityDomain::ENV: emitEnvCapability(cg, instr, spec); break;
        case CapabilityDomain::SYSTEM: emitSystemCapability(cg, instr, spec); break;
        case CapabilityDomain::SIGNAL: emitSignalCapability(cg, instr, spec); break;
        case CapabilityDomain::RANDOM: emitRandomCapability(cg, instr, spec); break;
        case CapabilityDomain::ERROR: emitErrorCapability(cg, instr, spec); break;
        case CapabilityDomain::DEBUG: emitDebugCapability(cg, instr, spec); break;
        case CapabilityDomain::MODULE: emitModuleCapability(cg, instr, spec); break;
        case CapabilityDomain::TTY: emitTTYCapability(cg, instr, spec); break;
        case CapabilityDomain::SECURITY: emitSecurityCapability(cg, instr, spec); break;
        case CapabilityDomain::GPU: emitGPUCapability(cg, instr, spec); break;
    }
}

void TargetInfo::emitExternCall(CodeGen& cg, ir::Instruction& instr) {
    auto* ei = dynamic_cast<ir::ExternCallInstruction*>(&instr);
    if (!ei) return;
    const auto* spec = findCapability(ei->getCapability());
    if (!spec || !validateCapability(instr, *spec)) {
        emitUnsupportedCapability(cg, instr, spec);
        return;
    }
    emitDomainCapability(cg, instr, *spec);
}

void TargetInfo::emitIOCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitFSCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitMemoryCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitProcessCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitThreadCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitSyncCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitTimeCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitEventCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitNetCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitIPCCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitEnvCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitSystemCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitSignalCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitRandomCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitErrorCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitDebugCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitModuleCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitTTYCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitSecurityCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitGPUCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }

SIMDContext TargetInfo::createSIMDContext(const ir::VectorType* type) const {
    SIMDContext ctx;
    ctx.vectorWidth = type->getBitWidth();
    ctx.vectorType = const_cast<ir::VectorType*>(type);
    return ctx;
}

std::string TargetInfo::getVectorRegister(const std::string& baseReg, unsigned) const {
    return baseReg;
}

std::string TargetInfo::getVectorInstruction(const std::string& baseInstr, const SIMDContext&) const {
    return baseInstr;
}

std::string TargetInfo::formatConstant(const ir::ConstantInt* C) const {
    return getImmediatePrefix() + std::to_string(C->getValue());
}

int32_t TargetInfo::getStackOffset(const CodeGen& cg, ir::Value* val) const {
    auto it = cg.getStackOffsets().find(val);
    if (it != cg.getStackOffsets().end()) return it->second;
    return 0;
}

}
}
