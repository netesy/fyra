#pragma once

#include "transforms/TransformPass.h"

namespace transforms {

// Collapses a constant-count repetition loop to one iteration when every
// iteration performs the same deterministic writes to non-escaping local
// storage.  This is deliberately narrower than general dead-store elimination:
// reads must come from distinct local allocations and the repetition induction
// may only control the loop.
class IdempotentLoopCollapse : public TransformPass {
public:
    explicit IdempotentLoopCollapse(std::shared_ptr<ErrorReporter> reporter = nullptr)
        : TransformPass("Idempotent Loop Collapse", reporter) {}

protected:
    bool performTransformation(ir::Function& function) override;
};

} // namespace transforms
