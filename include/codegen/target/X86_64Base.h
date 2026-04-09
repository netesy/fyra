#ifndef FYRA_X86_64_BASE_H
#define FYRA_X86_64_BASE_H
#include "codegen/target/TargetInfo.h"
#include <string>
#include <vector>
#include <map>
namespace codegen {
namespace execgen { class Assembler; }
namespace target {
class X86_64Base : public TargetInfo {
public:
    X86_64Base();
    virtual void initRegisters() {}
    virtual TypeInfo getTypeInfo(const ir::Type* type) const override;
    virtual const std::vector<std::string>& getRegisters(RegisterClass regClass) const override;
    virtual const std::string& getReturnRegister(const ir::Type* type) const override;
    virtual const std::vector<std::string>& getIntegerArgumentRegisters() const override { return integerArgRegs; }
    virtual const std::vector<std::string>& getFloatArgumentRegisters() const override { return floatArgRegs; }
    virtual const std::string& getIntegerReturnRegister() const override { return intReturnReg; }
    virtual const std::string& getFloatReturnRegister() const override { return floatReturnReg; }
    virtual size_t getMaxRegistersForArgs() const override { return integerArgRegs.size(); }
    virtual bool isCallerSaved(const std::string& reg) const override;
    virtual bool isCalleeSaved(const std::string& reg) const override;
    virtual bool isReserved(const std::string& reg) const override;
    virtual void emitBasicBlockStart(CodeGen& cg, ir::BasicBlock& bb) override;
    void emitRegMem(execgen::Assembler& as, uint8_t rex, uint8_t opcode, uint8_t reg, int32_t offset);
    void emitLoadValue(CodeGen& cg, execgen::Assembler& as, ir::Value* v, uint8_t regIdx);
    void emitStoreResult(CodeGen& cg, ir::Instruction& instr, uint8_t regIdx);
    uint8_t getRex(const ir::Type* t);
    uint8_t getOpcode(uint8_t baseOp, const ir::Type* t);
    uint8_t getArchRegIndex(const std::string& regName);

    virtual std::string getName() const override { return "x86_64-base"; }
    virtual void emitFunctionPrologue(CodeGen&, ir::Function&) override {}
    virtual void emitFunctionEpilogue(CodeGen&, ir::Function&) override {}
    virtual void emitRet(CodeGen&, ir::Instruction&) override {}
    virtual void emitAdd(CodeGen&, ir::Instruction&) override {}
    virtual void emitSub(CodeGen&, ir::Instruction&) override {}
    virtual void emitMul(CodeGen&, ir::Instruction&) override {}
    virtual void emitDiv(CodeGen&, ir::Instruction&) override {}
    virtual void emitRem(CodeGen&, ir::Instruction&) override {}
    virtual void emitAnd(CodeGen&, ir::Instruction&) override {}
    virtual void emitOr(CodeGen&, ir::Instruction&) override {}
    virtual void emitXor(CodeGen&, ir::Instruction&) override {}
    virtual void emitShl(CodeGen&, ir::Instruction&) override {}
    virtual void emitShr(CodeGen&, ir::Instruction&) override {}
    virtual void emitSar(CodeGen&, ir::Instruction&) override {}
    virtual void emitNeg(CodeGen&, ir::Instruction&) override {}
    virtual void emitNot(CodeGen&, ir::Instruction&) override {}
    virtual void emitCopy(CodeGen&, ir::Instruction&) override {}
    virtual void emitCall(CodeGen&, ir::Instruction&) override {}
    virtual void emitFAdd(CodeGen&, ir::Instruction&) override {}
    virtual void emitFSub(CodeGen&, ir::Instruction&) override {}
    virtual void emitFMul(CodeGen&, ir::Instruction&) override {}
    virtual void emitFDiv(CodeGen&, ir::Instruction&) override {}
    virtual void emitCmp(CodeGen&, ir::Instruction&) override {}
    virtual void emitCast(CodeGen&, ir::Instruction&, const ir::Type*, const ir::Type*) override {}
    virtual void emitVAStart(CodeGen&, ir::Instruction&) override {}
    virtual void emitVAArg(CodeGen&, ir::Instruction&) override {}
    virtual void emitLoad(CodeGen&, ir::Instruction&) override {}
    virtual void emitStore(CodeGen&, ir::Instruction&) override {}
    virtual void emitAlloc(CodeGen&, ir::Instruction&) override {}
    virtual void emitBr(CodeGen&, ir::Instruction&) override {}
    virtual void emitJmp(CodeGen&, ir::Instruction&) override {}
    virtual void emitPassArgument(CodeGen&, size_t, const std::string&, const ir::Type*) override {}
    virtual void emitGetArgument(CodeGen&, size_t, const std::string&, const ir::Type*) override {}
    virtual std::string formatStackOperand(int offset) const override { return std::to_string(offset) + "(%rbp)"; }
    virtual std::string formatGlobalOperand(const std::string& name) const override { return name + "(%rip)"; }

protected:
    std::map<std::string, uint8_t> regToIdx;
    std::vector<std::string> integerRegs, floatRegs, vectorRegs;
    std::map<std::string, bool> callerSaved, calleeSaved;
    std::string intReturnReg, floatReturnReg, framePtrReg, stackPtrReg;
    std::vector<std::string> integerArgRegs, floatArgRegs;
};
}
}
#endif
