#include "transforms/CFGBuilder.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"

namespace transforms {

void CFGBuilder::run(ir::Function& func) {
    for (auto& bb_ptr : func.getBasicBlocks()) {
        ir::BasicBlock* current_bb = bb_ptr.get();
        if (current_bb->getInstructions().empty()) {
            continue;
        }

        // The terminator is the last instruction.
        ir::Instruction* terminator = current_bb->getInstructions().back().get();

        if (terminator->getOpcode() == ir::Instruction::Jmp) {
            ir::BasicBlock* target = static_cast<ir::BasicBlock*>(terminator->getOperands()[0]->get());
            current_bb->addSuccessor(target);
            target->addPredecessor(current_bb);
        } else if (terminator->getOpcode() == ir::Instruction::Jnz) {
            ir::BasicBlock* targetTrue = dynamic_cast<ir::BasicBlock*>(terminator->getOperands()[1]->get());
            ir::BasicBlock* targetFalse = dynamic_cast<ir::BasicBlock*>(terminator->getOperands()[2]->get());

            if (targetTrue) {
                current_bb->addSuccessor(targetTrue);
                targetTrue->addPredecessor(current_bb);
            }

            if (targetFalse) {
                current_bb->addSuccessor(targetFalse);
                targetFalse->addPredecessor(current_bb);
            }
        } else if (terminator->getOpcode() == ir::Instruction::Br) {
            if (terminator->getOperands().size() == 1) {
                ir::BasicBlock* target = static_cast<ir::BasicBlock*>(terminator->getOperands()[0]->get());
                current_bb->addSuccessor(target);
                target->addPredecessor(current_bb);
            } else if (terminator->getOperands().size() >= 3) {
                ir::BasicBlock* targetTrue = static_cast<ir::BasicBlock*>(terminator->getOperands()[1]->get());
                ir::BasicBlock* targetFalse = static_cast<ir::BasicBlock*>(terminator->getOperands()[2]->get());

                current_bb->addSuccessor(targetTrue);
                targetTrue->addPredecessor(current_bb);

                current_bb->addSuccessor(targetFalse);
                targetFalse->addPredecessor(current_bb);
            }
        }
        // `ret` instructions have no successors.
    }
}

} // namespace transforms
