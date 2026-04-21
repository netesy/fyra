#include "codegen/target/MacOS_AArch64.h"
#include "codegen/CodeGen.h"
#include "ir/Function.h"
#include "ir/Use.h"
#include <ostream>

namespace codegen {
namespace target {

MacOS_AArch64::MacOS_AArch64() : AArch64() {}

void MacOS_AArch64::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        // macOS standard: prefix symbols with underscore
        *os << "_" << func.getName() << ":\n";
    }

    // Use base AArch64 class for stack frame setup
    AArch64::emitFunctionPrologue(cg, func);
}

void MacOS_AArch64::emitStartFunction(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "\n.globl _main\n";
        *os << "_main:\n";
        *os << "  bl _main_user\n";
        *os << "  ret\n";
    }
}

uint64_t MacOS_AArch64::getSyscallNumber(ir::SyscallId id) const {
    const uint64_t unix_class = 0x02000000;
    switch (id) {
        case ir::SyscallId::Exit: return unix_class | 1;
        case ir::SyscallId::Read: return unix_class | 3;
        case ir::SyscallId::Write: return unix_class | 4;
        case ir::SyscallId::OpenAt: return unix_class | 463;
        case ir::SyscallId::Close: return unix_class | 6;
        case ir::SyscallId::LSeek: return unix_class | 199;
        case ir::SyscallId::MMap: return unix_class | 197;
        case ir::SyscallId::MUnmap: return unix_class | 73;
        case ir::SyscallId::MProtect: return unix_class | 74;
        case ir::SyscallId::MkDirAt: return unix_class | 461;
        case ir::SyscallId::UnlinkAt: return unix_class | 462;
        case ir::SyscallId::RenameAt: return unix_class | 464;
        case ir::SyscallId::GetDents64: return unix_class | 344;
        case ir::SyscallId::ClockGetTime: return unix_class | 427;
        case ir::SyscallId::NanoSleep: return unix_class | 240;
        case ir::SyscallId::Uname: return unix_class | 116;
        case ir::SyscallId::GetPid: return unix_class | 20;
        case ir::SyscallId::GetTid: return unix_class | 286;
        case ir::SyscallId::Fork: return unix_class | 2;
        case ir::SyscallId::Execve: return unix_class | 59;
        case ir::SyscallId::Wait4: return unix_class | 7;
        case ir::SyscallId::Kill: return unix_class | 37;
        default: return 0;
    }
}

void MacOS_AArch64::emitSyscall(CodeGen& cg, ir::Instruction& instr) {
    auto* syscallInstr = dynamic_cast<ir::SyscallInstruction*>(&instr);
    ir::SyscallId sid = syscallInstr ? syscallInstr->getSyscallId() : ir::SyscallId::None;

    if (auto* os = cg.getTextStream()) {
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
        *os << "  svc #0x80\n";
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  str x0, " << cg.getValueAsOperand(&instr) << "\n";
        }
    }
}

namespace {
void emitMacArg(CodeGen& cg, ir::Instruction& instr, std::ostream& os, size_t idx, const char* reg) {
    if (idx < instr.getOperands().size()) os << "  mov " << reg << ", " << cg.getValueAsOperand(instr.getOperands()[idx]->get()) << "\n";
    else os << "  mov " << reg << ", #0\n";
}
void emitMacRet(CodeGen& cg, ir::Instruction& instr, std::ostream& os) {
    if (instr.getType()->getTypeID() != ir::Type::VoidTyID) os << "  str x0, " << cg.getValueAsOperand(&instr) << "\n";
}
}

bool MacOS_AArch64::supportsCapability(const CapabilitySpec& spec) const {
    return spec.id != CapabilityId::GPU_COMPUTE;
}

