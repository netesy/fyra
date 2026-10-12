#pragma once

#include "ir/Function.h"
#include "ir/Instruction.h"
#include <map>
#include <set>
#include <vector>
#include <cstdint>

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
    // Code emission needs exact operand liveness, not allocation intervals.
    void runForOperandLiveness(ir::Function& func);

    const std::map<const ir::Instruction*, LiveRange>& getLiveRanges() const {
        return liveRanges;
    }

    // Returns true if value is live immediately after instruction according to CFG-aware pre-spill analysis
    bool isLiveAfter(const ir::Instruction* instruction, const ir::Value* value) const;

    // Returns true if value is live immediately before instruction according
    // to the same CFG-aware dataflow solution as isLiveAfter().
    bool isLiveBefore(const ir::Instruction* instruction, const ir::Value* value) const;

    std::set<const ir::Value*> valuesLiveAcrossCalls() const;

    // Returns true if use represents the final live use of its original pre-spill SSA value
    bool isLastUseOfOperand(const ir::Instruction* user, const ir::Use* use) const;

private:
    void computePerInstructionCFGLiveness(ir::Function& func);

    // Map from a virtual register (defined by an instruction) to its live range.
    std::map<const ir::Instruction*, LiveRange> liveRanges;

    // Exact CFG sets use dense indices instead of one allocation per member.
    using LiveBits = std::vector<uint64_t>;
    std::map<const ir::Value*, size_t> valueIndices;
    std::vector<const ir::Value*> indexedValues;
    // Instruction snapshots keep only nonzero words. The block equations
    // remain dense and exact; this changes storage, never the live set.
    struct LiveSnapshot {
        std::vector<std::pair<size_t, uint64_t>> chunks;
        LiveSnapshot() = default;
        explicit LiveSnapshot(const LiveBits& bits);
        bool contains(size_t index) const;
    };
    std::map<const ir::Instruction*, LiveSnapshot> liveAfterMap;
    std::map<const ir::Instruction*, LiveSnapshot> liveBeforeMap;
};

} // namespace transforms
