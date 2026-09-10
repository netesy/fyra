#include "transforms/DiamondMatcher.h"
#include "transforms/DominatorTree.h"
#include "transforms/InstructionSafety.h"
#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include <unordered_set>
#include <iterator>

namespace transforms {
namespace {

ir::Instruction* terminator(ir::BasicBlock* block) {
    if (!block || block->getInstructions().empty()) return nullptr;
    return block->getInstructions().back().get();
}

bool hasNoEarlierTerminator(ir::BasicBlock* block) {
    if (!block || block->getInstructions().empty()) return false;
    auto last = std::prev(block->getInstructions().end());
    for (auto it = block->getInstructions().begin(); it != last; ++it) {
        switch ((*it)->getOpcode()) {
            case ir::Instruction::Ret: case ir::Instruction::Jmp:
            case ir::Instruction::Jz: case ir::Instruction::Jnz:
            case ir::Instruction::Br: case ir::Instruction::Hlt:
                return false;
            default:
                break;
        }
    }
    return true;
}

bool isExactJump(ir::BasicBlock* from, ir::BasicBlock* to) {
    auto* term = terminator(from);
    return term && term->getOpcode() == ir::Instruction::Jmp &&
           term->getOperands().size() == 1 && term->getOperands()[0] &&
           term->getOperands()[0]->get() == to;
}

bool armIsSafe(ir::BasicBlock* arm, ir::BasicBlock* merge,
               const std::unordered_set<ir::Instruction*>& mergePhis) {
    for (auto& owned : arm->getInstructions()) {
        auto* instruction = owned.get();
        if (instruction == terminator(arm)) continue;
        if (!isSafeToSpeculativelyExecute(*instruction)) return false;
        for (auto* use : instruction->getUseList()) {
            auto* user = dynamic_cast<ir::Instruction*>(use->getUser());
            if (!user) return false;
            if (user->getParent() == arm) continue;
            if (user->getParent() == merge && mergePhis.count(user)) continue;
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<SingleDiamond> matchSpeculatableSingleDiamond(
    ir::Function& function, ir::BasicBlock* conditionBlock) {
    if (!conditionBlock || conditionBlock->getParent() != &function) return std::nullopt;
    auto* branch = terminator(conditionBlock);
    if (!branch || !hasNoEarlierTerminator(conditionBlock) ||
        (branch->getOpcode() != ir::Instruction::Jnz &&
                    branch->getOpcode() != ir::Instruction::Br) ||
        branch->getOperands().size() != 3 || !branch->getOperands()[0] ||
        !branch->getOperands()[1] || !branch->getOperands()[2] ||
        conditionBlock->getSuccessors().size() != 2)
        return std::nullopt;

    auto* trueBlock = dynamic_cast<ir::BasicBlock*>(branch->getOperands()[1]->get());
    auto* falseBlock = dynamic_cast<ir::BasicBlock*>(branch->getOperands()[2]->get());
    if (!trueBlock || !falseBlock || trueBlock == falseBlock ||
        trueBlock->getParent() != &function || falseBlock->getParent() != &function ||
        conditionBlock->getSuccessors()[0] != trueBlock ||
        conditionBlock->getSuccessors()[1] != falseBlock) return std::nullopt;
    if (trueBlock->getPredecessors().size() != 1 ||
        trueBlock->getPredecessors()[0] != conditionBlock ||
        falseBlock->getPredecessors().size() != 1 ||
        falseBlock->getPredecessors()[0] != conditionBlock ||
        trueBlock->getSuccessors().size() != 1 || falseBlock->getSuccessors().size() != 1)
        return std::nullopt;

    auto* merge = trueBlock->getSuccessors()[0];
    if (!merge || merge->getParent() != &function ||
        merge != falseBlock->getSuccessors()[0] || merge == conditionBlock ||
        merge == trueBlock || merge == falseBlock ||
        merge->getPredecessors().size() != 2 ||
        merge->getPredecessors()[0] == merge->getPredecessors()[1] ||
        (merge->getPredecessors()[0] != trueBlock &&
         merge->getPredecessors()[1] != trueBlock) ||
        (merge->getPredecessors()[0] != falseBlock &&
         merge->getPredecessors()[1] != falseBlock) ||
        !hasNoEarlierTerminator(trueBlock) || !hasNoEarlierTerminator(falseBlock) ||
        !isExactJump(trueBlock, merge) || !isExactJump(falseBlock, merge))
        return std::nullopt;

    DominatorTree dominance;
    dominance.run(function);
    if (!dominance.dominates(conditionBlock, trueBlock) ||
        !dominance.dominates(conditionBlock, falseBlock) ||
        !dominance.dominates(conditionBlock, merge)) return std::nullopt;

    SingleDiamond result{conditionBlock, trueBlock, falseBlock, merge, branch,
                         branch->getOperands()[0]->get(), {}};
    if (!result.condition || !result.condition->getType() ||
        !result.condition->getType()->isIntegerTy()) return std::nullopt;
    std::unordered_set<ir::Instruction*> phiSet;
    bool sawNonPhi = false;
    for (auto& owned : merge->getInstructions()) {
        auto* phi = dynamic_cast<ir::PhiNode*>(owned.get());
        if (!phi) { sawNonPhi = true; continue; }
        if (sawNonPhi) return std::nullopt;
        if (phi->getOperands().size() != 4 ||
            !phi->getIncomingValueForBlock(trueBlock) ||
            !phi->getIncomingValueForBlock(falseBlock) ||
            phi->getIncomingValueForBlock(trueBlock)->getType() != phi->getType() ||
            phi->getIncomingValueForBlock(falseBlock)->getType() != phi->getType())
            return std::nullopt;
        result.mergePhis.push_back(phi);
        phiSet.insert(phi);
    }
    if (result.mergePhis.empty()) return std::nullopt;

    // The condition may not secretly feed another operation that would need a
    // separate rewrite when the branch is removed.
    for (auto* use : result.condition->getUseList())
        if (use->getUser() != branch) return std::nullopt;

    if (!armIsSafe(trueBlock, merge, phiSet) || !armIsSafe(falseBlock, merge, phiSet))
        return std::nullopt;
    return result;
}

} // namespace transforms
