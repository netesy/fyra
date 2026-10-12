#include "transforms/Mem2Reg.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include "transforms/AllocaPromotion.h"
#include <set>
#include <vector>
#include <map>

namespace transforms {

// This pass is designed to run *after* the SSARenamer pass.
// SSARenamer replaces all `load` instructions with their SSA values, which
// makes all `alloc` and `store` instructions for local variables dead code.
// This pass simply cleans up these now-redundant instructions.
bool Mem2Reg::run(ir::Function& func) {
    std::set<ir::Instruction*> localAllocs;
    std::map<ir::BasicBlock*, std::vector<ir::Instruction*>> deadStores;

    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (isPromotableAlloca(instr.get()))
                localAllocs.insert(instr.get());
        }
    }

    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if ((instr->getOpcode() != ir::Instruction::Store && instr->getOpcode() != ir::Instruction::Storel &&
                 instr->getOpcode() != ir::Instruction::Stored && instr->getOpcode() != ir::Instruction::Stores) || instr->getOperands().size() < 2)
                continue;
            auto* pointer = dynamic_cast<ir::Instruction*>(instr->getOperands()[1]->get());
            if (pointer && localAllocs.count(pointer)) deadStores[bb.get()].push_back(instr.get());
        }
    }

    bool changed = !deadStores.empty();
    for (auto& bb : func.getBasicBlocks()) {
        auto found = deadStores.find(bb.get());
        if (found != deadStores.end()) bb->removeInstructions(found->second);
    }

    std::map<ir::BasicBlock*, std::vector<ir::Instruction*>> deadAllocs;
    for (auto* alloc : localAllocs)
        if (alloc->use_empty()) deadAllocs[alloc->getParent()].push_back(alloc);
    changed |= !deadAllocs.empty();
    for (auto& bb : func.getBasicBlocks()) {
        auto found = deadAllocs.find(bb.get());
        if (found != deadAllocs.end()) bb->removeInstructions(found->second);
    }
    return changed;
}

} // namespace transforms
