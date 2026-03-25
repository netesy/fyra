#include "transforms/ControlFlowSimplification.h"
#include "ir/Use.h"
#include "ir/Constant.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/PhiNode.h"
#include "ir/Function.h"
#include <algorithm>
#include <vector>
#include <set>
#include <iostream>

namespace transforms {

bool ControlFlowSimplification::performTransformation(ir::Function& func) {
    bool changed = false;
    
    if (simplifyConstantBranches(func)) {
        changed = true;
    }
    
    if (eliminateUnreachableBlocks(func)) {
        changed = true;
    }

    if (mergeBlocks(func)) {
        changed = true;
    }
    
    return changed;
}

bool ControlFlowSimplification::simplifyConstantBranches(ir::Function& func) {
    bool changed = false;
    for (auto& bb_ptr : func.getBasicBlocks()) {
        ir::BasicBlock* bb = bb_ptr.get();
        if (!bb || bb->getInstructions().empty()) continue;
        ir::Instruction* term = bb->getInstructions().back().get();
        
        if (!isConditionalBranch(term)) {
            continue;
        }

        auto* condConst = dynamic_cast<ir::ConstantInt*>(getConstantCondition(term));
        if (!condConst) {
            continue;
        }

        if (term->getOperands().size() < 2) continue;
        ir::BasicBlock* trueDest = dynamic_cast<ir::BasicBlock*>(term->getOperands()[1]->get());
        ir::BasicBlock* falseDest = (term->getOperands().size() > 2) ? dynamic_cast<ir::BasicBlock*>(term->getOperands()[2]->get()) : nullptr;

        bool condVal = condConst->getValue() != 0;
        if (term->getOpcode() == ir::Instruction::Jz) condVal = !condVal;

        ir::BasicBlock* target = condVal ? trueDest : falseDest;
        ir::BasicBlock* other = condVal ? falseDest : trueDest;

        if (!target) continue;

        auto jmp = std::make_unique<ir::Instruction>(nullptr, ir::Instruction::Jmp, std::vector<ir::Value*>{target}, bb);
        bb->getInstructions().pop_back();
        bb->getInstructions().push_back(std::move(jmp));

        if (other) {
            for (auto& instr : other->getInstructions()) {
                if (!instr) continue;
                if (auto* phi = dynamic_cast<ir::PhiNode*>(instr.get())) {
                    phi->removeIncomingValue(bb);
                } else if (instr->getOpcode() != ir::Instruction::Phi) {
                    break;
                }
            }
        }

        changed = true;
        constant_branches_simplified_++;
    }
    return changed;
}

bool ControlFlowSimplification::eliminateUnreachableBlocks(ir::Function& func) {
    if (func.getBasicBlocks().empty()) return false;
    
    std::set<ir::BasicBlock*> reachable;
    std::vector<ir::BasicBlock*> worklist;
    
    ir::BasicBlock* entry = func.getBasicBlocks().front().get();
    reachable.insert(entry);
    worklist.push_back(entry);
    
    while (!worklist.empty()) {
        ir::BasicBlock* curr = worklist.back();
        worklist.pop_back();
        
        if (!curr || curr->getInstructions().empty()) continue;
        ir::Instruction* term = curr->getInstructions().back().get();
        
        for (auto& op : term->getOperands()) {
            if (!op) continue;
            if (auto* next = dynamic_cast<ir::BasicBlock*>(op->get())) {
                if (reachable.insert(next).second) {
                    worklist.push_back(next);
                }
            }
        }
    }
    
    bool changed = false;
    auto& blocks = func.getBasicBlocks();
    auto it = blocks.begin();
    if (it != blocks.end()) ++it; 
    
    while (it != blocks.end()) {
        ir::BasicBlock* bb = it->get();
        if (reachable.find(bb) == reachable.end()) {
            if (!bb->getInstructions().empty()) {
                ir::Instruction* term = bb->getInstructions().back().get();
                for (auto& op : term->getOperands()) {
                    if (!op) continue;
                    if (auto* succ = dynamic_cast<ir::BasicBlock*>(op->get())) {
                        for (auto& instr : succ->getInstructions()) {
                            if (auto* phi = dynamic_cast<ir::PhiNode*>(instr.get())) {
                                phi->removeIncomingValue(bb);
                            } else if (instr->getOpcode() != ir::Instruction::Phi) {
                                break;
                            }
                        }
                    }
                }
            }
            it = blocks.erase(it);
            changed = true;
            unconditional_branches_eliminated_++;
        } else {
            ++it;
        }
    }
    return changed;
}

bool ControlFlowSimplification::mergeBlocks(ir::Function& func) {
    bool changed = false;
    auto& blocks = func.getBasicBlocks();
    if (blocks.empty()) return false;

    bool block_changed = true;
    while (block_changed) {
        block_changed = false;
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            ir::BasicBlock* bb = it->get();
            if (!bb || bb->getInstructions().empty()) continue;
            
            if (!hasOneSuccessor(bb)) continue;
            ir::BasicBlock* succ = getSuccessor(bb);
            if (!succ || succ == bb) continue;
            if (!canMergeBlocks(bb, succ, func)) continue;

            mergeBlocksImpl(bb, succ, func);
            changed = true;
            block_changed = true;
            blocks_merged_++;
            break;
        }
    }
    return changed;
}

