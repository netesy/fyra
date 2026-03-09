#include "transforms/DominanceFrontier.h"

namespace transforms {

void DominanceFrontier::run(ir::Function& func, const DominatorTree& domTree) {
    for (auto& bb_ptr : func.getBasicBlocks()) {
        ir::BasicBlock* b = bb_ptr.get();
        if (b->getPredecessors().size() >= 2) {
            for (ir::BasicBlock* p : b->getPredecessors()) {
                ir::BasicBlock* runner = p;
                while (runner != domTree.getImmediateDominator(b)) {
                    if (runner == nullptr) break; // Should not happen in a valid CFG with entry
                    frontiers[runner].insert(b);
                    runner = domTree.getImmediateDominator(runner);
                }
            }
        }
    }
}

const std::set<ir::BasicBlock*>& DominanceFrontier::getFrontier(ir::BasicBlock* bb) const {
    static const std::set<ir::BasicBlock*> emptySet;
    auto it = frontiers.find(bb);
    if (it != frontiers.end()) {
        return it->second;
    }
    return emptySet;
}

} // namespace transforms
