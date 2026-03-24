#pragma once

#include "TargetInfo.h"
#include "codegen/execgen/Assembler.h"

namespace codegen {
namespace target {

class AArch64 : public TargetInfo {
public:
    // Core TargetInfo methods
    std::string getName() const override;
    std::string getImmediatePrefix() const override;
    size_t getPointerSize() const override;
    TypeInfo getTypeInfo(const ir::Type* type) const override;
    std::string formatStackOperand(int offset) const override;
    std::string formatGlobalOperand(const std::string& name) const override;
    
    // Register information
    const std::vector<std::string>& getRegisters(RegisterClass regClass) const override;
    const std::string& getReturnRegister(const ir::Type* type) const override;
    
    // Stack frame management
    void emitPrologue(CodeGen& cg, int stack_size) override;
    void emitEpilogue(CodeGen& cg) override;
    void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    void emitFunctionEpilogue(CodeGen& cg, ir::Function& func) override;
    void emitStartFunction(CodeGen& cg) override;
    
    // Enhanced calling convention support
    const std::vector<std::string>& getIntegerArgumentRegisters() const override;
    const std::vector<std::string>& getFloatArgumentRegisters() const override;
    const std::string& getIntegerReturnRegister() const override;
    const std::string& getFloatReturnRegister() const override;
    
    // Argument passing
    size_t getMaxRegistersForArgs() const override;
    void emitPassArgument(CodeGen& cg, size_t argIndex,
                        const std::string& value, const ir::Type* type) override;
    void emitGetArgument(CodeGen& cg, size_t argIndex,
                       const std::string& dest, const ir::Type* type) override;

    // Instruction emission - existing methods
    void emitRet(CodeGen& cg, ir::Instruction& instr) override;
    void emitAdd(CodeGen& cg, ir::Instruction& instr) override;
    void emitSub(CodeGen& cg, ir::Instruction& instr) override;
    void emitMul(CodeGen& cg, ir::Instruction& instr) override;
    void emitDiv(CodeGen& cg, ir::Instruction& instr) override;
    void emitRem(CodeGen& cg, ir::Instruction& instr) override;
    void emitAnd(CodeGen& cg, ir::Instruction& instr) override;
    void emitOr(CodeGen& cg, ir::Instruction& instr) override;
    void emitXor(CodeGen& cg, ir::Instruction& instr) override;
    void emitShl(CodeGen& cg, ir::Instruction& instr) override;
    void emitShr(CodeGen& cg, ir::Instruction& instr) override;
    void emitSar(CodeGen& cg, ir::Instruction& instr) override;
    void emitNeg(CodeGen& cg, ir::Instruction& instr) override;
    void emitCopy(CodeGen& cg, ir::Instruction& instr) override;
    void emitCall(CodeGen& cg, ir::Instruction& instr) override;
    
    // New instruction emission methods
    void emitNot(CodeGen& cg, ir::Instruction& instr) override;

    // Floating point operations
    void emitFAdd(CodeGen& cg, ir::Instruction& instr) override;
    void emitFSub(CodeGen& cg, ir::Instruction& instr) override;
    void emitFMul(CodeGen& cg, ir::Instruction& instr) override;
    void emitFDiv(CodeGen& cg, ir::Instruction& instr) override;

    void emitCmp(CodeGen& cg, ir::Instruction& instr) override;
    void emitCast(CodeGen& cg, ir::Instruction& instr,
                 const ir::Type* fromType, const ir::Type* toType) override;
    void emitVAStart(CodeGen& cg, ir::Instruction& instr) override;
    void emitVAArg(CodeGen& cg, ir::Instruction& instr) override;
    void emitVAEnd(CodeGen& cg, ir::Instruction& instr) override;
    void emitLoad(CodeGen& cg, ir::Instruction& instr) override;
    void emitStore(CodeGen& cg, ir::Instruction& instr) override;
    void emitAlloc(CodeGen& cg, ir::Instruction& instr) override;
    void emitSyscall(CodeGen& cg, ir::Instruction& instr) override;
    uint64_t getSyscallNumber(ir::SyscallId id) const override;
    void emitBr(CodeGen& cg, ir::Instruction& instr) override;
    void emitJmp(CodeGen& cg, ir::Instruction& instr) override;
    
    // === NEON/Vector Support ===
    VectorCapabilities getVectorCapabilities() const override;
    bool supportsVectorWidth(unsigned width) const override;
    bool supportsVectorType(const ir::VectorType* type) const override;
    unsigned getOptimalVectorWidth(const ir::Type* elementType) const override;
    
    // Vector instruction emission
    void emitVectorLoad(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorStore(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorArithmetic(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorLogical(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorComparison(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorShuffle(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorBroadcast(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorExtract(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorInsert(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorNot(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorHorizontalOp(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorReduction(CodeGen& cg, ir::VectorInstruction& instr) override;
    
    // Advanced NEON operations
    void emitVectorNeg(CodeGen& cg, ir::VectorInstruction& instr) override;
    
    // Fused instruction support
    bool supportsFusedPattern(FusedPattern pattern) const override;
    void emitFusedMultiplyAdd(CodeGen& cg, const ir::FusedInstruction& instr) override;
    
    // Register classification
    bool isCallerSaved(const std::string& reg) const override;
    bool isCalleeSaved(const std::string& reg) const override;
    
private:
    void emitAddBinary(CodeGen& cg, ir::Instruction& instr);
    void emitSubBinary(CodeGen& cg, ir::Instruction& instr);
    void emitMulBinary(CodeGen& cg, ir::Instruction& instr);
    void emitDivBinary(CodeGen& cg, ir::Instruction& instr);
    void emitAndBinary(CodeGen& cg, ir::Instruction& instr);
    void emitOrBinary(CodeGen& cg, ir::Instruction& instr);
    void emitXorBinary(CodeGen& cg, ir::Instruction& instr);
    void emitShlBinary(CodeGen& cg, ir::Instruction& instr);
    void emitShrBinary(CodeGen& cg, ir::Instruction& instr);
    void emitSarBinary(CodeGen& cg, ir::Instruction& instr);
    void emitNegBinary(CodeGen& cg, ir::Instruction& instr);
    // Helper methods
    size_t calculateStackArgumentOffset(size_t argIndex, const ir::Type* type) const;
    size_t align_to_16(size_t size) const { return (size + 15) & ~15; }
    std::string getWRegister(const std::string& xReg) const;
    std::string getAArch64InstructionSuffix(const ir::Type* type) const;
    
    // NEON helper methods
    std::string getNEONRegister(const std::string& baseReg, unsigned lanes) const;
    std::string getNEONLanes(const ir::VectorType* type) const;
    std::string getNEONElementType(const ir::Type* elementType) const;
    std::string getNEONInstruction(const std::string& baseInstr, const ir::VectorType* type) const;
    bool hasAdvancedSIMD() const { return true; } // NEON is standard on AArch64
    bool hasCrypto() const { return false; } // Optional crypto extensions
    bool hasSVE() const { return false; } // Scalable Vector Extensions
    
    // Advanced control flow helpers
    void emitConditionalBranch(CodeGen& cg, const std::string& condition,
                              const std::string& trueLabel, const std::string& falseLabel);
    void emitCompareAndBranch(CodeGen& cg, const std::string& reg,
                             const std::string& label, bool branchOnZero = true);
    std::string getConditionCode(const std::string& op, bool isFloat = false, bool isUnsigned = false) const;

protected:
    void emitLoadValue(CodeGen& cg, execgen::Assembler& assembler, ir::Value* val, uint8_t reg);
};

} // namespace target
} // namespace codegen