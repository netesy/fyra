#pragma once

#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include <map>
#include <vector>

namespace transforms {

class DominatorTree {
public:
    void run(ir::Function& func);
    void runPostDominator(ir::Function& func);

    ir::BasicBlock* getImmediateDominator(ir::BasicBlock* bb) const;
    const std::vector<ir::BasicBlock*>& getChildren(ir::BasicBlock* bb) const;
    bool dominates(ir::BasicBlock* a, ir::BasicBlock* b) const;

private:
    std::map<ir::BasicBlock*, ir::BasicBlock*> iDomMap; // Immediate dominator map
    std::map<ir::BasicBlock*, std::vector<ir::BasicBlock*>> childrenMap;
};

} // namespace transforms
