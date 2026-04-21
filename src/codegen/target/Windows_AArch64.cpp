#include "codegen/target/Windows_AArch64.h"
#include "codegen/CodeGen.h"
#include "ir/Use.h"
#include "ir/Instruction.h"

namespace codegen {
namespace target {

void Windows_AArch64::emitStartFunction(CodeGen& cg) {
    // Windows entry point is typically 'mainCRTStartup' or 'WinMainCRTStartup'
    // which calls 'main' or 'WinMain'.
    // For now, we'll just emit a simple entry point that calls main if needed,
    // or leave it to the linker/CRT.
    if (auto* os = cg.getTextStream()) {
        *os << "\n; Windows ARM64 Entry Point\n";
        *os << ".globl mainCRTStartup\n";
        *os << "mainCRTStartup:\n";
        *os << "  bl main\n";
        *os << "  ret\n";
    }
}

bool Windows_AArch64::isReserved(const std::string& reg) const {
    // x18 is reserved for the TEB on Windows ARM64
    if (reg == "x18") return true;
    return false;
}

void Windows_AArch64::emitRem(CodeGen& cg, ir::Instruction& instr) {
    AArch64::emitRem(cg, instr);
}

uint64_t Windows_AArch64::getSyscallNumber(ir::SyscallId id) const {
    // Windows ARM64 syscall numbers (similarly unstable)
    switch (id) {
        case ir::SyscallId::Exit: return 0x002C;
        case ir::SyscallId::Read: return 0x0006;
        case ir::SyscallId::Write: return 0x0008;
        case ir::SyscallId::Close: return 0x000F;
        default: return 0;
    }
}

void Windows_AArch64::emitSyscall(CodeGen& cg, ir::Instruction& instr) {
    auto* syscallInstr = dynamic_cast<ir::SyscallInstruction*>(&instr);
    ir::SyscallId sid = syscallInstr ? syscallInstr->getSyscallId() : ir::SyscallId::None;

    // Direct syscalls on Windows ARM64 are very rare and discouraged.
    // However, for consistency:
    // x16: syscall number, x0-x7: arguments
    if (auto* os = cg.getTextStream()) {
        *os << "  # Windows ARM64 Syscall (Highly unstable)\n";
        if (sid != ir::SyscallId::None) {
            *os << "  mov x16, #" << getSyscallNumber(sid) << "\n";
        } else {
            *os << "  mov x16, " << cg.getValueAsOperand(instr.getOperands()[0]->get()) << "\n";
        }

        size_t startArg = (sid != ir::SyscallId::None) ? 0 : 1;
        for (size_t i = startArg; i < instr.getOperands().size(); ++i) {
            size_t argIdx = (sid != ir::SyscallId::None) ? i : i - 1;
            if (argIdx < 8) {
                *os << "  mov x" << argIdx << ", " << cg.getValueAsOperand(instr.getOperands()[i]->get()) << "\n";
            }
        }
        *os << "  svc #0\n";
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  str x0, " << cg.getValueAsOperand(&instr) << "\n";
        }
    } else {
        auto& assembler = cg.getAssembler();
        // mov x16, num
        if (sid != ir::SyscallId::None) {
            uint64_t num = getSyscallNumber(sid);
            assembler.emitDWord(0xD2800010 | ((num & 0xFFFF) << 5));
        } else {
            emitLoadValue(cg, assembler, instr.getOperands()[0]->get(), 16);
        }

        size_t startArg = (sid != ir::SyscallId::None) ? 0 : 1;
        for (size_t i = startArg; i < instr.getOperands().size(); ++i) {
            size_t argIdx = (sid != ir::SyscallId::None) ? i : i - 1;
            if (argIdx < 8) {
                emitLoadValue(cg, assembler, instr.getOperands()[i]->get(), argIdx);
            }
        }
        assembler.emitDWord(0xD4000001); // svc #0
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            int32_t destOffset = cg.getStackOffset(&instr);
            assembler.emitDWord(0xF8000BA0 | ((destOffset & 0x1FF) << 12));
        }
    }
}

