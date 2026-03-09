#pragma once

#include "ir/Function.h"
#include "transforms/DominanceFrontier.h"

namespace transforms {

class PhiInsertion {
public:
    void run(ir::Function& func, const DominanceFrontier& df);
};

} // namespace transforms
