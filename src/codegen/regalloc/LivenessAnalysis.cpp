#include "codegen/regalloc/LivenessAnalysis.h"
#include "transforms/CFGBuilder.h"
#include "transforms/LoopInvariantCodeMotion.h"
#include "transforms/Loop.h"
#include "ir/BasicBlock.h"
#include "ir/PhiNode.h"
#include "ir/User.h"
#include "ir/Use.h"
#include <algorithm>
#include <iostream>
#include <climits>
#include <deque>

namespace transforms {

LivenessAnalysis::LiveSnapshot::LiveSnapshot(const LiveBits& bits) {
    chunks.reserve(std::count_if(bits.begin(), bits.end(), [](uint64_t word) { return word != 0; }));
    for (size_t word = 0; word < bits.size(); ++word)
        if (bits[word]) chunks.emplace_back(word, bits[word]);
}

bool LivenessAnalysis::LiveSnapshot::contains(size_t index) const {
    const size_t word = index / 64;
    auto found = std::lower_bound(chunks.begin(), chunks.end(), word,
        [](const auto& chunk, size_t wanted) { return chunk.first < wanted; });
    return found != chunks.end() && found->first == word &&
        (found->second & (uint64_t(1) << (index % 64)));
}


void LivenessAnalysis::runForOperandLiveness(ir::Function& func) {
    liveRanges.clear();
    CFGBuilder::run(func);
    computePerInstructionCFGLiveness(func);
}

std::set<const ir::Value*> LivenessAnalysis::valuesLiveAcrossCalls() const {
    LiveBits across((indexedValues.size() + 63) / 64, 0);
    for (const auto& [instruction, before] : liveBeforeMap) {
        const auto opcode = instruction->getOpcode();
        if (opcode != ir::Instruction::Call && opcode != ir::Instruction::ExternCall &&
            opcode != ir::Instruction::Syscall) continue;
        const auto& after = liveAfterMap.at(instruction);
        size_t left = 0, right = 0;
        while (left < before.chunks.size() && right < after.chunks.size()) {
            const auto& a = before.chunks[left]; const auto& b = after.chunks[right];
            if (a.first < b.first) ++left;
            else if (b.first < a.first) ++right;
            else { across[a.first] |= a.second & b.second; ++left; ++right; }
        }
    }
    std::set<const ir::Value*> values;
    for (size_t index = 0; index < indexedValues.size(); ++index)
        if (across[index / 64] & (uint64_t(1) << (index % 64))) values.insert(indexedValues[index]);
    return values;
}

void LivenessAnalysis::run(ir::Function& func) {
    liveRanges.clear();
    CFGBuilder::run(func);

    // Assign instruction numbers
    std::map<const ir::Instruction*, int> instrNumbering;
    int i = 0;
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            instrNumbering[instr.get()] = i++;
        }
    }

    // Compute live ranges by finding def and last use
    for (auto& [instr, num] : instrNumbering) {
        // We only care about instructions that define a value
        if (instr->getType()->getTypeID() != ir::Type::VoidTyID) {
            liveRanges[instr] = {num, num}; // Initialize range to def site
        }
    }

    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            int use_site = instrNumbering[instr.get()];
            if (auto* phi = dynamic_cast<ir::PhiNode*>(instr.get())) {
                for (size_t k = 0; k + 1 < phi->getOperands().size(); k += 2) {
                    ir::Value* predVal = phi->getOperands()[k]->get();
                    ir::Value* incomingVal = phi->getOperands()[k+1]->get();
                    auto* incomingBB = dynamic_cast<ir::BasicBlock*>(predVal);
                    if (auto* op_instr = dynamic_cast<ir::Instruction*>(incomingVal)) {
                        if (liveRanges.count(op_instr) && incomingBB && !incomingBB->getInstructions().empty()) {
                            int term_site = instrNumbering[incomingBB->getInstructions().back().get()];
                            liveRanges[op_instr].end = std::max(liveRanges[op_instr].end, term_site);
                        }
                    }
                }
            } else {
                for (auto& operand : instr->getOperands()) {
                    if (auto* op_instr = dynamic_cast<ir::Instruction*>(operand->get())) {
                        if (liveRanges.count(op_instr)) {
                            liveRanges[op_instr].end = std::max(liveRanges[op_instr].end, use_site);
                        }
                    }
                }
            }
        }
    }

    // Extend live ranges across loop latches for non-Phi loop-invariant variables defined outside the loop
    std::vector<std::unique_ptr<Loop>> loops;
    // Acyclic functions have no latch intervals to extend. Avoid constructing
    // full dominator sets for long chains of checked-call continuations.
    std::map<ir::BasicBlock*, size_t> remaining;
    std::deque<ir::BasicBlock*> ready;
    for (auto& block : func.getBasicBlocks()) remaining[block.get()] = block->getPredecessors().size();
    for (auto& [block, incoming] : remaining) if (!incoming) ready.push_back(block);
    size_t visited = 0;
    while (!ready.empty()) {
        auto* block = ready.front(); ready.pop_front(); ++visited;
        for (auto* successor : block->getSuccessors())
            if (--remaining.at(successor) == 0) ready.push_back(successor);
    }
    if (visited != func.getBasicBlocks().size()) {
        LoopInvariantCodeMotion licm;
        licm.findLoops(func, loops);
    }

    for (const auto& loopPtr : loops) {
        if (!loopPtr) continue;
        int max_latch_site = -1;
        for (ir::BasicBlock* bb : loopPtr->blocks) {
            if (bb->getInstructions().empty()) continue;
            int bb_end_site = instrNumbering[bb->getInstructions().back().get()];
            for (ir::BasicBlock* succ : bb->getSuccessors()) {
                if (succ == loopPtr->header) {
                    max_latch_site = std::max(max_latch_site, bb_end_site);
                    break;
                }
            }
        }
        if (max_latch_site < 0) continue;

        for (ir::BasicBlock* bb : loopPtr->blocks) {
            for (auto& instr_ptr : bb->getInstructions()) {
                for (auto& operand : instr_ptr->getOperands()) {
                    if (auto* op_instr = dynamic_cast<ir::Instruction*>(operand->get())) {
                        if (liveRanges.count(op_instr)) {
                            // Extend live range to loop latch for:
                            // 1. Loop-invariants defined outside the loop
                            // 2. Loop-carried variables defined in loop header
                            if (loopPtr->blocks.count(op_instr->getParent()) == 0 || op_instr->getParent() == loopPtr->header) {
                                liveRanges[op_instr].end = std::max(liveRanges[op_instr].end, max_latch_site);
                            }
                        }
                    }
                }
            }
        }
    }

    // Remove ranges for dead variables (no uses)
    std::vector<const ir::Instruction*> dead_vars;
    for (auto const& [var, range] : liveRanges) {
        if (var->getUseList().empty()) {
            dead_vars.push_back(var);
        }
    }
    for (auto* var : dead_vars) {
        liveRanges.erase(var);
    }

    // Compute CFG-aware per-instruction liveness
    computePerInstructionCFGLiveness(func);

    // Block layout is not execution order. A value can remain live through
    // a late-emitted branch even when its last explicit use occurs earlier
    // in the layout. Include the CFG solution in the bounding interval, so
    // spill-slot reuse cannot overwrite values needed after that branch.
    // Resolve SSA identities once, then accumulate exact layout bounds in
    // indexed storage. Repeated map lookups and RTTI for every live member
    // are unnecessary; the CFG liveness solution remains unchanged.
    std::vector<int> firstLive(indexedValues.size(), INT_MAX);
    std::vector<int> lastLive(indexedValues.size(), -1);
    for (const auto& [instruction, number] : instrNumbering) {
        const auto& before = liveBeforeMap.at(instruction);
        const auto& after = liveAfterMap.at(instruction);
        size_t left = 0, right = 0;
        while (left < before.chunks.size() || right < after.chunks.size()) {
            const size_t beforeWord = left < before.chunks.size() ? before.chunks[left].first : SIZE_MAX;
            const size_t afterWord = right < after.chunks.size() ? after.chunks[right].first : SIZE_MAX;
            const size_t word = std::min(beforeWord, afterWord);
            uint64_t members = 0;
            if (beforeWord == word) members |= before.chunks[left++].second;
            if (afterWord == word) members |= after.chunks[right++].second;
            while (members) {
                unsigned bit = 0;
#if defined(__GNUC__) || defined(__clang__)
                bit = static_cast<unsigned>(__builtin_ctzll(members));
#else
                auto shifted = members;
                while ((shifted & 1) == 0) { ++bit; shifted >>= 1; }
#endif
                members &= members - 1;
                const size_t index = word * 64 + bit;
                firstLive[index] = std::min(firstLive[index], number);
                lastLive[index] = std::max(lastLive[index], number);
            }
        }
    }
    for (size_t index = 0; index < indexedValues.size(); ++index) {
        if (lastLive[index] < 0) continue;
        auto* definition = dynamic_cast<const ir::Instruction*>(indexedValues[index]);
        auto range = liveRanges.find(definition);
        if (range == liveRanges.end()) continue;
        range->second.start = std::min(range->second.start, firstLive[index]);
        range->second.end = std::max(range->second.end, lastLive[index]);
    }
}

