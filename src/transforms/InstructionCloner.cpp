#include "transforms/InstructionCloner.h"

namespace transforms {

std::unique_ptr<ir::Instruction> InstructionCloner::cloneInstruction(
    const ir::Instruction* srcInstr,
    const ValueMapper& vmap,
    ir::BasicBlock* targetBB
) {
    if (!srcInstr) return nullptr;

    std::vector<ir::Value*> remappedOperands;
    for (const auto& opUse : srcInstr->getOperands()) {
        if (!opUse) {
            remappedOperands.push_back(nullptr);
            continue;
        }
        ir::Value* origVal = opUse->get();
        auto it = vmap.find(origVal);
        if (it != vmap.end()) {
            remappedOperands.push_back(it->second);
        } else {
            remappedOperands.push_back(origVal);
        }
    }

    std::unique_ptr<ir::Instruction> cloned;

    if (auto* srcPhi = dynamic_cast<const ir::PhiNode*>(srcInstr)) {
        auto newPhi = std::make_unique<ir::PhiNode>(
            srcPhi->getType(),
            srcPhi->getOperands().size(),
            nullptr,
            targetBB
        );
        cloned = std::move(newPhi);
    } else if (auto* srcSys = dynamic_cast<const ir::SyscallInstruction*>(srcInstr)) {
        cloned = std::make_unique<ir::SyscallInstruction>(
            srcSys->getType(),
            remappedOperands,
            srcSys->getSyscallId(),
            targetBB
        );
    } else if (auto* srcExt = dynamic_cast<const ir::ExternCallInstruction*>(srcInstr)) {
        cloned = std::make_unique<ir::ExternCallInstruction>(
            srcExt->getType(),
            remappedOperands,
            srcExt->getCapability(),
            targetBB
        );
    } else {
        cloned = std::make_unique<ir::Instruction>(
            srcInstr->getType(),
            srcInstr->getOpcode(),
            remappedOperands,
            targetBB
        );
    }

    if (!srcInstr->getName().empty()) {
        cloned->setName(srcInstr->getName() + ".unrolled");
    }
    return cloned;
}

} // namespace transforms
