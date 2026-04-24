#include "codegen/abi/ABIAnalysis.h"
#include "ir/IRBuilder.h"
#include "ir/Type.h"
#include "ir/Parameter.h"
#include <iostream>

namespace transforms {

ABIAnalysis::ABIAnalysis(std::unique_ptr<codegen::target::TargetInfo> targetInfo)
    : targetInfo(std::move(targetInfo)) {
}

void ABIAnalysis::run(ir::Function& func) {
    std::cout << "Running ABI analysis on function: " << func.getName() << std::endl;
    
    // Analyze calling convention requirements
    analyzeCallingSconvention(func);
    
    // Analyze parameter passing according to ABI
    analyzeParameterPassing(func);
    
    // Analyze return value handling
    analyzeReturnValueHandling(func);
    
    // Analyze stack layout requirements
    analyzeStackLayout(func);
    
    // Insert ABI-specific lowering
    insertABILowering(func);
}

void ABIAnalysis::analyzeCallingSconvention(ir::Function& func) {
    // Determine the calling convention based on target
    std::cout << "  Analyzing calling convention for target: " << targetInfo->getName() << std::endl;
    
    // Check if function uses variadic arguments
    if (func.isVariadic()) {
        std::cout << "  Function is variadic - special ABI handling required" << std::endl;
    }
    
    // Analyze register usage requirements
    auto& intArgRegs = targetInfo->getIntegerArgumentRegisters();
    auto& floatArgRegs = targetInfo->getFloatArgumentRegisters();
    
    std::cout << "  Available integer argument registers: " << intArgRegs.size() << std::endl;
    std::cout << "  Available float argument registers: " << floatArgRegs.size() << std::endl;
}

void ABIAnalysis::analyzeParameterPassing(ir::Function& func) {
    std::cout << "  Analyzing parameter passing for " << func.getParameters().size() << " parameters" << std::endl;
    
    size_t intRegIndex = 0;
    size_t floatRegIndex = 0;
    size_t stackOffset = 0;
    
    auto& intArgRegs = targetInfo->getIntegerArgumentRegisters();
    auto& floatArgRegs = targetInfo->getFloatArgumentRegisters();
    
    for (auto& param : func.getParameters()) {
        auto typeInfo = targetInfo->getTypeInfo(param->getType());
        
        if (typeInfo.isFloatingPoint) {
            if (floatRegIndex < floatArgRegs.size()) {
                std::cout << "    Parameter " << param->getName() << " -> float register " 
                         << floatArgRegs[floatRegIndex] << std::endl;
                floatRegIndex++;
            } else {
                std::cout << "    Parameter " << param->getName() << " -> stack offset " 
                         << stackOffset << std::endl;
                stackOffset += (typeInfo.size + 7) / 8; // Round up to 8-byte alignment
            }
        } else {
            if (intRegIndex < intArgRegs.size()) {
                std::cout << "    Parameter " << param->getName() << " -> integer register " 
                         << intArgRegs[intRegIndex] << std::endl;
                intRegIndex++;
            } else {
                std::cout << "    Parameter " << param->getName() << " -> stack offset " 
                         << stackOffset << std::endl;
                stackOffset += (typeInfo.size + 7) / 8; // Round up to 8-byte alignment
            }
        }
    }
}

void ABIAnalysis::analyzeReturnValueHandling(ir::Function& func) {
    ir::Type* returnType = func.getType();
    
    if (returnType->isVoidTy()) {
        std::cout << "  Function returns void - no return value handling needed" << std::endl;
        return;
    }
    
    auto typeInfo = targetInfo->getTypeInfo(returnType);
    
    if (typeInfo.isFloatingPoint) {
        std::cout << "  Return value -> float register " << targetInfo->getFloatReturnRegister() << std::endl;
    } else {
        std::cout << "  Return value -> integer register " << targetInfo->getIntegerReturnRegister() << std::endl;
    }
}

void ABIAnalysis::analyzeStackLayout(ir::Function& func) {
    (void)func;
    std::cout << "  Analyzing stack layout requirements" << std::endl;
    
    // Calculate required stack space for local variables
    size_t localVarSpace = 0;
    
    // Calculate space for spilled registers
    size_t spillSpace = 0;
    
    // Calculate alignment requirements
    size_t alignment = targetInfo->getStackAlignment();
    
    size_t totalStackSpace = (localVarSpace + spillSpace + alignment - 1) & ~(alignment - 1);
    
    std::cout << "  Required stack space: " << totalStackSpace << " bytes" << std::endl;
    std::cout << "  Stack alignment: " << alignment << " bytes" << std::endl;
}

void ABIAnalysis::insertABILowering(ir::Function& func) {
    std::cout << "  Inserting ABI-specific lowering transformations" << std::endl;
    
    // This would insert ABI-specific code transformations
    // For now, we just log what would be done
    
    // Insert parameter loading code at function entry
    if (!func.getParameters().empty()) {
        std::cout << "    Would insert parameter loading code for " 
                 << func.getParameters().size() << " parameters" << std::endl;
    }
    
    // Insert return value preparation code before returns
    // (This would require scanning all return instructions)
    std::cout << "    Would insert return value preparation code" << std::endl;
    
    // Insert calling convention adjustments for function calls
    std::cout << "    Would insert calling convention adjustments for function calls" << std::endl;
}

} // namespace transforms