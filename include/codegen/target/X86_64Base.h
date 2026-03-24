#pragma once
#include "TargetInfo.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace codegen {
namespace target {

class X86_64Base : public TargetInfo {
protected:
    virtual void initRegisters() = 0;
    std::vector<std::string> integerRegs;
    std::vector<std::string> floatRegs;
    std::vector<std::string> vectorRegs;
    std::string intReturnReg;
    std::string floatReturnReg;
    std::string stackPtrReg;
    std::string framePtrReg;
    std::unordered_map<std::string, bool> callerSaved;
    std::unordered_map<std::string, bool> calleeSaved;
    
public:
    X86_64Base();
    
    TypeInfo getTypeInfo(const ir::Type* type) const override;
    const std::vector<std::string>& getRegisters(RegisterClass regClass) const override;
    const std::string& getReturnRegister(const ir::Type* type) const override;
    
    void emitPrologue(CodeGen& cg, int stack_size) override;
    void emitEpilogue(CodeGen& cg) override;
    
    bool isCallerSaved(const std::string& reg) const override;
    bool isCalleeSaved(const std::string& reg) const override;
    
    std::string formatStackOperand(int offset) const override;
    std::string formatGlobalOperand(const std::string& name) const override;
    std::string getRegisterName(const std::string& baseReg, const ir::Type* type) const override;
};

}
}
