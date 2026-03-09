#pragma once

#include "ir/Function.h"

namespace transforms {

/**
 * @brief This pass performs Global Value Numbering to eliminate redundant
 * computations. This initial implementation is a Local GVN, operating on a
 * per-basic-block basis.
 */
class GVN {
public:
    bool run(ir::Function& func);
};

} // namespace transforms
