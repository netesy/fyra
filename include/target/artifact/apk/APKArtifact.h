#pragma once
#include "target/core/TargetInfo.h"
#include <memory>

namespace codegen {
namespace target {
namespace artifact {

class APKArtifact : public TargetInfo {
    std::unique_ptr<TargetInfo> baseTarget;
public:
    APKArtifact(std::unique_ptr<TargetInfo> base);

    std::string getName() const override;
    TypeInfo getTypeInfo(const ir::Type* t) const override;
    const std::vector<std::string>& getRegisters(RegisterClass rc) const override;
    const std::string& getReturnRegister(const ir::Type* t) const override;
    const std::vector<std::string>& getIntegerArgumentRegisters() const override;
    const std::vector<std::string>& getFloatArgumentRegisters() const override;
    const std::string& getIntegerReturnRegister() const override;
    const std::string& getFloatReturnRegister() const override;

    void emitHeader(CodeGen& cg) override;
    void emitFooter(CodeGen& cg) override;
    void emitPrologue(CodeGen& cg, int i) override;
    void emitEpilogue(CodeGen& cg) override;
    void emitFunctionPrologue(CodeGen& cg, ir::Function& f) override;
    void emitFunctionEpilogue(CodeGen& cg, ir::Function& f) override;
    void emitBasicBlockStart(CodeGen& cg, ir::BasicBlock& bb) override;
    void emitStructuredFunctionBody(CodeGen& cg, ir::Function& f) override;
    void emitStartFunction(CodeGen& cg) override;
    size_t getMaxRegistersForArgs() const override;
    void emitPassArgument(CodeGen& cg, size_t i, const std::string& v, const ir::Type* t) override;
    void emitGetArgument(CodeGen& cg, size_t i, const std::string& d, const ir::Type* t) override;
    void emitRet(CodeGen& cg, ir::Instruction& i) override;
    void emitAdd(CodeGen& cg, ir::Instruction& i) override;
    void emitSub(CodeGen& cg, ir::Instruction& i) override;
    void emitMul(CodeGen& cg, ir::Instruction& i) override;
    void emitDiv(CodeGen& cg, ir::Instruction& i) override;
    void emitRem(CodeGen& cg, ir::Instruction& i) override;
    void emitAnd(CodeGen& cg, ir::Instruction& i) override;
    void emitOr(CodeGen& cg, ir::Instruction& i) override;
    void emitXor(CodeGen& cg, ir::Instruction& i) override;
    void emitShl(CodeGen& cg, ir::Instruction& i) override;
    void emitShr(CodeGen& cg, ir::Instruction& i) override;
    void emitSar(CodeGen& cg, ir::Instruction& i) override;
    void emitNeg(CodeGen& cg, ir::Instruction& i) override;
    void emitNot(CodeGen& cg, ir::Instruction& i) override;
    void emitCopy(CodeGen& cg, ir::Instruction& i) override;
    void emitCall(CodeGen& cg, ir::Instruction& i) override;
    void emitFAdd(CodeGen& cg, ir::Instruction& i) override;
    void emitFSub(CodeGen& cg, ir::Instruction& i) override;
    void emitFMul(CodeGen& cg, ir::Instruction& i) override;
    void emitFDiv(CodeGen& cg, ir::Instruction& i) override;
    void emitCmp(CodeGen& cg, ir::Instruction& i) override;
    void emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* f, const ir::Type* t) override;
    void emitVAStart(CodeGen& cg, ir::Instruction& i) override;
    void emitVAArg(CodeGen& cg, ir::Instruction& i) override;
    void emitVAEnd(CodeGen& cg, ir::Instruction& i) override;
    void emitLoad(CodeGen& cg, ir::Instruction& i) override;
    void emitStore(CodeGen& cg, ir::Instruction& i) override;
    void emitAlloc(CodeGen& cg, ir::Instruction& i) override;
    void emitSyscall(CodeGen& cg, ir::Instruction& i) override;
    void emitExternCall(CodeGen& cg, ir::Instruction& i) override;
    void emitBr(CodeGen& cg, ir::Instruction& i) override;
    void emitJmp(CodeGen& cg, ir::Instruction& i) override;

    bool supportsCapability(const CapabilitySpec& spec) const override;
    void emitIOCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitFSCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitMemoryCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitProcessCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitThreadCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitSyncCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitTimeCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitEventCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitNetCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitIPCCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitEnvCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitSystemCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitSignalCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitRandomCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitErrorCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitDebugCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitModuleCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitTTYCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitSecurityCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitGPUCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;

    uint64_t getSyscallNumber(ir::SyscallId id) const override;

    std::string formatStackOperand(int o) const override;
    std::string formatGlobalOperand(const std::string& n) const override;
    bool isCallerSaved(const std::string& r) const override;
    bool isCalleeSaved(const std::string& r) const override;

    void buildAPK(const std::string& outputPrefix);
};

}
}
}
