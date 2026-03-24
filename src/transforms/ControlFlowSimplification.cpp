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
        
        if (term->getOpcode() == ir::Instruction::Br || term->getOpcode() == ir::Instruction::Jnz || term->getOpcode() == ir::Instruction::Jz) {
            if (term->getOperands().empty() || !term->getOperands()[0]) continue;
            ir::Value* cond = term->getOperands()[0]->get();
            if (auto* ci = dynamic_cast<ir::ConstantInt*>(cond)) {
                bool condVal = ci->getValue() != 0;
                if (term->getOpcode() == ir::Instruction::Jz) condVal = !condVal;

                if (term->getOperands().size() < 2) continue;
                ir::BasicBlock* trueDest = dynamic_cast<ir::BasicBlock*>(term->getOperands()[1]->get());
                ir::BasicBlock* falseDest = (term->getOperands().size() > 2) ? dynamic_cast<ir::BasicBlock*>(term->getOperands()[2]->get()) : nullptr;
                
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
                            auto& ops = phi->getOperands();
                            for (size_t i = 0; i + 1 < ops.size(); i += 2) {
                                if (ops[i] && ops[i]->get() == bb) {
                                    ops.erase(ops.begin() + i, ops.begin() + i + 2);
                                    break;
                                }
                            }
                        } else if (instr->getOpcode() != ir::Instruction::Phi) break;
                    }
                }
                
                changed = true;
                constant_branches_simplified_++;
            }
        }
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
            // Unreachable block removal logic...
            it = blocks.erase(it);
            changed = true;
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
            
            ir::Instruction* term = bb->getInstructions().back().get();
            if (term->getOpcode() == ir::Instruction::Jmp) {
                if (term->getOperands().empty() || !term->getOperands()[0]) continue;
                ir::BasicBlock* succ = dynamic_cast<ir::BasicBlock*>(term->getOperands()[0]->get());
                if (!succ || succ == bb || succ == blocks.front().get()) continue;

                if (hasOnePredecessor(succ, func)) {
                    bb->getInstructions().pop_back(); 
                    auto& succInstrs = succ->getInstructions();
                    while (!succInstrs.empty()) {
                        if (auto* phi = dynamic_cast<ir::PhiNode*>(succInstrs.front().get())) {
                            ir::Value* val = phi->getIncomingValueForBlock(bb);
                            if (val) phi->replaceAllUsesWith(val);
                            succInstrs.erase(succInstrs.begin());
                        } else {
                            ir::Instruction* si = succInstrs.front().get();
                            si->setParent(bb);
                            bb->getInstructions().push_back(std::move(succInstrs.front()));
                            succInstrs.erase(succInstrs.begin());
                        }
                    }
                    for (auto it2 = blocks.begin(); it2 != blocks.end(); ++it2) {
                        if (it2->get() == succ) {
                            blocks.erase(it2);
                            break;
                        }
                    }
                    changed = true;
                    block_changed = true;
                    blocks_merged_++;
                    break; 
                }
            }
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

bool ControlFlowSimplification::eliminateUnconditionalBranches(ir::Function& func) {
    (void)func; return false;
}

bool ControlFlowSimplification::simplifyBranches(ir::Function& func) {
    return simplifyConstantBranches(func);
}

bool ControlFlowSimplification::canMergeBlocks(ir::BasicBlock* pred, ir::BasicBlock* succ) {
    (void)pred; (void)succ; return false;
}

void ControlFlowSimplification::mergeBlocksImpl(ir::BasicBlock* pred, ir::BasicBlock* succ) {
    (void)pred; (void)succ;
}

bool ControlFlowSimplification::hasOneSuccessor(ir::BasicBlock* block) {
    (void)block; return false;
}

ir::BasicBlock* ControlFlowSimplification::getSuccessor(ir::BasicBlock* block) {
    (void)block; return nullptr;
}

std::vector<ir::BasicBlock*> ControlFlowSimplification::getPredecessors(ir::BasicBlock* block, ir::Function& func) {
    (void)block; (void)func; return {};
}

bool ControlFlowSimplification::isTerminator(ir::Instruction* instr) {
    (void)instr; return false;
}

bool ControlFlowSimplification::isUnconditionalBranch(ir::Instruction* instr) {
    (void)instr; return false;
}

bool ControlFlowSimplification::isConditionalBranch(ir::Instruction* instr) {
    (void)instr; return false;
}

ir::Constant* ControlFlowSimplification::getConstantCondition(ir::Instruction* branch) {
    (void)branch; return nullptr;
}

} // namespace transforms
