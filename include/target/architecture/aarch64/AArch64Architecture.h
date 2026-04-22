#pragma once
#include "target/core/ArchitectureInfo.h"
#include <set>

namespace codegen {
namespace execgen { class Assembler; }
namespace target {

class AArch64Architecture : public ArchitectureInfo {
public:
    AArch64Architecture();

    size_t getPointerSize() const override { return 64; }
    TypeInfo getTypeInfo(const ir::Type* type) const override;
    const std::vector<std::string>& getRegisters(RegisterClass regClass) const override;
    const std::string& getReturnRegister(const ir::Type* type) const override;
    const std::vector<std::string>& getIntegerArgumentRegisters() const override;
    const std::vector<std::string>& getFloatArgumentRegisters() const override;
    const std::string& getIntegerReturnRegister() const override;
    const std::string& getFloatReturnRegister() const override;

    void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    void emitFunctionEpilogue(CodeGen& cg, ir::Function& func) override;
    void emitStartFunction(CodeGen& cg) override;

    size_t getMaxRegistersForArgs() const override { return 8; }
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
    std::string getRegisterName(const std::string& base, const ir::Type* type) const override;
    std::string getImmediatePrefix() const override { return "#"; }

private:
    void emitLoadValue(CodeGen& cg, class execgen::Assembler& assembler, ir::Value* val, uint8_t reg);
    std::string getWRegister(const std::string& xReg) const;
    size_t align_to_16(size_t size) const { return (size + 15) & ~15; }
    std::string getConditionCode(const std::string& op, bool isFloat = false, bool isUnsigned = false) const;
    int32_t currentStackOffset = 0;
};

}
}
