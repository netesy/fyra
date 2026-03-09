#include "transforms/Mem2Reg.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include <vector>

namespace transforms {

// This pass is designed to run *after* the SSARenamer pass.
// SSARenamer replaces all `load` instructions with their SSA values, which
// makes all `alloc` and `store` instructions for local variables dead code.
// This pass simply cleans up these now-redundant instructions.
bool Mem2Reg::run(ir::Function& func) {
    std::vector<ir::Instruction*> to_remove;

    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getOpcode() == ir::Instruction::Alloc ||
                instr->getOpcode() == ir::Instruction::Alloc4 ||
                instr->getOpcode() == ir::Instruction::Alloc16 ||
                instr->getOpcode() == ir::Instruction::Store) {
                to_remove.push_back(instr.get());
            }
        }
    }

    if (to_remove.empty()) {
        return false;
    }

    for (auto& bb : func.getBasicBlocks()) {
        bb->removeInstructions(to_remove);
    }

    return true;
}

} // namespace transforms
