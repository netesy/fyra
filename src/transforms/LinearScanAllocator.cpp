#include "transforms/LinearScanAllocator.h"
#include "transforms/LiveIntervalAnalysis.h"
#include "ir/Function.h"
#include <algorithm>
#include <cassert>
#include <iostream>

namespace transforms {

// Define the number of available physical registers for our target
const unsigned int NUM_PHYSICAL_REGISTERS = 3; // Use a small number to force spilling

void LinearScanAllocator::run(ir::Function& func) {
    linearScan(func);
}

void LinearScanAllocator::linearScan(ir::Function& func) {
    LiveIntervalAnalysis interval_analysis;
    interval_analysis.run(func);
    const auto& intervals = interval_analysis.getIntervals();

    for (unsigned int i = 0; i < NUM_PHYSICAL_REGISTERS; ++i) {
        free_registers.push_back({i});
    }

    for (const auto& current_interval : intervals) {
        expireOldIntervals(current_interval.getStart());

        if (active_intervals.size() == NUM_PHYSICAL_REGISTERS) {
            spillAtInterval(current_interval);
        } else {
            PhysicalReg reg = free_registers.back();
            free_registers.pop_back();
            vreg_to_location_map[current_interval.getVreg()] = reg;
            active_intervals.push_back(&current_interval);
            std::sort(active_intervals.begin(), active_intervals.end(),
                [](const LiveInterval* a, const LiveInterval* b) {
                    return a->getEnd() < b->getEnd();
                });
        }
    }
}

void LinearScanAllocator::expireOldIntervals(int current_start_point) {
    auto it = active_intervals.begin();
    while (it != active_intervals.end()) {
        const LiveInterval* interval = *it;
        if (interval->getEnd() >= current_start_point) {
            return;
        }

        // This interval has expired. Add its register back to the free pool.
        RegLocation loc = vreg_to_location_map.at(interval->getVreg());
        if (std::holds_alternative<PhysicalReg>(loc)) {
            free_registers.push_back(std::get<PhysicalReg>(loc));
        }

        it = active_intervals.erase(it);
    }
}

void LinearScanAllocator::spillAtInterval(const LiveInterval& current_interval) {
    // The last interval in `active` is the one with the latest end point.
    const LiveInterval* spill_candidate = active_intervals.back();

    if (spill_candidate->getEnd() > current_interval.getEnd()) {
        // Spill the candidate from the active set
        RegLocation loc = vreg_to_location_map.at(spill_candidate->getVreg());
        PhysicalReg reg = std::get<PhysicalReg>(loc);

        // Assign the freed register to the current interval
        vreg_to_location_map[current_interval.getVreg()] = reg;

        // Assign a stack slot to the spilled interval
        vreg_to_location_map[spill_candidate->getVreg()] = StackSlot{next_stack_slot++};

        // The current interval takes the place of the spilled one in the active list
        active_intervals.pop_back();
        active_intervals.push_back(&current_interval);
        std::sort(active_intervals.begin(), active_intervals.end(),
            [](const LiveInterval* a, const LiveInterval* b) {
                return a->getEnd() < b->getEnd();
            });

    } else {
        // Spill the current interval
        vreg_to_location_map[current_interval.getVreg()] = StackSlot{next_stack_slot++};
    }
}

} // namespace transforms
