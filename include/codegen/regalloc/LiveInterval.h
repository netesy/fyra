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
    LiveInterval(ir::Instruction* vreg, int start, int end, bool liveAcrossCall = false, double spillWeight = 1.0)
        : vreg(vreg), start(start), end(end), liveAcrossCall(liveAcrossCall), spillWeight(spillWeight) {}

    ir::Instruction* getVreg() const { return vreg; }
    int getStart() const { return start; }
    int getEnd() const { return end; }
    bool isLiveAcrossCall() const { return liveAcrossCall; }
    void setLiveAcrossCall(bool val) { liveAcrossCall = val; }

    double getSpillWeight() const { return spillWeight; }
    void setSpillWeight(double w) { spillWeight = w; }

    // For sorting intervals by their start point
    bool operator<(const LiveInterval& other) const {
        return start < other.start;
    }

private:
    ir::Instruction* vreg; // The virtual register (the instruction that defines it)
    int start;
    int end;
    bool liveAcrossCall;
    double spillWeight = 1.0;
};

} // namespace transforms
