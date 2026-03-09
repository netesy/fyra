#pragma once

#include "X86_64Base.h"
#include <string>
#include <vector>

// Forward declarations
namespace ir {
    class Type;
    class Instruction;
    class Function;
}

namespace codegen {
    class CodeGen;
    
    namespace target {
        class TargetInfo;
    }
}

namespace codegen {
namespace target {

class SystemV_x64 : public X86_64Base {
private:
    // Argument registers for System V ABI
    std::vector<std::string> integerArgRegs;
    std::vector<std::string> floatArgRegs;
    
protected:
    void initRegisters() override;
    
public:
    SystemV_x64();
    
    // Target properties
    std::string getName() const override { return "x86_64-unknown-linux-gnu"; }
    
    // Register information
    const std::vector<std::string>& getRegisters(RegisterClass regClass) const override;
    const std::string& getReturnRegister(const ir::Type* type) const override;
    
    // Register sets for function calls
    const std::vector<std::string>& getIntegerArgumentRegisters() const override;
    const std::vector<std::string>& getFloatArgumentRegisters() const override;
    const std::string& getIntegerReturnRegister() const override;
    const std::string& getFloatReturnRegister() const override;
    
    // Argument passing
    size_t getMaxRegistersForArgs() const override;
    void emitPassArgument(CodeGen& cg, size_t argIndex,
                         const std::string& value, const ir::Type* type) override;
    void emitGetArgument(CodeGen& cg, size_t argIndex,
                        const std::string& dest, const ir::Type* type) override;
    
    // Stack frame management
    void emitPrologue(CodeGen& cg, int stack_size) override;
    void emitEpilogue(CodeGen& cg) override;
    void emitFunctionPrologue(CodeGen& cg, ir::Function& func) override;
    void emitFunctionEpilogue(CodeGen& cg, ir::Function& func) override;
    
    // Executable entry point
    void emitStartFunction(CodeGen& cg) override;

    // Instruction emission
    void emitRet(CodeGen& cg, ir::Instruction& instr) override;
    void emitCall(CodeGen& cg, ir::Instruction& instr) override;
    
    // Arithmetic operations
    void emitAdd(CodeGen& cg, ir::Instruction& instr) override;
    void emitSub(CodeGen& cg, ir::Instruction& instr) override;
    void emitMul(CodeGen& cg, ir::Instruction& instr) override;
    void emitDiv(CodeGen& cg, ir::Instruction& instr) override;
    void emitRem(CodeGen& cg, ir::Instruction& instr) override;
    void emitAnd(CodeGen& cg, ir::Instruction& instr) override;
    void emitSyscall(CodeGen& cg, ir::Instruction& instr) override;
    void emitOr(CodeGen& cg, ir::Instruction& instr) override;
    void emitXor(CodeGen& cg, ir::Instruction& instr) override;
    void emitShl(CodeGen& cg, ir::Instruction& instr) override;
    void emitShr(CodeGen& cg, ir::Instruction& instr) override;
    void emitSar(CodeGen& cg, ir::Instruction& instr) override;
    void emitNeg(CodeGen& cg, ir::Instruction& instr) override;
    void emitNot(CodeGen& cg, ir::Instruction& instr) override;
    void emitCopy(CodeGen& cg, ir::Instruction& instr) override;
    
    // Floating point operations
    void emitFAdd(CodeGen& cg, ir::Instruction& instr) override;
    void emitFSub(CodeGen& cg, ir::Instruction& instr) override;
    void emitFMul(CodeGen& cg, ir::Instruction& instr) override;
    void emitFDiv(CodeGen& cg, ir::Instruction& instr) override;

    // Comparison operations
    void emitCmp(CodeGen& cg, ir::Instruction& instr) override;
    
    // Type conversion
    void emitCast(CodeGen& cg, ir::Instruction& instr,
                 const ir::Type* fromType, const ir::Type* toType) override;
    
    // Variadic arguments
    void emitVAStart(CodeGen& cg, ir::Instruction& instr) override;
    void emitVAArg(CodeGen& cg, ir::Instruction& instr) override;
    void emitVAEnd(CodeGen& cg, ir::Instruction& instr) override;
    
    // Memory operations
    void emitLoad(CodeGen& cg, ir::Instruction& instr) override;
    void emitStore(CodeGen& cg, ir::Instruction& instr) override;
    void emitAlloc(CodeGen& cg, ir::Instruction& instr) override;
    
    // Control flow
    void emitBr(CodeGen& cg, ir::Instruction& instr) override;
    void emitJmp(CodeGen& cg, ir::Instruction& instr) override;
    
    // SIMD capabilities are inherited from X86_64Base with default implementations
    // bool hasAVX() const override;  // Removed - inherited with default implementation
    // bool hasAVX2() const override; // Removed - inherited with default implementation
    // bool hasFMA() const override;  // Removed - inherited with default implementation
};

} // namespace target
} // namespace codegen