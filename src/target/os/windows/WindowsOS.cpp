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

void WindowsOS::emitIOCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::IO_WRITE) {
        // io.write(fd, buf, len) — lower to GetStdHandle + WriteFile (no CRT needed).
        // args[0]=fd (1=stdout, 2=stderr), args[1]=buf ptr, args[2]=len
        auto args = getArgValues(instr);
        if (auto* os = cg.getTextStream()) {
            // GetStdHandle(STD_OUTPUT_HANDLE=-11) → handle in rax
            // We map fd==1 → -11, fd==2 → -12 at compile time.
            // For simplicity emit STD_OUTPUT_HANDLE constant (-11 = 0xFFFFFFF5 as DWORD).
            *os << "  sub rsp, 32\n";
            *os << "  mov ecx, 0xFFFFFFF5\n";  // STD_OUTPUT_HANDLE = -11
            *os << "  call GetStdHandle\n";
            *os << "  add rsp, 32\n";
            // rax = handle — now call WriteFile(handle, buf, len, &written, NULL)
            // Push a dummy DWORD for bytes_written on the stack, then call.
            *os << "  sub rsp, 48\n";          // shadow(32) + bytes_written(4) + padding(12)
            *os << "  mov rcx, rax\n";         // arg1: handle
            // arg2: buf
            bool bufIsGlobal = args.size() > 1 && dynamic_cast<ir::GlobalVariable*>(args[1]) != nullptr;
            if (bufIsGlobal)
                *os << "  lea rdx, [rel " << args[1]->getName() << "]\n";
            else if (args.size() > 1)
                *os << "  mov rdx, " << cg.getValueAsOperand(args[1]) << "\n";
            // arg3: len (DWORD — truncate i64 to eax then move to r8d)
            if (args.size() > 2)
                *os << "  mov r8d, dword ptr " << cg.getValueAsOperand(args[2]) << "\n";
            // arg4: &bytes_written — pointer into our extra stack space (offset 40)
            *os << "  lea r9, [rsp + 40]\n";
            // arg5 (stack slot 0): NULL overlap handle (must be at [rsp + 32])
            *os << "  mov qword ptr [rsp + 32], 0\n";
            *os << "  call WriteFile\n";
            *os << "  add rsp, 48\n";
        } else {
            // Binary path
            auto& as = cg.getAssembler();
            as.emitBytes({0x48, 0x83, 0xEC, 0x20}); // sub rsp, 32
            as.emitBytes({0xB9, 0xF5, 0xFF, 0xFF, 0xFF}); // mov ecx, -11
            as.emitByte(0xE8); uint64_t off1 = as.getCodeSize(); as.emitDWord(0);
            cg.addRelocation(::codegen::CodeGen::RelocationInfo{off1, "R_X86_64_PC32", -4, "GetStdHandle", ".text"});
            as.emitBytes({0x48, 0x83, 0xC4, 0x20}); // add rsp, 32

            as.emitBytes({0x48, 0x83, 0xEC, 0x30}); // sub rsp, 48
            as.emitBytes({0x48, 0x89, 0xC1}); // mov rcx, rax

            auto emitRegMem = [&](uint8_t rex, uint8_t opcode, uint8_t reg, int32_t offset) {
                if (rex) as.emitByte(rex);
                as.emitByte(opcode);
                if (offset >= -128 && offset <= 127) { as.emitByte(0x45 | (reg << 3)); as.emitByte((uint8_t)offset); }
                else { as.emitByte(0x85 | (reg << 3)); as.emitDWord(offset); }
            };

            bool bufIsGlobal = args.size() > 1 && dynamic_cast<ir::GlobalVariable*>(args[1]) != nullptr;
            if (bufIsGlobal) {
                as.emitBytes({0x48, 0x8D, 0x15});
                uint64_t off2 = as.getCodeSize(); as.emitDWord(0);
                cg.addRelocation(::codegen::CodeGen::RelocationInfo{off2, "R_X86_64_PC32", -4, args[1]->getName(), ".text"});
            } else if (args.size() > 1) {
                emitRegMem(0x48, 0x8B, 2, cg.getStackOffsets()[args[1]]); // mov rdx, [rbp+offset]
            }

            if (args.size() > 2) {
                if (auto constInt = dynamic_cast<ir::ConstantInt*>(args[2])) {
                    as.emitByte(0x41); as.emitByte(0xB8); // mov r8d, imm32
                    as.emitDWord((uint32_t)constInt->getValue());
                } else {
                    emitRegMem(0x44, 0x8B, 0, cg.getStackOffsets()[args[2]]); // mov r8d, dword ptr [rbp+offset]
                }
            }

            as.emitBytes({0x4C, 0x8D, 0x4C, 0x24, 0x28}); // lea r9, [rsp + 40]
            as.emitBytes({0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00}); // mov qword ptr [rsp + 32], 0
            
            as.emitByte(0xE8); uint64_t off3 = as.getCodeSize(); as.emitDWord(0);
            cg.addRelocation(::codegen::CodeGen::RelocationInfo{off3, "R_X86_64_PC32", -4, "WriteFile", ".text"});
            
            as.emitBytes({0x48, 0x83, 0xC4, 0x30}); // add rsp, 48
        }
        return;
    }
    std::string func;
    switch (spec.id) {
        case CapabilityId::IO_READ:  func = "_read";    break;
        case CapabilityId::IO_OPEN:  func = "_open";    break;
        case CapabilityId::IO_CLOSE: func = "_close";   break;
        case CapabilityId::IO_SEEK:  func = "_lseek";   break;
        case CapabilityId::IO_STAT:  func = "_fstat";   break;
        case CapabilityId::IO_FLUSH: func = "_commit";  break;
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
        case CapabilityId::FS_RENAME: func = "MoveFileA"; break;
        case CapabilityId::FS_MKDIR: func = "CreateDirectoryA"; break;
        case CapabilityId::FS_RMDIR: func = "RemoveDirectoryA"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitMemoryCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::MEMORY_ALLOC) {
        auto* size_val = instr.getOperands()[0]->get();
        if (auto* os = cg.getTextStream()) {
            std::string op = cg.getValueAsOperand(size_val);
            *os << "  sub rsp, 48\n";
            *os << "  mov rcx, 0\n";
            if (dynamic_cast<ir::GlobalVariable*>(size_val)) {
                *os << "  lea rdx, " << op << "\n";
            } else {
                *os << "  mov rdx, " << op << "\n";
            }
            *os << "  mov r8d, 12288\n"; // 0x3000 (MEM_COMMIT | MEM_RESERVE)
            *os << "  mov r9d, 4\n";    // 0x04 (PAGE_READWRITE)
            *os << "  call VirtualAlloc\n";
            *os << "  mov [rbp + " << cg.getStackOffsets()[&instr] << "], rax\n";
            *os << "  add rsp, 48\n";
        } else {
            auto& as = cg.getAssembler();
            as.emitBytes({0x48, 0x83, 0xEC, 0x30}); // sub rsp, 48
            as.emitBytes({0x48, 0xC7, 0xC1, 0x00, 0x00, 0x00, 0x00}); // mov rcx, 0

            auto emitRegMem = [&](uint8_t rex, uint8_t opcode, uint8_t reg, int32_t offset) {
                if (rex) as.emitByte(rex);
                as.emitByte(opcode);
                if (offset >= -128 && offset <= 127) { as.emitByte(0x45 | (reg << 3)); as.emitByte((uint8_t)offset); }
                else { as.emitByte(0x85 | (reg << 3)); as.emitDWord(offset); }
            };

            if (auto* constInt = dynamic_cast<ir::ConstantInt*>(size_val)) {
                as.emitBytes({0x48, 0xC7, 0xC2}); // mov rdx, imm32
                as.emitDWord((uint32_t)constInt->getValue());
            } else if (dynamic_cast<ir::GlobalVariable*>(size_val)) {
                as.emitBytes({0x48, 0x8D, 0x15});
                uint64_t off = as.getCodeSize(); as.emitDWord(0);
                cg.addRelocation(::codegen::CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, size_val->getName(), ".text"});
            } else {
                emitRegMem(0x48, 0x8B, 2, cg.getStackOffsets()[size_val]); // mov rdx, [rbp+off]
            }
            as.emitBytes({0x41, 0xB8, 0x00, 0x30, 0x00, 0x00}); // mov r8d, 0x3000
            as.emitBytes({0x41, 0xB9, 0x04, 0x00, 0x00, 0x00}); // mov r9d, 4
            as.emitByte(0xE8); uint64_t off = as.getCodeSize(); as.emitDWord(0);
            cg.addRelocation(::codegen::CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, "VirtualAlloc", ".text"});
            emitRegMem(0x48, 0x89, 0, cg.getStackOffsets()[&instr]); // mov [rbp+off], rax
            as.emitBytes({0x48, 0x83, 0xC4, 0x30}); // add rsp, 48
        }
        return;
    }

    if (spec.id == CapabilityId::MEMORY_USAGE) {
        if (auto* os = cg.getTextStream()) *os << "  # memory.usage stub\n";
        return;
    }

    std::string func;
    switch (spec.id) {
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
        case CapabilityId::PROCESS_GETPID: func = "GetCurrentProcessId"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitThreadCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::THREAD_SPAWN: func = "CreateThread"; break;
        case CapabilityId::THREAD_JOIN: func = "WaitForSingleObject"; break;
        case CapabilityId::THREAD_DETACH: func = "CloseHandle"; break;
        case CapabilityId::THREAD_YIELD: func = "SwitchToThread"; break;
        case CapabilityId::THREAD_GETID: func = "GetCurrentThreadId"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitSyncCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::SYNC_MUTEX_LOCK:
                *os << "  # mutex.lock portable stub\n";
                break;
            case CapabilityId::SYNC_MUTEX_UNLOCK:
                *os << "  # mutex.unlock portable stub\n";
                break;
            case CapabilityId::SYNC_ATOMIC_ADD:
                *os << "  # atomic.add portable stub\n";
                break;
            case CapabilityId::SYNC_ATOMIC_SUB:
                *os << "  # atomic.sub portable stub\n";
                break;
            case CapabilityId::SYNC_ATOMIC_CAS:
                *os << "  # atomic.cas portable stub\n";
                break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
}

