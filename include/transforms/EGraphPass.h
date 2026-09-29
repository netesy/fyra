#pragma once

#include "TransformPass.h"
#include "ir/Function.h"
#include <memory>

namespace transforms {

class EGraphPass : public TransformPass {
public:
    explicit EGraphPass(std::shared_ptr<ErrorReporter> error_reporter = nullptr)
        : TransformPass("EGraph Scoped Equality Saturation", error_reporter) {}

    bool run(ir::Function& func) {
        return TransformPass::run(func);
    }

protected:
    bool performTransformation(ir::Function& func) override;
    bool validatePreconditions(ir::Function& func) override;
};

} // namespace transforms