void MacOS_AArch64::emitIOCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    auto* os = cg.getTextStream(); if (!os) return emitUnsupportedCapability(cg, instr, &spec);
    switch (spec.id) {
        case CapabilityId::IO_READ: emitMacArg(cg, instr, *os, 0, "x0"); emitMacArg(cg, instr, *os, 1, "x1"); emitMacArg(cg, instr, *os, 2, "x2"); *os << "  bl _read\n"; break;
        case CapabilityId::IO_WRITE: emitMacArg(cg, instr, *os, 0, "x0"); emitMacArg(cg, instr, *os, 1, "x1"); emitMacArg(cg, instr, *os, 2, "x2"); *os << "  bl _write\n"; break;
        case CapabilityId::IO_OPEN: emitMacArg(cg, instr, *os, 0, "x0"); emitMacArg(cg, instr, *os, 1, "x1"); emitMacArg(cg, instr, *os, 2, "x2"); *os << "  bl _open\n"; break;
        case CapabilityId::IO_CLOSE: emitMacArg(cg, instr, *os, 0, "x0"); *os << "  bl _close\n"; break;
        case CapabilityId::IO_SEEK: emitMacArg(cg, instr, *os, 0, "x0"); emitMacArg(cg, instr, *os, 1, "x1"); emitMacArg(cg, instr, *os, 2, "x2"); *os << "  bl _lseek\n"; break;
        case CapabilityId::IO_STAT: emitMacArg(cg, instr, *os, 0, "x0"); emitMacArg(cg, instr, *os, 1, "x1"); *os << "  bl _fstat\n"; break;
        case CapabilityId::IO_FLUSH: emitMacArg(cg, instr, *os, 0, "x0"); *os << "  bl _fsync\n"; break;
        default: return emitUnsupportedCapability(cg, instr, &spec);
    }
    emitMacRet(cg, instr, *os);
}

void MacOS_AArch64::emitFSCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    if (spec.id == CapabilityId::FS_OPEN || spec.id == CapabilityId::FS_CREATE) return emitIOCapability(cg, instr, CapabilitySpec{CapabilityId::IO_OPEN, "io.open", CapabilityDomain::IO, 2, 3, true, true});
    auto* os = cg.getTextStream(); if (!os) return emitUnsupportedCapability(cg, instr, &spec);
    if (spec.id == CapabilityId::FS_STAT) { emitMacArg(cg, instr, *os, 0, "x0"); emitMacArg(cg, instr, *os, 1, "x1"); *os << "  bl _stat\n"; }
    else if (spec.id == CapabilityId::FS_REMOVE) { emitMacArg(cg, instr, *os, 0, "x0"); *os << "  bl _unlink\n"; }
    else return emitUnsupportedCapability(cg, instr, &spec);
    emitMacRet(cg, instr, *os);
}

void MacOS_AArch64::emitMemoryCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    auto* os = cg.getTextStream(); if (!os) return emitUnsupportedCapability(cg, instr, &spec);
    if (spec.id == CapabilityId::MEMORY_ALLOC) { emitMacArg(cg, instr, *os, 0, "x0"); *os << "  bl _malloc\n"; }
    else if (spec.id == CapabilityId::MEMORY_FREE) { emitMacArg(cg, instr, *os, 0, "x0"); *os << "  bl _free\n"; }
    else if (spec.id == CapabilityId::MEMORY_MAP) { emitMacArg(cg, instr, *os, 0, "x0"); emitMacArg(cg, instr, *os, 1, "x1"); emitMacArg(cg, instr, *os, 2, "x2"); emitMacArg(cg, instr, *os, 3, "x3"); emitMacArg(cg, instr, *os, 4, "x4"); emitMacArg(cg, instr, *os, 5, "x5"); *os << "  bl _mmap\n"; }
    else if (spec.id == CapabilityId::MEMORY_PROTECT) { emitMacArg(cg, instr, *os, 0, "x0"); emitMacArg(cg, instr, *os, 1, "x1"); emitMacArg(cg, instr, *os, 2, "x2"); *os << "  bl _mprotect\n"; }
    else return emitUnsupportedCapability(cg, instr, &spec);
    emitMacRet(cg, instr, *os);
}

