#ifndef FYRA_SYSTEMV_X64_H
#define FYRA_SYSTEMV_X64_H
#include "codegen/target/X86_64Base.h"
namespace codegen {
namespace target {
class SystemV_x64 : public X86_64Base {
public:
    SystemV_x64();
    virtual std::string getName() const override { return "linux"; }
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
    void emitExternCall(CodeGen&, ir::Instruction&) override;
    virtual uint64_t getSyscallNumber(ir::SyscallId id) const override;
    virtual void emitStartFunction(CodeGen& cg) override;
    virtual std::string getRegisterName(const std::string& baseReg, const ir::Type* type) const override { (void)type; return "%" + baseReg; }

protected:
    void emitIOCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitFSCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitProcessCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitMemoryCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitSyncCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitThreadCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitNetCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitTimeCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitEventCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitIPCCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitEnvCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitSystemCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitSignalCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitTTYCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitSecurityCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitGPUCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
};
}
}
#endif