void WindowsOS::emitTimeCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::TIME_NOW: func = "GetSystemTimeAsFileTime"; break;
        case CapabilityId::TIME_MONOTONIC: func = "QueryPerformanceCounter"; break;
        case CapabilityId::TIME_SLEEP: func = "Sleep"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitEventCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::EVENT_POLL: func = "WaitForMultipleObjects"; break;
        case CapabilityId::EVENT_CREATE: func = "CreateEventA"; break;
        case CapabilityId::EVENT_MODIFY: func = "SetEvent"; break;
        case CapabilityId::EVENT_CLOSE: func = "CloseHandle"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
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
        case CapabilityId::NET_CLOSE: func = "closesocket"; break;
        case CapabilityId::NET_BIND: func = "bind"; break;
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
    if (spec.id == CapabilityId::IPC_CONNECT || spec.id == CapabilityId::IPC_LISTEN) {
        if (auto* os = cg.getTextStream()) *os << "  # ipc portable stub\n";
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
    std::string func;
    switch (spec.id) {
        case CapabilityId::SYSTEM_INFO: func = "GetSystemInfo"; break;
        case CapabilityId::SYSTEM_REBOOT:
        case CapabilityId::SYSTEM_SHUTDOWN: func = "ExitWindowsEx"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitSignalCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::SIGNAL_WAIT) {
        if (auto* os = cg.getTextStream()) *os << "  # signal.wait stub\n";
        return;
    }
    std::string func;
    switch (spec.id) {
        case CapabilityId::SIGNAL_SEND: func = "GenerateConsoleCtrlEvent"; break;
        case CapabilityId::SIGNAL_REGISTER: func = "SetConsoleCtrlHandler"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitRandomCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::RANDOM_U64 && spec.id != CapabilityId::RANDOM_BYTES) {
        cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
        return;
    }
    arch.emitNativeLibraryCall(cg, "BCryptGenRandom", getArgValues(instr));
}

