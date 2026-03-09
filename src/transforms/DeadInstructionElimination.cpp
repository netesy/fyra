#include "transforms/DeadInstructionElimination.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include "ir/BasicBlock.h"
#include <list>
#include <vector>
#include <set>
#include <algorithm>
#include <queue>

namespace transforms {

bool DeadInstructionElimination::performTransformation(ir::Function& func) {
    bool changed = false;
    
    // Reset statistics
    dead_instructions_removed_ = 0;
    unreachable_blocks_removed_ = 0;
    dead_stores_removed_ = 0;
    
    // Phase 1: Eliminate unreachable basic blocks
    if (eliminateUnreachableBlocks(func)) {
        changed = true;
        reportInfo("Eliminated " + std::to_string(unreachable_blocks_removed_) + " unreachable blocks");
    }
    
    // Phase 2: Eliminate dead stores (before general DCE for better results)
    if (eliminateDeadStores(func)) {
        changed = true;
        reportInfo("Eliminated " + std::to_string(dead_stores_removed_) + " dead stores");
    }
    
    // Phase 3: Eliminate dead instructions
    if (eliminateDeadInstructions(func)) {
        changed = true;
        reportInfo("Eliminated " + std::to_string(dead_instructions_removed_) + " dead instructions");
    }
    
    return changed;
}

bool DeadInstructionElimination::eliminateDeadInstructions(ir::Function& func) {
    std::set<ir::Instruction*> liveInstructions;

    // Mark all instructions with side effects and terminators as live
    markLiveInstructions(func, liveInstructions);
    
    // Propagate liveness backwards through use-def chains
    std::set<ir::Instruction*> worklist = liveInstructions;
    propagateLiveness(liveInstructions, worklist);
    
    // Collect dead instructions
    std::set<ir::Instruction*> deadInstructions;
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (liveInstructions.find(instr.get()) == liveInstructions.end()) {
                deadInstructions.insert(instr.get());
            }
        }
    }
    
    if (deadInstructions.empty()) {
        return false;
    }
    
    dead_instructions_removed_ = deadInstructions.size();
    
    // Remove dead instructions
    for (auto& bb : func.getBasicBlocks()) {
        auto& instructions = bb->getInstructions();
        instructions.remove_if([&](const std::unique_ptr<ir::Instruction>& instr) {
            return deadInstructions.count(instr.get());
        });
    }
    
    return true;
}

bool DeadInstructionElimination::eliminateUnreachableBlocks(ir::Function& func) {
    std::set<ir::BasicBlock*> reachableBlocks;
    findReachableBlocks(func, reachableBlocks);
    
    // Find unreachable blocks
    std::vector<ir::BasicBlock*> unreachableBlocks;
    for (auto& bb : func.getBasicBlocks()) {
        if (reachableBlocks.find(bb.get()) == reachableBlocks.end()) {
            unreachableBlocks.push_back(bb.get());
        }
    }
    
    if (unreachableBlocks.empty()) {
        return false;
    }
    
    unreachable_blocks_removed_ = unreachableBlocks.size();
    
    // Remove unreachable blocks
    auto& basicBlocks = func.getBasicBlocks();
    basicBlocks.remove_if([&](const std::unique_ptr<ir::BasicBlock>& bb) {
        return std::find(unreachableBlocks.begin(), unreachableBlocks.end(), bb.get()) != unreachableBlocks.end();
    });
    
    return true;
}

bool DeadInstructionElimination::eliminateDeadStores(ir::Function& func) {
    bool changed = false;
    std::set<ir::Instruction*> to_remove;

    for (auto& bb : func.getBasicBlocks()) {
        std::map<ir::Value*, ir::Instruction*> lastStoreTo;
        std::vector<ir::Instruction*> local_dead;

        for (auto& instr_ptr : bb->getInstructions()) {
            ir::Instruction* instr = instr_ptr.get();

            if (isStoreInstruction(instr)) {
                ir::Value* ptr = instr->getOperands()[1]->get();
                if (lastStoreTo.count(ptr)) {
                    // Previous store to same ptr in same block is dead if no load intervened
                    local_dead.push_back(lastStoreTo[ptr]);
                }
                lastStoreTo[ptr] = instr;
            } else if (isLoadInstruction(instr)) {
                // Load from any pointer makes the last store to it potentially live
                ir::Value* ptr = instr->getOperands()[0]->get();
                lastStoreTo.erase(ptr);

                // Conservatively assume a load could alias with anything if it's not a direct alloca
                if (!dynamic_cast<ir::Instruction*>(ptr) || !isAllocaInstruction(dynamic_cast<ir::Instruction*>(ptr))) {
                    lastStoreTo.clear();
                }
            } else if (instr->getOpcode() == ir::Instruction::Call) {
                // Calls can read any memory
                lastStoreTo.clear();
            }
        }
        
        for (auto* ds : local_dead) to_remove.insert(ds);
    }

    if (to_remove.empty()) return false;

    for (auto& bb : func.getBasicBlocks()) {
        auto& instrs = bb->getInstructions();
        instrs.remove_if([&](auto& i) { return to_remove.count(i.get()); });
    }

    dead_stores_removed_ = to_remove.size();
    return true;
}

