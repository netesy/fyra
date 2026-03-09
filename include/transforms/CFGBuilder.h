#pragma once

#include "ir/Function.h"

namespace transforms {

class CFGBuilder {
public:
    static void run(ir::Function& func);
};

} // namespace transforms
