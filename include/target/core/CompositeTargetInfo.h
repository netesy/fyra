#pragma once
#include "target/core/TargetInfo.h"
#include "target/core/ArchitectureInfo.h"
#include "target/core/OperatingSystemInfo.h"
#include <memory>

namespace target {

class CompositeTargetInfo : public TargetInfo {
public:
    CompositeTargetInfo(std::unique_ptr<ArchitectureInfo> arch, std::unique_ptr<OperatingSystemInfo> os);

    std::string getName() const override;
    size_t getPointerSize() const override;
    size_t getStackAlignment() const override;
    TypeInfo getTypeInfo(const ir::Type* type) const override;
    const std::vector<std::string>& getRegisters(RegisterClass regClass) const override;
    const std::string& getReturnRegister(const ir::Type* type) const override;
    const std::vector<std::string>& getIntegerArgumentRegisters() const override;
    const std::vector<std::string>& getFloatArgumentRegisters() const override;
    const std::string& getIntegerReturnRegister() const override;
    const std::string& getFloatReturnRegister() const override;

    void emitHeader(CodeGen& cg) override;
    void emitFooter(CodeGen& cg) override;
    void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    void emitFunctionEpilogue(CodeGen& cg, ir::Function& func) override;
    void emitBasicBlockStart(CodeGen& cg, ir::BasicBlock& bb) override;
    void emitStructuredFunctionBody(CodeGen& cg, ir::Function& func) override;
    void emitStartFunction(CodeGen& cg) override;

    size_t getMaxRegistersForArgs() const override;
    void emitPassArgument(CodeGen& cg, size_t idx, const std::string& val, const ir::Type* type) override;
    void emitGetArgument(CodeGen& cg, size_t idx, const std::string& dest, const ir::Type* type) override;

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
    void emitBr(CodeGen& cg, ir::Instruction& i) override;
    void emitJmp(CodeGen& cg, ir::Instruction& i) override;

    VectorCapabilities getVectorCapabilities() const override;
    bool supportsVectorWidth(unsigned w) const override;
    bool supportsVectorType(const ir::VectorType* t) const override;
    unsigned getOptimalVectorWidth(const ir::Type* t) const override;
    void emitVectorLoad(CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorStore(CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorArithmetic(CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorLogical(CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorNeg(CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorNot(CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorComparison(CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorShuffle(CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorBroadcast(CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorExtract(CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorInsert(CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorGather(CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorScatter(CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorHorizontalOp(CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorReduction(CodeGen& cg, ir::VectorInstruction& i) override;

    bool supportsFusedPattern(FusedPattern p) const override;
    void emitFusedMultiplyAdd(CodeGen& cg, const ir::FusedInstruction& i) override;
    void emitFusedMultiplySubtract(CodeGen& cg, const ir::FusedInstruction& i) override;
    void emitLoadAndOperate(CodeGen& cg, ir::Instruction& l, ir::Instruction& o) override;
    bool emitCmpAndBranchFusion(CodeGen& cg, ir::Instruction& c, ir::Instruction& b) override;
    void emitComplexAddressing(CodeGen& cg, ir::Instruction& i) override;

    void emitDebugInfo(CodeGen& cg, const ir::Function& f) override;
    void emitLineInfo(CodeGen& cg, unsigned l, const std::string& f) override;
    void emitProfilingHook(CodeGen& cg, const std::string& h) override;
    void emitStackUnwindInfo(CodeGen& cg, const ir::Function& f) override;

    std::string formatStackOperand(int o) const override;
    std::string formatGlobalOperand(const std::string& n) const override;
    std::string getImmediatePrefix() const override;
    std::string getLabelPrefix() const override;
    std::string getAssemblyFileExtension() const override;
    std::string getObjectFileExtension() const override;
    std::string getExecutableFileExtension() const override;
    std::string getDataRelocationType() const override;
    bool isCallerSaved(const std::string& r) const override;
    bool isCalleeSaved(const std::string& r) const override;
    bool isReserved(const std::string& r) const override;
    std::string getRegisterName(const std::string& b, const ir::Type* t) const override;

protected:
    std::unique_ptr<ArchitectureInfo> architecture;
    std::unique_ptr<OperatingSystemInfo> os;
};

}
}
