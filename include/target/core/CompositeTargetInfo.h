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

    void emitHeader(codegen::CodeGen& cg) override;
    void emitFooter(codegen::CodeGen& cg) override;
    void emitFunctionPrologue(codegen::CodeGen& cg, ir::Function& func) override;
    void emitFunctionEpilogue(codegen::CodeGen& cg, ir::Function& func) override;
    void emitBasicBlockStart(codegen::CodeGen& cg, ir::BasicBlock& bb) override;
    void emitStructuredFunctionBody(codegen::CodeGen& cg, ir::Function& func) override;
    void emitStartFunction(codegen::CodeGen& cg) override;

    size_t getMaxRegistersForArgs() const override;
    void emitPassArgument(codegen::CodeGen& cg, size_t idx, const std::string& val, const ir::Type* type) override;
    void emitGetArgument(codegen::CodeGen& cg, size_t idx, const std::string& dest, const ir::Type* type) override;

    void emitRet(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitAdd(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitSub(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitMul(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitDiv(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitRem(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitAnd(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitOr(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitXor(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitShl(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitShr(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitSar(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitNeg(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitNot(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitCopy(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitCall(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitFAdd(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitFSub(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitFMul(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitFDiv(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitCmp(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitCast(codegen::CodeGen& cg, ir::Instruction& i, const ir::Type* f, const ir::Type* t) override;
    void emitVAStart(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitVAArg(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitVAEnd(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitLoad(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitStore(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitAlloc(codegen::CodeGen& cg, ir::Instruction& i) override;

    void emitSyscall(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitExternCall(codegen::CodeGen& cg, ir::Instruction& i) override;

    bool supportsCapability(const CapabilitySpec& spec) const override;
    void emitIOCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitFSCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitMemoryCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitProcessCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitThreadCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitSyncCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitTimeCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitEventCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitNetCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitIPCCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitEnvCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitSystemCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitSignalCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitRandomCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitErrorCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitDebugCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitModuleCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitTTYCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitSecurityCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;
    void emitGPUCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) override;

    uint64_t getSyscallNumber(ir::SyscallId id) const override;
    void emitBr(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitJmp(codegen::CodeGen& cg, ir::Instruction& i) override;

    VectorCapabilities getVectorCapabilities() const override;
    bool supportsVectorWidth(unsigned w) const override;
    bool supportsVectorType(const ir::VectorType* t) const override;
    unsigned getOptimalVectorWidth(const ir::Type* t) const override;
    void emitVectorLoad(codegen::CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorStore(codegen::CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorArithmetic(codegen::CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorLogical(codegen::CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorNeg(codegen::CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorNot(codegen::CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorComparison(codegen::CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorShuffle(codegen::CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorBroadcast(codegen::CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorExtract(codegen::CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorInsert(codegen::CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorGather(codegen::CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorScatter(codegen::CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorHorizontalOp(codegen::CodeGen& cg, ir::VectorInstruction& i) override;
    void emitVectorReduction(codegen::CodeGen& cg, ir::VectorInstruction& i) override;

    bool supportsFusedPattern(FusedPattern p) const override;
    void emitFusedMultiplyAdd(codegen::CodeGen& cg, const ir::FusedInstruction& i) override;
    void emitFusedMultiplySubtract(codegen::CodeGen& cg, const ir::FusedInstruction& i) override;
    void emitLoadAndOperate(codegen::CodeGen& cg, ir::Instruction& l, ir::Instruction& o) override;
    bool emitCmpAndBranchFusion(codegen::CodeGen& cg, ir::Instruction& c, ir::Instruction& b) override;
    void emitComplexAddressing(codegen::CodeGen& cg, ir::Instruction& i) override;

    void emitDebugInfo(codegen::CodeGen& cg, const ir::Function& f) override;
    void emitLineInfo(codegen::CodeGen& cg, unsigned l, const std::string& f) override;
    void emitProfilingHook(codegen::CodeGen& cg, const std::string& h) override;
    void emitStackUnwindInfo(codegen::CodeGen& cg, const ir::Function& f) override;

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
