#pragma once

#include "transforms/TransformPass.h"
#include "transforms/ErrorReporter.h"
#include "ir/Function.h"
#include "target/core/TargetDescriptor.h"
#include <memory>

namespace transforms {

class LoopVectorizer : public TransformPass {
public:
    LoopVectorizer(std::shared_ptr<ErrorReporter> reporter = nullptr,
                   target::TargetDescriptor target = {target::Arch::X64, target::OS::Linux},
                   bool allowFPReassociate = false)
        : TransformPass("LoopVectorizer", reporter), errorReporter(reporter), target_(std::move(target)), allowFPReassociate_(allowFPReassociate) {}

    bool performTransformation(ir::Function& func) override;

private:
    std::shared_ptr<ErrorReporter> errorReporter;
    target::TargetDescriptor target_;
    bool allowFPReassociate_ = false;
};

} // namespace transforms
