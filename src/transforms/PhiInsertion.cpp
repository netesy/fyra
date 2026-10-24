#include "transforms/PhiInsertion.h"
#include "ir/Instruction.h"
#include "ir/IRBuilder.h"
#include "ir/Use.h"
#include "transforms/AllocaPromotion.h"
#include <vector>
#include <set>

namespace transforms {

void PhiInsertion::run(ir::Function& func, const DominanceFrontier& df) {
    // 1. Find all alloc instructions (our "variables")
    std::vector<ir::Instruction*> allocs;
    for (auto& bb_ptr : func.getBasicBlocks()) {
        for (auto& instr_ptr : bb_ptr->getInstructions()) {
            if (isPromotableAlloca(instr_ptr.get())) {
                allocs.push_back(instr_ptr.get());
            }
        }
    }

    // 2. For each variable, find its definition sites and insert phis
    ir::IRBuilder builder(func.getParent()->getContextShared());
    for (ir::Instruction* alloc : allocs) {
        std::set<ir::BasicBlock*> def_sites;
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
                        def_sites.insert(bb_ptr.get());
                    }
                }
            }
        }

        // 3. Iterative dominance-frontier placement (Cytron et al.).  A block
        //    that receives a phi becomes a new definition site for the same
        //    variable, so it must itself be reprocessed and its dominance
        //    frontier scanned.  Without this worklist the loop-header phi of a
        //    variable first assigned inside a loop body is never inserted: the
        //    phi only lands at the inner join, so the loop-carried value cannot
        //    reach the header and downstream renaming forwards the entry
        //    initializer to every in-loop read (a loop that accumulates into a
        //    local never observes its own updates).
        std::vector<ir::BasicBlock*> worklist(def_sites.begin(), def_sites.end());
        std::set<ir::BasicBlock*> phi_placed;
        while (!worklist.empty()) {
            ir::BasicBlock* def_site = worklist.back();
            worklist.pop_back();
            const auto& frontier = df.getFrontier(def_site);
            for (ir::BasicBlock* bb : frontier) {
                if (phi_placed.count(bb)) continue; // Already has a phi for this variable.
                phi_placed.insert(bb);
                builder.setInsertPoint(bb);
                unsigned num_preds = bb->getPredecessors().size();
                builder.createPhi(static_cast<ir::PointerType*>(alloc->getType())->getElementType(),
                                  2 * num_preds, alloc);
                worklist.push_back(bb); // A phi is itself a definition: reprocess.
            }
        }
    }
}

} // namespace transforms
