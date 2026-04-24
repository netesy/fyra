#pragma once
#include "target/core/ArchitectureInfo.h"
#include <map>

namespace codegen {
namespace asm_ { class Assembler; }
namespace target {

enum class X64ABI { SystemV, Windows };

class X64Architecture : public ArchitectureInfo {
public:
    X64Architecture(X64ABI abi);

    size_t getPointerSize() const override { return 8; }
    size_t getStackAlignment() const override { return 16; }
    TypeInfo getTypeInfo(const ir::Type* type) const override;
    const std::vector<std::string>& getRegisters(RegisterClass regClass) const override;
    const std::string& getReturnRegister(const ir::Type* type) const override;
    const std::vector<std::string>& getIntegerArgumentRegisters() const override { return integerArgRegs; }
    const std::vector<std::string>& getFloatArgumentRegisters() const override { return floatArgRegs; }
    const std::string& getIntegerReturnRegister() const override { return intReturnReg; }
    const std::string& getFloatReturnRegister() const override { return floatReturnReg; }

    void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    void emitFunctionEpilogue(CodeGen& cg, ir::Function& func) override;
    void emitHeader(CodeGen& cg) override;

    size_t getMaxRegistersForArgs() const override { return integerArgRegs.size(); }
    void emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) override;
    void emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) override;

    void emitRet(CodeGen& cg, ir::Instruction& i) override;
    void emitAdd(CodeGen& cg, ir::Instruction& i) override;
    void emitSub(CodeGen& cg, ir::Instruction& i) override;
    void emitMul(CodeGen& cg, ir::Instruction& i) override;
    void emitDiv(CodeGen& cg, ir::Instruction& i) override;
    void emitRem(CodeGen& cg, ir::Instruction& i) override;
    void emitAnd(CodeGen& cg, ir::Instruction& i) override;
    void emitOr(CodeGen& cg, ir::Instruction& i) override;
    void emitXor(CodeGen& cg, ir::Instruction& i) override;
    void emitShl(CodeGen& cg, ir::Instruction& i) override;
    void emitShr(CodeGen& cg, ir::Instruction& i) override;
    void emitSar(CodeGen& cg, ir::Instruction& i) override;
    void emitNeg(CodeGen& cg, ir::Instruction& i) override;
    void emitNot(CodeGen& cg, ir::Instruction& i) override;
    void emitCopy(CodeGen& cg, ir::Instruction& i) override;
    void emitCall(CodeGen& cg, ir::Instruction& i) override;
    void emitFAdd(CodeGen& cg, ir::Instruction& i) override;
    void emitFSub(CodeGen& cg, ir::Instruction& i) override;
    void emitFMul(CodeGen& cg, ir::Instruction& i) override;
    void emitFDiv(CodeGen& cg, ir::Instruction& i) override;
    void emitCmp(CodeGen& cg, ir::Instruction& i) override;
    void emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* from, const ir::Type* to) override;
    void emitVAStart(CodeGen& cg, ir::Instruction& i) override;
    void emitVAArg(CodeGen& cg, ir::Instruction& i) override;
    void emitLoad(CodeGen& cg, ir::Instruction& i) override;
    void emitStore(CodeGen& cg, ir::Instruction& i) override;
    void emitAlloc(CodeGen& cg, ir::Instruction& i) override;
    void emitBr(CodeGen& cg, ir::Instruction& i) override;
    void emitJmp(CodeGen& cg, ir::Instruction& i) override;

    void emitSyscall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) override;
    void emitExternCall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) override;

    std::string formatStackOperand(int offset) const override;
    std::string formatGlobalOperand(const std::string& name) const override;
    bool isCallerSaved(const std::string& reg) const override;
    bool isCalleeSaved(const std::string& reg) const override;
    bool isReserved(const std::string& reg) const override;
    std::string getRegisterName(const std::string& base, const ir::Type* type) const override;

private:
    X64ABI abi;
    std::vector<std::string> integerRegs, floatRegs, vectorRegs;
    std::vector<std::string> integerArgRegs, floatArgRegs;
    std::string intReturnReg, floatReturnReg, framePtrReg, stackPtrReg;
    std::map<std::string, bool> callerSaved, calleeSaved;
    void initRegisters();

    void emitRegMem(class asm_::Assembler& as, uint8_t rex, uint8_t opcode, uint8_t reg, int32_t offset);
    void emitLoadValue(CodeGen& cg, class asm_::Assembler& as, ir::Value* v, uint8_t regIdx);
    void emitStoreResult(CodeGen& cg, ir::Instruction& instr, uint8_t regIdx);
    uint8_t getRex(const ir::Type* t);
    uint8_t getOpcode(uint8_t baseOp, const ir::Type* t);
    uint8_t getArchRegIndex(const std::string& regName);
};

}
}
