#include "transforms/DominatorTree.h"
#include <vector>
#include <set>
#include <unordered_set>
#include <algorithm>
#include <iterator>
#include <bit>
#include <cstdint>

namespace transforms {

void DominatorTree::run(ir::Function& func) {
    iDomMap.clear();
    childrenMap.clear();

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

    // The original intersection equations, stored densely. Checked-call
    // continuations can produce hundreds of blocks; tree nodes for every
    // member of every dominator set made sanitizer builds allocation-heavy.
    using Bits = std::vector<uint64_t>;
    const size_t words = (blocks.size() + 63) / 64;
    Bits all(words, ~uint64_t(0));
    if (blocks.size() % 64) all.back() = (uint64_t(1) << (blocks.size() % 64)) - 1;
    std::vector<Bits> domSets(blocks.size(), all);
    domSets[0].assign(words, 0); domSets[0][0] = 1;
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t index = 1; index < blocks.size(); ++index) {
            auto* block = blocks[index];
            Bits next(words, 0);
            const auto& predecessors = block->getPredecessors();
            if (!predecessors.empty()) {
                next = domSets[blockToIndex.at(predecessors[0])];
                for (size_t predecessor = 1; predecessor < predecessors.size(); ++predecessor) {
                    const auto& incoming = domSets[blockToIndex.at(predecessors[predecessor])];
                    for (size_t word = 0; word < words; ++word) next[word] &= incoming[word];
                }
            }
            next[index / 64] |= uint64_t(1) << (index % 64);
            if (next != domSets[index]) { domSets[index] = std::move(next); changed = true; }
        }
    }
    auto cardinality = [](const Bits& bits) {
        size_t count = 0; for (auto word : bits) count += std::popcount(word); return count;
    };
    // Keep the previous pointer ordering for ambiguous unreachable components.
    std::set<ir::BasicBlock*> ordered(blocks.begin(), blocks.end());
    std::vector<size_t> orderedIndices;
    for (auto* block : ordered) orderedIndices.push_back(blockToIndex.at(block));
    std::vector<size_t> depths;
    for (const auto& dominators : domSets) depths.push_back(cardinality(dominators));
    for (size_t index = 0; index < blocks.size(); ++index) {
        auto* block = blocks[index];
        if (block == entry) { iDomMap[block] = nullptr; continue; }
        auto strict = domSets[index]; strict[index / 64] &= ~(uint64_t(1) << (index % 64));
        std::vector<size_t> candidates;
        size_t deepest = blocks.size(), depth = 0;
        for (auto position : orderedIndices) {
            if (!(strict[position / 64] & (uint64_t(1) << (position % 64)))) continue;
            candidates.push_back(position);
            auto count = depths[position];
            if (deepest == blocks.size() || count > depth) { deepest = position; depth = count; }
        }
        auto qualifies = [&](size_t candidate) {
            const auto& dominators = domSets[candidate];
            for (size_t word = 0; word < words; ++word)
                if ((dominators[word] & strict[word]) != strict[word]) return false;
            return true;
        };
        if (deepest != blocks.size() && qualifies(deepest)) { iDomMap[block] = blocks[deepest]; continue; }
        for (auto candidate : candidates)
            if (qualifies(candidate)) { iDomMap[block] = blocks[candidate]; break; }
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
