#ifndef FYRA_WINDOWS_X64_H
#define FYRA_WINDOWS_X64_H
#include "codegen/target/X86_64Base.h"
namespace codegen {
namespace target {
class Windows_x64 : public X86_64Base {
public:
    Windows_x64();
    virtual std::string getName() const override { return "windows"; }
    virtual void initRegisters() override;
    virtual void emitHeader(CodeGen& cg) override;
    virtual void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    virtual void emitFunctionEpilogue(CodeGen& cg, ir::Function& func) override;
    virtual void emitRet(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitAdd(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitSub(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitMul(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitDiv(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitRem(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitCopy(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitCall(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitCmp(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitBr(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitJmp(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitAnd(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitOr(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitXor(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitShl(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitShr(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitSar(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitNeg(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitNot(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitLoad(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitStore(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitAlloc(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitSyscall(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitExternCall(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitStartFunction(CodeGen& cg) override;
    virtual std::string formatStackOperand(int offset) const override;
    virtual std::string formatGlobalOperand(const std::string& name) const override;
};
}
}
#endif