namespace {
void emitWinA64Arg(CodeGen& cg, ir::Instruction& instr, size_t idx, const char* reg, std::ostream& os) {
    if (idx < instr.getOperands().size()) {
        os << "  mov " << reg << ", " << cg.getValueAsOperand(instr.getOperands()[idx]->get()) << "\n";
    } else {
        os << "  mov " << reg << ", #0\n";
    }
}
void emitWinA64StoreResult(CodeGen& cg, ir::Instruction& instr, std::ostream& os) {
    if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
        os << "  str x0, " << cg.getValueAsOperand(&instr) << "\n";
    }
}
}

bool Windows_AArch64::supportsCapability(const CapabilitySpec& spec) const {
    return spec.id != CapabilityId::GPU_COMPUTE;
}

void Windows_AArch64::emitIOCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    auto* os = cg.getTextStream();
    if (!os) return emitUnsupportedCapability(cg, instr, &spec);
    switch (spec.id) {
        case CapabilityId::IO_READ: emitWinA64Arg(cg, instr, 0, "x0", *os); emitWinA64Arg(cg, instr, 1, "x1", *os); emitWinA64Arg(cg, instr, 2, "x2", *os); *os << "  bl ReadFile\n"; break;
        case CapabilityId::IO_WRITE: emitWinA64Arg(cg, instr, 0, "x0", *os); emitWinA64Arg(cg, instr, 1, "x1", *os); emitWinA64Arg(cg, instr, 2, "x2", *os); *os << "  bl WriteFile\n"; break;
        case CapabilityId::IO_OPEN: emitWinA64Arg(cg, instr, 0, "x0", *os); *os << "  bl CreateFileA\n"; break;
        case CapabilityId::IO_CLOSE: emitWinA64Arg(cg, instr, 0, "x0", *os); *os << "  bl CloseHandle\n"; break;
        case CapabilityId::IO_SEEK: emitWinA64Arg(cg, instr, 0, "x0", *os); emitWinA64Arg(cg, instr, 1, "x1", *os); emitWinA64Arg(cg, instr, 2, "x2", *os); *os << "  bl SetFilePointerEx\n"; break;
        case CapabilityId::IO_STAT: emitWinA64Arg(cg, instr, 0, "x0", *os); emitWinA64Arg(cg, instr, 1, "x1", *os); *os << "  bl GetFileInformationByHandle\n"; break;
        case CapabilityId::IO_FLUSH: emitWinA64Arg(cg, instr, 0, "x0", *os); *os << "  bl FlushFileBuffers\n"; break;
        default: return emitUnsupportedCapability(cg, instr, &spec);
    }
    emitWinA64StoreResult(cg, instr, *os);
}

void Windows_AArch64::emitFSCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (spec.id == CapabilityId::FS_OPEN || spec.id == CapabilityId::FS_CREATE) return emitIOCapability(cg, instr, CapabilitySpec{CapabilityId::IO_OPEN, "io.open", CapabilityDomain::IO, 2, 3, true, true});
    auto* os = cg.getTextStream();
    if (!os) return emitUnsupportedCapability(cg, instr, &spec);
    if (spec.id == CapabilityId::FS_STAT) { emitWinA64Arg(cg, instr, 0, "x0", *os); emitWinA64Arg(cg, instr, 1, "x1", *os); *os << "  bl GetFileAttributesExA\n"; }
    else if (spec.id == CapabilityId::FS_REMOVE) { emitWinA64Arg(cg, instr, 0, "x0", *os); *os << "  bl DeleteFileA\n"; }
    else return emitUnsupportedCapability(cg, instr, &spec);
    emitWinA64StoreResult(cg, instr, *os);
}

void Windows_AArch64::emitMemoryCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    auto* os = cg.getTextStream();
    if (!os) return emitUnsupportedCapability(cg, instr, &spec);
    if (spec.id == CapabilityId::MEMORY_ALLOC || spec.id == CapabilityId::MEMORY_MAP) { emitWinA64Arg(cg, instr, 0, "x0", *os); *os << "  bl VirtualAlloc\n"; }
    else if (spec.id == CapabilityId::MEMORY_FREE) { emitWinA64Arg(cg, instr, 0, "x0", *os); *os << "  bl VirtualFree\n"; }
    else if (spec.id == CapabilityId::MEMORY_PROTECT) { emitWinA64Arg(cg, instr, 0, "x0", *os); emitWinA64Arg(cg, instr, 1, "x1", *os); emitWinA64Arg(cg, instr, 2, "x2", *os); *os << "  bl VirtualProtect\n"; }
    else return emitUnsupportedCapability(cg, instr, &spec);
    emitWinA64StoreResult(cg, instr, *os);
}

