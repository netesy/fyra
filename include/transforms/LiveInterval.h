#pragma once

#include "ir/Instruction.h"

namespace transforms {

/**
 * @brief Represents the live interval of a virtual register.
 * An interval is defined by a start and end point, which correspond to
 * instruction numbers.
 */
class LiveInterval {
public:
    LiveInterval(ir::Instruction* vreg, int start, int end)
        : vreg(vreg), start(start), end(end) {}

    ir::Instruction* getVreg() const { return vreg; }
    int getStart() const { return start; }
    int getEnd() const { return end; }

    // For sorting intervals by their start point
    bool operator<(const LiveInterval& other) const {
        return start < other.start;
    }

private:
    ir::Instruction* vreg; // The virtual register (the instruction that defines it)
    int start;
    int end;
};

} // namespace transforms
