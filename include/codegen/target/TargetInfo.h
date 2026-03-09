#pragma once

#include "ir/Function.h"
#include "ir/Type.h"
#include "ir/SIMDInstruction.h"
#include "ir/Constant.h"
#include <vector>
#include <string>
#include <ostream>
#include <cstdint>

namespace codegen {
class CodeGen; // Forward declaration
namespace target {

// Register class for register allocation
enum class RegisterClass {
    Integer,
    Float,
    Vector,
};

// Vector capability information
struct VectorCapabilities {
    std::vector<unsigned> supportedWidths;  // 128, 256, 512 bits
    bool supportsIntegerVectors;
    bool supportsFloatVectors;
    bool supportsDoubleVectors;
    bool supportsFMA;              // Fused multiply-add
    bool supportsGatherScatter;    // Gather/scatter operations
    bool supportsHorizontalOps;   // Horizontal add/sub/mul
    bool supportsMaskedOps;        // Predicated/masked operations
    unsigned maxVectorWidth;       // Maximum supported width
    std::string simdExtension;     // "SSE", "AVX", "NEON", etc.
};

// SIMD instruction emission context
struct SIMDContext {
    unsigned vectorWidth;
    ir::VectorType* vectorType;
    std::string elementSuffix;  // "ps", "pd", "epi32", etc.
    std::string widthSuffix;    // "x", "y", "z" for XMM/YMM/ZMM
};

// Fused instruction patterns
enum class FusedPattern {
    MultiplyAdd,        // a * b + c
    MultiplySubtract,   // a * b - c
    LoadAndOperate,     // Load followed by operation
    CompareAndBranch,   // Compare with conditional branch
    AddressCalculation  // Complex addressing [base + index*scale + offset]
};

// Type information for ABI
struct TypeInfo {
    uint64_t size;      // Size in bits
    uint64_t align;     // Alignment in bits
    RegisterClass regClass; // Register class for this type
    bool isFloatingPoint;  // Is this a floating-point type?
    bool isSigned;         // Is this a signed type?
};

class TargetInfo {
public:
    virtual ~TargetInfo() = default;

    // Target properties
    virtual std::string getName() const = 0;
    virtual size_t getPointerSize() const = 0;
    virtual size_t getStackAlignment() const { return 16; } // Default to 16-byte stack alignment

    // Type information
    virtual TypeInfo getTypeInfo(const ir::Type* type) const = 0;
    
    // Register information
    virtual const std::vector<std::string>& getRegisters(RegisterClass regClass) const = 0;
    virtual const std::string& getReturnRegister(const ir::Type* type) const = 0;
    
    // Enhanced calling convention support
    virtual const std::vector<std::string>& getIntegerArgumentRegisters() const = 0;
    virtual const std::vector<std::string>& getFloatArgumentRegisters() const = 0;
    virtual const std::string& getIntegerReturnRegister() const = 0;
    virtual const std::string& getFloatReturnRegister() const = 0;
    
    // Stack frame management
    virtual void emitPrologue(CodeGen& cg, int stack_size) = 0;
    virtual void emitEpilogue(CodeGen& cg) = 0;
    virtual void emitFunctionPrologue(CodeGen& cg, ir::Function& func) = 0;
    virtual void emitFunctionEpilogue(CodeGen& cg, ir::Function& func) = 0;
    virtual void emitStructuredFunctionBody(CodeGen& cg, ir::Function& func) {
        (void)cg; (void)func;
    }

    // Executable entry point
    virtual void emitStartFunction(CodeGen& cg) {
        (void)cg;
    }

    // Argument passing
    virtual size_t getMaxRegistersForArgs() const = 0;
    virtual void emitPassArgument(CodeGen& cg, size_t argIndex,
                                const std::string& value, const ir::Type* type) = 0;
    virtual void emitGetArgument(CodeGen& cg, size_t argIndex,
                               const std::string& dest, const ir::Type* type) = 0;

