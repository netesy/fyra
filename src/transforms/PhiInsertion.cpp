#include "transforms/PhiInsertion.h"
#include "ir/Instruction.h"
#include "ir/IRBuilder.h"
#include "ir/Use.h"
#include <vector>
#include <set>

namespace transforms {

void PhiInsertion::run(ir::Function& func, const DominanceFrontier& df) {
    // 1. Find all alloc instructions (our "variables")
    std::vector<ir::Instruction*> allocs;
    for (auto& bb_ptr : func.getBasicBlocks()) {
        for (auto& instr_ptr : bb_ptr->getInstructions()) {
            if (instr_ptr->getOpcode() == ir::Instruction::Alloc ||
                instr_ptr->getOpcode() == ir::Instruction::Alloc4 ||
                instr_ptr->getOpcode() == ir::Instruction::Alloc16) {
                allocs.push_back(instr_ptr.get());
            }
        }
    }

    // 2. For each variable, find its definition sites and insert phis
    for (ir::Instruction* alloc : allocs) {
        std::vector<ir::BasicBlock*> def_sites;
        // Find all stores to this alloc
        for (auto& bb_ptr : func.getBasicBlocks()) {
            for (auto& instr_ptr : bb_ptr->getInstructions()) {
                if (instr_ptr->getOpcode() == ir::Instruction::Store ||
                    instr_ptr->getOpcode() == ir::Instruction::Storeb ||
                    instr_ptr->getOpcode() == ir::Instruction::Storeh ||
                    instr_ptr->getOpcode() == ir::Instruction::Storel ||
                    instr_ptr->getOpcode() == ir::Instruction::Stores ||
                    instr_ptr->getOpcode() == ir::Instruction::Stored) {
                    // This is a simplification. A real compiler would do alias analysis.
                    // We just check if the pointer operand is the alloc instruction.
                    if (instr_ptr->getOperands()[1]->get() == alloc) {
                        def_sites.push_back(bb_ptr.get());
                    }
                }
            }
        }

        // 3. Compute dominance frontiers for these sites
        std::set<ir::BasicBlock*> phi_blocks;
        for (ir::BasicBlock* def_site : def_sites) {
            const auto& frontier = df.getFrontier(def_site);
            phi_blocks.insert(frontier.begin(), frontier.end());
        }

        // 4. Insert phi nodes
        ir::IRBuilder builder(func.getParent()->getContextShared());
        for (ir::BasicBlock* bb : phi_blocks) {
            builder.setInsertPoint(bb);
            unsigned num_preds = bb->getPredecessors().size();
            builder.createPhi(alloc->getType(), 2 * num_preds, alloc);
        }
    }
}

} // namespace transforms
