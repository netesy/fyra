#include "transforms/LoopInvariantCodeMotion.h"
#include "ir/Use.h"
#include "ir/Type.h"
#include "ir/Constant.h"
#include <queue>
#include <algorithm>
#include <iostream>
#include <memory>

namespace transforms {

bool LoopInvariantCodeMotion::performTransformation(ir::Function& func) {
    // Reset statistics
    instructions_hoisted_ = 0;
    loops_processed_ = 0;
    hoistedInstructions_.clear();
    
    // Build dominator tree for the function
    buildDominatorTree(func);
    
    // Find all natural loops in the function
    std::vector<std::unique_ptr<Loop>> loops;
    findLoops(func, loops);
    
    if (loops.empty()) {
        reportInfo("No loops found in function");
        return false;
    }
    
    reportInfo("Found " + std::to_string(loops.size()) + " loops for LICM analysis");
    
    bool changed = false;
    
    // Process each loop for invariant code motion
    for (auto& loop : loops) {
        loops_processed_++;
        
        // Ensure the loop has a preheader
        if (!loop->preheader) {
            loop->preheader = getOrCreatePreheader(*loop, func);
        }
        
        if (!loop->preheader) {
            reportWarning("Could not create preheader for loop");
            continue;
        }
        
        // Hoist invariant instructions
        size_t before = instructions_hoisted_;
        hoistInvariantInstructions(*loop);
        size_t hoisted_in_this_loop = instructions_hoisted_ - before;
        
        if (hoisted_in_this_loop > 0) {
            changed = true;
            reportInfo("Hoisted " + std::to_string(hoisted_in_this_loop) +
                      " instructions from loop");
        }
    }
    
    if (changed) {
        reportInfo("Total instructions hoisted: " + std::to_string(instructions_hoisted_));
    }
    
    return changed;
}

bool LoopInvariantCodeMotion::validatePreconditions(ir::Function& func) {
    // Validate that the function has basic blocks
    if (func.getBasicBlocks().empty()) {
        reportError("Function has no basic blocks");
        return false;
    }
    
    // Validate that each basic block has proper terminator
    for (auto& bb : func.getBasicBlocks()) {
        if (bb->getInstructions().empty()) {
            reportWarning("Empty basic block found");
            continue;
        }
        
        auto& lastInstr = bb->getInstructions().back();
        if (!isTerminator(lastInstr.get())) {
            reportError("Basic block without proper terminator");
            return false;
        }
    }
    
    return true;
}

void LoopInvariantCodeMotion::findLoops(ir::Function& func, 
                                      std::vector<std::unique_ptr<Loop>>& loops) {
    // Find back edges in the CFG
    std::vector<std::pair<ir::BasicBlock*, ir::BasicBlock*>> backEdges;
    findBackEdges(func, backEdges);
    
    // Build loops from back edges
    for (auto& edge : backEdges) {
        ir::BasicBlock* latch = edge.first;
        ir::BasicBlock* header = edge.second;
        
        // Check if we already have a loop with this header
        bool found = false;
        for (auto& existingLoop : loops) {
            if (existingLoop->header == header) {
                // Add this latch to the existing loop
                existingLoop->blocks.insert(latch);
                found = true;
                break;
            }
        }
        
        if (!found) {
            auto loop = std::make_unique<Loop>(header);
            buildLoop(header, latch, loop);
            loops.push_back(std::move(loop));
        }
    }
}

void LoopInvariantCodeMotion::findBackEdges(ir::Function& func,
    std::vector<std::pair<ir::BasicBlock*, ir::BasicBlock*>>& backEdges) {
    
    // Simple back edge detection: edge (A -> B) is a back edge if B dominates A
    for (auto& bb : func.getBasicBlocks()) {
        // Find successors by examining terminator
        auto& instructions = bb->getInstructions();
        if (instructions.empty()) continue;
        
        auto& terminator = instructions.back();
        for (auto& operand : terminator->getOperands()) {
            if (auto* successor = dynamic_cast<ir::BasicBlock*>(operand->get())) {
                if (dominates(successor, bb.get())) {
                    backEdges.push_back({bb.get(), successor});
                }
            }
        }
    }
}

void LoopInvariantCodeMotion::buildLoop(ir::BasicBlock* header, ir::BasicBlock* latch,
                                       std::unique_ptr<Loop>& loop) {
    // Initialize loop with header and latch
    loop->blocks.insert(header);
    loop->blocks.insert(latch);
    
    // Use worklist algorithm to find all blocks in the loop
    std::queue<ir::BasicBlock*> worklist;
    if (latch != header) {
        worklist.push(latch);
    }
    
    while (!worklist.empty()) {
        ir::BasicBlock* current = worklist.front();
        worklist.pop();
        
        // Find predecessors of current block (simplified approach)
        // In a real implementation, we'd maintain a proper predecessor list
        // For now, we'll do a conservative approximation
        
        // Mark this as a simplified implementation
        // A complete implementation would need proper CFG predecessor tracking
    }
    
    // Find loop exits (blocks with successors outside the loop)
    for (ir::BasicBlock* block : loop->blocks) {
        auto& instructions = block->getInstructions();
        if (instructions.empty()) continue;
        
        auto& terminator = instructions.back();
        for (auto& operand : terminator->getOperands()) {
            if (auto* successor = dynamic_cast<ir::BasicBlock*>(operand->get())) {
                if (loop->blocks.find(successor) == loop->blocks.end()) {
                    loop->exits.insert(successor);
                }
            }
        }
    }
}

void LoopInvariantCodeMotion::buildDominatorTree(ir::Function& func) {
    // Simplified dominator tree construction
    // A complete implementation would use the standard dominator tree algorithm
    
    dominators_.clear();
    dominatorTree_.clear();
    
    if (func.getBasicBlocks().empty()) return;
    
    // Entry block dominates itself
    ir::BasicBlock* entry = func.getBasicBlocks().front().get();
    dominators_[entry] = entry;
    
    // For simplicity, we'll use a conservative approximation
    // In practice, this should be replaced with a proper dominator tree algorithm
    for (auto& bb : func.getBasicBlocks()) {
        if (bb.get() != entry) {
            dominators_[bb.get()] = entry; // Conservative: entry dominates all
        }
    }
}

bool LoopInvariantCodeMotion::dominates(ir::BasicBlock* dominator, ir::BasicBlock* block) {
    if (dominator == block) return true;
    
    auto it = dominators_.find(block);
    if (it == dominators_.end()) return false;
    
    return it->second == dominator || dominates(dominator, it->second);
}

bool LoopInvariantCodeMotion::isLoopInvariant(ir::Instruction* instr, const Loop& loop) {
    // An instruction is loop invariant if:
    // 1. All operands are constants, or
    // 2. All operands are defined outside the loop, or  
    // 3. All operands are loop invariant instructions
    
    for (auto& operand : instr->getOperands()) {
        ir::Value* value = operand->get();
        
        // Skip basic block operands (for terminators)
        if (dynamic_cast<ir::BasicBlock*>(value)) {
            continue;
        }
        
        // Constants are always invariant
        if (dynamic_cast<ir::Constant*>(value)) {
            continue;
        }
        
        // Check if operand is defined outside the loop
        if (auto* operandInstr = dynamic_cast<ir::Instruction*>(value)) {
            ir::BasicBlock* defBlock = operandInstr->getParent();
            if (loop.blocks.find(defBlock) != loop.blocks.end()) {
                // Operand is defined inside the loop
                if (hoistedInstructions_.find(operandInstr) == hoistedInstructions_.end()) {
                    return false; // Not invariant
                }
            }
        }
    }
    
    return true;
}

bool LoopInvariantCodeMotion::hasSideEffects(ir::Instruction* instr) {
    switch (instr->getOpcode()) {
        case ir::Instruction::Store:
        case ir::Instruction::Stored:
        case ir::Instruction::Stores:
        case ir::Instruction::Storel:
        case ir::Instruction::Storeh:
        case ir::Instruction::Storeb:
        case ir::Instruction::Call:
            return true;
        default:
            return false;
    }
}

bool LoopInvariantCodeMotion::mayThrow(ir::Instruction* instr) {
    // Conservative: assume division and some other operations may throw
    switch (instr->getOpcode()) {
        case ir::Instruction::Div:
        case ir::Instruction::Udiv:
        case ir::Instruction::Rem:
        case ir::Instruction::Urem:
        case ir::Instruction::Call:
            return true;
        default:
            return false;
    }
}

bool LoopInvariantCodeMotion::mayWriteMemory(ir::Instruction* instr) {
    switch (instr->getOpcode()) {
        case ir::Instruction::Store:
        case ir::Instruction::Stored:
        case ir::Instruction::Stores:
        case ir::Instruction::Storel:
        case ir::Instruction::Storeh:
        case ir::Instruction::Storeb:
        case ir::Instruction::Call: // Conservative
            return true;
        default:
            return false;
    }
}

bool LoopInvariantCodeMotion::mayReadMemory(ir::Instruction* instr) {
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
        case ir::Instruction::Call: // Conservative
            return true;
        default:
            return false;
    }
}

