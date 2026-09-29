#pragma once

#include "ir/Function.h"

namespace target { class TargetInfo; }

namespace transforms {

/**
 * @brief This pass rewrites the IR after register allocation. It replaces
 * virtual register operands with physical registers and inserts load/store
 * instructions for spilled variables.
 */
class RegAllocRewriter {
public:
    bool run(ir::Function& func);
    bool run(ir::Function& func, const ::target::TargetInfo* targetInfo);
};

} // namespace transforms
