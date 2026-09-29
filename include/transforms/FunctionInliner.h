#pragma once

#include "transforms/TransformPass.h"
#include "ir/Module.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"

namespace transforms {

struct InlineCost {
    size_t calleeInstructions = 0;
    size_t callerLoopDepth = 0;
    size_t calleeLoopDepth = 0;
    size_t calleeLoopBlocks = 0;
    size_t argumentCount = 0;
    size_t liveAcrossCallEstimate = 0;
    bool exposesSCEVOpportunity = false;
    bool exposesVectorizationOpportunity = false;

    int calculateBenefit() const;
    int calculateCost() const;
    int getNetScore() const { return calculateBenefit() - calculateCost(); }
    bool isProfitable() const { return getNetScore() > 0; }
};

class FunctionInliner {
public:
    FunctionInliner(size_t maxInstrThreshold = 25) : threshold(maxInstrThreshold) {}

    bool runOnModule(ir::Module& module);
    bool canInline(const ir::Function* callee, const ir::Function* caller) const;
    bool isLoopCallInlineLegal(const ir::Function* callee, const ir::Function* caller, ir::BasicBlock* callBlock, std::string& reason) const;
    bool shouldInline(const ir::Instruction* callInst, const ir::Function* callee, const ir::Function* caller, InlineCost& costOut) const;
    bool inlineCall(ir::Instruction* callInst, ir::Function* callee, ir::Function* caller);

private:
    size_t threshold;
    bool exposesScalarEvolutionOpportunity(const ir::Instruction* callInst, const ir::Function* callee) const;
    bool exposesVectorizationOpportunity(const ir::Instruction* callInst, const ir::Function* callee) const;
};

} // namespace transforms
