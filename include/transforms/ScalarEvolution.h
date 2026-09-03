#pragma once

#include "transforms/Loop.h"
#include "transforms/LoopInvariantCodeMotion.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/PhiNode.h"
#include "ir/Constant.h"
#include "ir/Type.h"
#include <memory>
#include <vector>
#include <set>

namespace transforms {

class ScalarEvolution {
public:
    ScalarEvolution() = default;

    bool run(ir::Function& func);

private:
    struct IndVar {
        ir::PhiNode* phi = nullptr;
        ir::Instruction* stepInst = nullptr;
        int64_t initVal = 0;
        int64_t stepVal = 0;
        ir::Value* boundVal = nullptr;
        int64_t constantBound = 0;
        bool isConstantBound = false;
        bool isSlt = true; // true = slt, false = sle
    };

    struct LoopRecurrence {
        ir::PhiNode* sumPhi = nullptr;
        ir::Instruction* sumNextInst = nullptr;
        ir::Value* initSumVal = nullptr;

        // Polynomial coefficients for f(i) = a*i^2 + b*i + c
        int64_t coeffA = 0;
        int64_t coeffB = 0;
        int64_t coeffC = 0;
        bool isValid = false;
    };

    bool processLoop(Loop& loop, ir::Function& func);
    bool analyzeInductionVariable(Loop& loop, IndVar& indVar);
    bool analyzeRecurrence(Loop& loop, const IndVar& indVar, LoopRecurrence& rec);
    bool isSafeToEliminate(Loop& loop);

    ir::Value* generateClosedForm(ir::Function& func, ir::BasicBlock* preheader, const IndVar& indVar, const LoopRecurrence& rec);
    void eliminateLoop(Loop& loop, ir::Value* closedFormVal, ir::Function& func);
};

} // namespace transforms