    // Instruction emission
    virtual void emitRet(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitAdd(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitSub(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitMul(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitDiv(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitRem(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitAnd(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitOr(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitXor(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitShl(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitShr(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitSar(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitNeg(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitNot(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitCopy(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitCall(CodeGen& cg, ir::Instruction& instr) = 0;

    // Floating point operations
    virtual void emitFAdd(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitFSub(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitFMul(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitFDiv(CodeGen& cg, ir::Instruction& instr) = 0;
    
    // Comparison operations
    virtual void emitCmp(CodeGen& cg, ir::Instruction& instr) = 0;
    
    // Type conversion
    virtual void emitCast(CodeGen& cg, ir::Instruction& instr,
                         const ir::Type* fromType, const ir::Type* toType) = 0;
    
    // Variadic arguments
    virtual void emitVAStart(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitVAArg(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitVAEnd(CodeGen& cg, ir::Instruction& instr) = 0;
    
    // Memory operations
    virtual void emitLoad(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitStore(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitAlloc(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitSyscall(CodeGen& cg, ir::Instruction& instr) {
        (void)cg; (void)instr;
    }
    
    // Control flow
    virtual void emitBr(CodeGen& cg, ir::Instruction& instr) = 0;
    virtual void emitJmp(CodeGen& cg, ir::Instruction& instr) = 0;
    
    // === SIMD/Vector Support ===
    // Vector capabilities
    virtual VectorCapabilities getVectorCapabilities() const = 0;
    virtual bool supportsVectorWidth(unsigned width) const = 0;
    virtual bool supportsVectorType(const ir::VectorType* type) const = 0;
    virtual unsigned getOptimalVectorWidth(const ir::Type* elementType) const = 0;
    
    // Vector instruction emission
    virtual void emitVectorLoad(CodeGen& cg, ir::VectorInstruction& instr) = 0;
    virtual void emitVectorStore(CodeGen& cg, ir::VectorInstruction& instr) = 0;
    virtual void emitVectorArithmetic(CodeGen& cg, ir::VectorInstruction& instr) = 0;
    virtual void emitVectorLogical(CodeGen& cg, ir::VectorInstruction& instr) = 0;
    virtual void emitVectorNeg(CodeGen& cg, ir::VectorInstruction& instr) = 0;
    virtual void emitVectorNot(CodeGen& cg, ir::VectorInstruction& instr) {
        (void)cg; (void)instr;
    }
    virtual void emitVectorComparison(CodeGen& cg, ir::VectorInstruction& instr) = 0;
    virtual void emitVectorShuffle(CodeGen& cg, ir::VectorInstruction& instr) = 0;
    virtual void emitVectorBroadcast(CodeGen& cg, ir::VectorInstruction& instr) = 0;
    virtual void emitVectorExtract(CodeGen& cg, ir::VectorInstruction& instr) = 0;
    virtual void emitVectorInsert(CodeGen& cg, ir::VectorInstruction& instr) = 0;
    
    // Advanced SIMD operations
    virtual void emitVectorGather(CodeGen& cg, ir::VectorInstruction& instr) {
        (void)cg; (void)instr;
    }
    virtual void emitVectorScatter(CodeGen& cg, ir::VectorInstruction& instr) {
        (void)cg; (void)instr;
    }
    virtual void emitVectorHorizontalOp(CodeGen& cg, ir::VectorInstruction& instr) {
        (void)cg; (void)instr;
    }
    virtual void emitVectorReduction(CodeGen& cg, ir::VectorInstruction& instr) {
        (void)cg; (void)instr;
    }
    
    // === Fused Instruction Support ===
    virtual bool supportsFusedPattern(FusedPattern pattern) const = 0;
    virtual void emitFusedMultiplyAdd(CodeGen& cg, const ir::FusedInstruction& instr) {
        (void)cg; (void)instr;
    }
    virtual void emitFusedMultiplySubtract(CodeGen& cg, const ir::FusedInstruction& instr) {
        (void)cg; (void)instr;
    }
    virtual void emitComplexAddressing(CodeGen& cg, ir::Instruction& instr) {
        (void)cg; (void)instr;
    }
    virtual void emitLoadAndOperate(CodeGen& cg, ir::Instruction& load, ir::Instruction& op) {
        (void)cg; (void)load; (void)op;
    }
    
    // === Debug and Runtime Support ===
    virtual void emitDebugInfo(CodeGen& cg, const ir::Function& func) {
        (void)cg; (void)func;
    }
    virtual void emitLineInfo(CodeGen& cg, unsigned line, const std::string& file) {
        (void)cg; (void)line; (void)file;
    }
    virtual void emitProfilingHook(CodeGen& cg, const std::string& hookName) {
        (void)cg; (void)hookName;
    }
    virtual void emitStackUnwindInfo(CodeGen& cg, const ir::Function& func) {
        (void)cg; (void)func;
    }
    
    // SIMD instruction helpers
    virtual SIMDContext createSIMDContext(const ir::VectorType* type) const;
    virtual std::string getVectorRegister(const std::string& baseReg, unsigned width) const;
    virtual std::string getVectorInstruction(const std::string& baseInstr, const SIMDContext& ctx) const;
    
    // Utility functions
    virtual std::string formatConstant(const ir::ConstantInt* C) const;
    virtual std::string formatStackOperand(int offset) const = 0;
    virtual std::string formatGlobalOperand(const std::string& name) const = 0;
    virtual std::string getImmediatePrefix() const { return "$"; }
    virtual std::string getLabelPrefix() const { return "L"; }
    virtual std::string getAssemblyFileExtension() const { return ".s"; }
    virtual std::string getObjectFileExtension() const { return ".o"; }
    virtual std::string getExecutableFileExtension() const { return ""; }
    virtual std::string getDataRelocationType() const { return "R_X86_64_64"; }
    
    // Helper functions for register allocation
    virtual bool isCallerSaved(const std::string& reg) const = 0;
    virtual bool isCalleeSaved(const std::string& reg) const = 0;
    virtual bool isReserved(const std::string& reg) const {
        (void)reg;
        return false;
    }

    // Helper for stack offsets
    virtual int32_t getStackOffset(const CodeGen& cg, ir::Value* val) const;
    virtual void resetStackOffset() { currentStackOffset = 0; }
    virtual int32_t getCurrentStackOffset() const { return currentStackOffset; }
    virtual void setCurrentStackOffset(int32_t offset) { currentStackOffset = offset; }

protected:
    int32_t currentStackOffset = 0;
};

} // namespace target
} // namespace codegen
