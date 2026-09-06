#include "codegen/regalloc/LivenessAnalysis.h"
#include "transforms/CFGBuilder.h"
#include "transforms/LoopInvariantCodeMotion.h"
#include "transforms/Loop.h"
#include "ir/BasicBlock.h"
#include "ir/PhiNode.h"
#include "ir/User.h"
#include "ir/Use.h"
#include <algorithm>
#include <iostream>
#include <climits>

namespace transforms {

void LivenessAnalysis::run(ir::Function& func) {
    CFGBuilder::run(func);
    computeLiveSets(func);

    // Assign instruction numbers
    std::map<const ir::Instruction*, int> instrNumbering;
    int i = 0;
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            instrNumbering[instr.get()] = i++;
        }
    }

    // Compute live ranges by finding def and last use
    for (auto& [instr, num] : instrNumbering) {
        // We only care about instructions that define a value
        if (instr->getType()->getTypeID() != ir::Type::VoidTyID) {
            liveRanges[instr] = {num, num}; // Initialize range to def site
        }
    }

    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            int use_site = instrNumbering[instr.get()];
            if (auto* phi = dynamic_cast<ir::PhiNode*>(instr.get())) {
                for (size_t k = 0; k + 1 < phi->getOperands().size(); k += 2) {
                    ir::Value* predVal = phi->getOperands()[k]->get();
                    ir::Value* incomingVal = phi->getOperands()[k+1]->get();
                    auto* incomingBB = dynamic_cast<ir::BasicBlock*>(predVal);
                    if (auto* op_instr = dynamic_cast<ir::Instruction*>(incomingVal)) {
                        if (liveRanges.count(op_instr) && incomingBB && !incomingBB->getInstructions().empty()) {
                            int term_site = instrNumbering[incomingBB->getInstructions().back().get()];
                            liveRanges[op_instr].end = std::max(liveRanges[op_instr].end, term_site);
                        }
                    }
                }
            } else {
                for (auto& operand : instr->getOperands()) {
                    if (auto* op_instr = dynamic_cast<ir::Instruction*>(operand->get())) {
                        if (liveRanges.count(op_instr)) {
                            liveRanges[op_instr].end = std::max(liveRanges[op_instr].end, use_site);
                        }
                    }
                }
            }
        }
    }

    // Extend live ranges across loop latches for non-Phi loop-invariant variables defined outside the loop
    std::vector<std::unique_ptr<Loop>> loops;
    LoopInvariantCodeMotion licm;
    licm.findLoops(func, loops);

    for (const auto& loopPtr : loops) {
        if (!loopPtr) continue;
        int max_latch_site = -1;
        for (ir::BasicBlock* bb : loopPtr->blocks) {
            if (bb->getInstructions().empty()) continue;
            int bb_end_site = instrNumbering[bb->getInstructions().back().get()];
            for (ir::BasicBlock* succ : bb->getSuccessors()) {
                if (succ == loopPtr->header) {
                    max_latch_site = std::max(max_latch_site, bb_end_site);
                    break;
                }
            }
        }
        if (max_latch_site < 0) continue;

        for (ir::BasicBlock* bb : loopPtr->blocks) {
            for (auto& instr_ptr : bb->getInstructions()) {
                for (auto& operand : instr_ptr->getOperands()) {
                    if (auto* op_instr = dynamic_cast<ir::Instruction*>(operand->get())) {
                        if (loopPtr->blocks.count(op_instr->getParent()) == 0) {
                            if (liveRanges.count(op_instr)) {
                                liveRanges[op_instr].end = std::max(liveRanges[op_instr].end, max_latch_site);
                            }
                        }
                    }
                }
            }
        }
    }

    // Remove ranges for dead variables (no uses)
    std::vector<const ir::Instruction*> dead_vars;
    for (auto const& [var, range] : liveRanges) {
        if (var->getUseList().empty()) {
            dead_vars.push_back(var);
        }
    }
    for (auto* var : dead_vars) {
        liveRanges.erase(var);
    }

    // Compute CFG-aware per-instruction liveness
    computePerInstructionCFGLiveness(func);
}

bool LivenessAnalysis::isLiveAfter(const ir::Instruction* instruction, const ir::Value* value) const {
    if (!instruction || !value) return false;
    auto it = liveAfterMap.find(instruction);
    if (it == liveAfterMap.end()) return false;
    return it->second.count(value) > 0;
}

bool LivenessAnalysis::isLastUseOfOperand(const ir::Instruction* user, const ir::Use* use) const {
    if (!user || !use) return false;
    const ir::Value* origVal = use->getOriginalValue();
    if (!origVal) return false;
    return !isLiveAfter(user, origVal);
}