void Windows_AArch64::emitProcessCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    auto* os = cg.getTextStream();
    if (!os) return emitUnsupportedCapability(cg, instr, &spec);
    if (spec.id == CapabilityId::PROCESS_EXIT) { emitWinA64Arg(cg, instr, 0, "x0", *os); *os << "  bl ExitProcess\n"; }
    else if (spec.id == CapabilityId::PROCESS_ABORT) { *os << "  bl abort\n"; }
    else if (spec.id == CapabilityId::PROCESS_SLEEP) { emitWinA64Arg(cg, instr, 0, "x0", *os); *os << "  bl Sleep\n"; }
    else if (spec.id == CapabilityId::PROCESS_SPAWN) { emitWinA64Arg(cg, instr, 0, "x0", *os); emitWinA64Arg(cg, instr, 1, "x1", *os); *os << "  bl CreateProcessA\n"; }
    else if (spec.id == CapabilityId::PROCESS_ARGS) { *os << "  bl GetCommandLineA\n"; }
    else return emitUnsupportedCapability(cg, instr, &spec);
    emitWinA64StoreResult(cg, instr, *os);
}

void Windows_AArch64::emitThreadCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    auto* os = cg.getTextStream(); if (!os) return emitUnsupportedCapability(cg, instr, &spec);
    if (spec.id == CapabilityId::THREAD_SPAWN) { emitWinA64Arg(cg, instr, 0, "x0", *os); emitWinA64Arg(cg, instr, 1, "x1", *os); *os << "  bl CreateThread\n"; }
    else if (spec.id == CapabilityId::THREAD_JOIN) { emitWinA64Arg(cg, instr, 0, "x0", *os); *os << "  bl WaitForSingleObject\n"; }
    else return emitUnsupportedCapability(cg, instr, &spec);
    emitWinA64StoreResult(cg, instr, *os);
}

void Windows_AArch64::emitSyncCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    auto* os = cg.getTextStream(); if (!os) return emitUnsupportedCapability(cg, instr, &spec);
    if (spec.id == CapabilityId::SYNC_MUTEX_LOCK || spec.id == CapabilityId::SYNC_MUTEX_UNLOCK) { emitWinA64Arg(cg, instr, 0, "x0", *os); *os << "  bl InterlockedExchange64\n"; }
    else return emitUnsupportedCapability(cg, instr, &spec);
    emitWinA64StoreResult(cg, instr, *os);
}

void Windows_AArch64::emitTimeCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    auto* os = cg.getTextStream(); if (!os) return emitUnsupportedCapability(cg, instr, &spec);
    if (spec.id == CapabilityId::TIME_NOW) *os << "  bl GetSystemTimeAsFileTime\n";
    else if (spec.id == CapabilityId::TIME_MONOTONIC) *os << "  bl QueryPerformanceCounter\n";
    else return emitUnsupportedCapability(cg, instr, &spec);
    emitWinA64StoreResult(cg, instr, *os);
}

