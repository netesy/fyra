#pragma once
#include "target/core/TargetInfo.h"
#include <string>
#include <vector>
#include <string_view>

namespace codegen {
class CodeGen;
namespace target {

class OperatingSystemInfo {
public:
    virtual ~OperatingSystemInfo() = default;
    virtual std::string getName() const = 0;
    virtual uint64_t getSyscallNumber(ir::SyscallId id) const { return 0; }
    virtual std::string getExecutableFileExtension() const { return ""; }

    virtual bool supportsCapability(const CapabilitySpec& spec) const = 0;
    virtual void emitIOCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitFSCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitMemoryCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitProcessCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitThreadCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitSyncCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitTimeCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitEventCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitNetCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitIPCCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitEnvCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitSystemCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitSignalCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitRandomCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitErrorCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitDebugCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitModuleCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitTTYCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitSecurityCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;
    virtual void emitGPUCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec, const class ArchitectureInfo& arch) const = 0;

    virtual void emitHeader(CodeGen& cg) {}
    virtual void emitStartFunction(CodeGen& cg, const class ArchitectureInfo& arch) {}
};

}
}
