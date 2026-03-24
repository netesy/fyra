#include "transforms/DeadInstructionElimination.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include <vector>
#include <set>
#include <iostream>
#include <algorithm>

namespace transforms {

bool DeadInstructionElimination::performTransformation(ir::Function& func) {
    bool changed = false;
    bool iteration_changed = true;
    
    while (iteration_changed) {
        iteration_changed = false;
        
        if (eliminateUnreachableBlocks(func)) {
            changed = true;
            iteration_changed = true;
        }
        
        if (eliminateDeadInstructions(func)) {
            changed = true;
            iteration_changed = true;
        }

        if (eliminateDeadStores(func)) {
            changed = true;
            iteration_changed = true;
        }
    }
    
    return changed;
}

bool DeadInstructionElimination::eliminateDeadInstructions(ir::Function& func) {
    std::set<ir::Instruction*> live;
    markLiveInstructions(func, live);
    
    bool changed = false;
    for (auto& bb_ptr : func.getBasicBlocks()) {
        ir::BasicBlock* bb = bb_ptr.get();
        auto& instrs = bb->getInstructions();
        auto it = instrs.begin();
        while (it != instrs.end()) {
            ir::Instruction* instr = it->get();
            if (live.find(instr) == live.end() && !hasSideEffects(instr) && !isTerminator(instr)) {
                it = instrs.erase(it);
                changed = true;
                dead_instructions_removed_++;
            } else {
                ++it;
            }
        }
    }
    return changed;
}

bool DeadInstructionElimination::eliminateUnreachableBlocks(ir::Function& func) {
    std::set<ir::BasicBlock*> reachable;
    findReachableBlocks(func, reachable);
    
    bool changed = false;
    auto& blocks = func.getBasicBlocks();
    auto it = blocks.begin();
    if (it != blocks.end()) ++it; // Skip entry block
    
    while (it != blocks.end()) {
        ir::BasicBlock* bb = it->get();
        if (reachable.find(bb) == reachable.end()) {
            // Before removing, update PHI nodes in successors
            if (!bb->getInstructions().empty()) {
                ir::Instruction* term = bb->getInstructions().back().get();
                for (auto& op : term->getOperands()) {
                    if (auto* succ = dynamic_cast<ir::BasicBlock*>(op->get())) {
                        for (auto& instr : succ->getInstructions()) {
                            if (auto* phi = dynamic_cast<ir::PhiNode*>(instr.get())) {
                                phi->removeIncomingValue(bb);
                            } else if (instr->getOpcode() != ir::Instruction::Phi) break;
                        }
                    }
                }
            }
            it = blocks.erase(it);
            changed = true;
            unreachable_blocks_removed_++;
        } else {
            ++it;
        }
    }
    return changed;
}

bool DeadInstructionElimination::eliminateDeadStores(ir::Function& func) {
    // Basic dead store elimination could be implemented here
    (void)func;
    return false;
}

bool DeadInstructionElimination::hasSideEffects(const ir::Instruction* instr) const {
    if (!instr) return false;
    ir::Instruction::Opcode op = instr->getOpcode();
    return op == ir::Instruction::Call || 
           op == ir::Instruction::Syscall || 
           op == ir::Instruction::Store ||
           op == ir::Instruction::Ret;
}

bool DeadInstructionElimination::isTerminator(const ir::Instruction* instr) const {
    if (!instr) return false;
    ir::Instruction::Opcode op = instr->getOpcode();
    return op == ir::Instruction::Ret || 
           op == ir::Instruction::Jmp || 
           op == ir::Instruction::Jnz || 
           op == ir::Instruction::Jz ||
           op == ir::Instruction::Br;
}

void DeadInstructionElimination::findReachableBlocks(ir::Function& func, std::set<ir::BasicBlock*>& reachable) {
    if (func.getBasicBlocks().empty()) return;
    
    std::vector<ir::BasicBlock*> worklist;
    ir::BasicBlock* entry = func.getBasicBlocks().front().get();
    
    reachable.insert(entry);
    worklist.push_back(entry);
    
    while (!worklist.empty()) {
        ir::BasicBlock* curr = worklist.back();
        worklist.pop_back();
        
        if (curr->getInstructions().empty()) continue;
        ir::Instruction* term = curr->getInstructions().back().get();
        
        for (auto& op : term->getOperands()) {
            if (auto* succ = dynamic_cast<ir::BasicBlock*>(op->get())) {
                if (reachable.insert(succ).second) {
                    worklist.push_back(succ);
                }
            }
        }
    }
}

void DeadInstructionElimination::markLiveInstructions(ir::Function& func, std::set<ir::Instruction*>& live) {
    std::set<ir::Instruction*> worklist;
    
    for (auto& bb_ptr : func.getBasicBlocks()) {
        for (auto& instr_ptr : bb_ptr->getInstructions()) {
            ir::Instruction* instr = instr_ptr.get();
            if (hasSideEffects(instr) || isTerminator(instr)) {
                live.insert(instr);
                worklist.insert(instr);
            }
        }
    }
    
    propagateLiveness(live, worklist);
}

void DeadInstructionElimination::propagateLiveness(std::set<ir::Instruction*>& live, std::set<ir::Instruction*>& worklist) {
    while (!worklist.empty()) {
        ir::Instruction* curr = *worklist.begin();
        worklist.erase(worklist.begin());
        
        for (auto& op : curr->getOperands()) {
            if (auto* instr_op = dynamic_cast<ir::Instruction*>(op->get())) {
                if (live.insert(instr_op).second) {
                    worklist.insert(instr_op);
                }
            }
        }
    }
}

// Stub implementations for other methods in header
bool DeadInstructionElimination::isAllocaInstruction(const ir::Instruction* instr) const { (void)instr; return false; }
bool DeadInstructionElimination::isStoreInstruction(const ir::Instruction* instr) const { (void)instr; return false; }
bool DeadInstructionElimination::isLoadInstruction(const ir::Instruction* instr) const { (void)instr; return false; }
bool DeadInstructionElimination::mayAlias(ir::Value* ptr1, ir::Value* ptr2) const { (void)ptr1; (void)ptr2; return true; }
std::vector<ir::Instruction*> DeadInstructionElimination::findInterveningStores(ir::Instruction* load, ir::Instruction* store) const { (void)load; (void)store; return {}; }

} // namespace transforms
