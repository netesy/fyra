#include "codegen/regalloc/LinearScanAllocator.h"
#include "codegen/regalloc/LiveIntervalAnalysis.h"
#include "ir/Function.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <set>

namespace transforms {

// Define the number of available physical registers for our target
// For x86_64 System V, we have 14 general purpose registers, 
// but we'll reserve some for ABI and scratch purposes.
const unsigned int NUM_PHYSICAL_REGISTERS = 13;

void LinearScanAllocator::run(ir::Function& func) {
    linearScan(func);
}

void LinearScanAllocator::linearScan(ir::Function& func) {
    LiveIntervalAnalysis interval_analysis;
    interval_analysis.run(func);
    const auto& intervals = interval_analysis.getIntervals();

    stats.numVregs = intervals.size();
    std::set<unsigned int> used_regs;

    // Allocate caller-saved registers (0..7) first for non-call intervals,
    // and callee-saved registers (8..12) for higher register pressure / live intervals.
    for (int i = (int)NUM_PHYSICAL_REGISTERS - 1; i >= 0; --i) {
        free_registers.push_back({(unsigned int)i});
    }

    for (const auto& current_interval : intervals) {
        expireOldIntervals(current_interval.getStart());

        if (active_intervals.size() == NUM_PHYSICAL_REGISTERS) {
            spillAtInterval(current_interval);
        } else {
            PhysicalReg reg = free_registers.back();
            free_registers.pop_back();
            used_regs.insert(reg.index);
            vreg_to_location_map[current_interval.getVreg()] = reg;
            active_intervals.push_back(&current_interval);
            std::sort(active_intervals.begin(), active_intervals.end(),
                [](const LiveInterval* a, const LiveInterval* b) {
                    return a->getEnd() < b->getEnd();
                });
        }
    }
    stats.numPhysicalRegsUsed = used_regs.size();
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
    stats.numSpills++;
    const LiveInterval* spill_candidate = active_intervals.back();

    if (spill_candidate->getEnd() > current_interval.getEnd()) {
        RegLocation loc = vreg_to_location_map.at(spill_candidate->getVreg());
        PhysicalReg reg = std::get<PhysicalReg>(loc);

        vreg_to_location_map[current_interval.getVreg()] = reg;
        vreg_to_location_map[spill_candidate->getVreg()] = StackSlot{next_stack_slot++};

        active_intervals.pop_back();
        active_intervals.push_back(&current_interval);
        std::sort(active_intervals.begin(), active_intervals.end(),
            [](const LiveInterval* a, const LiveInterval* b) {
                return a->getEnd() < b->getEnd();
            });
    } else {
        vreg_to_location_map[current_interval.getVreg()] = StackSlot{next_stack_slot++};
    }
}

} // namespace transforms
