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

    std::vector<PhysicalReg> free_caller_regs;
    std::vector<PhysicalReg> free_callee_regs;

    // Caller-saved registers: indices 0..7 (r10, r11, rcx, rdx, rsi, rdi, r8, r9)
    for (int i = 7; i >= 0; --i) free_caller_regs.push_back({(unsigned int)i});
    // Callee-saved registers: indices 8..12 (rbx, r12, r13, r14, r15)
    for (int i = 12; i >= 8; --i) free_callee_regs.push_back({(unsigned int)i});

    for (const auto& current_interval : intervals) {
        if (current_interval.isLiveAcrossCall()) {
            stats.liveAcrossCalls++;
        }

        expireOldIntervals(current_interval.getStart(), free_caller_regs, free_callee_regs);

        bool assigned = false;
        PhysicalReg reg;

        if (current_interval.isLiveAcrossCall()) {
            if (!free_callee_regs.empty()) {
                reg = free_callee_regs.back();
                free_callee_regs.pop_back();
                assigned = true;
                stats.crossCallCalleeSaved++;
            } else {
                // Must spill to stack across call to prevent caller-saved clobbering
                assigned = false;
            }
        } else {
            if (!free_caller_regs.empty()) {
                reg = free_caller_regs.back();
                free_caller_regs.pop_back();
                assigned = true;
                stats.callerSavedUsed++;
            } else if (!free_callee_regs.empty()) {
                reg = free_callee_regs.back();
                free_callee_regs.pop_back();
                assigned = true;
            }
        }

        if (assigned) {
            used_regs.insert(reg.index);
            if (reg.index >= 8) stats.calleeSavedUsed++;
            current_interval.getVreg()->setPhysicalRegister(reg.index);
            vreg_to_location_map[current_interval.getVreg()] = reg;
            active_intervals.push_back(&current_interval);
            std::sort(active_intervals.begin(), active_intervals.end(),
                [](const LiveInterval* a, const LiveInterval* b) {
                    return a->getEnd() < b->getEnd();
                });
        } else {
            if (current_interval.isLiveAcrossCall()) stats.crossCallSpills++;
            spillAtInterval(current_interval, free_caller_regs, free_callee_regs);
        }
    }
    stats.numPhysicalRegsUsed = used_regs.size();
}

void LinearScanAllocator::expireOldIntervals(int current_start_point, std::vector<PhysicalReg>& free_caller, std::vector<PhysicalReg>& free_callee) {
    auto it = active_intervals.begin();
    while (it != active_intervals.end()) {
        const LiveInterval* interval = *it;
        if (interval->getEnd() >= current_start_point) {
            return;
        }

        RegLocation loc = vreg_to_location_map.at(interval->getVreg());
        if (std::holds_alternative<PhysicalReg>(loc)) {
            PhysicalReg reg = std::get<PhysicalReg>(loc);
            if (reg.index >= 8) {
                free_callee.push_back(reg);
            } else {
                free_caller.push_back(reg);
            }
        } else if (std::holds_alternative<StackSlot>(loc)) {
            StackSlot slot = std::get<StackSlot>(loc);
            free_stack_slots.push_back(slot);
        }

        it = active_intervals.erase(it);
    }
}

void LinearScanAllocator::spillAtInterval(const LiveInterval& current_interval, std::vector<PhysicalReg>& free_caller, std::vector<PhysicalReg>& free_callee) {
    stats.numSpills++;
    if (active_intervals.empty()) {
        vreg_to_location_map[current_interval.getVreg()] = StackSlot{next_stack_slot++};
        return;
    }

    const LiveInterval* spill_candidate = active_intervals.back();

    if (spill_candidate->getEnd() > current_interval.getEnd()) {
        RegLocation loc = vreg_to_location_map.at(spill_candidate->getVreg());
        if (std::holds_alternative<PhysicalReg>(loc)) {
            PhysicalReg reg = std::get<PhysicalReg>(loc);
            if (!current_interval.isLiveAcrossCall() || reg.index >= 8) {
                current_interval.getVreg()->setPhysicalRegister(reg.index);
                vreg_to_location_map[current_interval.getVreg()] = reg;

                spill_candidate->getVreg()->setPhysicalRegister(-1);
                vreg_to_location_map[spill_candidate->getVreg()] = StackSlot{next_stack_slot++};

                active_intervals.pop_back();
                active_intervals.push_back(&current_interval);
                std::sort(active_intervals.begin(), active_intervals.end(),
                    [](const LiveInterval* a, const LiveInterval* b) {
                        return a->getEnd() < b->getEnd();
                    });
                return;
            }
        }
    }

    if (!free_stack_slots.empty()) {
        StackSlot slot = free_stack_slots.back();
        free_stack_slots.pop_back();
        vreg_to_location_map[current_interval.getVreg()] = slot;
    } else {
        vreg_to_location_map[current_interval.getVreg()] = StackSlot{next_stack_slot++};
    }
}

} // namespace transforms
