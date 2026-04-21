#pragma once

#include "AArch64.h"

namespace codegen {
namespace target {

class MacOS_AArch64 : public AArch64 {
public:
    MacOS_AArch64();

    std::string getName() const override { return "macos-aarch64"; }

    void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    void emitStartFunction(CodeGen& cg) override;
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
