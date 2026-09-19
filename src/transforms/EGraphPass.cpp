#include "transforms/EGraphPass.h"
#include "transforms/EGraph.h"
#include "ir/BasicBlock.h"
#include "ir/IRBuilder.h"
#include <iostream>

namespace transforms {

bool EGraphPass::performTransformation(ir::Function& func) {
    if (func.getBasicBlocks().empty()) return false;

    bool changed = false;
    auto ctx = func.getParent() ? func.getParent()->getContextShared() : std::make_shared<ir::IRContext>();
    ir::IRBuilder builder(ctx);
    if (func.getParent()) builder.setModule(func.getParent());

    for (auto& bbPtr : func.getBasicBlocks()) {
        ir::BasicBlock* bb = bbPtr.get();
        if (!bb) continue;

        EGraph egraph;
        std::vector<std::pair<ir::Instruction*, EClassId>> instRoots;

        for (auto& instPtr : bb->getInstructions()) {
            ir::Instruction* inst = instPtr.get();
            if (!inst) continue;

            ir::Instruction::Opcode op = inst->getOpcode();
            if (op == ir::Instruction::Udiv || op == ir::Instruction::Div ||
                op == ir::Instruction::Urem || op == ir::Instruction::Rem ||
                op == ir::Instruction::Mul || op == ir::Instruction::VMul ||
                op == ir::Instruction::VAdd || op == ir::Instruction::VSub) {
                EClassId root = egraph.addValue(inst);
                instRoots.push_back({inst, root});
            }
        }

        if (instRoots.empty()) continue;

        if (egraph.saturate(5)) {
            std::map<EClassId, ir::Value*> extractedMap;
            for (auto& [origInst, root] : instRoots) {
                ir::Value* bestVal = egraph.extractBest(root, builder, bb, extractedMap);
                if (bestVal && bestVal != origInst) {
                    origInst->replaceAllUsesWith(bestVal);
                    changed = true;
                }
            }
        }
    }

    return changed;
}

bool EGraphPass::validatePreconditions(ir::Function& f) {
    return !f.getBasicBlocks().empty();
}

} // namespace transforms
