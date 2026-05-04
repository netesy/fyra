#include "target/os/windows/WindowsOS.h"
#include "codegen/CodeGen.h"
#include "target/core/ArchitectureInfo.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include "codegen/asm/Assembler.h"
#include <ostream>

namespace target {

namespace {
std::vector<ir::Value*> getArgValues(ir::Instruction& instr) {
    std::vector<ir::Value*> args;
    for (auto& op : instr.getOperands()) args.push_back(op->get());
    return args;
}
}

bool WindowsOS::supportsCapability(const CapabilitySpec& spec) const {
    if (spec.domain == CapabilityDomain::GPU) return true;
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
        case CapabilityId::MEMORY_ALLOC:
        case CapabilityId::MEMORY_FREE:
        case CapabilityId::MEMORY_MAP:
        case CapabilityId::MEMORY_PROTECT:
        case CapabilityId::PROCESS_EXIT:
        case CapabilityId::PROCESS_ABORT:
        case CapabilityId::PROCESS_SLEEP:
        case CapabilityId::PROCESS_SPAWN:
        case CapabilityId::PROCESS_ARGS:
        case CapabilityId::THREAD_SPAWN:
        case CapabilityId::THREAD_JOIN:
        case CapabilityId::SYNC_MUTEX_LOCK:
        case CapabilityId::SYNC_MUTEX_UNLOCK:
        case CapabilityId::TIME_NOW:
        case CapabilityId::TIME_MONOTONIC:
        case CapabilityId::EVENT_POLL:
        case CapabilityId::NET_SOCKET:
        case CapabilityId::NET_CONNECT:
        case CapabilityId::NET_LISTEN:
        case CapabilityId::NET_ACCEPT:
        case CapabilityId::NET_SEND:
        case CapabilityId::NET_RECV:
        case CapabilityId::IPC_SEND:
        case CapabilityId::IPC_RECV:
        case CapabilityId::ENV_GET:
        case CapabilityId::ENV_SET:
        case CapabilityId::ENV_LIST:
        case CapabilityId::SYSTEM_INFO:
        case CapabilityId::SIGNAL_SEND:
        case CapabilityId::SIGNAL_REGISTER:
        case CapabilityId::RANDOM_U64:
        case CapabilityId::ERROR_GET:
        case CapabilityId::DEBUG_LOG:
        case CapabilityId::MODULE_LOAD:
        case CapabilityId::TTY_ISATTY:
        case CapabilityId::SECURITY_CHMOD:
            return true;
        default:
            return false;
    }
}

void WindowsOS::emitIOCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::IO_WRITE: func = "WriteFile"; break;
        case CapabilityId::IO_READ: func = "ReadFile"; break;
        case CapabilityId::IO_OPEN: func = "CreateFileA"; break;
        case CapabilityId::IO_CLOSE: func = "CloseHandle"; break;
        case CapabilityId::IO_SEEK: func = "SetFilePointerEx"; break;
        case CapabilityId::IO_STAT: func = "GetFileInformationByHandle"; break;
        case CapabilityId::IO_FLUSH: func = "FlushFileBuffers"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitFSCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::FS_OPEN:
        case CapabilityId::FS_CREATE: func = "CreateFileA"; break;
        case CapabilityId::FS_STAT: func = "GetFileAttributesExA"; break;
        case CapabilityId::FS_REMOVE: func = "DeleteFileA"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitMemoryCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::MEMORY_ALLOC: func = "VirtualAlloc"; break;
        case CapabilityId::MEMORY_MAP: func = "MapViewOfFile"; break;
        case CapabilityId::MEMORY_FREE: func = "VirtualFree"; break;
        case CapabilityId::MEMORY_PROTECT: func = "VirtualProtect"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitProcessCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::PROCESS_EXIT: func = "ExitProcess"; break;
        case CapabilityId::PROCESS_ABORT: func = "abort"; break;
        case CapabilityId::PROCESS_SLEEP: func = "Sleep"; break;
        case CapabilityId::PROCESS_SPAWN: func = "CreateProcessA"; break;
        case CapabilityId::PROCESS_ARGS: func = "GetCommandLineA"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitThreadCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::THREAD_SPAWN: func = "CreateThread"; break;
        case CapabilityId::THREAD_JOIN: func = "WaitForSingleObject"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitSyncCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::SYNC_MUTEX_LOCK) {
            *os << "  # mutex.lock portable stub\n";
        } else if (spec.id == CapabilityId::SYNC_MUTEX_UNLOCK) {
            *os << "  # mutex.unlock portable stub\n";
        } else {
            cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
            return;
        }
    }
}

