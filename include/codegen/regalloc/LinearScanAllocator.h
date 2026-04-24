#pragma once

#include "ir/Function.h"
#include "ir/Instruction.h"
#include <map>
#include <vector>

#include <variant>

namespace transforms {

// Represents a physical machine register.
struct PhysicalReg {
    unsigned int index;
    bool operator==(const PhysicalReg& other) const { return index == other.index; }
};

// Represents a stack slot.
struct StackSlot {
    unsigned int index;
    bool operator==(const StackSlot& other) const { return index == other.index; }
};

// The location for a virtual register can be either a physical register or a stack slot.
using RegLocation = std::variant<PhysicalReg, StackSlot>;

/**
 * @brief This pass implements the Linear Scan Register Allocation algorithm.
 */
class LinearScanAllocator {
public:
    void run(ir::Function& func);

    const std::map<ir::Instruction*, RegLocation>& getRegisterMap() const {
        return vreg_to_location_map;
    }

private:
    void linearScan(ir::Function& func);
    void expireOldIntervals(int current_start_point);
    void spillAtInterval(const class LiveInterval& interval);

    unsigned int next_stack_slot = 0;
    std::vector<PhysicalReg> free_registers;
    std::vector<const class LiveInterval*> active_intervals;
    std::map<ir::Instruction*, RegLocation> vreg_to_location_map;
};

} // namespace transforms
