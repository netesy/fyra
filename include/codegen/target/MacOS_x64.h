#ifndef FYRA_MACOS_X64_H
#define FYRA_MACOS_X64_H
#include "codegen/target/SystemV_x64.h"
namespace codegen {
namespace target {
class MacOS_x64 : public SystemV_x64 {
public:
    MacOS_x64();
    virtual std::string getName() const override { return "macos"; }
    virtual void initRegisters() override;
    virtual void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    virtual void emitSyscall(CodeGen& cg, ir::Instruction& i) override;
    virtual void emitExternCall(CodeGen& cg, ir::Instruction& i) override;
    virtual uint64_t getSyscallNumber(ir::SyscallId id) const override;
    virtual void emitStartFunction(CodeGen& cg) override;

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
