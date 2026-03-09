#include "transforms/GVN.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Value.h"
#include "ir/Use.h"
#include <vector>
#include <map>
#include <string>
#include <algorithm>
#include <sstream>

namespace transforms {

// A simple canonical representation for an expression.
// For commutative ops, we sort operands by their pointer value to ensure
// `add a, b` and `add b, a` produce the same key.
std::string getValueKey(const ir::Instruction* instr) {
    std::stringstream key;
    key << instr->getOpcode();

    switch (instr->getOpcode()) {
        case ir::Instruction::Add:
        case ir::Instruction::Mul: {
            // Commutative ops: sort operands
            std::vector<ir::Value*> operands;
            for (const auto& use : instr->getOperands()) {
                operands.push_back(use->get());
            }
            std::sort(operands.begin(), operands.end());
            for (ir::Value* op : operands) {
                key << ":" << op;
            }
            break;
        }
        case ir::Instruction::Sub:
        case ir::Instruction::Div: {
            // Non-commutative ops
            for (const auto& use : instr->getOperands()) {
                key << ":" << use->get();
            }
            break;
        }
        default:
            // This instruction cannot be GVN'd
            return "";
    }
    return key.str();
}

bool GVN::run(ir::Function& func) {
    bool changed = false;

    for (auto& bb : func.getBasicBlocks()) {
        std::map<std::string, ir::Instruction*> value_map;
        std::vector<ir::Instruction*> to_remove;

        for (auto& instr_ptr : bb->getInstructions()) {
            ir::Instruction* instr = instr_ptr.get();
            std::string key = getValueKey(instr);

            if (key.empty()) {
                continue; // Instruction cannot be eliminated
            }

            if (value_map.count(key)) {
                // Found a redundant instruction
                ir::Instruction* redundant_instr = instr;
                ir::Instruction* original_instr = value_map.at(key);
                redundant_instr->replaceAllUsesWith(original_instr);
                to_remove.push_back(redundant_instr);
                changed = true;
            } else {
                // First time seeing this value
                value_map[key] = instr;
            }
        }

        if (!to_remove.empty()) {
            bb->removeInstructions(to_remove);
        }
    }

    return changed;
}

} // namespace transforms
