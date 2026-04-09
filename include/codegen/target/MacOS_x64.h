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
    virtual uint64_t getSyscallNumber(ir::SyscallId id) const override;
    virtual void emitStartFunction(CodeGen& cg) override;
};
}
}
#endif
