#pragma once

#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include <vector>

namespace transforms {

class InstructionScheduler {
public:
    /// Runs latency-aware list scheduling on all basic blocks in the function.
    static bool run(ir::Function& func);

    /// Runs list scheduling on a single basic block.
    static bool scheduleBasicBlock(ir::BasicBlock& bb);

    /// Returns static cycle latency for an instruction opcode.
    static unsigned getInstructionLatency(ir::Instruction::Opcode op);
};

} // namespace transforms