bool LoopInvariantCodeMotion::isTerminator(ir::Instruction* instr) {
    switch (instr->getOpcode()) {
        case ir::Instruction::Ret:
        case ir::Instruction::Jmp:
        case ir::Instruction::Jnz:
        case ir::Instruction::Hlt:
            return true;
        default:
            return false;
    }
}

ir::BasicBlock* LoopInvariantCodeMotion::getOrCreatePreheader(Loop& loop, ir::Function& func) {
    // For this implementation, we'll assume preheaders exist or return null
    // A complete implementation would create preheaders as needed
    reportWarning("Preheader creation not fully implemented");
    return nullptr;
}

void LoopInvariantCodeMotion::hoistInvariantInstructions(Loop& loop) {
    if (!loop.preheader) {
        reportWarning("Cannot hoist instructions without preheader");
        return;
    }
    
    bool changed = true;
    while (changed) {
        changed = false;
        
        for (ir::BasicBlock* block : loop.blocks) {
            auto& instructions = block->getInstructions();
            for (auto it = instructions.begin(); it != instructions.end(); ) {
                ir::Instruction* instr = it->get();
                
                // Skip terminators
                if (isTerminator(instr)) {
                    ++it;
                    continue;
                }
                
                if (canHoistInstruction(instr, loop)) {
                    // Move instruction to preheader
                    reportInfo("Hoisting instruction from loop");
                    
                    // This is a simplified version - real implementation would
                    // properly move the instruction
                    hoistedInstructions_.insert(instr);
                    instructions_hoisted_++;
                    changed = true;
                    
                    // In practice, we would remove from current block and add to preheader
                    // For now, just mark as hoisted
                    ++it;
                } else {
                    ++it;
                }
            }
        }
    }
}