void WindowsOS::emitTimeCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::TIME_NOW: func = "GetSystemTimeAsFileTime"; break;
        case CapabilityId::TIME_MONOTONIC: func = "QueryPerformanceCounter"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitEventCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::EVENT_POLL) { cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return; }
    arch.emitNativeLibraryCall(cg, "WaitForMultipleObjects", getArgValues(instr));
}

void WindowsOS::emitNetCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::NET_SOCKET: func = "socket"; break;
        case CapabilityId::NET_CONNECT: func = "connect"; break;
        case CapabilityId::NET_LISTEN: func = "listen"; break;
        case CapabilityId::NET_ACCEPT: func = "accept"; break;
        case CapabilityId::NET_SEND: func = "send"; break;
        case CapabilityId::NET_RECV: func = "recv"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitIPCCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::IPC_SEND) {
        emitIOCapability(cg, instr, CapabilitySpec{CapabilityId::IO_WRITE, "io.write", CapabilityDomain::IO, 3, 3, true, true}, arch);
        return;
    }
    if (spec.id == CapabilityId::IPC_RECV) {
        emitIOCapability(cg, instr, CapabilitySpec{CapabilityId::IO_READ, "io.read", CapabilityDomain::IO, 3, 3, true, true}, arch);
        return;
    }
    cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
}

void WindowsOS::emitEnvCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::ENV_GET: func = "GetEnvironmentVariableA"; break;
        case CapabilityId::ENV_LIST: func = "GetEnvironmentStringsA"; break;
        case CapabilityId::ENV_SET: func = "SetEnvironmentVariableA"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitSystemCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::SYSTEM_INFO) { cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return; }
    arch.emitNativeLibraryCall(cg, "GetSystemInfo", getArgValues(instr));
}

void WindowsOS::emitSignalCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::SIGNAL_SEND: func = "GenerateConsoleCtrlEvent"; break;
        case CapabilityId::SIGNAL_REGISTER: func = "SetConsoleCtrlHandler"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitRandomCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::RANDOM_U64) { cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return; }
    arch.emitNativeLibraryCall(cg, "BCryptGenRandom", getArgValues(instr));
}

void WindowsOS::emitErrorCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::ERROR_GET) { cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return; }
    arch.emitNativeLibraryCall(cg, "GetLastError", getArgValues(instr));
}

void WindowsOS::emitDebugCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::DEBUG_LOG) { cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return; }
    arch.emitNativeLibraryCall(cg, "OutputDebugStringA", getArgValues(instr));
}

void WindowsOS::emitModuleCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::MODULE_LOAD) { cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return; }
    arch.emitNativeLibraryCall(cg, "LoadLibraryA", getArgValues(instr));
}

void WindowsOS::emitTTYCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::TTY_ISATTY) { cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return; }
    arch.emitNativeLibraryCall(cg, "GetConsoleMode", getArgValues(instr));
}

void WindowsOS::emitSecurityCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::SECURITY_CHMOD) { cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return; }
    arch.emitNativeLibraryCall(cg, "_chmod", getArgValues(instr));
}

void WindowsOS::emitGPUCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, ArchitectureInfo& a) const {
     cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s);
}

void WindowsOS::emitHeader(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << ".section .data\n.align 8\nheap_ptr:\n  .quad __fyra_heap\n";
        *os << ".section .bss\n.align 16\n__fyra_heap:\n  .zero 1048576\n";
        *os << ".text\n";
    }
}

void WindowsOS::emitStartFunction(CodeGen& cg, const ArchitectureInfo& arch) {
    if (auto* os = cg.getTextStream()) {
        *os << ".globl _start\n_start:\n";
        const_cast<ArchitectureInfo&>(arch).emitNativeLibraryCall(cg, "main", {});
        const_cast<ArchitectureInfo&>(arch).emitNativeLibraryCall(cg, "ExitProcess", {});
    } else {
        auto& as = cg.getAssembler();
        CodeGen::SymbolInfo s; s.name = "_start"; s.sectionName = ".text"; s.value = as.getCodeSize(); s.type = 2; s.binding = 1; cg.addSymbol(s);
        const_cast<ArchitectureInfo&>(arch).emitNativeLibraryCall(cg, "main", {});
        const_cast<ArchitectureInfo&>(arch).emitNativeLibraryCall(cg, "ExitProcess", {});
    }
}

}
