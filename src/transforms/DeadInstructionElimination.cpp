#include "transforms/DeadInstructionElimination.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include <vector>
#include <set>
#include <unordered_set>
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
    bool changed = false;

    for (auto& bb_ptr : func.getBasicBlocks()) {
        ir::BasicBlock* bb = bb_ptr.get();
        if (!bb) continue;

        std::unordered_set<ir::Value*> loaded_since_store;
        std::unordered_set<ir::Value*> overwritten_since_store;
        std::vector<ir::Instruction*> to_remove;

        for (auto it = bb->getInstructions().rbegin(); it != bb->getInstructions().rend(); ++it) {
            ir::Instruction* instr = it->get();
            if (!instr) continue;

            if (isLoadInstruction(instr)) {
                if (!instr->getOperands().empty() && instr->getOperands()[0]) {
                    ir::Value* ptr = instr->getOperands()[0]->get();
                    if (ptr) {
                        loaded_since_store.insert(ptr);
                        overwritten_since_store.erase(ptr);
                    }
                }
                continue;
            }

            if (isStoreInstruction(instr)) {
                if (instr->getOperands().size() < 2 || !instr->getOperands()[1]) {
                    continue;
                }

                ir::Value* ptr = instr->getOperands()[1]->get();
                if (!ptr) continue;

                const bool has_read_since = loaded_since_store.find(ptr) != loaded_since_store.end();
                const bool overwritten_later = overwritten_since_store.find(ptr) != overwritten_since_store.end();

                if (overwritten_later && !has_read_since) {
                    to_remove.push_back(instr);
                    dead_stores_removed_++;
                    changed = true;
                    continue;
                }

                overwritten_since_store.insert(ptr);
                loaded_since_store.erase(ptr);
                continue;
            }

            if (instr->getOpcode() == ir::Instruction::Call ||
                instr->getOpcode() == ir::Instruction::Syscall) {
                loaded_since_store.clear();
                overwritten_since_store.clear();
            }
        }

        if (!to_remove.empty()) {
            bb->removeInstructions(to_remove);
        }
    }

    return changed;
}

bool DeadInstructionElimination::hasSideEffects(const ir::Instruction* instr) const {
    if (!instr) return false;
    ir::Instruction::Opcode op = instr->getOpcode();
    return op == ir::Instruction::Call || 
           op == ir::Instruction::Syscall || 
           isStoreInstruction(instr) ||
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

bool DeadInstructionElimination::isStoreInstruction(const ir::Instruction* instr) const {
    if (!instr) return false;

    switch (instr->getOpcode()) {
        case ir::Instruction::Store:
        case ir::Instruction::Stored:
        case ir::Instruction::Stores:
        case ir::Instruction::Storel:
        case ir::Instruction::Storeh:
        case ir::Instruction::Storeb:
        case ir::Instruction::VStore:
        case ir::Instruction::VScatter:
            return true;
        default:
            return false;
    }
}

bool DeadInstructionElimination::isLoadInstruction(const ir::Instruction* instr) const {
    if (!instr) return false;

    switch (instr->getOpcode()) {
        case ir::Instruction::Load:
        case ir::Instruction::Loadd:
        case ir::Instruction::Loads:
        case ir::Instruction::Loadl:
        case ir::Instruction::Loaduw:
        case ir::Instruction::Loadsh:
        case ir::Instruction::Loaduh:
        case ir::Instruction::Loadsb:
        case ir::Instruction::Loadub:
        case ir::Instruction::VLoad:
        case ir::Instruction::VGather:
            return true;
        default:
            return false;
    }
}

bool DeadInstructionElimination::mayAlias(ir::Value* ptr1, ir::Value* ptr2) const {
    if (!ptr1 || !ptr2) return true;
    return ptr1 == ptr2;
}

} // namespace transforms
