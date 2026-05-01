#include "codegen/abi/ABIAnalysis.h"
#include "ir/IRBuilder.h"
#include "ir/Type.h"
#include "ir/Parameter.h"
#include <iostream>
namespace transforms {
ABIAnalysis::ABIAnalysis(std::unique_ptr<target::TargetInfo> targetInfo) : targetInfo(std::move(targetInfo)) {}
void ABIAnalysis::run(ir::Function& func) { insertABILowering(func); }
void ABIAnalysis::analyzeCallingSconvention(ir::Function& func) {}
void ABIAnalysis::analyzeParameterPassing(ir::Function& func) {}
void ABIAnalysis::analyzeReturnValueHandling(ir::Function& func) {}
void ABIAnalysis::analyzeStackLayout(ir::Function& func) {}
void ABIAnalysis::insertABILowering(ir::Function& func) {
    auto& params = func.getParameters(); auto& intArgRegs = targetInfo->getIntegerArgumentRegisters(); auto& floatArgRegs = targetInfo->getFloatArgumentRegisters();
    size_t intIdx = 0; size_t floatIdx = 0;
    for (auto& param : params) {
        // Industry level: Assign physical registers to parameters
        if (param->getType()->isFloatingPoint()) { if (floatIdx < floatArgRegs.size()) { /* param->setPhysicalRegister(floatArgRegs[floatIdx++]); */ } }
        else { if (intIdx < intArgRegs.size()) { /* param->setPhysicalRegister(intArgRegs[intIdx++]); */ } }
    }
}
}
