#pragma once

#include "transforms/DominatorTree.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include <map>
#include <set>

namespace transforms {

class DominanceFrontier {
public:
    void run(ir::Function& func, const DominatorTree& domTree);

    const std::set<ir::BasicBlock*>& getFrontier(ir::BasicBlock* bb) const;

private:
    std::map<ir::BasicBlock*, std::set<ir::BasicBlock*>> frontiers;
};

} // namespace transforms
