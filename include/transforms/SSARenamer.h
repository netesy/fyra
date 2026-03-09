#pragma once

#include "ir/Function.h"
#include "transforms/DominatorTree.h"
#include <map>
#include <vector>
#include <stack>

namespace transforms {

class SSARenamer {
public:
    void run(ir::Function& func, DominatorTree& domTree);

private:
    void renameBlock(ir::BasicBlock* bb);

    // Map from an alloc instruction (representing a variable) to its stack of renamed values
    std::map<ir::Instruction*, std::stack<ir::Value*>> varStacks;

    DominatorTree* domTree;
};

} // namespace transforms
