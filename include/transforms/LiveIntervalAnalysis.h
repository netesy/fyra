#pragma once

#include "ir/Function.h"
#include "transforms/LiveInterval.h"
#include <vector>

namespace transforms {

/**
 * @brief This analysis consumes the results of LivenessAnalysis to build
 * a list of LiveInterval objects, sorted by their start point.
 */
class LiveIntervalAnalysis {
public:
    void run(ir::Function& func);

    const std::vector<LiveInterval>& getIntervals() const {
        return intervals;
    }

private:
    std::vector<LiveInterval> intervals;
};

} // namespace transforms
