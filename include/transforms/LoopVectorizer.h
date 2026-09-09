#pragma once

#include "transforms/TransformPass.h"
#include "transforms/ErrorReporter.h"
#include "ir/Function.h"
#include <memory>

namespace transforms {

class LoopVectorizer : public TransformPass {
public:
    LoopVectorizer(std::shared_ptr<ErrorReporter> reporter = nullptr)
        : TransformPass("LoopVectorizer", reporter), errorReporter(reporter) {}

    bool performTransformation(ir::Function& func) override;

private:
    std::shared_ptr<ErrorReporter> errorReporter;
};

} // namespace transforms