bool DeadInstructionElimination::hasSideEffects(const ir::Instruction* instr) const {
    switch (instr->getOpcode()) {
        case ir::Instruction::Ret:
        case ir::Instruction::Jmp:
        case ir::Instruction::Jnz:
        case ir::Instruction::Br:
        case ir::Instruction::Store:
        case ir::Instruction::Stored:
        case ir::Instruction::Stores:
        case ir::Instruction::Storel:
        case ir::Instruction::Storeh:
        case ir::Instruction::Storeb:
        case ir::Instruction::Alloc:
        case ir::Instruction::Alloc4:
        case ir::Instruction::Alloc16:
        case ir::Instruction::Call:
        case ir::Instruction::Hlt:
            return true;
        default:
            return false;
    }
}

bool DeadInstructionElimination::isTerminator(const ir::Instruction* instr) const {
    switch (instr->getOpcode()) {
        case ir::Instruction::Ret:
        case ir::Instruction::Jmp:
        case ir::Instruction::Jnz:
        case ir::Instruction::Br:
        case ir::Instruction::Hlt:
            return true;
        default:
            return false;
    }
}

bool DeadInstructionElimination::isAllocaInstruction(const ir::Instruction* instr) const {
    switch (instr->getOpcode()) {
        case ir::Instruction::Alloc:
        case ir::Instruction::Alloc4:
        case ir::Instruction::Alloc16:
            return true;
        default:
            return false;
    }
}

bool DeadInstructionElimination::isStoreInstruction(const ir::Instruction* instr) const {
    switch (instr->getOpcode()) {
        case ir::Instruction::Store:
        case ir::Instruction::Stored:
        case ir::Instruction::Stores:
        case ir::Instruction::Storel:
        case ir::Instruction::Storeh:
        case ir::Instruction::Storeb:
            return true;
        default:
            return false;
    }
}

bool DeadInstructionElimination::isLoadInstruction(const ir::Instruction* instr) const {
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
            return true;
        default:
            return false;
    }
}

void DeadInstructionElimination::findReachableBlocks(ir::Function& func, std::set<ir::BasicBlock*>& reachable) {
    if (func.getBasicBlocks().empty()) {
        return;
    }
    
    std::queue<ir::BasicBlock*> worklist;
    ir::BasicBlock* entry = func.getBasicBlocks().front().get();
    
    worklist.push(entry);
    reachable.insert(entry);
    
    while (!worklist.empty()) {
        ir::BasicBlock* current = worklist.front();
        worklist.pop();
        
        // Find successor blocks by examining terminators
        for (auto& instr : current->getInstructions()) {
            if (isTerminator(instr.get())) {
                // Add successors to worklist
                for (auto& operand : instr->getOperands()) {
                    if (auto* bb = dynamic_cast<ir::BasicBlock*>(operand->get())) {
                        if (reachable.find(bb) == reachable.end()) {
                            reachable.insert(bb);
                            worklist.push(bb);
                        }
                    }
                }
            }
        }
    }
}

void DeadInstructionElimination::markLiveInstructions(ir::Function& func, std::set<ir::Instruction*>& live) {
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            // Mark instructions with side effects as live
            if (hasSideEffects(instr.get()) || isTerminator(instr.get())) {
                live.insert(instr.get());
            }
        }
    }
}

void DeadInstructionElimination::propagateLiveness(std::set<ir::Instruction*>& live, std::set<ir::Instruction*>& worklist) {
    std::vector<ir::Instruction*> q;
    for (auto* instr : worklist) q.push_back(instr);

    while (!q.empty()) {
        ir::Instruction* instr = q.back();
        q.pop_back();

        for (auto& operand : instr->getOperands()) {
            if (auto* opInstr = dynamic_cast<ir::Instruction*>(operand->get())) {
                if (live.find(opInstr) == live.end()) {
                    live.insert(opInstr);
                    q.push_back(opInstr);
                }
            }
        }
    }
}


} // namespace transforms
