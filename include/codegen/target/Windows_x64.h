#ifndef FYRA_WINDOWS_X64_H
#define FYRA_WINDOWS_X64_H
#include "codegen/target/X86_64Base.h"
namespace codegen {
namespace target {
class Windows_x64 : public X86_64Base {
public:
    Windows_x64();
    virtual std::string getName() const override { return "windows"; }
    virtual void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    virtual void emitFunctionEpilogue(CodeGen& cg, ir::Function& func) override;
    virtual void emitRet(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitAdd(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitSub(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitMul(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitDiv(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitCopy(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitCall(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitCmp(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitBr(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitJmp(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitVAStart(CodeGen& cg, ir::Instruction& i) override { (void)cg;(void)i; }
    virtual void emitVAArg(CodeGen& cg, ir::Instruction& i) override { (void)cg;(void)i; }
    virtual void emitLoad(CodeGen& cg, ir::Instruction& i) override { (void)cg;(void)i; }
    virtual void emitStore(CodeGen& cg, ir::Instruction& i) override { (void)cg;(void)i; }
    virtual void emitAlloc(CodeGen& cg, ir::Instruction& i) override { (void)cg;(void)i; }
    virtual void emitPassArgument(CodeGen& cg, size_t index, const std::string& reg, const ir::Type* type) override { (void)cg;(void)index;(void)reg;(void)type; }
    virtual void emitGetArgument(CodeGen& cg, size_t index, const std::string& reg, const ir::Type* type) override { (void)cg;(void)index;(void)reg;(void)type; }
    virtual void emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* from, const ir::Type* to) override { (void)cg;(void)i;(void)from;(void)to; }
    virtual void emitStartFunction(CodeGen& cg) override;
    virtual std::string formatStackOperand(int offset) const override;
    virtual std::string formatGlobalOperand(const std::string& name) const override;
    virtual std::string getImmediatePrefix() const override { return ""; }
    virtual void emitPrologue(CodeGen& cg, int size) override;
    virtual void emitEpilogue(CodeGen& cg) override;
    virtual const std::vector<std::string>& getRegisters(RegisterClass regClass) const override;
    virtual const std::string& getReturnRegister(const ir::Type* type) const override;
protected:
    virtual void initRegisters() override;
};
}
}
#endif
