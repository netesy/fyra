#pragma once
#include "X86_64Base.h"

namespace codegen {
namespace target {

class Windows_x64 : public X86_64Base {
private:
    std::vector<std::string> integerArgRegs;
    std::vector<std::string> floatArgRegs;
protected:
    void initRegisters() override;
public:
    Windows_x64();
    std::string getName() const override { return "win64"; }
    const std::vector<std::string>& getIntegerArgumentRegisters() const override { return integerArgRegs; }
    const std::vector<std::string>& getFloatArgumentRegisters() const override { return floatArgRegs; }
    const std::string& getIntegerReturnRegister() const override { return intReturnReg; }
    const std::string& getFloatReturnRegister() const override { return floatReturnReg; }
    size_t getMaxRegistersForArgs() const override { return 4; }

    void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    void emitFunctionEpilogue(CodeGen& cg, ir::Function& func) override;
    
    void emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) override;
    void emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) override;

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
    void emitNot(CodeGen& cg, ir::Instruction& instr) override;
    void emitCopy(CodeGen& cg, ir::Instruction& instr) override;
    void emitCall(CodeGen& cg, ir::Instruction& instr) override;

    void emitFAdd(CodeGen& cg, ir::Instruction& instr) override;
    void emitFSub(CodeGen& cg, ir::Instruction& instr) override;
    void emitFMul(CodeGen& cg, ir::Instruction& instr) override;
    void emitFDiv(CodeGen& cg, ir::Instruction& instr) override;
    
    void emitCmp(CodeGen& cg, ir::Instruction& instr) override;
    void emitCast(CodeGen& cg, ir::Instruction& instr, const ir::Type* fromType, const ir::Type* toType) override;
    
    void emitLoad(CodeGen& cg, ir::Instruction& instr) override;
    void emitStore(CodeGen& cg, ir::Instruction& instr) override;
    void emitAlloc(CodeGen& cg, ir::Instruction& instr) override;
    
    void emitBr(CodeGen& cg, ir::Instruction& instr) override;
    void emitJmp(CodeGen& cg, ir::Instruction& instr) override;

    void emitVAStart(CodeGen& cg, ir::Instruction& instr) override;
    void emitVAArg(CodeGen& cg, ir::Instruction& instr) override;
};

}
}
