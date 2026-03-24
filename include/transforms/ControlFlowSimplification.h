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

class ControlFlowSimplification : public TransformPass {
public:
    explicit ControlFlowSimplification(std::shared_ptr<ErrorReporter> error_reporter = nullptr)
        : TransformPass("Control Flow Simplification", error_reporter) {}

protected:
    bool performTransformation(ir::Function& func) override;

private:
    bool simplifyBranches(ir::Function& func);
    bool simplifyConstantBranches(ir::Function& func);
    bool eliminateUnconditionalBranches(ir::Function& func);
    bool eliminateUnreachableBlocks(ir::Function& func);
    
    bool mergeBlocks(ir::Function& func);
    bool canMergeBlocks(ir::BasicBlock* pred, ir::BasicBlock* succ);
    void mergeBlocksImpl(ir::BasicBlock* pred, ir::BasicBlock* succ);
    
    bool hasOnePredecessor(ir::BasicBlock* block, ir::Function& func);
    bool hasOneSuccessor(ir::BasicBlock* block);
    ir::BasicBlock* getSuccessor(ir::BasicBlock* block);
    std::vector<ir::BasicBlock*> getPredecessors(ir::BasicBlock* block, ir::Function& func);
    
    bool isTerminator(ir::Instruction* instr);
    bool isUnconditionalBranch(ir::Instruction* instr);
    bool isConditionalBranch(ir::Instruction* instr);
    ir::Constant* getConstantCondition(ir::Instruction* branch);
    
    size_t constant_branches_simplified_ = 0;
    size_t blocks_merged_ = 0;
    size_t unconditional_branches_eliminated_ = 0;
};

} // namespace transforms
