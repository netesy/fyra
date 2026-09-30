#pragma once

#include "target/core/ArchitectureInfo.h"

namespace target {

class SPIRVArchitecture : public ArchitectureInfo {
public:
    SPIRVArchitecture();

    Arch getArch() const override { return Arch::SPIRV; }
    size_t getPointerSize() const override { return 4; } // 32-bit logical pointers in Vulkan/OpenCL compute
    TypeInfo getTypeInfo(const ir::Type* type) const override;
    const std::vector<std::string>& getRegisters(RegisterClass regClass) const override;
    const std::string& getReturnRegister(const ir::Type* type) const override;
    const std::vector<std::string>& getIntegerArgumentRegisters() const override;
    const std::vector<std::string>& getFloatArgumentRegisters() const override;
    const std::string& getIntegerReturnRegister() const override;
    const std::string& getFloatReturnRegister() const override;

    void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    void emitFunctionEpilogue(CodeGen& cg, ir::Function& func) override;
    void emitHeader(CodeGen& cg) override;
    void emitFooter(CodeGen& cg) override;
    void emitStartFunction(CodeGen& cg) override;

    size_t getMaxRegistersForArgs() const override { return 0; }
    void emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) override;
    void emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) override;

    void emitRet(CodeGen& cg, ir::Instruction& i) override;
    void emitAdd(CodeGen& cg, ir::Instruction& i) override;
    void emitSMin(CodeGen& cg, ir::Instruction& i) override;
    void emitSMax(CodeGen& cg, ir::Instruction& i) override;
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
    bool emitTailCall(CodeGen& cg, ir::Instruction& callInst, ir::Instruction& retInst) override { return false; }
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
    void emitNativeSyscall(CodeGen& cg, uint64_t syscallNum, const std::vector<ir::Value*>& args) override;
    void emitNativeLibraryCall(CodeGen& cg, const std::string& name, const std::vector<ir::Value*>& args) override;

    std::string formatStackOperand(int offset) const override;
    std::string formatGlobalOperand(const std::string& name) const override;
    bool isCallerSaved(const std::string& reg) const override { return false; }
    bool isCalleeSaved(const std::string& reg) const override { return false; }

    std::string getAssemblyFileExtension() const override { return ".spvasm"; }
    std::string getObjectFileExtension() const override { return ".spv"; }

private:
    uint32_t nextId = 1;
};

}
