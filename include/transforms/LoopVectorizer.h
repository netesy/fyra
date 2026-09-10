#pragma once

#include "transforms/TransformPass.h"
#include "transforms/ErrorReporter.h"
#include "ir/Function.h"
#include <memory>

#include "target/core/TargetInfo.h"

namespace transforms {

class LoopVectorizer : public TransformPass {
public:
    explicit LoopVectorizer(const target::TargetInfo& target,
                            std::shared_ptr<ErrorReporter> reporter = nullptr,
                            target::VectorLoweringMode mode = target::VectorLoweringMode::TextAssembly)
        : TransformPass("LoopVectorizer", reporter), target(target), loweringMode(mode), errorReporter(reporter) {}

    bool performTransformation(ir::Function& func) override;

private:
    const target::TargetInfo& target;
    target::VectorLoweringMode loweringMode;
    std::shared_ptr<ErrorReporter> errorReporter;
};

} // namespace transforms
