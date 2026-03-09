#include "transforms/LivenessAnalysis.h"
#include "ir/BasicBlock.h"
#include "ir/User.h"
#include "ir/Use.h"
#include <algorithm>
#include <iostream>
#include <climits>

namespace transforms {

void LivenessAnalysis::run(ir::Function& func) {
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
            for (auto& operand : instr->getOperands()) {
                if (auto* op_instr = dynamic_cast<ir::Instruction*>(operand->get())) {
                    // op_instr is a virtual register being used
                    if (liveRanges.count(op_instr)) {
                        liveRanges[op_instr].end = std::max(liveRanges[op_instr].end, use_site);
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
