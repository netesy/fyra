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

private:
    void computeLiveSets(ir::Function& func);

    // Map from a virtual register (defined by an instruction) to its live range.
    std::map<const ir::Instruction*, LiveRange> liveRanges;

    // Live-in and live-out sets for each basic block.
    std::map<ir::BasicBlock*, std::set<ir::Instruction*>> liveIn;
    std::map<ir::BasicBlock*, std::set<ir::Instruction*>> liveOut;
};

} // namespace transforms
