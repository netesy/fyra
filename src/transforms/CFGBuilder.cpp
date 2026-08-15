#include "transforms/CFGBuilder.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"

namespace transforms {

void CFGBuilder::run(ir::Function& func) {
    auto& blocks = func.getBasicBlocks();
    for (auto& bb : blocks) {
        bb->clearPredecessors();
        bb->clearSuccessors();
    }
    for (auto it = blocks.begin(); it != blocks.end(); ++it) {
        ir::BasicBlock* current_bb = it->get();
        auto next_it = std::next(it);
        ir::BasicBlock* next_bb = (next_it != blocks.end()) ? next_it->get() : nullptr;

        if (current_bb->getInstructions().empty()) {
            if (next_bb) {
                current_bb->addSuccessor(next_bb);
                next_bb->addPredecessor(current_bb);
            }
            continue;
        }

        // Find the FIRST terminator instruction in the basic block.
        ir::Instruction* terminator = nullptr;
        for (auto& inst : current_bb->getInstructions()) {
            if (!inst) continue;
            ir::Instruction::Opcode op = inst->getOpcode();
            if (op == ir::Instruction::Jmp ||
                op == ir::Instruction::Br ||
                op == ir::Instruction::Jnz ||
                op == ir::Instruction::Ret) {
                terminator = inst.get();
                break;
            }
        }

        bool has_explicit_terminator = (terminator != nullptr);

        if (has_explicit_terminator) {
            ir::Instruction::Opcode op = terminator->getOpcode();
            if (op == ir::Instruction::Jmp) {
                if (!terminator->getOperands().empty() && terminator->getOperands()[0]) {
                    if (auto* target = dynamic_cast<ir::BasicBlock*>(terminator->getOperands()[0]->get())) {
                        current_bb->addSuccessor(target);
                        target->addPredecessor(current_bb);
                    }
                }
            } else if (op == ir::Instruction::Jnz) {
                if (terminator->getOperands().size() > 1 && terminator->getOperands()[1]) {
                    if (auto* targetTrue = dynamic_cast<ir::BasicBlock*>(terminator->getOperands()[1]->get())) {
                        current_bb->addSuccessor(targetTrue);
                        targetTrue->addPredecessor(current_bb);
                    }
                }
                if (terminator->getOperands().size() > 2 && terminator->getOperands()[2]) {
                    if (auto* targetFalse = dynamic_cast<ir::BasicBlock*>(terminator->getOperands()[2]->get())) {
                        current_bb->addSuccessor(targetFalse);
                        targetFalse->addPredecessor(current_bb);
                    }
                }
            } else if (op == ir::Instruction::Br) {
                if (terminator->getOperands().size() == 1 && terminator->getOperands()[0]) {
                    if (auto* target = dynamic_cast<ir::BasicBlock*>(terminator->getOperands()[0]->get())) {
                        current_bb->addSuccessor(target);
                        target->addPredecessor(current_bb);
                    }
                } else if (terminator->getOperands().size() >= 3) {
                    if (terminator->getOperands()[1]) {
                        if (auto* targetTrue = dynamic_cast<ir::BasicBlock*>(terminator->getOperands()[1]->get())) {
                            current_bb->addSuccessor(targetTrue);
                            targetTrue->addPredecessor(current_bb);
                        }
                    }
                    if (terminator->getOperands()[2]) {
                        if (auto* targetFalse = dynamic_cast<ir::BasicBlock*>(terminator->getOperands()[2]->get())) {
                            current_bb->addSuccessor(targetFalse);
                            targetFalse->addPredecessor(current_bb);
                        }
                    }
                }
            }
        }

        if (!has_explicit_terminator && next_bb) {
            current_bb->addSuccessor(next_bb);
            next_bb->addPredecessor(current_bb);
        }
    }
}

} // namespace transforms
