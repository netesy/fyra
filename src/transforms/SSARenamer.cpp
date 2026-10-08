#include "transforms/SSARenamer.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/PhiNode.h"
#include "ir/Constant.h"
#include "ir/IRContext.h"
#include "ir/Type.h"
#include "ir/Module.h"
#include "ir/Use.h"
#include "transforms/AllocaPromotion.h"
#include <iostream>
#include <map>
#include <vector>

namespace transforms {

void SSARenamer::run(ir::Function& func, DominatorTree& dt) {
    domTree = &dt;

    // 1. Find all variables (allocs) and initialize their stacks
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (isPromotableAlloca(instr.get())) {
                ir::Type* ty = static_cast<ir::PointerType*>(instr->getType())->getElementType();
                ir::IRContext& context = *func.getParent()->getContext();
                if (ty->isInteger()) {
                    varStacks[instr.get()].push(ir::ConstantInt::get(dynamic_cast<ir::IntegerType*>(ty), 0));
                } else if (ty->isFloatingPoint()) {
                    varStacks[instr.get()].push(ir::ConstantFP::get(ty, 0.0));
                } else {
                    varStacks[instr.get()].push(ir::ConstantInt::get(context.getIntegerType(64), 0));
                }
            }
        }
    }

    // 2. Start the recursive renaming process
    renameBlock(func.getBasicBlocks().front().get());
}

void SSARenamer::renameBlock(ir::BasicBlock* bb) {
    std::vector<ir::Instruction*> local_defs;
    std::vector<ir::Instruction*> dead_loads;

    // 1. Rename Phi nodes (they are new definitions)
    for (auto& instr_ptr : bb->getInstructions()) {
        if (auto* phi = dynamic_cast<ir::PhiNode*>(instr_ptr.get())) {
            ir::Instruction* var = phi->getVariable();
            varStacks[var].push(phi);
            local_defs.push_back(var);
        }
    }

    // Observe definitions in program order: a load after a store in the same
    // block must see that store, rather than the incoming block value.
    for (auto& instruction : bb->getInstructions()) {
        auto op = instruction->getOpcode();
        const bool load = op == ir::Instruction::Load || op == ir::Instruction::Loadl ||
                          op == ir::Instruction::Loads || op == ir::Instruction::Loadd;
        const bool store = op == ir::Instruction::Store || op == ir::Instruction::Storel ||
                           op == ir::Instruction::Stores || op == ir::Instruction::Stored;
        if (!load && !store) continue;
        auto* variable = dynamic_cast<ir::Instruction*>(instruction->getOperands()[store ? 1 : 0]->get());
        if (!variable || !varStacks.count(variable)) continue;
        if (store) {
            varStacks[variable].push(instruction->getOperands()[0]->get());
            local_defs.push_back(variable);
        } else if (!varStacks[variable].empty()) {
            instruction->replaceAllUsesWith(varStacks[variable].top());
            dead_loads.push_back(instruction.get());
        }
    }

    // 4. Fill in phi operands for successors in the CFG
    for (ir::BasicBlock* succ : bb->getSuccessors()) {
        for (auto& instr_ptr : succ->getInstructions()) {
            if (auto* phi = dynamic_cast<ir::PhiNode*>(instr_ptr.get())) {
                ir::Instruction* var = phi->getVariable();
                if (!var) continue; // Do not overwrite pre-existing SSA phi nodes
                ir::Value* incoming_val = nullptr;
                if (varStacks.count(var) && !varStacks[var].empty()) {
                    incoming_val = varStacks[var].top();
                }
                phi->setIncomingValueForBlock(bb, incoming_val);
            }
        }
    }

    // 5. Recurse on children in the dominator tree
    for (ir::BasicBlock* child : domTree->getChildren(bb)) {
        renameBlock(child);
    }

    // 6. Pop all definitions made in this block
    for (ir::Instruction* var : local_defs) {
        if (!varStacks[var].empty()) {
            varStacks[var].pop();
        }
    }

    // 7. Remove dead loads
    if (!dead_loads.empty()) {
        bb->removeInstructions(dead_loads);
    }
}

} // namespace transforms
