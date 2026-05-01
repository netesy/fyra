#pragma once
#include "target/core/OperatingSystemInfo.h"

namespace target {

class WASIOS : public OperatingSystemInfo {
public:
    std::string getName() const override { return "wasi"; }
    bool supportsCapability(const CapabilitySpec& spec) const override { return true; }
    void emitIOCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitFSCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitMemoryCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitProcessCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override;
    void emitThreadCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitSyncCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitTimeCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitEventCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitNetCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitIPCCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitEnvCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitSystemCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitSignalCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitRandomCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitErrorCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitDebugCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitModuleCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitTTYCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitSecurityCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}
    void emitGPUCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, class ArchitectureInfo& arch) const override {}

    void emitHeader(CodeGen& cg) override;
};

}
}
