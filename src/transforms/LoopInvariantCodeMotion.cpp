#include "transforms/LoopInvariantCodeMotion.h"
#include "transforms/CFGBuilder.h"
#include "transforms/DominatorTree.h"
#include "ir/Function.h"
#include "ir/Instruction.h"
#include "ir/IRBuilder.h"
#include "ir/Constant.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include <map>
#include <set>
#include <vector>
#include <algorithm>
#include <iostream>

namespace transforms {

static DominatorTree globalDomTree;

void LoopInvariantCodeMotion::buildDominatorTree(ir::Function& func) {
    globalDomTree.run(func);
}

bool LoopInvariantCodeMotion::dominates(ir::BasicBlock* dominator, ir::BasicBlock* block) {
    return globalDomTree.dominates(dominator, block);
}

void LoopInvariantCodeMotion::findBackEdges(ir::Function& func, std::vector<std::pair<ir::BasicBlock*, ir::BasicBlock*>>& backEdges) {
    CFGBuilder::run(func);
    buildDominatorTree(func);

    for (auto& bb_ptr : func.getBasicBlocks()) {
        ir::BasicBlock* bb = bb_ptr.get();
        for (ir::BasicBlock* succ : bb->getSuccessors()) {
            if (dominates(succ, bb)) {
                backEdges.push_back({bb, succ}); // {latch, header}
            }
        }
    }
}

void LoopInvariantCodeMotion::buildLoop(ir::BasicBlock* header, ir::BasicBlock* latch, std::unique_ptr<Loop>& loop) {
    loop = std::make_unique<Loop>(header);
    loop->blocks.insert(header);

    if (header != latch) {
        loop->blocks.insert(latch);
        std::vector<ir::BasicBlock*> worklist = {latch};
        while (!worklist.empty()) {
            ir::BasicBlock* curr = worklist.back();
            worklist.pop_back();

            for (ir::BasicBlock* pred : curr->getPredecessors()) {
                if (loop->blocks.insert(pred).second) {
                    worklist.push_back(pred);
                }
            }
        }
    }

    for (ir::BasicBlock* bb : loop->blocks) {
        for (ir::BasicBlock* succ : bb->getSuccessors()) {
            if (loop->blocks.find(succ) == loop->blocks.end()) {
                loop->exits.insert(bb);
            }
        }
    }
}

void LoopInvariantCodeMotion::findLoops(ir::Function& func, std::vector<std::unique_ptr<Loop>>& loops) {
    std::vector<std::pair<ir::BasicBlock*, ir::BasicBlock*>> backEdges;
    findBackEdges(func, backEdges);

    for (const auto& edge : backEdges) {
        std::unique_ptr<Loop> loop;
        buildLoop(edge.second, edge.first, loop);
        if (loop) {
            loops.push_back(std::move(loop));
        }
    }
}

bool LoopInvariantCodeMotion::isTerminator(ir::Instruction* instr) {
    if (!instr) return false;
    switch (instr->getOpcode()) {
        case ir::Instruction::Ret:
        case ir::Instruction::Jmp:
        case ir::Instruction::Jnz:
        case ir::Instruction::Jz:
        case ir::Instruction::Br:
        case ir::Instruction::Hlt:
            return true;
        default:
            return false;
    }
}

bool LoopInvariantCodeMotion::hasSideEffects(ir::Instruction* instr) {
    if (!instr) return false;
    ir::Instruction::Opcode op = instr->getOpcode();
    return op == ir::Instruction::Call ||
           op == ir::Instruction::Syscall ||
           op == ir::Instruction::ExternCall ||
           op == ir::Instruction::Alloc ||
           op == ir::Instruction::Alloc4 ||
           op == ir::Instruction::Alloc16 ||
           mayWriteMemory(instr) ||
           mayReadMemory(instr);
}

bool LoopInvariantCodeMotion::mayWriteMemory(ir::Instruction* instr) {
    if (!instr) return false;
    switch (instr->getOpcode()) {
        case ir::Instruction::Store:
        case ir::Instruction::Stored:
        case ir::Instruction::Stores:
        case ir::Instruction::Storel:
        case ir::Instruction::Storeh:
        case ir::Instruction::Storeb:
            return true;
        default:
            return false;
    }
}

bool LoopInvariantCodeMotion::mayReadMemory(ir::Instruction* instr) {
    if (!instr) return false;
    switch (instr->getOpcode()) {
        case ir::Instruction::Load:
        case ir::Instruction::Loadd:
        case ir::Instruction::Loads:
        case ir::Instruction::Loadl:
        case ir::Instruction::Loaduw:
        case ir::Instruction::Loadsh:
        case ir::Instruction::Loaduh:
        case ir::Instruction::Loadsb:
        case ir::Instruction::Loadub:
            return true;
        default:
            return false;
    }
}

bool LoopInvariantCodeMotion::mayThrow(ir::Instruction* instr) {
    if (!instr) return false;
    ir::Instruction::Opcode op = instr->getOpcode();
    if (op == ir::Instruction::Div || op == ir::Instruction::Udiv ||
        op == ir::Instruction::Rem || op == ir::Instruction::Urem) {
        if (instr->getOperands().size() > 1 && instr->getOperands()[1]) {
            if (auto* ci = dynamic_cast<ir::ConstantInt*>(instr->getOperands()[1]->get())) {
                return ci->getValue() == 0;
            }
        }
        return true;
    }
    return false;
}

bool LoopInvariantCodeMotion::hasMemoryDependencies(ir::Instruction* instr, const Loop& loop) {
    return mayReadMemory(instr) || mayWriteMemory(instr);
}

bool LoopInvariantCodeMotion::isLoopInvariant(ir::Instruction* instr, const Loop& loop) {
    if (!instr) return false;
    if (isTerminator(instr)) return false;
    if (dynamic_cast<ir::PhiNode*>(instr)) return false;
    if (hasSideEffects(instr)) return false;
    if (mayThrow(instr)) return false;

    for (const auto& op : instr->getOperands()) {
        if (!op) continue;
        ir::Value* val = op->get();
        if (!val) continue;

        if (dynamic_cast<ir::Constant*>(val) ||
            dynamic_cast<ir::Parameter*>(val) ||
            dynamic_cast<ir::GlobalValue*>(val) ||
            dynamic_cast<ir::BasicBlock*>(val)) {
            continue;
        }

        if (auto* opInst = dynamic_cast<ir::Instruction*>(val)) {
            if (loop.blocks.find(opInst->getParent()) == loop.blocks.end()) {
                continue; // Defined outside loop
            }
            if (hoistedInstructions_.find(opInst) != hoistedInstructions_.end()) {
                continue; // Already hoisted to preheader
            }
            return false; // Operand is defined inside loop and not hoisted
        }
    }

    return true;
}

bool LoopInvariantCodeMotion::dominatesAllExits(ir::Instruction* instr, const Loop& loop) {
    if (!instr || !instr->getParent()) return false;
    ir::BasicBlock* bb = instr->getParent();
    for (ir::BasicBlock* exit : loop.exits) {
        if (!dominates(bb, exit)) return false;
    }
    return true;
}

bool LoopInvariantCodeMotion::isExecutedOnAllPaths(ir::Instruction* instr, const Loop& loop) {
    return dominatesAllExits(instr, loop) || instr->getParent() == loop.header;
}

bool LoopInvariantCodeMotion::isMovableTo(ir::Instruction* instr, ir::BasicBlock* target, const Loop& loop) {
    return isLoopInvariant(instr, loop) && isExecutedOnAllPaths(instr, loop);
}

bool LoopInvariantCodeMotion::canHoistInstruction(ir::Instruction* instr, const Loop& loop) {
    if (!isLoopInvariant(instr, loop)) return false;
    // Speculatively safe operations (pure, non-throwing) can be safely hoisted to preheader
    if (!mayThrow(instr) && !hasSideEffects(instr)) return true;
    return isExecutedOnAllPaths(instr, loop);
}

ir::BasicBlock* LoopInvariantCodeMotion::getOrCreatePreheader(Loop& loop, ir::Function& func) {
    if (loop.preheader) return loop.preheader;

    std::vector<ir::BasicBlock*> outsidePreds;
    for (ir::BasicBlock* pred : loop.header->getPredecessors()) {
        if (loop.blocks.find(pred) == loop.blocks.end()) {
            outsidePreds.push_back(pred);
        }
    }

    if (outsidePreds.size() == 1) {
        ir::BasicBlock* pred = outsidePreds[0];
        if (pred->getSuccessors().size() == 1) {
            loop.preheader = pred;
            return pred;
        }
    }

    // Create a preheader basic block
    auto ctx = func.getParent() ? func.getParent()->getContextShared() : std::make_shared<ir::IRContext>();
    ir::IRBuilder builder(ctx);
    builder.setModule(func.getParent());

    ir::BasicBlock* preheader = builder.createBasicBlock("loop_preheader", &func);

    // Insert preheader immediately before loop header in func.getBasicBlocks()
    auto& blocks = func.getBasicBlocks();
    auto headerIt = blocks.end();
    for (auto it = blocks.begin(); it != blocks.end(); ++it) {
        if (it->get() == loop.header) {
            headerIt = it;
            break;
        }
    }

    if (headerIt != blocks.end()) {
        auto preheaderPtr = std::move(blocks.back());
        blocks.pop_back();
        blocks.insert(headerIt, std::move(preheaderPtr));
    }

    // Add unconditional jump in preheader to loop header
    builder.setInsertPoint(preheader);
    builder.createJmp(loop.header);

    // Redirect outside predecessors to jump to preheader instead of header
    for (ir::BasicBlock* pred : outsidePreds) {
        if (pred->getInstructions().empty()) continue;
        ir::Instruction* term = pred->getInstructions().back().get();
        for (auto& op : term->getOperands()) {
            if (op && op->get() == loop.header) {
                op->set(preheader);
            }
        }
    }

    // Update PHI nodes in header
    for (auto& instr_ptr : loop.header->getInstructions()) {
        if (auto* phi = dynamic_cast<ir::PhiNode*>(instr_ptr.get())) {
            if (outsidePreds.size() == 1) {
                ir::Value* val = phi->getIncomingValueForBlock(outsidePreds[0]);
                if (val) {
                    phi->removeIncomingValue(outsidePreds[0]);
                    phi->addIncoming(val, preheader);
                }
            } else if (outsidePreds.size() > 1) {
                auto preheaderPhi = std::make_unique<ir::PhiNode>(phi->getType(), outsidePreds.size(), nullptr, preheader);
                for (ir::BasicBlock* pred : outsidePreds) {
                    ir::Value* val = phi->getIncomingValueForBlock(pred);
                    if (val) {
                        preheaderPhi->addIncoming(val, pred);
                    }
                }
                ir::PhiNode* rawPreheaderPhi = preheaderPhi.get();
                preheader->getInstructions().insert(preheader->getInstructions().begin(), std::move(preheaderPhi));

                for (ir::BasicBlock* pred : outsidePreds) {
                    phi->removeIncomingValue(pred);
                }
                phi->addIncoming(rawPreheaderPhi, preheader);
            }
        } else {
            break;
        }
    }

    CFGBuilder::run(func);
    buildDominatorTree(func);

    loop.preheader = preheader;
    return preheader;
}

bool LoopInvariantCodeMotion::needsPreheader(const Loop& loop) {
    return loop.preheader == nullptr;
}

void LoopInvariantCodeMotion::moveInstruction(ir::Instruction* instr, ir::BasicBlock* target) {
    if (!instr || !target || instr->getParent() == target) return;

    ir::BasicBlock* src = instr->getParent();
    auto& srcInstrs = src->getInstructions();

    std::unique_ptr<ir::Instruction> movedPtr;
    for (auto it = srcInstrs.begin(); it != srcInstrs.end(); ++it) {
        if (it->get() == instr) {
            movedPtr = std::move(*it);
            srcInstrs.erase(it);
            break;
        }
    }

    if (!movedPtr) return;

    movedPtr->setParent(target);
    auto& targetInstrs = target->getInstructions();
    if (!targetInstrs.empty() && isTerminator(targetInstrs.back().get())) {
        targetInstrs.insert(std::prev(targetInstrs.end()), std::move(movedPtr));
    } else {
        targetInstrs.push_back(std::move(movedPtr));
    }
}

void LoopInvariantCodeMotion::hoistInvariantInstructions(Loop& loop) {
    bool changed = true;
    while (changed) {
        changed = false;
        for (ir::BasicBlock* bb : loop.blocks) {
            std::vector<ir::Instruction*> rawInstrs;
            for (auto& ptr : bb->getInstructions()) {
                if (ptr) rawInstrs.push_back(ptr.get());
            }

            for (ir::Instruction* instr : rawInstrs) {
                if (!instr) continue;

                if (canHoistInstruction(instr, loop)) {
                    ir::BasicBlock* preheader = getOrCreatePreheader(loop, *bb->getParent());
                    if (preheader) {
                        moveInstruction(instr, preheader);
                        hoistedInstructions_.insert(instr);
                        instructions_hoisted_++;
                        changed = true;
                    }
                }
            }
        }
    }
}

bool LoopInvariantCodeMotion::performTransformation(ir::Function& func) {
    instructions_hoisted_ = 0;
    loops_processed_ = 0;
    hoistedInstructions_.clear();

    std::vector<std::unique_ptr<Loop>> loops;
    findLoops(func, loops);

    for (auto& loop : loops) {
        loops_processed_++;
        hoistInvariantInstructions(*loop);
    }

    if (instructions_hoisted_ > 0) {
        CFGBuilder::run(func);
        return true;
    }

    return false;
}

bool LoopInvariantCodeMotion::validatePreconditions(ir::Function& func) {
    return !func.getBasicBlocks().empty();
}

} // namespace transforms
