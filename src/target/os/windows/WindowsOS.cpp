#include "target/os/windows/WindowsOS.h"
#include "codegen/CodeGen.h"
#include "target/core/ArchitectureInfo.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include "codegen/asm/Assembler.h"
#include <ostream>

namespace codegen {
namespace target {

namespace {
void emitWindowsStoreExternResult(CodeGen& cg, ir::Instruction& instr, const ArchitectureInfo& arch) {
    if (auto* os = cg.getTextStream()) {
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  mov " << cg.getValueAsOperand(&instr) << ", " << arch.getRegisterName("rax", instr.getType()) << "\n";
        }
    }
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

void WindowsOS::emitIOCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::IO_WRITE) {
            *os << "  sub rsp, 48\n  mov rcx, -11\n  call GetStdHandle\n  mov rcx, rax\n";
            *os << "  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
            *os << "  mov r8, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
            *os << "  lea r9, [rsp + 40]\n  mov qword ptr [rsp + 32], 0\n  call WriteFile\n  add rsp, 48\n";
        } else if (spec.id == CapabilityId::IO_OPEN) {
            *os << "  sub rsp, 64\n";
            *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  mov rdx, 3221225472\n  mov r8, 1\n  xor r9, r9\n";
            *os << "  mov qword ptr [rsp + 32], 3\n  mov qword ptr [rsp + 40], 128\n  mov qword ptr [rsp + 48], 0\n";
            *os << "  call CreateFileA\n  add rsp, 64\n";
        } else if (spec.id == CapabilityId::IO_CLOSE) {
            *os << "  sub rsp, 40\n  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  call CloseHandle\n  add rsp, 40\n";
        } else if (spec.id == CapabilityId::IO_READ) {
            *os << "  sub rsp, 48\n  mov rcx, -10\n  call GetStdHandle\n  mov rcx, rax\n";
            *os << "  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
            *os << "  mov r8, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
            *os << "  lea r9, [rsp + 40]\n  mov qword ptr [rsp + 32], 0\n  call ReadFile\n  add rsp, 48\n";
        } else if (spec.id == CapabilityId::IO_SEEK) {
            *os << "  sub rsp, 56\n";
            *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
            *os << "  lea r8, [rsp + 40]\n";
            *os << "  mov r9, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n";
            *os << "  call SetFilePointerEx\n";
            *os << "  mov rax, [rsp + 40]\n  add rsp, 56\n";
        } else if (spec.id == CapabilityId::IO_STAT) {
            *os << "  sub rsp, 40\n  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
            *os << "  call GetFileInformationByHandle\n  add rsp, 40\n";
        } else if (spec.id == CapabilityId::IO_FLUSH) {
            *os << "  sub rsp, 40\n  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  call FlushFileBuffers\n  add rsp, 40\n";
        } else {
            cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
            return;
        }
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitFSCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::FS_OPEN || spec.id == CapabilityId::FS_CREATE) {
        emitIOCapability(cg, instr, CapabilitySpec{CapabilityId::IO_OPEN, "io.open", CapabilityDomain::IO, 2, 3, true, true}, arch);
        return;
    }
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::FS_STAT) {
            *os << "  sub rsp, 40\n";
            *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  mov rdx, 0\n";
            *os << "  mov r8, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
            *os << "  call GetFileAttributesExA\n  add rsp, 40\n";
        } else if (spec.id == CapabilityId::FS_REMOVE) {
            *os << "  sub rsp, 40\n  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  call DeleteFileA\n  add rsp, 40\n";
        } else {
            cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
            return;
        }
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitMemoryCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::MEMORY_ALLOC) {
            *os << "  sub rsp, 40\n  xor rcx, rcx\n";
            *os << "  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  mov r8, 12288\n  mov r9, 4\n  call VirtualAlloc\n  add rsp, 40\n";
        } else if (spec.id == CapabilityId::MEMORY_MAP) {
             *os << "  xor rax, rax\n";
        } else if (spec.id == CapabilityId::MEMORY_FREE) {
            *os << "  sub rsp, 40\n  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  xor rdx, rdx\n  mov r8, 32768\n  call VirtualFree\n  add rsp, 40\n";
        } else if (spec.id == CapabilityId::MEMORY_PROTECT) {
             *os << "  xor rax, rax\n";
        } else {
            cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
            return;
        }
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitProcessCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::PROCESS_EXIT) {
            *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  call ExitProcess\n";
        } else if (spec.id == CapabilityId::PROCESS_ABORT) {
            *os << "  call abort\n";
        } else if (spec.id == CapabilityId::PROCESS_SLEEP) {
            *os << "  sub rsp, 40\n  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  call Sleep\n  add rsp, 40\n";
        } else if (spec.id == CapabilityId::PROCESS_SPAWN) {
            *os << "  sub rsp, 96\n";
            *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
            *os << "  xor r8, r8\n  xor r9, r9\n";
            *os << "  mov qword ptr [rsp + 32], 0\n  mov qword ptr [rsp + 40], 0\n";
            *os << "  mov qword ptr [rsp + 48], 0\n  mov qword ptr [rsp + 56], 0\n";
            *os << "  lea rax, [rsp + 64]\n  mov qword ptr [rsp + 64], 0\n";
            *os << "  mov qword ptr [rsp + 72], 0\n";
            *os << "  mov qword ptr [rsp + 64], 104\n";
            *os << "  mov qword ptr [rsp + 80], rax\n";
            *os << "  call CreateProcessA\n";
            *os << "  mov rax, [rsp + 88]\n";
            *os << "  add rsp, 96\n";
        } else if (spec.id == CapabilityId::PROCESS_ARGS) {
            *os << "  sub rsp, 40\n  call GetCommandLineA\n";
            if (!instr.getOperands().empty()) {
                *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
                *os << "  mov rdx, rax\n";
                *os << "  call lstrcpyA\n";
                *os << "  mov rax, rcx\n";
            }
            *os << "  add rsp, 40\n";
        } else {
            cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
            return;
        }
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitThreadCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::THREAD_SPAWN) {
            *os << "  sub rsp, 56\n  xor rcx, rcx\n";
            *os << "  mov rdx, 0\n";
            *os << "  mov r8, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  mov r9, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
            *os << "  mov qword ptr [rsp + 32], 0\n  mov qword ptr [rsp + 40], 0\n";
            *os << "  call CreateThread\n  add rsp, 56\n";
        } else if (spec.id == CapabilityId::THREAD_JOIN) {
            *os << "  sub rsp, 40\n";
            *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  mov rdx, -1\n  call WaitForSingleObject\n";
            *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  call CloseHandle\n  add rsp, 40\n";
        } else {
            cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
            return;
        }
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitSyncCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::SYNC_MUTEX_LOCK) {
            *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  mov rax, 1\n  .Lmutex_retry_" << cg.labelCounter << ":\n";
            *os << "  lock xchg qword ptr [rcx], rax\n  test rax, rax\n  jnz .Lmutex_retry_" << cg.labelCounter << "\n";
            cg.labelCounter++;
        } else if (spec.id == CapabilityId::SYNC_MUTEX_UNLOCK) {
            *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  mov qword ptr [rcx], 0\n";
        } else {
            cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
            return;
        }
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitTimeCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::TIME_NOW) {
            *os << "  sub rsp, 40\n  lea rcx, [rsp + 32]\n  call GetSystemTimeAsFileTime\n  mov rax, [rsp + 32]\n  add rsp, 40\n";
        } else if (spec.id == CapabilityId::TIME_MONOTONIC) {
            *os << "  sub rsp, 40\n  lea rcx, [rsp + 32]\n  call QueryPerformanceCounter\n  mov rax, [rsp + 32]\n  add rsp, 40\n";
        } else {
            cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
            return;
        }
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitEventCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::EVENT_POLL) { cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return; }
    if (auto* os = cg.getTextStream()) {
        *os << "  sub rsp, 40\n";
        *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov rdx, " << (instr.getOperands().size() > 1 ? cg.getValueAsOperand(instr.getOperands()[1]->get()) : "0") << "\n";
        *os << "  mov r8, 0\n  call WaitForMultipleObjects\n  add rsp, 40\n";
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitNetCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        switch (spec.id) {
            case CapabilityId::NET_SOCKET: *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  mov r8, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  call socket\n"; break;
            case CapabilityId::NET_CONNECT: *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  mov r8, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  call connect\n"; break;
            case CapabilityId::NET_LISTEN: *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  call listen\n"; break;
            case CapabilityId::NET_ACCEPT: *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  mov r8, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  call accept\n"; break;
            case CapabilityId::NET_SEND: *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  mov r8, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  mov r9, " << cg.getValueAsOperand(instr.getOperands()[3]->get()) << "\n  call send\n"; break;
            case CapabilityId::NET_RECV: *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n  mov r8, " << cg.getValueAsOperand(instr.getOperands()[2]->get()) << "\n  mov r9, " << cg.getValueAsOperand(instr.getOperands()[3]->get()) << "\n  call recv\n"; break;
            default: cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return;
        }
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitIPCCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
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

void WindowsOS::emitEnvCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::ENV_GET) {
            *os << "  sub rsp, 40\n";
            *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  mov rdx, " << (instr.getOperands().size() > 1 ? cg.getValueAsOperand(instr.getOperands()[1]->get()) : "0") << "\n";
            *os << "  mov r8, 4096\n  call GetEnvironmentVariableA\n  add rsp, 40\n";
        } else if (spec.id == CapabilityId::ENV_LIST) {
            *os << "  sub rsp, 40\n  call GetEnvironmentStringsA\n  add rsp, 40\n";
        } else {
            cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
            return;
        }
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitSystemCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::SYSTEM_INFO) { cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return; }
    if (auto* os = cg.getTextStream()) {
        *os << "  sub rsp, 40\n";
        *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  call GetSystemInfo\n  add rsp, 40\n";
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitSignalCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        if (spec.id == CapabilityId::SIGNAL_SEND) {
            *os << "  sub rsp, 40\n";
            *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
            *os << "  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
            *os << "  call GenerateConsoleCtrlEvent\n  add rsp, 40\n";
        } else if (spec.id == CapabilityId::SIGNAL_REGISTER) {
            *os << "  sub rsp, 40\n";
            *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
            *os << "  mov rdx, 1\n  call SetConsoleCtrlHandler\n  add rsp, 40\n";
        } else {
            cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
            return;
        }
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitRandomCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::RANDOM_U64) { cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return; }
    if (auto* os = cg.getTextStream()) {
        *os << "  sub rsp, 48\n  xor rcx, rcx\n  lea rdx, [rsp + 32]\n  mov r8, 8\n  mov r9, 2\n  call BCryptGenRandom\n";
        *os << "  mov rax, [rsp + 32]\n  add rsp, 48\n";
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitErrorCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::ERROR_GET) { cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return; }
    if (auto* os = cg.getTextStream()) *os << "  sub rsp, 40\n  call GetLastError\n  add rsp, 40\n";
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitDebugCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::DEBUG_LOG) { cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return; }
    if (auto* os = cg.getTextStream()) {
        *os << "  sub rsp, 40\n  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  call OutputDebugStringA\n  add rsp, 40\n";
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitModuleCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::MODULE_LOAD) { cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return; }
    if (auto* os = cg.getTextStream()) {
        *os << "  sub rsp, 40\n";
        *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  call LoadLibraryA\n  add rsp, 40\n";
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitTTYCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::TTY_ISATTY) { cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return; }
    if (auto* os = cg.getTextStream()) {
        *os << "  sub rsp, 48\n";
        *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n  call GetStdHandle\n";
        *os << "  mov rcx, rax\n  lea rdx, [rsp + 40]\n  call GetConsoleMode\n";
        *os << "  add rsp, 48\n";
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitSecurityCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (spec.id != CapabilityId::SECURITY_CHMOD) { cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec); return; }
    if (auto* os = cg.getTextStream()) {
        *os << "  sub rsp, 40\n";
        *os << "  mov rcx, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        *os << "  mov rdx, " << cg.getValueAsOperand(instr.getOperands()[1]->get()) << "\n";
        *os << "  call _chmod\n  add rsp, 40\n";
    }
    emitWindowsStoreExternResult(cg, instr, arch);
}

void WindowsOS::emitGPUCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const {
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
        *os << ".globl _start\n_start:\n  sub rsp, 40\n  call main\n  mov rcx, rax\n  call ExitProcess\n";
    } else {
        auto& as = cg.getAssembler();
        CodeGen::SymbolInfo s; s.name = "_start"; s.sectionName = ".text"; s.value = as.getCodeSize(); s.type = 2; s.binding = 1; cg.addSymbol(s);
        as.emitBytes({0x48, 0x83, 0xEC, 0x28}); // sub rsp, 40
        as.emitByte(0xE8); uint64_t off = as.getCodeSize(); as.emitDWord(0); cg.addRelocation(CodeGen::RelocationInfo{off, "R_X86_64_PC32", -4, "main", ".text"});
        as.emitBytes({0x48, 0x89, 0xC1}); // mov rcx, rax
        as.emitByte(0xE8); uint64_t off2 = as.getCodeSize(); as.emitDWord(0); cg.addRelocation(CodeGen::RelocationInfo{off2, "R_X86_64_PC32", -4, "ExitProcess", ".text"});
    }
}

}
}
