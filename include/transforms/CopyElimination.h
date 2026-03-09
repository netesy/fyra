#pragma once

#include "ir/Function.h"

namespace transforms {

/**
 * @brief This pass performs copy elimination (also known as copy propagation).
 * It finds simple copy instructions (e.g., a = b) and replaces all subsequent
 * uses of 'a' with 'b', removing the original copy instruction.
 */
class CopyElimination {
public:
    bool run(ir::Function& func);
};

} // namespace transforms
