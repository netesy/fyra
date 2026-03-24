#pragma once

#include "TargetInfo.h"

namespace codegen {
namespace target {

class RiscV64 : public TargetInfo {
public:
    RiscV64();
    ~RiscV64();
    // Target properties
    std::string getName() const override { return "riscv64"; }
    std::string getImmediatePrefix() const override { return ""; }
    size_t getPointerSize() const override;
    std::string formatStackOperand(int offset) const override;
    std::string formatGlobalOperand(const std::string& name) const override;

    // Type information
    TypeInfo getTypeInfo(const ir::Type* type) const override;

    // Register information
    const std::vector<std::string>& getRegisters(RegisterClass regClass) const override;
    const std::string& getReturnRegister(const ir::Type* type) const override;

    // Enhanced calling convention support
    const std::vector<std::string>& getIntegerArgumentRegisters() const override;
    const std::vector<std::string>& getFloatArgumentRegisters() const override;
    const std::string& getIntegerReturnRegister() const override;
    const std::string& getFloatReturnRegister() const override;

    // Stack frame management
    void emitPrologue(CodeGen& cg, int stack_size) override;
    void emitEpilogue(CodeGen& cg) override;
    void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    void emitFunctionEpilogue(CodeGen& cg, ir::Function& func) override;

    // Executable entry point
    void emitStartFunction(CodeGen& cg) override;

    // Argument passing
    size_t getMaxRegistersForArgs() const override { return 8; }
    void emitPassArgument(CodeGen& cg, size_t argIndex,
                                const std::string& value, const ir::Type* type) override;
    void emitGetArgument(CodeGen& cg, size_t argIndex,
                               const std::string& dest, const ir::Type* type) override;

    // Instruction emission
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
    void emitSyscall(CodeGen& cg, ir::Instruction& instr) override;
    uint64_t getSyscallNumber(ir::SyscallId id) const override;
    void emitShr(CodeGen& cg, ir::Instruction& instr) override;
    void emitSar(CodeGen& cg, ir::Instruction& instr) override;
    void emitNeg(CodeGen& cg, ir::Instruction& instr) override;
    void emitNot(CodeGen& cg, ir::Instruction& instr) override;
    void emitCopy(CodeGen& cg, ir::Instruction& instr) override;
    void emitCall(CodeGen& cg, ir::Instruction& instr) override;

    // Floating point operations
    void emitFAdd(CodeGen& cg, ir::Instruction& instr) override;
    void emitFSub(CodeGen& cg, ir::Instruction& instr) override;
    void emitFMul(CodeGen& cg, ir::Instruction& instr) override;
    void emitFDiv(CodeGen& cg, ir::Instruction& instr) override;

    // Comparison operations
    void emitCmp(CodeGen& cg, ir::Instruction& instr) override;

    // Type conversion
    void emitCast(CodeGen& cg, ir::Instruction& instr,
                         const ir::Type* fromType, const ir::Type* toType) override;

    // Variadic arguments
    void emitVAStart(CodeGen& cg, ir::Instruction& instr) override;
    void emitVAArg(CodeGen& cg, ir::Instruction& instr) override;
    void emitVAEnd(CodeGen& cg, ir::Instruction& instr) override;

    // Memory operations
    void emitLoad(CodeGen& cg, ir::Instruction& instr) override;
    void emitStore(CodeGen& cg, ir::Instruction& instr) override;
    void emitAlloc(CodeGen& cg, ir::Instruction& instr) override;

    // Control flow
    void emitBr(CodeGen& cg, ir::Instruction& instr) override;
    void emitJmp(CodeGen& cg, ir::Instruction& instr) override;

    // === SIMD/Vector Support ===
    VectorCapabilities getVectorCapabilities() const override;
    bool supportsVectorWidth(unsigned width) const override;
    bool supportsVectorType(const ir::VectorType* type) const override;
    unsigned getOptimalVectorWidth(const ir::Type* elementType) const override;

    void emitVectorLoad(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorStore(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorArithmetic(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorLogical(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorNeg(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorComparison(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorShuffle(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorBroadcast(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorExtract(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorInsert(CodeGen& cg, ir::VectorInstruction& instr) override;

    // === Fused Instruction Support ===
    bool supportsFusedPattern(FusedPattern pattern) const override;
    void emitFusedMultiplyAdd(CodeGen& cg, const ir::FusedInstruction& instr) override;
    void emitComplexAddressing(CodeGen& cg, ir::Instruction& instr) override;
    void emitLoadAndOperate(CodeGen& cg, ir::Instruction& load, ir::Instruction& op) override;

    // === Debug and Runtime Support ===
    void emitDebugInfo(CodeGen& cg, const ir::Function& func) override;
    void emitLineInfo(CodeGen& cg, unsigned line, const std::string& file) override;
    void emitProfilingHook(CodeGen& cg, const std::string& hookName) override;
    void emitStackUnwindInfo(CodeGen& cg, const ir::Function& func) override;

    // SIMD instruction helpers
    SIMDContext createSIMDContext(const ir::VectorType* type) const;
    std::string getVectorRegister(const std::string& baseReg, unsigned width) const;
    std::string getVectorInstruction(const std::string& baseInstr, const SIMDContext& ctx) const;
    void emitVectorGather(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorScatter(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorHorizontalOp(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitVectorReduction(CodeGen& cg, ir::VectorInstruction& instr) override;
    void emitFusedMultiplySubtract(CodeGen& cg, const ir::FusedInstruction& instr) override;

    // Helper functions for register allocation
    bool isCallerSaved(const std::string& reg) const override;
    bool isCalleeSaved(const std::string& reg) const override;
};

} // namespace target
} // namespace codegen