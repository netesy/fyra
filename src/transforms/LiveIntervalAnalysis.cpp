#include "transforms/LiveIntervalAnalysis.h"
#include "transforms/LivenessAnalysis.h"
#include <algorithm>

namespace transforms {

void LiveIntervalAnalysis::run(ir::Function& func) {
    // 1. Run the underlying liveness analysis
    LivenessAnalysis liveness;
    liveness.run(func);
    const auto& liveRanges = liveness.getLiveRanges();

    // 2. Clear out any old data
    intervals.clear();

    // 3. Create LiveInterval objects from the results
    for (const auto& pair : liveRanges) {
        const ir::Instruction* vreg = pair.first;
        const LiveRange& range = pair.second;
        // The const_cast is safe here because we know the analysis pipeline
        // won't deallocate the instruction while we hold a pointer to it.
        intervals.emplace_back(const_cast<ir::Instruction*>(vreg), range.start, range.end);
    }

    // 4. Sort the intervals by their starting point
    std::sort(intervals.begin(), intervals.end());
}

} // namespace transforms
