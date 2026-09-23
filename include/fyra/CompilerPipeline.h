#pragma once

#include "ir/Module.h"
#include <string>
#include <vector>
#include <memory>

namespace fyra {

enum class OptimizationLevel {
    O0,
    O1,
    O2
};

struct PipelineConfig {
    std::string targetTriple{"x64-linux-bin"};
    OptimizationLevel optLevel{OptimizationLevel::O2};
    bool validate{true};
    bool enableSLP{true};
    bool enableLoopVectorization{true};
    bool enableLoopUnroll{true};
};

struct PipelineResult {
    bool success{false};
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

// Deep copy helper for ir::Module
std::unique_ptr<ir::Module> cloneModule(const ir::Module& srcModule);

class CompilerPipeline {
public:
    CompilerPipeline() = default;

    PipelineResult run(ir::Module& module, const PipelineConfig& config);

    PipelineResult runValidation(ir::Module& module);
    PipelineResult runSSA(ir::Module& module);
    PipelineResult runOptimizations(ir::Module& module, const PipelineConfig& config);
    PipelineResult runRegAlloc(ir::Module& module, const PipelineConfig& config);

private:
    bool isSSAPrepared_{false};
    bool isOptimized_{false};
    bool isRegAllocated_{false};
};

} // namespace fyra
