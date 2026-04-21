#pragma once

#include "AArch64.h"

namespace codegen {
namespace target {

class Windows_AArch64 : public AArch64 {
public:
    std::string getName() const override { return "aarch64-pc-windows-msvc"; }

    // Windows doesn't use the Linux-style _start with syscall 93
    void emitStartFunction(CodeGen& cg) override;

    // Override register sets if necessary (e.g., x18 is reserved on Windows)
    bool isReserved(const std::string& reg) const override;

    void emitRem(CodeGen& cg, ir::Instruction& instr) override;
    void emitSyscall(CodeGen& cg, ir::Instruction& instr) override;
    uint64_t getSyscallNumber(ir::SyscallId id) const override;
    bool supportsCapability(const CapabilitySpec& spec) const override;
    void emitIOCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitFSCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitMemoryCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitProcessCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitThreadCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitSyncCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitTimeCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitEventCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitNetCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitIPCCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitEnvCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitSystemCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitSignalCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitRandomCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitErrorCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitDebugCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitModuleCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitTTYCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitSecurityCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitGPUCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
};

} // namespace target
} // namespace codegen
