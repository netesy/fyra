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
    virtual void initRegisters() override {}
    X86_64Base();
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
    virtual void emitBasicBlockStart(CodeGen& cg, ir::BasicBlock& bb) override;
    void emitRegMem(execgen::Assembler& as, uint8_t rex, uint8_t opcode, uint8_t reg, int32_t offset);
    void emitLoadValue(CodeGen& cg, execgen::Assembler& as, ir::Value* v, uint8_t regIdx);
    void emitStoreResult(CodeGen& cg, ir::Instruction& instr, uint8_t regIdx);
    uint8_t getRex(const ir::Type* t);
    uint8_t getOpcode(uint8_t baseOp, const ir::Type* t);
    uint8_t getArchRegIndex(const std::string& regName);
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
