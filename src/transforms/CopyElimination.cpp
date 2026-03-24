#include "transforms/CopyElimination.h"
#include "ir/Instruction.h"
#include "ir/Constant.h"
#include "ir/Value.h"
#include "ir/Use.h"
#include "ir/BasicBlock.h"
#include <vector>
#include <map>
#include <iostream>

namespace transforms {

bool CopyElimination::run(ir::Function& func) {
    bool changed = false;
    std::map<ir::Value*, ir::Value*> copy_map;
    std::vector<ir::Instruction*> to_remove;

    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr_ptr : bb->getInstructions()) {
            if (instr_ptr->getOpcode() == ir::Instruction::Copy) {
                if (instr_ptr->getOperands().empty()) continue;
                ir::Value* dest = instr_ptr.get();
                ir::Value* src = instr_ptr->getOperands()[0]->get();
                if (dest && src && dest != src) {
                    copy_map[dest] = src;
                    to_remove.push_back(instr_ptr.get());
                }
            }
        }
    }

    if (copy_map.empty()) return false;

    bool resolving = true;
    while(resolving) {
        resolving = false;
        for (auto const& [dest, src] : copy_map) {
            if (copy_map.count(src)) {
                ir::Value* final_src = copy_map.at(src);
                if (copy_map[dest] != final_src) {
                    copy_map[dest] = final_src;
                    resolving = true;
                }
            }
        }
    }

    for (auto const& [dest, src] : copy_map) {
        dest->replaceAllUsesWith(src);
        changed = true;
    }

    for (auto& bb : func.getBasicBlocks()) {
        bb->removeInstructions(to_remove);
    }

    return changed;
}

} // namespace transforms
