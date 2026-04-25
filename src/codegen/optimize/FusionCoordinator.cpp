#include "codegen/optimize/InstructionFusion.h"
#include <map>
#include <iostream>
#include "ir/Function.h"
#include "ir/BasicBlock.h"

namespace codegen {
namespace target {

FusionCoordinator::FusionCoordinator() : optimizationLevel(1), targetArchitecture("x86_64") {
    // Initialize with default optimizations enabled
    enabledOptimizations["fma"] = true;
    enabledOptimizations["load_operate"] = true;
    enabledOptimizations["compare_branch"] = true;
    enabledOptimizations["addressing"] = true;
}

void FusionCoordinator::setTargetArchitecture(const std::string& arch) {
    targetArchitecture = arch;
    
    // Reconfigure optimizers for the new architecture
    if (!FMAOptimizer::targetSupportsFMA(arch)) {
        enabledOptimizations["fma"] = false;
    }
}

void FusionCoordinator::setOptimizationLevel(unsigned level) {
    optimizationLevel = level;
    
    // Adjust optimization aggressiveness based on level
    if (level == 0) {
        // Disable all optimizations
        for (auto& opt : enabledOptimizations) {
            opt.second = false;
        }
    } else if (level == 1) {
        // Basic optimizations only
        enabledOptimizations["fma"] = true;
        enabledOptimizations["load_operate"] = true;
        enabledOptimizations["compare_branch"] = true;
        enabledOptimizations["addressing"] = false;
    } else if (level >= 2) {
        // Aggressive optimizations
        enabledOptimizations["fma"] = FMAOptimizer::targetSupportsFMA(targetArchitecture);
        enabledOptimizations["load_operate"] = true;
        enabledOptimizations["compare_branch"] = true;
        enabledOptimizations["addressing"] = true;
    }
}

void FusionCoordinator::optimizeFunction(ir::Function& func) {
    for (auto& block : func.getBasicBlocks()) {
        if (block) {
            optimizeBasicBlock(*block);
        }
    }
}

void FusionCoordinator::optimizeBasicBlock(ir::BasicBlock& block) {
    if (optimizationLevel == 0) return;
    
    // Apply FMA optimizations
    if (enabledOptimizations["fma"] && FMAOptimizer::targetSupportsFMA(targetArchitecture)) {
        auto fmaPatterns = fmaOptimizer.findFMAPatterns(block);
        for (const auto& pattern : fmaPatterns) {
            if (fmaOptimizer.isProfitableToFuse(pattern)) {
                fmaOptimizer.replaceFMAPattern(block, pattern);
            }
        }
    }
    
    // Apply load-operate optimizations
    if (enabledOptimizations["load_operate"]) {
        auto loadOpPatterns = loadOperateFusion.findLoadOperatePatterns(block);
        for (const auto& pattern : loadOpPatterns) {
            if (loadOperateFusion.isWorthFusing(pattern)) {
                loadOperateFusion.applyLoadOperateFusion(block, pattern);
            }
        }
    }
    
    // Apply compare-and-branch optimizations
    if (enabledOptimizations["compare_branch"]) {
        auto cmpBrPatterns = compareAndBranchFusion.findCompareAndBranchPatterns(block);
        for (const auto& pattern : cmpBrPatterns) {
            if (compareAndBranchFusion.shouldFuseCompareAndBranch(pattern)) {
                compareAndBranchFusion.applyCompareAndBranchFusion(block, pattern);
            }
        }
    }
    
    // Apply addressing mode optimizations
    if (enabledOptimizations["addressing"]) {
        auto addrOptimizations = addressingAnalyzer.findAddressingOptimizations(block);
        // Addressing optimizations are typically applied during code generation
        // rather than IR transformation, so we just collect them here
    }
}

FusionCoordinator::OptimizationReport FusionCoordinator::generateReport(ir::Function& func) {
    OptimizationReport report;
    report.fmaOptimizations = 0;
    report.loadOperateOptimizations = 0;
    report.compareAndBranchOptimizations = 0;
    report.addressingOptimizations = 0;
    report.estimatedSpeedup = 1.0;
    
    double totalBenefit = 1.0;
    
    for (auto& block : func.getBasicBlocks()) {
        if (!block) continue;
        
        // Count FMA optimizations
        if (enabledOptimizations["fma"]) {
            auto fmaPatterns = fmaOptimizer.findFMAPatterns(*block);
            for (const auto& pattern : fmaPatterns) {
                if (fmaOptimizer.isProfitableToFuse(pattern)) {
                    report.fmaOptimizations++;
                    totalBenefit *= fmaOptimizer.estimateFMABenefit(pattern);
                    report.appliedOptimizations.push_back("FMA fusion");
                }
            }
        }
        
        // Count load-operate optimizations
        if (enabledOptimizations["load_operate"]) {
            auto loadOpPatterns = loadOperateFusion.findLoadOperatePatterns(*block);
            for (const auto& pattern : loadOpPatterns) {
                if (loadOperateFusion.isWorthFusing(pattern)) {
                    report.loadOperateOptimizations++;
                    totalBenefit *= loadOperateFusion.estimateLoadOperateBenefit(pattern);
                    report.appliedOptimizations.push_back("Load-operate fusion");
                }
            }
        }
        
        // Count compare-and-branch optimizations
        if (enabledOptimizations["compare_branch"]) {
            auto cmpBrPatterns = compareAndBranchFusion.findCompareAndBranchPatterns(*block);
            for (const auto& pattern : cmpBrPatterns) {
                if (compareAndBranchFusion.shouldFuseCompareAndBranch(pattern)) {
                    report.compareAndBranchOptimizations++;
                    totalBenefit *= compareAndBranchFusion.estimateCompareAndBranchBenefit(pattern);
                    report.appliedOptimizations.push_back("Compare-and-branch fusion");
                }
            }
        }
        
        // Count addressing optimizations
        if (enabledOptimizations["addressing"]) {
            auto addrOptimizations = addressingAnalyzer.findAddressingOptimizations(*block);
            report.addressingOptimizations += addrOptimizations.size();
            for (size_t i = 0; i < addrOptimizations.size(); ++i) {
                totalBenefit *= 1.1; // Estimated benefit for each addressing optimization
                report.appliedOptimizations.push_back("Addressing mode optimization");
            }
        }
    }
    
    report.estimatedSpeedup = totalBenefit;
    return report;
}

void FusionCoordinator::printOptimizationSummary(const OptimizationReport& report, std::ostream& os) {
    os << "=== Instruction Fusion Optimization Report ===\n";
    os << "Target Architecture: " << targetArchitecture << "\n";
    os << "Optimization Level: " << optimizationLevel << "\n\n";
    
    os << "Optimizations Applied:\n";
    os << "  FMA fusions: " << report.fmaOptimizations << "\n";
    os << "  Load-operate fusions: " << report.loadOperateOptimizations << "\n";
    os << "  Compare-and-branch fusions: " << report.compareAndBranchOptimizations << "\n";
    os << "  Addressing mode optimizations: " << report.addressingOptimizations << "\n\n";
    
    os << "Total optimizations: " << report.appliedOptimizations.size() << "\n";
    os << "Estimated speedup: " << report.estimatedSpeedup << "x\n\n";
    
    if (!report.appliedOptimizations.empty()) {
        os << "Applied optimizations:\n";
        for (const auto& opt : report.appliedOptimizations) {
            os << "  - " << opt << "\n";
        }
    }
    
    os << "=========================================\n";
}

void FusionCoordinator::enableOptimization(const std::string& optimizationName, bool enable) {
    enabledOptimizations[optimizationName] = enable;
}

} // namespace target
} // namespace codegen