bool LivenessAnalysis::isLiveAfter(const ir::Instruction* instruction, const ir::Value* value) const {
    if (!instruction || !value) return false;
    auto it = liveAfterMap.find(instruction);
    if (it == liveAfterMap.end()) return false;
    auto index = valueIndices.find(value);
    return index != valueIndices.end() && it->second.contains(index->second);
}

bool LivenessAnalysis::isLiveBefore(const ir::Instruction* instruction, const ir::Value* value) const {
    if (!instruction || !value) return false;
    auto it = liveBeforeMap.find(instruction);
    if (it == liveBeforeMap.end()) return false;
    auto index = valueIndices.find(value);
    return index != valueIndices.end() && it->second.contains(index->second);
}

bool LivenessAnalysis::isLastUseOfOperand(const ir::Instruction* user, const ir::Use* use) const {
    if (!user || !use) return false;
    const ir::Value* origVal = use->getOriginalValue();
    if (!origVal) return false;
    return !isLiveAfter(user, origVal);
}

void LivenessAnalysis::computePerInstructionCFGLiveness(ir::Function& func) {
    liveAfterMap.clear(); liveBeforeMap.clear(); valueIndices.clear(); indexedValues.clear();
    auto index = [&](const ir::Value* value) {
        if (!value || !(dynamic_cast<const ir::Instruction*>(value) || dynamic_cast<const ir::Parameter*>(value))) return;
        if (valueIndices.emplace(value, indexedValues.size()).second) indexedValues.push_back(value);
    };
    for (auto& parameter : func.getParameters()) index(parameter.get());
    for (auto& block : func.getBasicBlocks()) for (auto& instruction : block->getInstructions()) {
        index(instruction.get()); for (auto& operand : instruction->getOperands()) index(operand->get());
    }
    const size_t words = (indexedValues.size() + 63) / 64;
    auto include = [&](LiveBits& bits, const ir::Value* value) {
        auto found = valueIndices.find(value);
        if (found != valueIndices.end()) bits[found->second / 64] |= uint64_t(1) << (found->second % 64);
    };
    auto exclude = [&](LiveBits& bits, const ir::Value* value) {
        auto found = valueIndices.find(value);
        if (found != valueIndices.end()) bits[found->second / 64] &= ~(uint64_t(1) << (found->second % 64));
    };
    auto transfer = [&](LiveBits& live, ir::Instruction* instruction) {
        if (instruction->getType() && !instruction->getType()->isVoidTy()) exclude(live, instruction);
        if (!dynamic_cast<ir::PhiNode*>(instruction))
            for (auto& operand : instruction->getOperands()) include(live, operand->get());
    };
    std::map<ir::BasicBlock*, LiveBits> incoming, outgoing, generated, killed;
    for (auto& block : func.getBasicBlocks()) {
        auto* node = block.get();
        incoming[node] = LiveBits(words); outgoing[node] = LiveBits(words);
        auto& gen = generated[node]; gen = LiveBits(words);
        auto& kill = killed[node]; kill = LiveBits(words);
        for (auto it = block->getInstructions().rbegin(); it != block->getInstructions().rend(); ++it) {
            transfer(gen, it->get());
            if ((*it)->getType() && !(*it)->getType()->isVoidTy()) include(kill, it->get());
        }
    }
    // Backward equations converge quickly in CFG postorder, regardless of the
    // emitter's block layout. Include disconnected components in the solution.
    std::deque<ir::BasicBlock*> pending;
    std::set<ir::BasicBlock*> queued, visited;
    for (auto& root : func.getBasicBlocks()) {
        if (!visited.insert(root.get()).second) continue;
        std::vector<std::pair<ir::BasicBlock*, size_t>> stack{{root.get(), 0}};
        while (!stack.empty()) {
            auto& [block, edge] = stack.back();
            if (edge < block->getSuccessors().size()) {
                auto* next = block->getSuccessors()[edge++];
                if (visited.insert(next).second) stack.emplace_back(next, 0);
            } else {
                pending.push_back(block); queued.insert(block); stack.pop_back();
            }
        }
    }
    while (!pending.empty()) {
        auto* block = pending.front(); pending.pop_front(); queued.erase(block);
        if (block->getInstructions().empty()) continue;
        LiveBits after(words);
        for (auto* successor : block->getSuccessors()) {
            if (successor->getInstructions().empty()) continue;
            auto edge = incoming.at(successor);
            for (auto& instruction : successor->getInstructions()) {
                auto* phi = dynamic_cast<ir::PhiNode*>(instruction.get());
                if (!phi) break;
                exclude(edge, phi); include(edge, phi->getIncomingValueForBlock(block));
            }
            for (size_t word = 0; word < words; ++word) after[word] |= edge[word];
        }
        outgoing[block] = after;
        const auto& gen = generated.at(block);
        const auto& kill = killed.at(block);
        for (size_t word = 0; word < words; ++word)
            after[word] = (after[word] & ~kill[word]) | gen[word];
        if (after != incoming[block]) {
            incoming[block] = std::move(after);
            for (auto* predecessor : block->getPredecessors())
                if (queued.insert(predecessor).second) pending.push_back(predecessor);
        }
    }
    // Materialize each per-instruction set once, after the block fixed point.
    for (auto& block : func.getBasicBlocks()) {
        auto live = outgoing.at(block.get());
        for (auto it = block->getInstructions().rbegin(); it != block->getInstructions().rend(); ++it) {
            auto* instruction = it->get(); liveAfterMap[instruction] = LiveSnapshot(live);
            transfer(live, instruction); liveBeforeMap[instruction] = LiveSnapshot(live);
        }
    }
}

} // namespace transforms
