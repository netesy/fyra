#pragma once

#include "transforms/TransformPass.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include <set>
#include <vector>
#include <map>
#include <memory>

namespace transforms {

/**
 * @brief Control Flow Simplification optimization pass
 * 
 * This pass simplifies control flow patterns by:
 * - Eliminating branches with constant conditions
 * - Merging basic blocks with single predecessors/successors
 * - Removing unreachable basic blocks
 * - Jump threading for improved branch prediction
 */
class ControlFlowSimplification : public TransformPass {
public:
    explicit ControlFlowSimplification(std::shared_ptr<ErrorReporter> error_reporter = nullptr)
        : TransformPass("Control Flow Simplification", error_reporter) {}

protected:
    /**
     * @brief Perform the actual control flow simplification
     * 
     * @param func Function to transform
     * @return true if IR was modified
     */
    bool performTransformation(ir::Function& func) override;

private:
    // Branch simplification methods
    bool simplifyBranches(ir::Function& func);
    bool simplifyConstantBranches(ir::Function& func);
    bool eliminateUnconditionalBranches(ir::Function& func);
    
    // Block merging methods
    bool mergeBlocks(ir::Function& func);
    bool canMergeBlocks(ir::BasicBlock* pred, ir::BasicBlock* succ);
    void mergeBlocksImpl(ir::BasicBlock* pred, ir::BasicBlock* succ);
    
    // Analysis methods
    bool hasOnePredecessor(ir::BasicBlock* block, ir::Function& func);
    bool hasOneSuccessor(ir::BasicBlock* block);
    ir::BasicBlock* getSuccessor(ir::BasicBlock* block);
    std::vector<ir::BasicBlock*> getPredecessors(ir::BasicBlock* block, ir::Function& func);
    
    // Helper methods
    bool isTerminator(ir::Instruction* instr);
    bool isUnconditionalBranch(ir::Instruction* instr);
    bool isConditionalBranch(ir::Instruction* instr);
    ir::Constant* getConstantCondition(ir::Instruction* branch);
    
    // Statistics
    size_t constant_branches_simplified_ = 0;
    size_t blocks_merged_ = 0;
    size_t unconditional_branches_eliminated_ = 0;
};

} // namespace transforms