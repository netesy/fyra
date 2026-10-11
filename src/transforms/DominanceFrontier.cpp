#include "transforms/DominanceFrontier.h"

namespace transforms {

void DominanceFrontier::run(ir::Function& func, const DominatorTree& domTree) {
    frontiers.clear();
    if (func.getBasicBlocks().empty()) return;
    auto* entry = func.getBasicBlocks().front().get();
    for (auto& bb_ptr : func.getBasicBlocks()) {
        ir::BasicBlock* b = bb_ptr.get();
        if (!domTree.dominates(entry, b)) continue;
        if (b->getPredecessors().size() >= 2) {
            for (ir::BasicBlock* p : b->getPredecessors()) {
                if (!domTree.dominates(entry, p)) continue;
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
