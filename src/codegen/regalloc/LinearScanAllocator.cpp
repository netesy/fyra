#include "codegen/regalloc/LinearScanAllocator.h"
#include "codegen/regalloc/LiveIntervalAnalysis.h"
#include "ir/Function.h"
#include "ir/Use.h"
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
    std::vector<PhysicalReg> free_xmm_regs;

    // Caller-saved registers: pure scratch (0:r10, 1:r11, 6:r8, 7:r9) allocated first before argument registers (5:rdi, 4:rsi)
    // rcx (2) and rdx (3) are reserved for division/remainder and shift instructions (%cl)
    static const std::vector<unsigned int> caller_indices = {0, 1, 6, 7, 5, 4};
    for (auto it = caller_indices.rbegin(); it != caller_indices.rend(); ++it) {
        free_caller_regs.push_back({*it});
    }
    // Callee-saved registers: indices 8..12 (rbx, r12, r13, r14, r15)
    for (int i = 12; i >= 8; --i) free_callee_regs.push_back({(unsigned int)i});
    // XMM registers: indices 100..115 (xmm0..xmm15)
    // Exclude reserved scratch XMM register if target specifies one
    unsigned int reserved_xmm_idx = 0;
    if (targetInfo) {
        reserved_xmm_idx = targetInfo->getReservedScratchVectorRegIndex();
    } else {
        reserved_xmm_idx = 115; // Default SystemV xmm15
    }
    for (int i = 115; i >= 100; --i) {
        if (reserved_xmm_idx != 0 && (unsigned int)i == reserved_xmm_idx) continue;
        free_xmm_regs.push_back({(unsigned int)i});
    }

    for (const auto& current_interval : intervals) {
        if (current_interval.isLiveAcrossCall()) {
            stats.liveAcrossCalls++;
        }

        expireOldIntervals(current_interval.getStart(), free_caller_regs, free_callee_regs, free_xmm_regs);

        bool assigned = false;
        PhysicalReg reg;

        ir::Instruction* instr = current_interval.getVreg();
        bool isVector = false;
        if (instr) {
            if (instr->getType() && (instr->getType()->isVectorTy() || instr->getType()->isSIMDType() || dynamic_cast<const ir::VectorType*>(instr->getType()) != nullptr)) {
                isVector = true;
            }
            switch (instr->getOpcode()) {
                case ir::Instruction::VLoad:
                case ir::Instruction::VStore:
                case ir::Instruction::VAdd:
                case ir::Instruction::VSub:
                case ir::Instruction::VMul:
                case ir::Instruction::VDiv:
                case ir::Instruction::VFAdd:
                case ir::Instruction::VFSub:
                case ir::Instruction::VFMul:
                case ir::Instruction::VFDiv:
                case ir::Instruction::VAnd:
                case ir::Instruction::VOr:
                case ir::Instruction::VXor:
                case ir::Instruction::VShl:
                case ir::Instruction::VShr:
                case ir::Instruction::VNot:
                case ir::Instruction::VBroadcast:
                case ir::Instruction::VInsert:
                case ir::Instruction::VShuffle:
                case ir::Instruction::VCmp:
                case ir::Instruction::VSelect:
                case ir::Instruction::VHAdd:
                case ir::Instruction::VHSub:
                case ir::Instruction::VHMul:
                case ir::Instruction::VHAnd:
                case ir::Instruction::VHOr:
                case ir::Instruction::VHXor:
                case ir::Instruction::VMin:
                case ir::Instruction::VMax:
                case ir::Instruction::VFMin:
                case ir::Instruction::VFMax:
                case ir::Instruction::FMA:
                case ir::Instruction::FMS:
                case ir::Instruction::FNMA:
                case ir::Instruction::FNMS:
                    isVector = true;
                    break;
                default: break;
            }
        }

        // Prefer operand 0's physical register if available to enable two-address in-place reuse
        int preferredRegIdx = -1;
        if (instr && !instr->getOperands().empty() && instr->getOperands()[0]) {
            if (auto* op0Inst = dynamic_cast<ir::Instruction*>(instr->getOperands()[0]->get())) {
                if (op0Inst->hasPhysicalRegister()) {
                    preferredRegIdx = (int)op0Inst->getPhysicalRegister();
                }
            }
        }

        if (isVector) {
            if (!free_xmm_regs.empty()) {
                auto prefIt = std::find_if(free_xmm_regs.begin(), free_xmm_regs.end(),
                    [preferredRegIdx](const PhysicalReg& pr) { return (int)pr.index == preferredRegIdx; });
                if (prefIt != free_xmm_regs.end()) {
                    reg = *prefIt;
                    free_xmm_regs.erase(prefIt);
                } else {
                    reg = free_xmm_regs.back();
                    free_xmm_regs.pop_back();
                }
                assigned = true;
            } else {
                throw std::runtime_error("XMM allocation pool exhausted and vector spilling is deferred");
            }
        } else if (current_interval.isLiveAcrossCall()) {
            if (!free_callee_regs.empty()) {
                auto prefIt = std::find_if(free_callee_regs.begin(), free_callee_regs.end(),
                    [preferredRegIdx](const PhysicalReg& pr) { return (int)pr.index == preferredRegIdx; });
                if (prefIt != free_callee_regs.end()) {
                    reg = *prefIt;
                    free_callee_regs.erase(prefIt);
                } else {
                    reg = free_callee_regs.back();
                    free_callee_regs.pop_back();
                }
                assigned = true;
                stats.crossCallCalleeSaved++;
            } else {
                // Must spill to stack across call to prevent caller-saved clobbering
                assigned = false;
            }
        } else {
            if (!free_caller_regs.empty()) {
                auto prefIt = std::find_if(free_caller_regs.begin(), free_caller_regs.end(),
                    [preferredRegIdx](const PhysicalReg& pr) { return (int)pr.index == preferredRegIdx; });
                if (prefIt != free_caller_regs.end()) {
                    reg = *prefIt;
                    free_caller_regs.erase(prefIt);
                } else {
                    reg = free_caller_regs.back();
                    free_caller_regs.pop_back();
                }
                assigned = true;
                stats.callerSavedUsed++;
            } else if (!free_callee_regs.empty()) {
                auto prefIt = std::find_if(free_callee_regs.begin(), free_callee_regs.end(),
                    [preferredRegIdx](const PhysicalReg& pr) { return (int)pr.index == preferredRegIdx; });
                if (prefIt != free_callee_regs.end()) {
                    reg = *prefIt;
                    free_callee_regs.erase(prefIt);
                } else {
                    reg = free_callee_regs.back();
                    free_callee_regs.pop_back();
                }
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

void LinearScanAllocator::expireOldIntervals(int current_start_point, std::vector<PhysicalReg>& free_caller, std::vector<PhysicalReg>& free_callee, std::vector<PhysicalReg>& free_xmm) {
    auto it = active_intervals.begin();
    while (it != active_intervals.end()) {
        const LiveInterval* interval = *it;
        if (interval->getEnd() > current_start_point) {
            return;
        }

        RegLocation loc = vreg_to_location_map.at(interval->getVreg());
        if (std::holds_alternative<PhysicalReg>(loc)) {
            PhysicalReg reg = std::get<PhysicalReg>(loc);
            if (reg.index >= 100 && reg.index <= 115) {
                free_xmm.push_back(reg);
            } else if (reg.index >= 8) {
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
