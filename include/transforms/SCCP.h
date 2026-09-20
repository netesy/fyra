#pragma once

#include "TransformPass.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include "ir/Type.h"
#include "transforms/DominatorTree.h"
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <limits>
#include <memory>
#include <cstdint>

namespace transforms {

struct CallKey {
    ir::Function* callee = nullptr;
    std::vector<std::pair<ir::Type*, uint64_t>> argValues;

    bool operator<(const CallKey& other) const {
        if (callee != other.callee) return callee < other.callee;
        if (argValues.size() != other.argValues.size()) return argValues.size() < other.argValues.size();
        for (size_t i = 0; i < argValues.size(); ++i) {
            if (argValues[i].first != other.argValues[i].first) return argValues[i].first < other.argValues[i].first;
            if (argValues[i].second != other.argValues[i].second) return argValues[i].second < other.argValues[i].second;
        }
        return false;
    }

    bool operator==(const CallKey& other) const {
        if (callee != other.callee) return false;
        if (argValues.size() != other.argValues.size()) return false;
        for (size_t i = 0; i < argValues.size(); ++i) {
            if (argValues[i].first != other.argValues[i].first) return false;
            if (argValues[i].second != other.argValues[i].second) return false;
        }
        return true;
    }
};

class SCCP : public TransformPass {
public:
    struct EvaluationContext {
        int totalModuleInstructionCount = 0;
        int maxModuleInstructionBudget = 250000000; // 250M cumulative
        int maxCallInstructionBudget    = 128000000; // 128M per call
        int maxCallIterationBudget      = 10000000;  // 10M iterations per call
        int maxRecursionDepth           = 64;

        std::map<CallKey, ir::Constant*> evalCache;
        std::unordered_map<ir::Function*, bool> purityCache;
        std::unordered_map<ir::Function*, std::shared_ptr<DominatorTree>> domTreeCache;

        void reset() {
            totalModuleInstructionCount = 0;
            evalCache.clear();
            purityCache.clear();
            domTreeCache.clear();
        }
    };

    explicit SCCP(std::shared_ptr<ErrorReporter> error_reporter = nullptr)
        : TransformPass("Enhanced SCCP", error_reporter) {}

    bool run(ir::Function& func) {
        return TransformPass::run(func);
    }

    const EvaluationContext& getEvaluationContext() const { return evalCtx; }

protected:
    bool performTransformation(ir::Function& func) override;
    bool validatePreconditions(ir::Function& func) override;

private:
    enum LatticeValue {
        Top,
        Constant,
        Bottom
    };

    struct LatticeEntry {
        LatticeValue type = Top;
        ir::Constant* constant = nullptr;
    };

    void initialize(ir::Function& func);
    void visit(ir::Instruction* instr, std::set<std::pair<ir::BasicBlock*, ir::BasicBlock*>>& executableEdges, std::set<ir::BasicBlock*>& executableBlocks, std::unordered_set<ir::Instruction*>& inInstructionWorklist);
    LatticeEntry getLatticeValue(ir::Value* val);
    void setLatticeValue(ir::Instruction* instr, LatticeEntry new_val, std::unordered_set<ir::Instruction*>& inInstructionWorklist);

    bool isFunctionPure(ir::Function* func, std::unordered_set<ir::Function*>& activeVisiting);
    ir::Constant* evaluatePureFunctionCall(
        ir::Function* callee,
        const std::vector<ir::Constant*>& argConstants,
        int depth,
        int& callStepCount,
        int& callBackedgeCount
    );

    ir::Constant* foldVectorInstruction(
        ir::Instruction* instr,
        const std::vector<ir::Constant*>& opConsts
    );

    CallKey createCallKey(ir::Function* callee, const std::vector<ir::Constant*>& argConstants);

    EvaluationContext evalCtx;
    std::map<ir::Value*, LatticeEntry> lattice;
    std::vector<ir::Instruction*> instructionWorklist;
    std::vector<ir::BasicBlock*> blockWorklist;
};

} // namespace transforms
