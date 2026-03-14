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
    uint64_t getSyscallNumber(ir::SyscallId id) const override;
};

} // namespace target
} // namespace codegen
