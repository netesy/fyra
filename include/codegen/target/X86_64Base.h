#pragma once

#include "TargetInfo.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace codegen {
namespace target {

class X86_64Base : public TargetInfo {
protected:
    // Register sets
    std::vector<std::string> integerRegs;
    std::vector<std::string> floatRegs;
    std::vector<std::string> vectorRegs;
    
    // Caller-saved registers
    std::unordered_map<std::string, bool> callerSaved;
    
    // Callee-saved registers
    std::unordered_map<std::string, bool> calleeSaved;
    
    // Return registers
    std::string intReturnReg;
    std::string floatReturnReg;
    
    // Stack pointer register
    std::string stackPtrReg;
    
    // Frame pointer register
    std::string framePtrReg;
    
    // Initialize register sets (to be implemented by derived classes)
    virtual void initRegisters() = 0;
    
public:
    X86_64Base();
    
    // Target properties
    std::string getName() const override { return "x86_64"; }
    size_t getPointerSize() const override { return 8; } // 64-bit pointers
    size_t getStackAlignment() const override { return 16; } // 16-byte stack alignment
    std::string formatStackOperand(int offset) const override;
    std::string formatGlobalOperand(const std::string& name) const override;
    
    // Type information
    TypeInfo getTypeInfo(const ir::Type* type) const override;
    
    // Register information
    const std::vector<std::string>& getRegisters(RegisterClass regClass) const override;
    const std::string& getReturnRegister(const ir::Type* type) const override;
    
    // Stack frame management
    void emitPrologue(CodeGen& cg, int stack_size) override;
    void emitEpilogue(CodeGen& cg) override;
    void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    void emitFunctionEpilogue(CodeGen& cg, ir::Function& func) override;
    
    // Register allocation helpers
    bool isCallerSaved(const std::string& reg) const override;
    bool isCalleeSaved(const std::string& reg) const override;
    
    // Register sets for function calls
    virtual const std::vector<std::string>& getIntegerArgumentRegisters() const = 0;
    virtual const std::vector<std::string>& getFloatArgumentRegisters() const = 0;
    virtual const std::string& getIntegerReturnRegister() const = 0;
    virtual const std::string& getFloatReturnRegister() const = 0;
    
    // Common instruction emission (to be overridden by ABI-specific implementations)
    void emitRet(CodeGen& cg, ir::Instruction& instr) override;
    void emitCall(CodeGen& cg, ir::Instruction& instr) override;
    
    // Default variadic argument handling
    void emitVAEnd(CodeGen& cg, ir::Instruction& instr) override;
    
    // Memory operations
    std::string getLoadStoreSuffix(const ir::Type* type) const;
    std::string getMoveInstruction(const ir::Type* type) const;
    std::string getRegisterName(const std::string& baseReg, const ir::Type* type) const;
    
    // === SIMD/Vector Support ===
    VectorCapabilities getVectorCapabilities() const override;
    bool supportsVectorWidth(unsigned width) const override;
    bool supportsVectorType(const ir::VectorType* type) const override;
    unsigned getOptimalVectorWidth(const ir::Type* elementType) const override;
    
    // Vector instruction emission
    void emitVectorLoad(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorStore(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorArithmetic(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorLogical(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorNeg(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorNot(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorComparison(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorShuffle(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorBroadcast(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorExtract(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorInsert(CodeGen& cg, ir::VectorInstruction& instr) override;
    
    // Advanced SIMD operations
    void emitVectorGather(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorScatter(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorHorizontalOp(CodeGen& cg, ir::VectorInstruction& instr) override;
    
    // Fused instruction support
    bool supportsFusedPattern(FusedPattern pattern) const override;
    void emitFusedMultiplyAdd(CodeGen& cg, const ir::FusedInstruction& instr) override;
    
    // SIMD helper methods
    std::string getVectorRegister(const std::string& baseReg, unsigned width) const override;
    std::string getVectorInstruction(const std::string& baseInstr, const SIMDContext& ctx) const override;
    std::string getVectorSuffix(const ir::VectorType* type) const;
    std::string getVectorRegisterPrefix(unsigned width) const;
    
    // SSE/AVX specific helpers
    virtual bool hasSSE() const { return true; }
    virtual bool hasSSE2() const { return true; }
    virtual bool hasSSE3() const { return true; }
    virtual bool hasAVX() const { return false; } // Override in derived classes
    virtual bool hasAVX2() const { return false; }
    virtual bool hasAVX512() const { return false; }
    virtual bool hasFMA() const { return false; }
    
    // Vector register management
    std::string allocateVectorRegister(unsigned width) const;
    void deallocateVectorRegister(const std::string& reg) const;
};

} // namespace target
} // namespace codegen