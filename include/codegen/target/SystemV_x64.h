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
    bool supportsCapability(const CapabilitySpec& spec) const override;
    void emitIOCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitFSCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitMemoryCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitProcessCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitThreadCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitSyncCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitTimeCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitEventCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitNetCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitIPCCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitEnvCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitSignalCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitRandomCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitErrorCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitDebugCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitModuleCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitTTYCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitSecurityCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    void emitGPUCapability(CodeGen&, ir::Instruction&, const CapabilitySpec&) override;
    virtual uint64_t getSyscallNumber(ir::SyscallId id) const override;
    virtual void emitStartFunction(CodeGen& cg) override;
    virtual std::string getRegisterName(const std::string& baseReg, const ir::Type* type) const override { (void)type; return "%" + baseReg; }
};
}
}
#endif