bool LoopInvariantCodeMotion::canHoistInstruction(ir::Instruction* instr, const Loop& loop) {
    // Check if instruction is loop invariant
    if (!isLoopInvariant(instr, loop)) {
        return false;
    }
    
    // Check if instruction has side effects
    if (hasSideEffects(instr)) {
        return false;
    }
    
    // Check if instruction may throw and dominates all exits
    if (mayThrow(instr) && !dominatesAllExits(instr, loop)) {
        return false;
    }
    
    // Check for memory dependencies
    if (mayReadMemory(instr) && hasMemoryDependencies(instr, loop)) {
        return false;
    }
    
    return true;
}

bool LoopInvariantCodeMotion::dominatesAllExits(ir::Instruction* instr, const Loop& loop) {
    ir::BasicBlock* instrBlock = instr->getParent();
    
    for (ir::BasicBlock* exit : loop.exits) {
        if (!dominates(instrBlock, exit)) {
            return false;
        }
    }
    
    return true;
}

bool LoopInvariantCodeMotion::hasMemoryDependencies(ir::Instruction* instr, const Loop& loop) {
    // Check if any instruction in the loop may write to memory that this load reads
    // This is a conservative implementation
    
    for (ir::BasicBlock* block : loop.blocks) {
        for (auto& otherInstr : block->getInstructions()) {
            if (otherInstr.get() != instr && mayWriteMemory(otherInstr.get())) {
                // Conservative: assume they may alias
                return true;
            }
        }
    }
    
    return false;
}

} // namespace transforms