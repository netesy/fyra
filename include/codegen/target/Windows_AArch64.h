#pragma once

#include "AArch64.h"

namespace codegen {
namespace target {

class Windows_AArch64 : public AArch64 {
public:
    std::string getName() const override { return "aarch64-pc-windows-msvc"; }

    // Windows doesn't use the Linux-style _start with syscall 93
    void emitStartFunction(CodeGen& cg) override;

    // Override register sets if necessary (e.g., x18 is reserved on Windows)
    bool isReserved(const std::string& reg) const override;

    void emitRem(CodeGen& cg, ir::Instruction& instr) override;
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
    void emitRandomCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitErrorCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitDebugCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
    void emitModuleCall(CodeGen& cg, ir::Instruction& instr, const std::string& cap);
};

} // namespace target
} // namespace codegen
