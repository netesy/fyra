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
#include <optional>

namespace transforms {

class ScalarEvolution {
public:
    ScalarEvolution() = default;

    bool run(ir::Function& func);

    // Analysis-only query: true only when SCEV can generate a closed-form
    // replacement for this loop in its current context.
    bool canEliminateClosedForm(ir::Function& func, ir::BasicBlock* header);

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

        // Recurrence metadata for mixed-width analysis
        uint32_t sourceWidth = 32;
        bool sourceSigned = true;
        ir::Instruction::Opcode extensionOp = ir::Instruction::ExtSW;
        uint32_t destWidth = 64;

        bool isValid = false;
    };

    struct ClosedFormPlan {
        ir::IntegerType* resultType = nullptr;
        uint64_t result = 0;
        ir::Value* existingValue = nullptr;
    };

    bool processLoop(Loop& loop, ir::Function& func);
    bool analyzeInductionVariable(Loop& loop, IndVar& indVar);
    bool analyzeRecurrence(Loop& loop, const IndVar& indVar, LoopRecurrence& rec);
    bool isSafeToEliminate(Loop& loop);

    std::optional<ClosedFormPlan> analyzeClosedForm(const IndVar& indVar,
                                                    const LoopRecurrence& rec);
    ir::Value* materializeClosedForm(ir::Function& func,
                                     const ClosedFormPlan& plan);
    void eliminateLoop(Loop& loop, ir::Value* closedFormVal, ir::Function& func);
};

} // namespace transforms
