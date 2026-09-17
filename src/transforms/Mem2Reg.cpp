#include "transforms/Mem2Reg.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include <set>
#include <vector>

namespace transforms {

// This pass is designed to run *after* the SSARenamer pass.
// SSARenamer replaces all `load` instructions with their SSA values, which
// makes all `alloc` and `store` instructions for local variables dead code.
// This pass simply cleans up these now-redundant instructions.
bool Mem2Reg::run(ir::Function& func) {
    std::set<ir::Instruction*> localAllocs;
    std::vector<ir::Instruction*> deadStores;

    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getOpcode() == ir::Instruction::Alloc ||
                instr->getOpcode() == ir::Instruction::Alloc4 ||
                instr->getOpcode() == ir::Instruction::Alloc16)
                localAllocs.insert(instr.get());
        }
    }

    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getOpcode() != ir::Instruction::Store || instr->getOperands().size() < 2)
                continue;
            auto* pointer = dynamic_cast<ir::Instruction*>(instr->getOperands()[1]->get());
            if (pointer && localAllocs.count(pointer)) deadStores.push_back(instr.get());
        }
    }

    bool changed = !deadStores.empty();
    for (auto& bb : func.getBasicBlocks()) {
        bb->removeInstructions(deadStores);
    }

    std::vector<ir::Instruction*> deadAllocs;
    for (auto* alloc : localAllocs)
        if (alloc->use_empty()) deadAllocs.push_back(alloc);
    changed |= !deadAllocs.empty();
    for (auto& bb : func.getBasicBlocks()) bb->removeInstructions(deadAllocs);
    return changed;
}

} // namespace transforms