void Windows_AArch64::emitEventCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if (spec.id != CapabilityId::EVENT_POLL) return emitUnsupportedCapability(cg, instr, &spec); if (auto* os = cg.getTextStream()) { emitWinA64Arg(cg, instr, 0, "x0", *os); *os << "  bl WaitForMultipleObjects\n"; emitWinA64StoreResult(cg, instr, *os);} }
void Windows_AArch64::emitNetCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if (auto* os = cg.getTextStream()) { switch(spec.id){case CapabilityId::NET_SOCKET:*os<<"  bl socket\n";break;case CapabilityId::NET_CONNECT:*os<<"  bl connect\n";break;case CapabilityId::NET_LISTEN:*os<<"  bl listen\n";break;case CapabilityId::NET_ACCEPT:*os<<"  bl accept\n";break;case CapabilityId::NET_SEND:*os<<"  bl send\n";break;case CapabilityId::NET_RECV:*os<<"  bl recv\n";break;default:return emitUnsupportedCapability(cg,instr,&spec);} emitWinA64StoreResult(cg,instr,*os);} }
void Windows_AArch64::emitIPCCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if (spec.id==CapabilityId::IPC_SEND) return emitIOCapability(cg,instr,CapabilitySpec{CapabilityId::IO_WRITE,"io.write",CapabilityDomain::IO,3,3,true,true}); if (spec.id==CapabilityId::IPC_RECV) return emitIOCapability(cg,instr,CapabilitySpec{CapabilityId::IO_READ,"io.read",CapabilityDomain::IO,3,3,true,true}); emitUnsupportedCapability(cg,instr,&spec); }
void Windows_AArch64::emitEnvCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if (auto* os = cg.getTextStream()) { if (spec.id==CapabilityId::ENV_GET) *os<<"  bl GetEnvironmentVariableA\n"; else if (spec.id==CapabilityId::ENV_LIST) *os<<"  bl GetEnvironmentStringsA\n"; else return emitUnsupportedCapability(cg,instr,&spec); emitWinA64StoreResult(cg,instr,*os);} }
void Windows_AArch64::emitSystemCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if (spec.id!=CapabilityId::SYSTEM_INFO) return emitUnsupportedCapability(cg,instr,&spec); if (auto* os=cg.getTextStream()){ emitWinA64Arg(cg,instr,0,"x0",*os); *os<<"  bl GetSystemInfo\n"; emitWinA64StoreResult(cg,instr,*os);} }
void Windows_AArch64::emitSignalCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if (auto* os=cg.getTextStream()){ if(spec.id==CapabilityId::SIGNAL_SEND)*os<<"  bl GenerateConsoleCtrlEvent\n"; else if(spec.id==CapabilityId::SIGNAL_REGISTER)*os<<"  bl SetConsoleCtrlHandler\n"; else return emitUnsupportedCapability(cg,instr,&spec); emitWinA64StoreResult(cg,instr,*os);} }
void Windows_AArch64::emitRandomCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if(spec.id!=CapabilityId::RANDOM_U64) return emitUnsupportedCapability(cg,instr,&spec); if(auto* os=cg.getTextStream()){ *os<<"  bl BCryptGenRandom\n"; emitWinA64StoreResult(cg,instr,*os);} }
void Windows_AArch64::emitErrorCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if(spec.id!=CapabilityId::ERROR_GET) return emitUnsupportedCapability(cg,instr,&spec); if(auto* os=cg.getTextStream()){ *os<<"  bl GetLastError\n"; emitWinA64StoreResult(cg,instr,*os);} }
void Windows_AArch64::emitDebugCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if(spec.id!=CapabilityId::DEBUG_LOG) return emitUnsupportedCapability(cg,instr,&spec); if(auto* os=cg.getTextStream()){ emitWinA64Arg(cg,instr,0,"x0",*os); *os<<"  bl OutputDebugStringA\n"; emitWinA64StoreResult(cg,instr,*os);} }
void Windows_AArch64::emitModuleCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if(spec.id!=CapabilityId::MODULE_LOAD) return emitUnsupportedCapability(cg,instr,&spec); if(auto* os=cg.getTextStream()){ emitWinA64Arg(cg,instr,0,"x0",*os); *os<<"  bl LoadLibraryA\n"; emitWinA64StoreResult(cg,instr,*os);} }
void Windows_AArch64::emitTTYCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if(spec.id!=CapabilityId::TTY_ISATTY) return emitUnsupportedCapability(cg,instr,&spec); if(auto* os=cg.getTextStream()){ emitWinA64Arg(cg,instr,0,"x0",*os); *os<<"  bl GetConsoleMode\n"; emitWinA64StoreResult(cg,instr,*os);} }
void Windows_AArch64::emitSecurityCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if(spec.id!=CapabilityId::SECURITY_CHMOD) return emitUnsupportedCapability(cg,instr,&spec); if(auto* os=cg.getTextStream()){ emitWinA64Arg(cg,instr,0,"x0",*os); emitWinA64Arg(cg,instr,1,"x1",*os); *os<<"  bl _chmod\n"; emitWinA64StoreResult(cg,instr,*os);} }
void Windows_AArch64::emitGPUCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { emitUnsupportedCapability(cg, instr, &spec); }

} // namespace target
} // namespace codegen
