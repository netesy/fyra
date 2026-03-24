#pragma once
#include "SystemV_x64.h"

namespace codegen {
namespace target {

class MacOS_x64 : public SystemV_x64 {
protected:
    void initRegisters() override;

public:
    MacOS_x64();
    std::string getName() const override { return "macos"; }
    
    void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    void emitStartFunction(CodeGen& cg) override;
    void emitSyscall(CodeGen& cg, ir::Instruction& instr) override;
    uint64_t getSyscallNumber(ir::SyscallId id) const override;
};

}
}