void WindowsOS::emitErrorCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::ERROR_GET: func = "GetLastError"; break;
        case CapabilityId::ERROR_STR: func = "FormatMessageA"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitDebugCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::DEBUG_LOG: func = "OutputDebugStringA"; break;
        case CapabilityId::DEBUG_BREAK: func = "DebugBreak"; break;
        case CapabilityId::DEBUG_TRACE: func = "OutputDebugStringA"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitModuleCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::MODULE_LOAD: func = "LoadLibraryA"; break;
        case CapabilityId::MODULE_UNLOAD: func = "FreeLibrary"; break;
        case CapabilityId::MODULE_GETSYM: func = "GetProcAddress"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitTTYCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::TTY_ISATTY: func = "GetConsoleMode"; break;
        case CapabilityId::TTY_GETSIZE: func = "GetConsoleScreenBufferInfo"; break;
        case CapabilityId::TTY_SETMODE: func = "SetConsoleMode"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitSecurityCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, ArchitectureInfo& arch) const {
    std::string func;
    switch (spec.id) {
        case CapabilityId::SECURITY_CHMOD: func = "_chmod"; break;
        case CapabilityId::SECURITY_CHOWN: func = "SetFileSecurityA"; break;
        case CapabilityId::SECURITY_GETUID: func = "GetUserNameA"; break;
        default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
    }
    arch.emitNativeLibraryCall(cg, func, getArgValues(instr));
}

void WindowsOS::emitGPUCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, ArchitectureInfo& a) const {
    if (auto* os = cg.getTextStream()) {
        switch (s.id) {
            case CapabilityId::GPU_COMPUTE:
            case CapabilityId::GPU_MALLOC:
            case CapabilityId::GPU_MEMCPY:
                *os << "  # gpu portable stub\n";
                break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); return;
        }
    }
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
        *os << "  and rsp, -16\n";
        const_cast<ArchitectureInfo&>(arch).emitNativeLibraryCall(cg, "main", {});
        *os << "  mov rcx, rax\n";
        const_cast<ArchitectureInfo&>(arch).emitNativeLibraryCall(cg, "ExitProcess", {});
    } else {
        auto& as = cg.getAssembler();
        CodeGen::SymbolInfo s; s.name = "_start"; s.sectionName = ".text"; s.value = as.getCodeSize(); s.type = 2; s.binding = 1; cg.addSymbol(s);
        as.emitBytes({0x48, 0x83, 0xE4, 0xF0}); // and rsp, -16
        const_cast<ArchitectureInfo&>(arch).emitNativeLibraryCall(cg, "main", {});
        as.emitBytes({0x48, 0x89, 0xC1}); // mov rcx, rax
        const_cast<ArchitectureInfo&>(arch).emitNativeLibraryCall(cg, "ExitProcess", {});
    }
}

}
