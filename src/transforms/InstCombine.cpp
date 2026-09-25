#include "transforms/InstCombine.h"
#include "ir/BasicBlock.h"
#include "ir/Constant.h"
#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include <vector>

namespace transforms {

bool InstCombinePass::performTransformation(ir::Function& func) {
    bool changed = false;
    for (auto& bb : func.getBasicBlocks()) {
        std::vector<ir::Instruction*> insts;
        for (auto& inst : bb->getInstructions()) {
            insts.push_back(inst.get());
        }

        auto& instList = bb->getInstructions();
        for (auto it = instList.begin(); it != instList.end(); ) {
            ir::Instruction* inst = it->get();
            if (!inst || inst->getOperands().size() < 2) {
                ++it;
                continue;
            }

            auto opc = inst->getOpcode();
            auto* lhs = inst->getOperands()[0]->get();
            auto* rhs = inst->getOperands()[1]->get();

            auto* cLhs = dynamic_cast<ir::ConstantInt*>(lhs);
            auto* cRhs = dynamic_cast<ir::ConstantInt*>(rhs);

            ir::Value* replacement = nullptr;

            // x + 0 -> x, x - 0 -> x
            if ((opc == ir::Instruction::Add || opc == ir::Instruction::Sub) && cRhs && cRhs->getValue() == 0) {
                replacement = lhs;
            }
            // 0 + x -> x
            else if (opc == ir::Instruction::Add && cLhs && cLhs->getValue() == 0) {
                replacement = rhs;
            }
            // x * 1 -> x, x / 1 -> x
            else if ((opc == ir::Instruction::Mul || opc == ir::Instruction::Div || opc == ir::Instruction::Udiv) && cRhs && cRhs->getValue() == 1) {
                replacement = lhs;
            }
            // 1 * x -> x
            else if (opc == ir::Instruction::Mul && cLhs && cLhs->getValue() == 1) {
                replacement = rhs;
            }
            // x ^ x -> 0, x - x -> 0
            else if ((opc == ir::Instruction::Xor || opc == ir::Instruction::Sub) && lhs == rhs) {
                if (auto ctx = func.getParent() ? func.getParent()->getContextShared() : nullptr) {
                    if (auto* iTy = dynamic_cast<ir::IntegerType*>(inst->getType())) {
                        replacement = ctx->getConstantInt(iTy, 0);
                    }
                }
            }
            // x & x -> x, x | x -> x
            else if ((opc == ir::Instruction::And || opc == ir::Instruction::Or) && lhs == rhs) {
                replacement = lhs;
            }

            if (replacement) {
                inst->replaceAllUsesWith(replacement);
                it = instList.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    return changed;
}

} // namespace transforms
