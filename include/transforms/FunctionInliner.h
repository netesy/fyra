#pragma once

#include "transforms/TransformPass.h"
#include "ir/Module.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"

namespace transforms {

class FunctionInliner {
public:
    FunctionInliner(size_t maxInstrThreshold = 25) : threshold(maxInstrThreshold) {}

    bool runOnModule(ir::Module& module);
    bool canInline(const ir::Function* callee, const ir::Function* caller) const;
    bool inlineCall(ir::Instruction* callInst, ir::Function* callee, ir::Function* caller);

private:
    size_t threshold;
    bool isLoopCallInlineLegal(const ir::Function* callee, const ir::Function* caller, ir::BasicBlock* callBlock, std::string& reason) const;
};

} // namespace transforms
