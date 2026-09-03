#pragma once

#include "transforms/TransformPass.h"
#include "transforms/Loop.h"
#include "transforms/ErrorReporter.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/PhiNode.h"
#include "ir/Constant.h"
#include <memory>
#include <vector>

namespace transforms {

class LoopUnrollPass : public TransformPass {
public:
    explicit LoopUnrollPass(std::shared_ptr<ErrorReporter> errorReporter = nullptr)
        : TransformPass("Loop Unrolling", errorReporter) {}

protected:
    bool performTransformation(ir::Function& func) override;

private:
    struct UnrollCandidate {
        Loop* loop = nullptr;
        ir::BasicBlock* header = nullptr;
        ir::BasicBlock* body = nullptr;
        ir::BasicBlock* preheader = nullptr;
        ir::BasicBlock* exitBB = nullptr;
        ir::PhiNode* indPhi = nullptr;
        ir::Instruction* indNext = nullptr;
        int64_t initVal = 0;
        int64_t stepVal = 0;
        ir::Value* boundVal = nullptr;
        bool isConstantBound = false;
        int64_t constantBound = 0;
        bool isSle = true; // true = sle, false = slt
    };

    bool isLegalToUnroll(Loop& loop, ir::Function& func, UnrollCandidate& cand);
    bool unrollLoopBy2(UnrollCandidate& cand, ir::Function& func);
};

} // namespace transforms
