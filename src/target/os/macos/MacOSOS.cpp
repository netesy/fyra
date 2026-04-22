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
void emitMacOSSyscallArgs(CodeGen& cg, ir::Instruction& instr, const ArchitectureInfo& arch, size_t maxArgs = 6) {
    static const char* regs[] = {"rdi", "rsi", "rdx", "r10", "r8", "r9"};
    if (auto* os = cg.getTextStream()) {
        for (size_t i = 0; i < std::min(instr.getOperands().size(), maxArgs); ++i) {
            *os << "  movq " << cg.getValueAsOperand(instr.getOperands()[i]->get()) << ", " << arch.getRegisterName(regs[i], instr.getOperands()[i]->get()->getType()) << "\n";
        }
    }
}
void emitMacOSStoreResult(CodeGen& cg, ir::Instruction& instr, const ArchitectureInfo& arch) {
    if (auto* os = cg.getTextStream()) {
        if (instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  movq " << arch.getRegisterName("rax", instr.getType()) << ", " << cg.getValueAsOperand(&instr) << "\n";
        }
    }
}
}

void MacOSOS::emitIOCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (auto* os = cg.getTextStream()) {
        uint64_t num = 0;
        if (spec.id == CapabilityId::IO_WRITE) num = 0x02000004;
        else if (spec.id == CapabilityId::IO_READ) num = 0x02000003;

        if (num) {
            *os << "  movq $" << num << ", " << arch.getRegisterName("rax", nullptr) << "\n";
            emitMacOSSyscallArgs(cg, instr, arch, 3);
            *os << "  syscall\n";
            emitMacOSStoreResult(cg, instr, arch);
        } else {
             cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
        }
    }
}

void MacOSOS::emitFSCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
}
void MacOSOS::emitMemoryCapability(CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    cg.getTargetInfo()->emitUnsupportedCapability(cg, instr, &spec);
}
void MacOSOS::emitProcessCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const ArchitectureInfo& arch) const {
    if (spec.id == CapabilityId::PROCESS_EXIT) {
        if (auto* os = cg.getTextStream()) {
            *os << "  movq $0x02000001, " << arch.getRegisterName("rax", nullptr) << "\n";
            emitMacOSSyscallArgs(cg, i, arch, 1);
            *os << "  syscall\n";
        }
    } else {
        cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &spec);
    }
}
void MacOSOS::emitThreadCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); }
void MacOSOS::emitSyncCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); }
void MacOSOS::emitTimeCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); }
void MacOSOS::emitEventCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); }
void MacOSOS::emitNetCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); }
void MacOSOS::emitIPCCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); }
void MacOSOS::emitEnvCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); }
void MacOSOS::emitSystemCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); }
void MacOSOS::emitSignalCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); }
void MacOSOS::emitRandomCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); }
void MacOSOS::emitErrorCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); }
void MacOSOS::emitDebugCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); }
void MacOSOS::emitModuleCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); }
void MacOSOS::emitTTYCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); }
void MacOSOS::emitSecurityCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); }
void MacOSOS::emitGPUCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& s, const ArchitectureInfo& a) const { cg.getTargetInfo()->emitUnsupportedCapability(cg, i, &s); }

void MacOSOS::emitStartFunction(CodeGen& cg, const ArchitectureInfo& arch) {
    if (auto* os = cg.getTextStream()) {
        *os << ".globl _main\n_main:\n  pushq %rbp\n  movq %rsp, %rbp\n  call main\n  popq %rbp\n  ret\n";
    }
}

}
}
