#include "transforms/RegAllocRewriter.h"
#include "transforms/LinearScanAllocator.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/IRBuilder.h"
#include "ir/Use.h"
#include "ir/Constant.h"
#include <map>
#include <vector>

namespace transforms {

bool RegAllocRewriter::run(ir::Function& func) {
    // 1. Run the allocator
    LinearScanAllocator allocator;
    allocator.run(func);
    const auto& location_map = allocator.getRegisterMap();

    // 2. Update the function's stack frame information
    int stack_frame_size = 0;
    for (const auto& [vreg, location] : location_map) {
        if (std::holds_alternative<StackSlot>(location)) {
            StackSlot slot = std::get<StackSlot>(location);
            // Assuming each slot is 8 bytes now for x64
            func.setStackSlotForVreg(vreg, slot.index * 8);
            stack_frame_size += 8;
        } else if (std::holds_alternative<PhysicalReg>(location)) {
            PhysicalReg reg = std::get<PhysicalReg>(location);
            vreg->setPhysicalRegister(reg.index);
        }
    }
    func.setStackFrameSize(stack_frame_size);

    // 3. Rewrite the IR
    ir::IRBuilder builder(func.getParent()->getContextShared());
    builder.setModule(func.getParent());

    for (auto& bb : func.getBasicBlocks()) {
        for (auto it = bb->getInstructions().begin(); it != bb->getInstructions().end(); ) {
            ir::Instruction* instr = it->get();

            // Rewrite operands (handle uses)
            // We need a copy of the operands because we might be modifying the use list
            std::vector<ir::Use*> uses;
            for (auto& use : instr->getOperands()) {
                uses.push_back(use.get());
            }

            for (ir::Use* use : uses) {
                ir::Value* operand_val = use->get();
                if (auto* operand_vreg = dynamic_cast<ir::Instruction*>(operand_val)) {
                    if (location_map.count(operand_vreg) && std::holds_alternative<StackSlot>(location_map.at(operand_vreg))) {
                        int slot = func.getStackSlotForVreg(operand_vreg);
                        builder.setInsertPoint(bb.get(), it);
                        ir::Instruction* load = builder.createLoadStack(operand_vreg->getType(), slot);
                        use->set(load);
                    }
                }
            }

            // Rewrite definitions
            if (location_map.count(instr) && std::holds_alternative<StackSlot>(location_map.at(instr))) {
                int slot = func.getStackSlotForVreg(instr);
                auto next_it = std::next(it);
                builder.setInsertPoint(bb.get(), next_it);
                builder.createStoreStack(instr, slot);
            }

            ++it;
        }
    }

    return true;
}

} // namespace transforms
