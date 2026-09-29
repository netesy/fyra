#include "transforms/EGraphPass.h"
#include "transforms/EGraph.h"
#include "ir/BasicBlock.h"
#include "ir/IRBuilder.h"
#include <iostream>
#include <algorithm>

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

        // Collect candidates first to avoid iterator invalidation during RAUW.
        struct Candidate {
            ir::Instruction* inst;
            EClassId root;
        };
        std::vector<Candidate> candidates;

        EGraph egraph;
        for (auto& instPtr : bb->getInstructions()) {
            ir::Instruction* inst = instPtr.get();
            if (!inst) continue;

            ir::Instruction::Opcode op = inst->getOpcode();
            if (op == ir::Instruction::Udiv || op == ir::Instruction::Div ||
                op == ir::Instruction::Urem || op == ir::Instruction::Rem ||
                op == ir::Instruction::Mul || op == ir::Instruction::VMul ||
                op == ir::Instruction::VAdd || op == ir::Instruction::VSub) {
                EClassId root = egraph.addValue(inst);
                candidates.push_back({inst, root});
            }
        }

        if (candidates.empty()) continue;

        if (egraph.saturate(5)) {
            std::map<EClassId, ir::Value*> extractedMap;
            for (auto& [origInst, root] : candidates) {
                // Insert replacement immediately BEFORE the original instruction
                // to preserve SSA use-def dominance ordering.
                auto it = std::find_if(bb->getInstructions().begin(), bb->getInstructions().end(),
                                       [&](const auto& p) { return p.get() == origInst; });
                if (it != bb->getInstructions().end()) {
                    builder.setInsertPoint(bb, it);
                } else {
                    builder.setInsertPoint(bb);
                }
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
