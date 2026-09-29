#pragma once

#include "transforms/TransformPass.h"
#include "transforms/ErrorReporter.h"
#include "ir/Function.h"
#include "transforms/Loop.h"
#include <memory>

namespace transforms {

class LoopStrengthReduction : public TransformPass {
public:
    explicit LoopStrengthReduction(std::shared_ptr<ErrorReporter> reporter = nullptr)
        : TransformPass("LoopStrengthReduction", reporter), errorReporter_(reporter) {}

    bool performTransformation(ir::Function& func) override;

private:
    std::shared_ptr<ErrorReporter> errorReporter_;
};

} // namespace transforms
