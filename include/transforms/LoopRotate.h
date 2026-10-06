#pragma once

#include "transforms/TransformPass.h"
#include "transforms/Loop.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include <memory>
#include <vector>

namespace transforms {

/**
 * @brief Loop Rotation Optimization Pass
 *
 * Converts top-tested loops (while/for loops with header conditional exits)
 * into bottom-tested loops (do-while form with a single conditional back-edge).
 *
 * Features:
 * - Preserves zero-trip execution via preheader entry guard.
 * - Rewrites header PHIs into body PHIs and exit PHIs.
 * - Target-independent IR transformation.
 * - Idempotent and safe for nested loops.
 */
class LoopRotate : public TransformPass {
public:
    explicit LoopRotate(std::shared_ptr<ErrorReporter> error_reporter = nullptr)
        : TransformPass("Loop Rotation", error_reporter) {}

    bool run(ir::Function& func);

protected:
    bool performTransformation(ir::Function& func) override;

private:
    bool rotateLoop(Loop& loop, ir::Function& func);
};

} // namespace transforms
