#pragma once

#include "ir/Function.h"
#include "ir/Instruction.h"
#include <map>
#include <set>

namespace transforms {

// Represents the live range of a virtual register.
// For now, we'll just use instruction numbers as a simple measure of "time".
struct LiveRange {
    int start = -1;
    int end = -1;
};

class LivenessAnalysis {
public:
    void run(ir::Function& func);

    const std::map<const ir::Instruction*, LiveRange>& getLiveRanges() const {
        return liveRanges;
    }

    // Returns true if value is live immediately after instruction according to CFG-aware pre-spill analysis
    bool isLiveAfter(const ir::Instruction* instruction, const ir::Value* value) const;

    // Returns true if use represents the final live use of its original pre-spill SSA value
    bool isLastUseOfOperand(const ir::Instruction* user, const ir::Use* use) const;

    const std::map<const ir::Instruction*, std::set<const ir::Value*>>& getLiveAfterMap() const {
        return liveAfterMap;
    }

private:
    void computeLiveSets(ir::Function& func);
    void computePerInstructionCFGLiveness(ir::Function& func);

    // Map from a virtual register (defined by an instruction) to its live range.
    std::map<const ir::Instruction*, LiveRange> liveRanges;

    // Live-in and live-out sets for each basic block.
    std::map<ir::BasicBlock*, std::set<ir::Instruction*>> liveIn;
    std::map<ir::BasicBlock*, std::set<ir::Instruction*>> liveOut;

    // CFG-aware per-instruction liveAfter sets
    std::map<const ir::Instruction*, std::set<const ir::Value*>> liveAfterMap;
    std::map<const ir::Instruction*, std::set<const ir::Value*>> liveBeforeMap;
};

} // namespace transforms
