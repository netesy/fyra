#pragma once
#include "ir/Type.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include "ir/SIMDInstruction.h"
#include "ir/Function.h"
#include "ir/Syscall.h"
#include "ir/BasicBlock.h"
#include "target/capabilities/Capabilities.h"
#include <string>
#include <vector>
#include <ostream>
#include <string_view>
namespace codegen { class CodeGen; }
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
    virtual void emitPrologue(codegen::CodeGen&, int) {}
    virtual void emitHeader(codegen::CodeGen& cg) = 0;
    virtual void emitFooter(codegen::CodeGen& cg) = 0;
    virtual void emitEpilogue(codegen::CodeGen&) {}
    virtual void emitFunctionPrologue(codegen::CodeGen&, ir::Function&) = 0;
    virtual void emitFunctionEpilogue(codegen::CodeGen&, ir::Function&) = 0;
    virtual void emitBasicBlockStart(codegen::CodeGen&, ir::BasicBlock&) {}
    virtual void emitStructuredFunctionBody(codegen::CodeGen&, ir::Function&) {}
    virtual void emitStartFunction(codegen::CodeGen&) {}
    virtual size_t getMaxRegistersForArgs() const = 0;
    virtual void emitPassArgument(codegen::CodeGen&, size_t, const std::string&, const ir::Type*) = 0;
    virtual void emitGetArgument(codegen::CodeGen&, size_t, const std::string&, const ir::Type*) = 0;
    virtual void emitRet(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitAdd(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitSub(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitMul(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitDiv(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitRem(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitAnd(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitOr(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitXor(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitShl(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitShr(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitSar(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitNeg(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitNot(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitCopy(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitCall(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitFAdd(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitFSub(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitFMul(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitFDiv(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitCmp(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitCast(codegen::CodeGen&, ir::Instruction&, const ir::Type*, const ir::Type*) = 0;
    virtual void emitVAStart(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitVAArg(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitVAEnd(codegen::CodeGen&, ir::Instruction&) {}
    virtual void emitLoad(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitStore(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitAlloc(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitSyscall(codegen::CodeGen&, ir::Instruction&) {}
    virtual void emitExternCall(codegen::CodeGen&, ir::Instruction&);
    virtual const CapabilitySpec* findCapability(std::string_view name) const;
    virtual bool validateCapability(ir::Instruction&, const CapabilitySpec&) const;
    virtual bool supportsCapability(const CapabilitySpec&) const;
    virtual void emitUnsupportedCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec* spec) const;
    virtual void emitDomainCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitIOCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitFSCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitMemoryCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitProcessCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitThreadCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitSyncCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitTimeCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitEventCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitNetCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitIPCCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitEnvCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitSystemCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitSignalCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitRandomCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitErrorCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitDebugCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitModuleCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitTTYCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitSecurityCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual void emitGPUCapability(codegen::CodeGen&, ir::Instruction&, const CapabilitySpec&);
    virtual uint64_t getSyscallNumber(ir::SyscallId) const { return 0; }
    virtual void emitBr(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual void emitJmp(codegen::CodeGen&, ir::Instruction&) = 0;
    virtual VectorCapabilities getVectorCapabilities() const { return VectorCapabilities(); }
    virtual bool supportsVectorWidth(unsigned) const { return false; }
    virtual bool supportsVectorType(const ir::VectorType*) const { return false; }
    virtual unsigned getOptimalVectorWidth(const ir::Type*) const { return 0; }
    virtual void emitVectorLoad(codegen::CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorStore(codegen::CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorArithmetic(codegen::CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorLogical(codegen::CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorNeg(codegen::CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorNot(codegen::CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorComparison(codegen::CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorShuffle(codegen::CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorBroadcast(codegen::CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorExtract(codegen::CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorInsert(codegen::CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorGather(codegen::CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorScatter(codegen::CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorHorizontalOp(codegen::CodeGen&, ir::VectorInstruction&) {}
    virtual void emitVectorReduction(codegen::CodeGen&, ir::VectorInstruction&) {}
    virtual bool supportsFusedPattern(FusedPattern) const { return false; }
    virtual void emitFusedMultiplyAdd(codegen::CodeGen&, const ir::FusedInstruction&) {}
    virtual void emitFusedMultiplySubtract(codegen::CodeGen&, const ir::FusedInstruction&) {}
    virtual void emitLoadAndOperate(codegen::CodeGen&, ir::Instruction&, ir::Instruction&) {}
    virtual bool emitCmpAndBranchFusion(codegen::CodeGen&, ir::Instruction&, ir::Instruction&) { return false; }
    virtual void emitComplexAddressing(codegen::CodeGen&, ir::Instruction&) {}
    virtual void emitDebugInfo(codegen::CodeGen&, const ir::Function&) {}
    virtual void emitLineInfo(codegen::CodeGen&, unsigned, const std::string&) {}
    virtual void emitProfilingHook(codegen::CodeGen&, const std::string&) {}
    virtual void emitStackUnwindInfo(codegen::CodeGen&, const ir::Function&) {}
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
    virtual int32_t getStackOffset(const codegen::CodeGen&, ir::Value*) const;
    virtual void resetStackOffset() { currentStackOffset = 0; }
    virtual std::string getFunctionEpilogueLabel(const ir::Function& func) const { return func.getName() + "_epilogue"; }
    virtual std::string getBBLabel(const ir::BasicBlock* bb) const { if (!bb) return "null_bb"; return bb->getParent()->getName() + "_" + bb->getName(); }
protected:
    int32_t currentStackOffset = 0;
};
}
