#pragma once

#include "AArch64.h"

namespace codegen {
namespace target {

class MacOS_AArch64 : public AArch64 {
public:
    MacOS_AArch64();

    std::string getName() const override { return "macos-aarch64"; }

    void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    void emitStartFunction(CodeGen& cg) override;
    void emitSyscall(CodeGen& cg, ir::Instruction& instr) override;
    void emitExternCall(CodeGen& cg, ir::Instruction& instr) override;
    uint64_t getSyscallNumber(ir::SyscallId id) const override;

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

} // namespace target
} // namespace codegen
