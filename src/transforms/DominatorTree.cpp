#include "transforms/DominatorTree.h"
#include <vector>
#include <set>
#include <algorithm>
#include <iterator>

namespace transforms {

void DominatorTree::run(ir::Function& func) {
    if (func.getBasicBlocks().empty()) {
        return;
    }

    std::vector<ir::BasicBlock*> blocks;
    std::map<ir::BasicBlock*, int> blockToIndex;
    int index = 0;
    for (auto& bb_ptr : func.getBasicBlocks()) {
        blocks.push_back(bb_ptr.get());
        blockToIndex[bb_ptr.get()] = index++;
    }

    ir::BasicBlock* entry = func.getBasicBlocks().front().get();

    std::map<ir::BasicBlock*, std::set<ir::BasicBlock*>> domSets;

    // Initialize dom sets
    std::set<ir::BasicBlock*> allBlocks(blocks.begin(), blocks.end());
    for (ir::BasicBlock* bb : blocks) {
        if (bb == entry) {
            domSets[bb] = {entry};
        } else {
            domSets[bb] = allBlocks;
        }
    }

    // Iteratively compute dominators
    bool changed = true;
    while (changed) {
        changed = false;
        for (ir::BasicBlock* bb : blocks) {
            if (bb == entry) continue;

            std::set<ir::BasicBlock*> newDomSet;
            if (!bb->getPredecessors().empty()) {
                // Intersect dominators of all predecessors
                newDomSet = domSets[bb->getPredecessors()[0]];
                for (size_t i = 1; i < bb->getPredecessors().size(); ++i) {
                    std::set<ir::BasicBlock*> temp;
                    std::set_intersection(newDomSet.begin(), newDomSet.end(),
                                          domSets[bb->getPredecessors()[i]].begin(), domSets[bb->getPredecessors()[i]].end(),
                                          std::inserter(temp, temp.begin()));
                    newDomSet = temp;
                }
            }

            // Add self to the set
            newDomSet.insert(bb);

            if (newDomSet != domSets[bb]) {
                domSets[bb] = newDomSet;
                changed = true;
            }
        }
    }

    // Now, compute the immediate dominators from the dom sets
    for (ir::BasicBlock* bb : blocks) {
        if (bb == entry) {
            iDomMap[bb] = nullptr; // Entry has no immediate dominator
            continue;
        }
        // sdom(n) = {d in dom(n) | d != n}
        std::set<ir::BasicBlock*> sdom = domSets[bb];
        sdom.erase(bb);

        for (ir::BasicBlock* d1 : sdom) {
            bool is_idom = true;
            for (ir::BasicBlock* d2 : sdom) {
                if (d1 != d2) {
                    // if d2 dominates d1, then d1 cannot be the idom
                    if (domSets[d1].count(d2)) {
                        is_idom = false;
                        break;
                    }
                }
            }
            if (is_idom) {
                iDomMap[bb] = d1;
                break;
            }
        }
    }

    // Build the children map from the idom map
    for (auto const& [bb, idom] : iDomMap) {
        if (idom != nullptr) {
            childrenMap[idom].push_back(bb);
        }
    }
}

ir::BasicBlock* DominatorTree::getImmediateDominator(ir::BasicBlock* bb) const {
    if (iDomMap.count(bb)) {
        return iDomMap.at(bb);
    }
    return nullptr;
}

const std::vector<ir::BasicBlock*>& DominatorTree::getChildren(ir::BasicBlock* bb) const {
    static const std::vector<ir::BasicBlock*> empty;
    auto it = childrenMap.find(bb);
    if (it != childrenMap.end()) {
        return it->second;
    }
    return empty;
}

bool DominatorTree::dominates(ir::BasicBlock* a, ir::BasicBlock* b) const {
    if (a == b) return true;
    ir::BasicBlock* current = b;
    while (current != nullptr) {
        current = getImmediateDominator(current);
        if (current == a) {
            return true;
        }
    }
    return false;
}

void DominatorTree::runPostDominator(ir::Function& func) {
    if (func.getBasicBlocks().empty()) return;

    std::vector<ir::BasicBlock*> blocks;
    for (auto& bb_ptr : func.getBasicBlocks()) {
        blocks.push_back(bb_ptr.get());
    }

    // Identify exit blocks (no successors)
    std::vector<ir::BasicBlock*> exits;
    for (ir::BasicBlock* bb : blocks) {
        if (bb->getSuccessors().empty()) {
            exits.push_back(bb);
        }
    }

    if (exits.empty()) return; // Irreducible or infinite loop

    std::map<ir::BasicBlock*, std::set<ir::BasicBlock*>> postDomSets;
    std::set<ir::BasicBlock*> allBlocks(blocks.begin(), blocks.end());

    for (ir::BasicBlock* bb : blocks) {
        bool isExit = std::find(exits.begin(), exits.end(), bb) != exits.end();
        if (isExit) {
            postDomSets[bb] = {bb};
        } else {
            postDomSets[bb] = allBlocks;
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (ir::BasicBlock* bb : blocks) {
            bool isExit = std::find(exits.begin(), exits.end(), bb) != exits.end();
            if (isExit) continue;

            std::set<ir::BasicBlock*> newPostDomSet;
            if (!bb->getSuccessors().empty()) {
                newPostDomSet = postDomSets[bb->getSuccessors()[0]];
                for (size_t i = 1; i < bb->getSuccessors().size(); ++i) {
                    std::set<ir::BasicBlock*> temp;
                    std::set_intersection(newPostDomSet.begin(), newPostDomSet.end(),
                                          postDomSets[bb->getSuccessors()[i]].begin(), postDomSets[bb->getSuccessors()[i]].end(),
                                          std::inserter(temp, temp.begin()));
                    newPostDomSet = temp;
                }
            }
            newPostDomSet.insert(bb);

            if (newPostDomSet != postDomSets[bb]) {
                postDomSets[bb] = newPostDomSet;
                changed = true;
            }
        }
    }

    iDomMap.clear();
    childrenMap.clear();

    for (ir::BasicBlock* bb : blocks) {
        bool isExit = std::find(exits.begin(), exits.end(), bb) != exits.end();
        if (isExit) {
            iDomMap[bb] = nullptr;
            continue;
        }
        std::set<ir::BasicBlock*> spdom = postDomSets[bb];
        spdom.erase(bb);

        for (ir::BasicBlock* d1 : spdom) {
            bool is_ipdom = true;
            for (ir::BasicBlock* d2 : spdom) {
                if (d1 != d2) {
                    if (postDomSets[d1].count(d2)) {
                        is_ipdom = false;
                        break;
                    }
                }
            }
            if (is_ipdom) {
                iDomMap[bb] = d1;
                break;
            }
        }
    }

    for (auto const& [bb, ipdom] : iDomMap) {
        if (ipdom != nullptr) {
            childrenMap[ipdom].push_back(bb);
        }
    }
}

} // namespace transforms