void MacOS_AArch64::emitProcessCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    auto* os = cg.getTextStream(); if (!os) return emitUnsupportedCapability(cg, instr, &spec);
    if (spec.id == CapabilityId::PROCESS_EXIT) { emitMacArg(cg, instr, *os, 0, "x0"); *os << "  bl _exit\n"; }
    else if (spec.id == CapabilityId::PROCESS_ABORT) *os << "  bl _abort\n";
    else if (spec.id == CapabilityId::PROCESS_SLEEP) { emitMacArg(cg, instr, *os, 0, "x0"); *os << "  bl _usleep\n"; }
    else if (spec.id == CapabilityId::PROCESS_SPAWN) { emitMacArg(cg, instr, *os, 0, "x0"); emitMacArg(cg, instr, *os, 1, "x1"); *os << "  bl _posix_spawn\n"; }
    else if (spec.id == CapabilityId::PROCESS_ARGS) { *os << "  bl _getprogname\n"; }
    else return emitUnsupportedCapability(cg, instr, &spec);
    emitMacRet(cg, instr, *os);
}

void MacOS_AArch64::emitThreadCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { auto* os=cg.getTextStream(); if(!os) return emitUnsupportedCapability(cg,instr,&spec); if(spec.id==CapabilityId::THREAD_SPAWN){ emitMacArg(cg,instr,*os,0,"x0"); emitMacArg(cg,instr,*os,1,"x1"); *os<<"  bl _pthread_create\n"; } else if(spec.id==CapabilityId::THREAD_JOIN){ emitMacArg(cg,instr,*os,0,"x0"); *os<<"  bl _pthread_join\n"; } else return emitUnsupportedCapability(cg,instr,&spec); emitMacRet(cg,instr,*os); }
void MacOS_AArch64::emitSyncCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { auto* os=cg.getTextStream(); if(!os) return emitUnsupportedCapability(cg,instr,&spec); if(spec.id==CapabilityId::SYNC_MUTEX_LOCK){ emitMacArg(cg,instr,*os,0,"x0"); *os<<"  bl _pthread_mutex_lock\n"; } else if(spec.id==CapabilityId::SYNC_MUTEX_UNLOCK){ emitMacArg(cg,instr,*os,0,"x0"); *os<<"  bl _pthread_mutex_unlock\n"; } else return emitUnsupportedCapability(cg,instr,&spec); emitMacRet(cg,instr,*os); }
void MacOS_AArch64::emitTimeCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { auto* os=cg.getTextStream(); if(!os) return emitUnsupportedCapability(cg,instr,&spec); if(spec.id==CapabilityId::TIME_NOW){ *os<<"  mov x0, #0\n  mov x1, sp\n  bl _clock_gettime\n"; } else if(spec.id==CapabilityId::TIME_MONOTONIC){ *os<<"  mov x0, #6\n  mov x1, sp\n  bl _clock_gettime\n"; } else return emitUnsupportedCapability(cg,instr,&spec); emitMacRet(cg,instr,*os); }
void MacOS_AArch64::emitEventCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { auto* os=cg.getTextStream(); if(!os||spec.id!=CapabilityId::EVENT_POLL) return emitUnsupportedCapability(cg,instr,&spec); emitMacArg(cg,instr,*os,0,"x0"); emitMacArg(cg,instr,*os,1,"x1"); emitMacArg(cg,instr,*os,2,"x2"); *os<<"  bl _kevent\n"; emitMacRet(cg,instr,*os); }
void MacOS_AArch64::emitNetCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { auto* os=cg.getTextStream(); if(!os) return emitUnsupportedCapability(cg,instr,&spec); switch(spec.id){case CapabilityId::NET_SOCKET:*os<<"  bl _socket\n";break;case CapabilityId::NET_CONNECT:*os<<"  bl _connect\n";break;case CapabilityId::NET_LISTEN:*os<<"  bl _listen\n";break;case CapabilityId::NET_ACCEPT:*os<<"  bl _accept\n";break;case CapabilityId::NET_SEND:*os<<"  bl _send\n";break;case CapabilityId::NET_RECV:*os<<"  bl _recv\n";break;default:return emitUnsupportedCapability(cg,instr,&spec);} emitMacRet(cg,instr,*os); }
void MacOS_AArch64::emitIPCCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if(spec.id==CapabilityId::IPC_SEND) return emitIOCapability(cg,instr,CapabilitySpec{CapabilityId::IO_WRITE,"io.write",CapabilityDomain::IO,3,3,true,true}); if(spec.id==CapabilityId::IPC_RECV) return emitIOCapability(cg,instr,CapabilitySpec{CapabilityId::IO_READ,"io.read",CapabilityDomain::IO,3,3,true,true}); emitUnsupportedCapability(cg,instr,&spec); }
void MacOS_AArch64::emitEnvCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { auto* os=cg.getTextStream(); if(!os) return emitUnsupportedCapability(cg,instr,&spec); if(spec.id==CapabilityId::ENV_GET){ emitMacArg(cg,instr,*os,0,"x0"); *os<<"  bl _getenv\n"; } else if(spec.id==CapabilityId::ENV_LIST){ *os<<"  bl __NSGetEnviron\n"; } else return emitUnsupportedCapability(cg,instr,&spec); emitMacRet(cg,instr,*os); }
void MacOS_AArch64::emitSystemCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { auto* os=cg.getTextStream(); if(!os||spec.id!=CapabilityId::SYSTEM_INFO) return emitUnsupportedCapability(cg,instr,&spec); *os<<"  bl _uname\n"; emitMacRet(cg,instr,*os); }
void MacOS_AArch64::emitSignalCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { auto* os=cg.getTextStream(); if(!os) return emitUnsupportedCapability(cg,instr,&spec); if(spec.id==CapabilityId::SIGNAL_SEND) *os<<"  bl _kill\n"; else if(spec.id==CapabilityId::SIGNAL_REGISTER) *os<<"  bl _signal\n"; else return emitUnsupportedCapability(cg,instr,&spec); emitMacRet(cg,instr,*os); }
void MacOS_AArch64::emitRandomCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if(spec.id!=CapabilityId::RANDOM_U64) return emitUnsupportedCapability(cg,instr,&spec); if(auto* os=cg.getTextStream()){ *os<<"  bl _arc4random\n"; emitMacRet(cg,instr,*os);} }
void MacOS_AArch64::emitErrorCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if(spec.id!=CapabilityId::ERROR_GET) return emitUnsupportedCapability(cg,instr,&spec); if(auto* os=cg.getTextStream()){ *os<<"  bl ___error\n  ldr x0, [x0]\n"; emitMacRet(cg,instr,*os);} }
void MacOS_AArch64::emitDebugCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if(spec.id!=CapabilityId::DEBUG_LOG) return emitUnsupportedCapability(cg,instr,&spec); if(auto* os=cg.getTextStream()){ emitMacArg(cg,instr,*os,0,"x0"); *os<<"  bl _puts\n"; emitMacRet(cg,instr,*os);} }
void MacOS_AArch64::emitModuleCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if(spec.id!=CapabilityId::MODULE_LOAD) return emitUnsupportedCapability(cg,instr,&spec); if(auto* os=cg.getTextStream()){ emitMacArg(cg,instr,*os,0,"x0"); *os<<"  bl _dlopen\n"; emitMacRet(cg,instr,*os);} }
void MacOS_AArch64::emitTTYCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if(spec.id!=CapabilityId::TTY_ISATTY) return emitUnsupportedCapability(cg,instr,&spec); if(auto* os=cg.getTextStream()){ emitMacArg(cg,instr,*os,0,"x0"); *os<<"  bl _isatty\n"; emitMacRet(cg,instr,*os);} }
void MacOS_AArch64::emitSecurityCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { if(spec.id!=CapabilityId::SECURITY_CHMOD) return emitUnsupportedCapability(cg,instr,&spec); if(auto* os=cg.getTextStream()){ emitMacArg(cg,instr,*os,0,"x0"); emitMacArg(cg,instr,*os,1,"x1"); *os<<"  bl _chmod\n"; emitMacRet(cg,instr,*os);} }
void MacOS_AArch64::emitGPUCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) { emitUnsupportedCapability(cg, instr, &spec); }

} // namespace target
} // namespace codegen
