#pragma once

#include "ir/Function.h"
#include "target/core/TargetInfo.h"
#include <memory>

namespace transforms {

class ABIAnalysis {
public:
    ABIAnalysis(std::unique_ptr<codegen::target::TargetInfo> targetInfo);
    
    // Run ABI analysis on a function
    void run(ir::Function& func);
    
    // Get target info
    const codegen::target::TargetInfo* getTargetInfo() const { return targetInfo.get(); }
    
private:
    std::unique_ptr<codegen::target::TargetInfo> targetInfo;
    
    // Helper methods
    void analyzeCallingSconvention(ir::Function& func);
    void analyzeParameterPassing(ir::Function& func);
    void analyzeReturnValueHandling(ir::Function& func);
    void analyzeStackLayout(ir::Function& func);
    void insertABILowering(ir::Function& func);
};

} // namespace transforms