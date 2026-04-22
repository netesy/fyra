#pragma once
#include "target/core/TargetInfo.h"
#include <string>
#include <vector>
#include <memory>

namespace codegen {
class CodeGen;
namespace target {

class ArchitectureInfo {
public:
    virtual ~ArchitectureInfo() = default;
    virtual size_t getPointerSize() const = 0;
    virtual size_t getStackAlignment() const { return 16; }
    virtual TypeInfo getTypeInfo(const ir::Type* type) const = 0;
    virtual const std::vector<std::string>& getRegisters(RegisterClass regClass) const = 0;
    virtual const std::string& getReturnRegister(const ir::Type* type) const = 0;
    virtual const std::vector<std::string>& getIntegerArgumentRegisters() const = 0;
    virtual const std::vector<std::string>& getFloatArgumentRegisters() const = 0;
    virtual const std::string& getIntegerReturnRegister() const = 0;
    virtual const std::string& getFloatReturnRegister() const = 0;

    virtual void emitFunctionPrologue(CodeGen& cg, ir::Function& func) = 0;
    virtual void emitFunctionEpilogue(CodeGen& cg, ir::Function& func) = 0;
    virtual void emitHeader(CodeGen& cg) {}
    virtual void emitStartFunction(CodeGen& cg) {}
    virtual void emitBasicBlockStart(CodeGen& cg, ir::BasicBlock& bb) {}
    virtual void emitStructuredFunctionBody(CodeGen& cg, ir::Function& func) {}

    virtual size_t getMaxRegistersForArgs() const = 0;
    virtual void emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) = 0;
    virtual void emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) = 0;

    virtual void emitRet(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitAdd(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitSub(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitMul(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitDiv(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitRem(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitAnd(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitOr(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitXor(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitShl(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitShr(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitSar(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitNeg(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitNot(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitCopy(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitCall(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitFAdd(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitFSub(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitFMul(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitFDiv(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitCmp(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* from, const ir::Type* to) = 0;
    virtual void emitVAStart(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitVAArg(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitVAEnd(CodeGen& cg, ir::Instruction& i) {}
    virtual void emitLoad(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitStore(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitAlloc(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitBr(CodeGen& cg, ir::Instruction& i) = 0;
    virtual void emitJmp(CodeGen& cg, ir::Instruction& i) = 0;

    virtual VectorCapabilities getVectorCapabilities() const { return VectorCapabilities(); }
    virtual bool supportsVectorWidth(unsigned width) const { return false; }
    virtual bool supportsVectorType(const ir::VectorType* type) const { return false; }
    virtual unsigned getOptimalVectorWidth(const ir::Type* type) const { return 0; }
    virtual void emitVectorLoad(CodeGen& cg, ir::VectorInstruction& i) {}
    virtual void emitVectorStore(CodeGen& cg, ir::VectorInstruction& i) {}
    virtual void emitVectorArithmetic(CodeGen& cg, ir::VectorInstruction& i) {}
    virtual void emitVectorLogical(CodeGen& cg, ir::VectorInstruction& i) {}
    virtual void emitVectorNeg(CodeGen& cg, ir::VectorInstruction& i) {}
    virtual void emitVectorNot(CodeGen& cg, ir::VectorInstruction& i) {}
    virtual void emitVectorComparison(CodeGen& cg, ir::VectorInstruction& i) {}
    virtual void emitVectorShuffle(CodeGen& cg, ir::VectorInstruction& i) {}
    virtual void emitVectorBroadcast(CodeGen& cg, ir::VectorInstruction& i) {}
    virtual void emitVectorExtract(CodeGen& cg, ir::VectorInstruction& i) {}
    virtual void emitVectorInsert(CodeGen& cg, ir::VectorInstruction& i) {}
    virtual void emitVectorGather(CodeGen& cg, ir::VectorInstruction& i) {}
    virtual void emitVectorScatter(CodeGen& cg, ir::VectorInstruction& i) {}
    virtual void emitVectorHorizontalOp(CodeGen& cg, ir::VectorInstruction& i) {}
    virtual void emitVectorReduction(CodeGen& cg, ir::VectorInstruction& i) {}

    virtual bool supportsFusedPattern(FusedPattern p) const { return false; }
    virtual void emitFusedMultiplyAdd(CodeGen& cg, const ir::FusedInstruction& i) {}
    virtual void emitFusedMultiplySubtract(CodeGen& cg, const ir::FusedInstruction& i) {}
    virtual void emitLoadAndOperate(CodeGen& cg, ir::Instruction& load, ir::Instruction& op) {}
    virtual bool emitCmpAndBranchFusion(CodeGen& cg, ir::Instruction& cmp, ir::Instruction& br) { return false; }
    virtual void emitComplexAddressing(CodeGen& cg, ir::Instruction& i) {}

    virtual void emitDebugInfo(CodeGen& cg, const ir::Function& func) {}
    virtual void emitLineInfo(CodeGen& cg, unsigned line, const std::string& file) {}
    virtual void emitProfilingHook(CodeGen& cg, const std::string& hook) {}
    virtual void emitStackUnwindInfo(CodeGen& cg, const ir::Function& func) {}

    virtual std::string formatStackOperand(int offset) const = 0;
    virtual std::string formatGlobalOperand(const std::string& name) const = 0;
    virtual std::string getImmediatePrefix() const { return "$"; }
    virtual std::string getLabelPrefix() const { return "L"; }
    virtual std::string getAssemblyFileExtension() const { return ".s"; }
    virtual std::string getObjectFileExtension() const { return ".o"; }
    virtual std::string getDataRelocationType() const { return "R_X86_64_64"; }
    virtual bool isCallerSaved(const std::string& reg) const = 0;
    virtual bool isCalleeSaved(const std::string& reg) const = 0;
    virtual bool isReserved(const std::string& reg) const { return false; }
    virtual std::string getRegisterName(const std::string& base, const ir::Type* type) const { return base; }
    virtual std::string formatConstant(const ir::ConstantInt* C) const;

    virtual void emitSyscall(CodeGen& cg, ir::Instruction& i, const class OperatingSystemInfo& osInfo) = 0;
    virtual void emitExternCall(CodeGen& cg, ir::Instruction& i, const class OperatingSystemInfo& osInfo) = 0;
};

}
}
