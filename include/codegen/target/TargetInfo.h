#pragma once
#include "ir/Type.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include "ir/SIMDInstruction.h"
#include "ir/Function.h"
#include "ir/Syscall.h"
#include <string>
#include <vector>
#include <ostream>

namespace codegen {
class CodeGen;
namespace target {
enum class RegisterClass { Integer, Float, Vector };
enum class FusedPattern { MultiplyAdd, MultiplySubtract, LoadAndOperate, CompareAndBranch, AddressCalculation };
struct VectorCapabilities { bool supportsSSE = false, supportsAVX = false, supportsAVX2 = false, supportsAVX512 = false, supportsNEON = false, maxVectorWidth = 0; std::vector<unsigned> supportedWidths; bool supportsFloatVectors = false, supportsIntegerVectors = false, supportsDoubleVectors = false, supportsMaskedOps = false, supportsGatherScatter = false, supportsFMA = false, supportsHorizontalOps = false; std::string simdExtension; };
struct TypeInfo { uint64_t size, align; RegisterClass regClass; bool isFloatingPoint, isSigned; };
struct SIMDContext { unsigned vectorWidth; ir::VectorType* vectorType; std::string elementSuffix, widthSuffix; };

class TargetInfo {
public:
    virtual ~TargetInfo() = default;
    virtual std::string getName() const = 0;
    virtual size_t getPointerSize() const { return 8; }
    virtual size_t getStackAlignment() const { return 16; }
    virtual TypeInfo getTypeInfo(const ir::Type* type) const = 0;
    virtual const std::vector<std::string>& getRegisters(RegisterClass regClass) const = 0;
    virtual const std::string& getReturnRegister(const ir::Type* type) const = 0;
    virtual const std::vector<std::string>& getIntegerArgumentRegisters() const = 0;
    virtual const std::vector<std::string>& getFloatArgumentRegisters() const = 0;
    virtual const std::string& getIntegerReturnRegister() const = 0;
    virtual const std::string& getFloatReturnRegister() const = 0;
    virtual void emitPrologue(CodeGen&, int) {}
    virtual void emitHeader(CodeGen&) {}
    virtual void emitEpilogue(CodeGen&) {}
    virtual void emitFunctionPrologue(CodeGen&, ir::Function&) = 0;
    virtual void emitFunctionEpilogue(CodeGen&, ir::Function&) = 0;
    virtual void emitStructuredFunctionBody(CodeGen&, ir::Function&) {}
    virtual void emitStartFunction(CodeGen&) {}
    virtual size_t getMaxRegistersForArgs() const = 0;
    virtual void emitPassArgument(CodeGen&, size_t, const std::string&, const ir::Type*) = 0;
    virtual void emitGetArgument(CodeGen&, size_t, const std::string&, const ir::Type*) = 0;
    virtual void emitRet(CodeGen&, ir::Instruction&) = 0;
    virtual void emitAdd(CodeGen&, ir::Instruction&) = 0;
    virtual void emitSub(CodeGen&, ir::Instruction&) = 0;
    virtual void emitMul(CodeGen&, ir::Instruction&) = 0;
    virtual void emitDiv(CodeGen&, ir::Instruction&) = 0;
    virtual void emitRem(CodeGen&, ir::Instruction&) = 0;
    virtual void emitAnd(CodeGen&, ir::Instruction&) = 0;
    virtual void emitOr(CodeGen&, ir::Instruction&) = 0;
    virtual void emitXor(CodeGen&, ir::Instruction&) = 0;
    virtual void emitShl(CodeGen&, ir::Instruction&) = 0;
    virtual void emitShr(CodeGen&, ir::Instruction&) = 0;
    virtual void emitSar(CodeGen&, ir::Instruction&) = 0;
    virtual void emitNeg(CodeGen&, ir::Instruction&) = 0;
    virtual void emitNot(CodeGen&, ir::Instruction&) = 0;
    virtual void emitCopy(CodeGen&, ir::Instruction&) = 0;
    virtual void emitCall(CodeGen&, ir::Instruction&) = 0;
    virtual void emitFAdd(CodeGen&, ir::Instruction&) = 0;
    virtual void emitFSub(CodeGen&, ir::Instruction&) = 0;
    virtual void emitFMul(CodeGen&, ir::Instruction&) = 0;
    virtual void emitFDiv(CodeGen&, ir::Instruction&) = 0;
    virtual void emitCmp(CodeGen&, ir::Instruction&) = 0;
    virtual void emitCast(CodeGen&, ir::Instruction&, const ir::Type*, const ir::Type*) = 0;
    virtual void emitVAStart(CodeGen&, ir::Instruction&) = 0;
    virtual void emitVAArg(CodeGen&, ir::Instruction&) = 0;
    virtual void emitVAEnd(CodeGen&, ir::Instruction&) {}
    virtual void emitLoad(CodeGen&, ir::Instruction&) = 0;
    virtual void emitStore(CodeGen&, ir::Instruction&) = 0;
    virtual void emitAlloc(CodeGen&, ir::Instruction&) = 0;
    virtual void emitSyscall(CodeGen&, ir::Instruction&) {}
    virtual uint64_t getSyscallNumber(ir::SyscallId) const { return 0; }
    virtual void emitBr(CodeGen&, ir::Instruction&) = 0;
    virtual void emitJmp(CodeGen&, ir::Instruction&) = 0;
    virtual VectorCapabilities getVectorCapabilities() const { return VectorCapabilities(); }
    virtual bool supportsVectorWidth(unsigned) const { return false; }
    virtual bool supportsVectorType(const ir::VectorType*) const { return false; }
    virtual unsigned getOptimalVectorWidth(const ir::Type*) const { return 0; }
    virtual void emitVectorLoad(CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorStore(CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorArithmetic(CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorLogical(CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorNeg(CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorNot(CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorComparison(CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorShuffle(CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorBroadcast(CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorExtract(CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorInsert(CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorGather(CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorScatter(CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorHorizontalOp(CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorReduction(CodeGen&, ir::VectorInstruction&) {}
    virtual bool supportsFusedPattern(FusedPattern) const { return false; }
    virtual void emitFusedMultiplyAdd(CodeGen&, const ir::FusedInstruction&) {}
    virtual void emitFusedMultiplySubtract(CodeGen&, const ir::FusedInstruction&) {}
    virtual void emitLoadAndOperate(CodeGen&, ir::Instruction&, ir::Instruction&) {}
    virtual void emitComplexAddressing(CodeGen&, ir::Instruction&) {}
    virtual void emitDebugInfo(CodeGen&, const ir::Function&) {}
    virtual void emitLineInfo(CodeGen&, unsigned, const std::string&) {}
    virtual void emitProfilingHook(CodeGen&, const std::string&) {}
    virtual void emitStackUnwindInfo(CodeGen&, const ir::Function&) {}
    virtual SIMDContext createSIMDContext(const ir::VectorType* type) const;
    virtual std::string getVectorRegister(const std::string& baseReg, unsigned width) const;
    virtual std::string getVectorInstruction(const std::string& baseInstr, const SIMDContext& ctx) const;
    virtual std::string formatConstant(const ir::ConstantInt* C) const;
    virtual std::string formatStackOperand(int offset) const = 0;
    virtual std::string formatGlobalOperand(const std::string& name) const = 0;
    virtual std::string getImmediatePrefix() const { return "$"; }
    virtual std::string getLabelPrefix() const { return "L"; }
    virtual std::string getAssemblyFileExtension() const { return ".s"; }
    virtual std::string getObjectFileExtension() const { return ".o"; }
    virtual std::string getExecutableFileExtension() const { return ""; }
    virtual std::string getDataRelocationType() const { return "R_X86_64_64"; }
    virtual bool isCallerSaved(const std::string&) const = 0;
    virtual bool isCalleeSaved(const std::string&) const = 0;
    virtual bool isReserved(const std::string&) const { return false; }
    virtual std::string getRegisterName(const std::string& baseReg, const ir::Type* type) const { (void)type; return baseReg; }
    virtual int32_t getStackOffset(const CodeGen&, ir::Value*) const;
    virtual void resetStackOffset() { currentStackOffset = 0; }
protected:
    int32_t currentStackOffset = 0;
};
}
}
