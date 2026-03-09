#pragma once

#include "ir/Function.h"

namespace transforms {

/**
 * @brief This pass removes alloc and store instructions that are made redundant
 * by SSA construction. It should be run after the SSARenamer pass.
 */
class Mem2Reg {
public:
    bool run(ir::Function& func);
};

} // namespace transforms
