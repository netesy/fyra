#pragma once

#include "TransformPass.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include <set>
#include <vector>
#include <map>

namespace transforms {

/**
 * @brief Enhanced Dead Code Elimination pass
 * 
 * This enhanced version supports:
 * - Dead instruction elimination
 * - Unreachable basic block elimination
 * - Dead store elimination with memory dependency analysis
 * - Unused variable elimination with comprehensive use-def chain analysis
 * - Side-effect preservation for function calls and memory operations
 */
class DeadInstructionElimination : public TransformPass {
public:
    explicit DeadInstructionElimination(std::shared_ptr<ErrorReporter> error_reporter = nullptr)
        : TransformPass("Enhanced Dead Code Elimination", error_reporter) {}

    // Legacy interface for compatibility
    bool run(ir::Function& func) {
        return TransformPass::run(func);
    }

protected:
    /**
     * @brief Perform the actual dead code elimination transformation
     * 
     * @param func Function to transform
     * @return true if IR was modified
     */
    bool performTransformation(ir::Function& func) override;

private:
    // Core DCE methods
    bool eliminateDeadInstructions(ir::Function& func);
    bool eliminateUnreachableBlocks(ir::Function& func);
    bool eliminateDeadStores(ir::Function& func);
    
    // Helper methods
    bool hasSideEffects(const ir::Instruction* instr) const;
    bool isTerminator(const ir::Instruction* instr) const;
    bool isAllocaInstruction(const ir::Instruction* instr) const;
    bool isStoreInstruction(const ir::Instruction* instr) const;
    bool isLoadInstruction(const ir::Instruction* instr) const;
    
    // Analysis methods
    void findReachableBlocks(ir::Function& func, std::set<ir::BasicBlock*>& reachable);
    void markLiveInstructions(ir::Function& func, std::set<ir::Instruction*>& live);
    void propagateLiveness(std::set<ir::Instruction*>& live, std::set<ir::Instruction*>& worklist);
    
    // Memory dependency analysis
    bool mayAlias(ir::Value* ptr1, ir::Value* ptr2) const;
    std::vector<ir::Instruction*> findInterveningStores(ir::Instruction* load, ir::Instruction* store) const;
    
    // Statistics tracking
    size_t dead_instructions_removed_ = 0;
    size_t unreachable_blocks_removed_ = 0;
    size_t dead_stores_removed_ = 0;
};

} // namespace transforms
