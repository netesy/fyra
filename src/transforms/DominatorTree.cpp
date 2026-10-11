#include "transforms/DominatorTree.h"
#include <vector>
#include <set>
#include <unordered_set>
#include <algorithm>
#include <iterator>

namespace transforms {

void DominatorTree::run(ir::Function& func) {
    iDomMap.clear(); childrenMap.clear(); intervals.clear();
    if (func.getBasicBlocks().empty()) return;
    std::vector<ir::BasicBlock*> blocks;
    std::map<ir::BasicBlock*, size_t> indices;
    for (auto& block : func.getBasicBlocks()) {
        indices.emplace(block.get(), blocks.size()); blocks.push_back(block.get());
    }
    // Entry-rooted reverse postorder. Disconnected components must not become
    // artificial dominator cycles or contribute predecessors to reachable joins.
    std::vector<bool> visited(blocks.size(), false);
    std::vector<size_t> postorder;
    std::vector<std::pair<size_t, size_t>> pending{{0, 0}};
    visited[0] = true;
    while (!pending.empty()) {
        auto& [position, edge] = pending.back();
        const auto& successors = blocks[position]->getSuccessors();
        if (edge < successors.size()) {
            auto next = indices.at(successors[edge++]);
            if (!visited[next]) { visited[next] = true; pending.emplace_back(next, 0); }
        } else { postorder.push_back(position); pending.pop_back(); }
    }
    std::vector<size_t> order(postorder.rbegin(), postorder.rend());
    const auto missing = blocks.size();
    std::vector<size_t> rank(blocks.size(), missing), parent(blocks.size(), missing);
    std::vector<std::vector<size_t>> predecessors(blocks.size());
    for (size_t position = 0; position < order.size(); ++position) {
        auto index = order[position]; rank[index] = position;
        for (auto* predecessor : blocks[index]->getPredecessors()) {
            auto incoming = indices.at(predecessor);
            if (visited[incoming]) predecessors[index].push_back(incoming);
        }
    }
    parent[0] = 0;
    // Cooper/Harvey/Kennedy intersection computes immediate dominators directly;
    // no quadratic dominator-set matrix is needed. Parent chains descend in
    // reverse-postorder rank, so intersection always terminates.
    auto intersect = [&](size_t left, size_t right) {
        while (left != right) {
            while (rank[left] > rank[right]) left = parent[left];
            while (rank[right] > rank[left]) right = parent[right];
        }
        return left;
    };
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto index : order) {
            if (index == 0) continue;
            size_t next = missing;
            for (auto predecessor : predecessors[index]) {
                if (parent[predecessor] == missing) continue;
                next = next == missing ? predecessor : intersect(next, predecessor);
            }
            if (parent[index] != next) { parent[index] = next; changed = true; }
        }
    }
    for (size_t index = 0; index < blocks.size(); ++index) {
        auto* dominator = index == 0 || parent[index] == missing ? nullptr : blocks[parent[index]];
        iDomMap[blocks[index]] = dominator;
        if (dominator) childrenMap[dominator].push_back(blocks[index]);
    }
    size_t clock = 0;
    std::vector<std::pair<ir::BasicBlock*, size_t>> stack{{blocks[0], 0}};
    intervals[blocks[0]].first = clock++;
    while (!stack.empty()) {
        auto& [block, child] = stack.back();
        const auto& children = getChildren(block);
        if (child < children.size()) {
            auto* next = children[child++]; intervals[next].first = clock++;
            stack.emplace_back(next, 0);
        } else { intervals[block].second = clock++; stack.pop_back(); }
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
    if (!a || !b) return false;
    if (a == b) return true;
    if (!intervals.empty()) {
        auto ancestor = intervals.find(a), descendant = intervals.find(b);
        return ancestor != intervals.end() && descendant != intervals.end() &&
            ancestor->second.first <= descendant->second.first &&
            descendant->second.second <= ancestor->second.second;
    }
    // Postdominator clients still use the forest representation. Guard cycles
    // in disconnected components instead of walking forever.
    std::set<ir::BasicBlock*> seen;
    for (auto* current = b; current && seen.insert(current).second;
         current = getImmediateDominator(current))
        if (current == a) return true;
    return false;
}

void DominatorTree::runPostDominator(ir::Function& func) {
    intervals.clear();
    iDomMap.clear();
    childrenMap.clear();
    if (func.getBasicBlocks().empty()) return;

    std::vector<ir::BasicBlock*> blocks;
    for (auto& bb_ptr : func.getBasicBlocks()) {
        blocks.push_back(bb_ptr.get());
    }

    // Identify exit blocks (no successors)
    std::unordered_set<ir::BasicBlock*> exits;
    for (ir::BasicBlock* bb : blocks) {
        if (bb->getSuccessors().empty()) {
            exits.insert(bb);
        }
    }

    if (exits.empty()) return; // Irreducible or infinite loop

    std::map<ir::BasicBlock*, std::set<ir::BasicBlock*>> postDomSets;
    std::set<ir::BasicBlock*> allBlocks(blocks.begin(), blocks.end());

    for (ir::BasicBlock* bb : blocks) {
        bool isExit = exits.find(bb) != exits.end();
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
            bool isExit = exits.find(bb) != exits.end();
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
        bool isExit = exits.find(bb) != exits.end();
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
