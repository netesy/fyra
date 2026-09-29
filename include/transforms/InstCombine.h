#pragma once

#include "transforms/TransformPass.h"
#include "ir/Instruction.h"
#include <memory>

namespace transforms {

class InstCombinePass : public TransformPass {
public:
    explicit InstCombinePass(std::shared_ptr<ErrorReporter> reporter = nullptr)
        : TransformPass("InstCombinePass", reporter) {}

    bool performTransformation(ir::Function& func) override;
};

} // namespace transforms
