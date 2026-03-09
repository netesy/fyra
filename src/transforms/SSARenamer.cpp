#include "transforms/SSARenamer.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/PhiNode.h"
#include "ir/Constant.h"
#include "ir/Use.h"
#include <iostream>
#include <map>
#include <vector>

namespace transforms {

void SSARenamer::run(ir::Function& func, DominatorTree& dt) {
    domTree = &dt;

    // 1. Find all variables (allocs) and initialize their stacks
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (instr->getOpcode() == ir::Instruction::Alloc ||
                instr->getOpcode() == ir::Instruction::Alloc4 ||
                instr->getOpcode() == ir::Instruction::Alloc16) {
                varStacks[instr.get()].push(nullptr); // Push a placeholder for undefined
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

    // 2. Replace loads with the current live value
    for (auto& instr_ptr : bb->getInstructions()) {
        if (instr_ptr->getOpcode() == ir::Instruction::Load ||
            instr_ptr->getOpcode() == ir::Instruction::Loadub ||
            instr_ptr->getOpcode() == ir::Instruction::Loadsb ||
            instr_ptr->getOpcode() == ir::Instruction::Loaduh ||
            instr_ptr->getOpcode() == ir::Instruction::Loadsh ||
            instr_ptr->getOpcode() == ir::Instruction::Loaduw ||
            instr_ptr->getOpcode() == ir::Instruction::Loadl ||
            instr_ptr->getOpcode() == ir::Instruction::Loads ||
            instr_ptr->getOpcode() == ir::Instruction::Loadd) {
            ir::Instruction* var = dynamic_cast<ir::Instruction*>(instr_ptr->getOperands()[0]->get());
            if (var && varStacks.count(var) && !varStacks[var].empty()) {
                ir::Value* live_val = varStacks[var].top();
                if (live_val) {
                    instr_ptr->replaceAllUsesWith(live_val);
                    dead_loads.push_back(instr_ptr.get());
                }
            }
        }
    }

    // 3. Rename definitions (stores)
    for (auto& instr_ptr : bb->getInstructions()) {
        if (instr_ptr->getOpcode() == ir::Instruction::Store ||
            instr_ptr->getOpcode() == ir::Instruction::Storeb ||
            instr_ptr->getOpcode() == ir::Instruction::Storeh ||
            instr_ptr->getOpcode() == ir::Instruction::Storel ||
            instr_ptr->getOpcode() == ir::Instruction::Stores ||
            instr_ptr->getOpcode() == ir::Instruction::Stored) {
            ir::Value* val = instr_ptr->getOperands()[0]->get();
            ir::Instruction* var = dynamic_cast<ir::Instruction*>(instr_ptr->getOperands()[1]->get());
            if (var && varStacks.count(var)) {
                varStacks[var].push(val);
                local_defs.push_back(var);
            }
        }
    }

    // 4. Fill in phi operands for successors in the CFG
    for (ir::BasicBlock* succ : bb->getSuccessors()) {
        for (auto& instr_ptr : succ->getInstructions()) {
            if (auto* phi = dynamic_cast<ir::PhiNode*>(instr_ptr.get())) {
                ir::Instruction* var = phi->getVariable();
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