bool ControlFlowSimplification::hasOnePredecessor(ir::BasicBlock* block, ir::Function& func) {
    int count = 0;
    for (auto& bb_ptr : func.getBasicBlocks()) {
        ir::BasicBlock* bb = bb_ptr.get();
        if (!bb || bb->getInstructions().empty()) continue;
        ir::Instruction* term = bb->getInstructions().back().get();
        for (auto& op : term->getOperands()) {
            if (op && op->get() == block) {
                count++;
                break; 
            }
        }
    }
    return count == 1;
}

bool ControlFlowSimplification::canMergeBlocks(ir::BasicBlock* pred, ir::BasicBlock* succ, ir::Function& func) {
    if (!pred || !succ) return false;
    if (succ == func.getBasicBlocks().front().get()) return false;
    if (!hasOnePredecessor(succ, func)) return false;
    return true;
}

void ControlFlowSimplification::mergeBlocksImpl(ir::BasicBlock* pred, ir::BasicBlock* succ, ir::Function& func) {
    if (!pred || !succ) return;
    if (pred->getInstructions().empty()) return;

    pred->getInstructions().pop_back();

    auto& succInstrs = succ->getInstructions();
    while (!succInstrs.empty()) {
        if (auto* phi = dynamic_cast<ir::PhiNode*>(succInstrs.front().get())) {
            ir::Value* val = phi->getIncomingValueForBlock(pred);
            if (val) {
                phi->replaceAllUsesWith(val);
            }
            succInstrs.erase(succInstrs.begin());
            continue;
        }

        ir::Instruction* moved = succInstrs.front().get();
        moved->setParent(pred);
        pred->getInstructions().push_back(std::move(succInstrs.front()));
        succInstrs.erase(succInstrs.begin());
    }

    auto& blocks = func.getBasicBlocks();
    for (auto it = blocks.begin(); it != blocks.end(); ++it) {
        if (it->get() == succ) {
            blocks.erase(it);
            return;
        }
    }
}

bool ControlFlowSimplification::hasOneSuccessor(ir::BasicBlock* block) {
    if (!block || block->getInstructions().empty()) return false;
    ir::Instruction* term = block->getInstructions().back().get();
    if (!isUnconditionalBranch(term)) return false;
    return getSuccessor(block) != nullptr;
}

ir::BasicBlock* ControlFlowSimplification::getSuccessor(ir::BasicBlock* block) {
    if (!block || block->getInstructions().empty()) return nullptr;
    ir::Instruction* term = block->getInstructions().back().get();
    if (!isUnconditionalBranch(term) || term->getOperands().empty() || !term->getOperands()[0]) {
        return nullptr;
    }
    return dynamic_cast<ir::BasicBlock*>(term->getOperands()[0]->get());
}

std::vector<ir::BasicBlock*> ControlFlowSimplification::getPredecessors(ir::BasicBlock* block, ir::Function& func) {
    std::vector<ir::BasicBlock*> preds;
    if (!block) return preds;

    for (auto& bb_ptr : func.getBasicBlocks()) {
        ir::BasicBlock* bb = bb_ptr.get();
        if (!bb || bb->getInstructions().empty()) continue;
        ir::Instruction* term = bb->getInstructions().back().get();
        for (auto& op : term->getOperands()) {
            if (op && op->get() == block) {
                preds.push_back(bb);
                break;
            }
        }
    }

    return preds;
}

bool ControlFlowSimplification::isTerminator(ir::Instruction* instr) {
    if (!instr) return false;
    switch (instr->getOpcode()) {
        case ir::Instruction::Ret:
        case ir::Instruction::Jmp:
        case ir::Instruction::Jnz:
        case ir::Instruction::Jz:
        case ir::Instruction::Br:
        case ir::Instruction::Hlt:
            return true;
        default:
            return false;
    }
}

bool ControlFlowSimplification::isUnconditionalBranch(ir::Instruction* instr) {
    return instr && instr->getOpcode() == ir::Instruction::Jmp;
}

bool ControlFlowSimplification::isConditionalBranch(ir::Instruction* instr) {
    if (!instr) return false;
    ir::Instruction::Opcode op = instr->getOpcode();
    return op == ir::Instruction::Br || op == ir::Instruction::Jnz || op == ir::Instruction::Jz;
}

ir::Constant* ControlFlowSimplification::getConstantCondition(ir::Instruction* branch) {
    if (!isConditionalBranch(branch) || branch->getOperands().empty() || !branch->getOperands()[0]) {
        return nullptr;
    }
    return dynamic_cast<ir::Constant*>(branch->getOperands()[0]->get());
}

} // namespace transforms
