#pragma once

#include "transforms/TransformPass.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include <set>
#include <vector>
#include <map>
#include <unordered_set>
#include <memory>

namespace transforms {

/**
 * @brief Loop-Invariant Code Motion optimization pass
 * 
 * This pass identifies computations within loops that produce the same result
 * on every iteration and moves them outside the loop to reduce redundant work.
 * 
 * Features:
 * - Natural loop detection using back-edges in CFG
 * - Loop invariant analysis for expressions and memory dependencies
 * - Safety checks for dominance and exception safety
 * - Preheader creation for instruction placement
 * - Comprehensive side-effect analysis
 */
class LoopInvariantCodeMotion : public TransformPass {
public:
    explicit LoopInvariantCodeMotion(std::shared_ptr<ErrorReporter> error_reporter = nullptr)
        : TransformPass("Loop-Invariant Code Motion", error_reporter) {}

protected:
    /**
     * @brief Perform the actual LICM transformation
     * 
     * @param func Function to transform
     * @return true if IR was modified
     */
    bool performTransformation(ir::Function& func) override;

    /**
     * @brief Validate that the function has a proper CFG structure
     * 
     * @param func Function to validate
     * @return true if preconditions are met
     */
    bool validatePreconditions(ir::Function& func) override;

private:
    /**
     * @brief Represents a natural loop in the CFG
     */
    struct Loop {
        ir::BasicBlock* header;           ///< Loop header block
        std::set<ir::BasicBlock*> blocks; ///< All blocks in the loop
        std::set<ir::BasicBlock*> exits;  ///< Loop exit blocks
        ir::BasicBlock* preheader;        ///< Preheader block (may be null initially)
        Loop* parent;                     ///< Parent loop (for nested loops)
        std::vector<Loop*> children;      ///< Nested loops
        
        Loop(ir::BasicBlock* h) : header(h), preheader(nullptr), parent(nullptr) {}
    };
    
    // Loop detection methods
    void findLoops(ir::Function& func, std::vector<std::unique_ptr<Loop>>& loops);
    void findBackEdges(ir::Function& func, std::vector<std::pair<ir::BasicBlock*, ir::BasicBlock*>>& backEdges);
    void buildLoop(ir::BasicBlock* header, ir::BasicBlock* latch, std::unique_ptr<Loop>& loop);
    void buildDominatorTree(ir::Function& func);
    bool dominates(ir::BasicBlock* dominator, ir::BasicBlock* block);
    
    // Loop analysis methods
    bool isLoopInvariant(ir::Instruction* instr, const Loop& loop);
    bool hasSideEffects(ir::Instruction* instr);
    bool mayThrow(ir::Instruction* instr);
    bool isMovableTo(ir::Instruction* instr, ir::BasicBlock* target, const Loop& loop);
    
    // Memory dependency analysis
    bool mayWriteMemory(ir::Instruction* instr);
    bool mayReadMemory(ir::Instruction* instr);
    bool hasMemoryDependencies(ir::Instruction* instr, const Loop& loop);
    
    // Preheader management
    ir::BasicBlock* getOrCreatePreheader(Loop& loop, ir::Function& func);
    bool needsPreheader(const Loop& loop);
    
    // Code motion methods
    void moveInstruction(ir::Instruction* instr, ir::BasicBlock* target);
    bool canHoistInstruction(ir::Instruction* instr, const Loop& loop);
    void hoistInvariantInstructions(Loop& loop);
    
    // Safety checks
    bool dominatesAllExits(ir::Instruction* instr, const Loop& loop);
    bool isExecutedOnAllPaths(ir::Instruction* instr, const Loop& loop);
    bool isTerminator(ir::Instruction* instr);
    
    // Helper data structures
    std::map<ir::BasicBlock*, ir::BasicBlock*> dominators_;
    std::map<ir::BasicBlock*, std::set<ir::BasicBlock*>> dominatorTree_;
    std::set<ir::Instruction*> hoistedInstructions_;
    
    // Statistics
    size_t instructions_hoisted_ = 0;
    size_t loops_processed_ = 0;
};

} // namespace transforms