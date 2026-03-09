#pragma once

#include "X86_64Base.h"

namespace codegen {
namespace target {

class Windows_x64 : public X86_64Base {
private:
    // Argument registers for Windows x64 ABI
    std::vector<std::string> integerArgRegs;
    std::vector<std::string> floatArgRegs;
    
protected:
    void initRegisters() override;
    
public:
    Windows_x64();
    
    // Target properties
    std::string getName() const override { return "x86_64-pc-windows-msvc"; }
    
    // Register sets for function calls
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
    
    // Stack frame management
    void emitPrologue(CodeGen& cg, int stack_size) override;
    void emitEpilogue(CodeGen& cg) override;
    void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    void emitFunctionEpilogue(CodeGen& cg, ir::Function& func) override;
    
    // Missing methods from TargetInfo
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
    void emitLoad(CodeGen& cg, ir::Instruction& instr) override;
    void emitStore(CodeGen& cg, ir::Instruction& instr) override;
    void emitAlloc(CodeGen& cg, ir::Instruction& instr) override;
    void emitSyscall(CodeGen& cg, ir::Instruction& instr) override;
    void emitBr(CodeGen& cg, ir::Instruction& instr) override;
    void emitJmp(CodeGen& cg, ir::Instruction& instr) override;
    
    // Arithmetic operations
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
    void emitRet(CodeGen& cg, ir::Instruction& instr) override;
    void emitCall(CodeGen& cg, ir::Instruction& instr) override;

    // === SIMD/Vector Support ===
    void emitVectorAdd(CodeGen& cg,
                      ir::VectorInstruction& instr);
    void emitVectorSub(CodeGen& cg,
                      ir::VectorInstruction& instr);
    void emitVectorMul(CodeGen& cg,
                      ir::VectorInstruction& instr);
    void emitVectorDiv(CodeGen& cg,
                      ir::VectorInstruction& instr);
    void emitVectorShl(CodeGen& cg,
                      ir::VectorInstruction& instr);
    void emitVectorShr(CodeGen& cg,
                      ir::VectorInstruction& instr);
    void emitVectorAnd(CodeGen& cg,
                      ir::VectorInstruction& instr);
    void emitVectorOr(CodeGen& cg,
                     ir::VectorInstruction& instr);
    void emitVectorXor(CodeGen& cg,
                      ir::VectorInstruction& instr);
    void emitVectorNeg(CodeGen& cg,
                      ir::VectorInstruction& instr);
    void emitVectorNot(CodeGen& cg,
                      ir::VectorInstruction& instr);
    void emitVectorLoad(CodeGen& cg,
                       ir::VectorInstruction& instr) override;
    void emitVectorStore(CodeGen& cg,
                        ir::VectorInstruction& instr) override;
    void emitVectorShuffle(CodeGen& cg,
                          ir::VectorInstruction& instr) override;
    void emitVectorExtract(CodeGen& cg,
                                ir::VectorInstruction& instr) override;
    void emitVectorInsert(CodeGen& cg,
                               ir::VectorInstruction& instr) override;
    void emitVectorExtractElement(CodeGen& cg,
                                  ir::VectorInstruction& instr);
    void emitVectorInsertElement(CodeGen& cg,
                                 ir::VectorInstruction& instr);
    
    // === Fused Instruction Support ===
    void emitFusedMultiplyAdd(CodeGen& cg,
                             const ir::FusedInstruction& instr) override;
    void emitLoadAndOperate(CodeGen& cg,
                           ir::Instruction& load, ir::Instruction& op) override;
    void emitComplexAddressing(CodeGen& cg,
                              ir::Instruction& instr) override;
    
private:
    // Helper functions for vector operations
    std::string getVectorSuffix(const ir::Type* elemType, unsigned elemCount) const;
    std::string getVectorWidthPrefix(unsigned bitWidth) const;
};

} // namespace target
} // namespace codegen