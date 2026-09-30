#pragma once
#include "target/core/ArchitectureInfo.h"
#include <map>
#include <string>

namespace target {

class BPFArchitecture : public ArchitectureInfo {
public:
    BPFArchitecture();
    ~BPFArchitecture() override = default;

    Arch getArch() const override { return Arch::BPF; }
    size_t getPointerSize() const override { return 8; }
    size_t getStackAlignment() const override { return 8; }

    TypeInfo getTypeInfo(const ir::Type* type) const override;
    const std::vector<std::string>& getRegisters(RegisterClass regClass) const override;
    const std::string& getReturnRegister(const ir::Type* type) const override;
    const std::vector<std::string>& getIntegerArgumentRegisters() const override { return integerArgRegs; }
    const std::vector<std::string>& getFloatArgumentRegisters() const override { return floatArgRegs; }
    const std::string& getIntegerReturnRegister() const override { return intReturnReg; }
    const std::string& getFloatReturnRegister() const override { return floatReturnReg; }
    size_t getMaxRegistersForArgs() const override { return 5; }

    void emitHeader(codegen::CodeGen& cg) override;
    void emitFooter(codegen::CodeGen& cg) override {}
    void emitFunctionPrologue(codegen::CodeGen& cg, ir::Function& func) override;
    void emitFunctionEpilogue(codegen::CodeGen& cg, ir::Function& func) override;

    void emitPassArgument(codegen::CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) override;
    void emitGetArgument(codegen::CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) override;

    void emitRet(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitAdd(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitSub(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitMul(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitDiv(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitRem(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitAnd(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitOr(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitXor(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitShl(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitShr(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitSar(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitNeg(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitNot(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitCopy(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitCall(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitFAdd(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitFSub(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitFMul(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitFDiv(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitCmp(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitCast(codegen::CodeGen& cg, ir::Instruction& i, const ir::Type* from, const ir::Type* to) override;
    void emitVAStart(codegen::CodeGen& cg, ir::Instruction& i) override {}
    void emitVAArg(codegen::CodeGen& cg, ir::Instruction& i) override {}
    void emitLoad(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitStore(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitAlloc(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitBr(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitJmp(codegen::CodeGen& cg, ir::Instruction& i) override;

    void emitSyscall(codegen::CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) override {}
    void emitExternCall(codegen::CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) override {}
    void emitNativeSyscall(codegen::CodeGen& cg, uint64_t syscallNum, const std::vector<ir::Value*>& args) override {}
    void emitNativeLibraryCall(codegen::CodeGen& cg, const std::string& name, const std::vector<ir::Value*>& args) override {}

    std::string formatStackOperand(int offset) const override;
    std::string formatGlobalOperand(const std::string& name) const override;
    bool isCallerSaved(const std::string& reg) const override;
    bool isCalleeSaved(const std::string& reg) const override;

    bool validateLegality(ir::Function& func, std::string& errorMsg) const;

private:
    std::vector<std::string> integerRegs;
    std::vector<std::string> integerArgRegs;
    std::vector<std::string> floatArgRegs;
    std::string intReturnReg;
    std::string floatReturnReg;

    void initRegisters();
    uint8_t getBpfRegIndex(const std::string& regName) const;
    void emitBpfInst(codegen::CodeGen& cg, uint8_t opcode, uint8_t dst, uint8_t src, int16_t off, int32_t imm);
};

} // namespace target