void LivenessAnalysis::computePerInstructionCFGLiveness(ir::Function& func) {
    liveAfterMap.clear();
    liveBeforeMap.clear();

    bool changed = true;
    while (changed) {
        changed = false;

        for (auto bb_it = func.getBasicBlocks().rbegin(); bb_it != func.getBasicBlocks().rend(); ++bb_it) {
            ir::BasicBlock* bb = bb_it->get();
            auto& instrs = bb->getInstructions();
            if (instrs.empty()) continue;

            // Compute liveAfter for the last instruction in the basic block
            ir::Instruction* last_instr = instrs.back().get();
            std::set<const ir::Value*> new_live_after_last;

            for (ir::BasicBlock* succ : bb->getSuccessors()) {
                if (succ->getInstructions().empty()) continue;
                ir::Instruction* succ_first = succ->getInstructions().front().get();

                // Start with liveBefore of the first instruction in the successor
                std::set<const ir::Value*> succ_live_in = liveBeforeMap[succ_first];

                // Remove Phi definitions in succ (they are defined in succ, not live across edge from pred)
                for (auto& succ_inst : succ->getInstructions()) {
                    if (auto* phi = dynamic_cast<const ir::PhiNode*>(succ_inst.get())) {
                        succ_live_in.erase(phi);
                        // Add the incoming value for edge bb -> succ
                        ir::Value* incoming = const_cast<ir::PhiNode*>(phi)->getIncomingValueForBlock(bb);
                        if (incoming && (dynamic_cast<const ir::Instruction*>(incoming) || dynamic_cast<const ir::Parameter*>(incoming))) {
                            succ_live_in.insert(incoming);
                        }
                    } else {
                        break;
                    }
                }

                new_live_after_last.insert(succ_live_in.begin(), succ_live_in.end());
            }

            if (liveAfterMap[last_instr] != new_live_after_last) {
                liveAfterMap[last_instr] = new_live_after_last;
                changed = true;
            }

            // Propagate backward through instructions inside the basic block
            for (auto inst_it = instrs.rbegin(); inst_it != instrs.rend(); ++inst_it) {
                ir::Instruction* curr = inst_it->get();

                if (inst_it != instrs.rbegin()) {
                    ir::Instruction* next_instr = std::prev(inst_it)->get();
                    if (liveAfterMap[curr] != liveBeforeMap[next_instr]) {
                        liveAfterMap[curr] = liveBeforeMap[next_instr];
                        changed = true;
                    }
                }

                // Compute liveBefore(curr) = use(curr) U (liveAfter(curr) - def(curr))
                std::set<const ir::Value*> new_live_before = liveAfterMap[curr];

                // Remove def(curr) if non-void
                if (curr->getType() && !curr->getType()->isVoidTy()) {
                    new_live_before.erase(curr);
                }

                // Add uses of curr (non-Phi instructions only; Phi incoming uses are handled on pred -> succ edges)
                if (!dynamic_cast<const ir::PhiNode*>(curr)) {
                    for (auto& op_use : curr->getOperands()) {
                        if (auto* val = op_use->get()) {
                            if (dynamic_cast<const ir::Instruction*>(val) || dynamic_cast<const ir::Parameter*>(val)) {
                                new_live_before.insert(val);
                            }
                        }
                    }
                }

                if (liveBeforeMap[curr] != new_live_before) {
                    liveBeforeMap[curr] = new_live_before;
                    changed = true;
                }
            }
        }
    }
}

void LivenessAnalysis::computeLiveSets(ir::Function& func) {
    std::map<ir::BasicBlock*, std::set<ir::Instruction*>> use;
    std::map<ir::BasicBlock*, std::set<ir::Instruction*>> def;

    // Compute Use and Def sets for all blocks
    for (auto& bb_ptr : func.getBasicBlocks()) {
        ir::BasicBlock* bb = bb_ptr.get();
        for (auto& instr_ptr : bb->getInstructions()) {
            // Defs
            if (instr_ptr->getType()->getTypeID() != ir::Type::VoidTyID) {
                def[bb].insert(instr_ptr.get());
            }
            // Uses
            for (auto& operand : instr_ptr->getOperands()) {
                if (auto* op_instr = dynamic_cast<ir::Instruction*>(operand->get())) {
                    use[bb].insert(op_instr);
                }
            }
        }
    }

    // Iteratively compute live-in and live-out
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto it = func.getBasicBlocks().rbegin(); it != func.getBasicBlocks().rend(); ++it) {
            ir::BasicBlock* bb = it->get();

            std::set<ir::Instruction*> new_live_out;
            for (ir::BasicBlock* succ : bb->getSuccessors()) {
                new_live_out.insert(liveIn[succ].begin(), liveIn[succ].end());
            }

            if (new_live_out != liveOut[bb]) {
                liveOut[bb] = new_live_out;
                changed = true;
            }

            std::set<ir::Instruction*> new_live_in = use[bb];
            std::set<ir::Instruction*> temp_out = liveOut[bb];
            for (ir::Instruction* d : def[bb]) {
                temp_out.erase(d);
            }
            new_live_in.insert(temp_out.begin(), temp_out.end());

            if (new_live_in != liveIn[bb]) {
                liveIn[bb] = new_live_in;
                changed = true;
            }
        }
    }
}

} // namespace transforms
