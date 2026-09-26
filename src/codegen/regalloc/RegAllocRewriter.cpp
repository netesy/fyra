#include "codegen/regalloc/RegAllocRewriter.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "target/core/TargetInfo.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/IRBuilder.h"
#include "ir/Use.h"
#include "ir/Constant.h"
#include <map>
#include <vector>
#include <iostream>

namespace transforms {

bool RegAllocRewriter::run(ir::Function& func) {
    return run(func, nullptr);
}

bool RegAllocRewriter::run(ir::Function& func, const ::target::TargetInfo* targetInfo) {
    // 1. Run the allocator
    LinearScanAllocator allocator;
    allocator.run(func, targetInfo);
    const auto& location_map = allocator.getRegisterMap();

    // 2. Update the function's stack frame information
    int numRegParams = std::max(1, std::min(static_cast<int>(func.getParameters().size()), 6));
    int paramOffsetBytes = numRegParams * 8;
    int stack_frame_size = paramOffsetBytes;
    for (const auto& [vreg, location] : location_map) {
        if (dynamic_cast<const ir::Parameter*>(vreg)) continue;
        if (std::holds_alternative<StackSlot>(location)) {
            StackSlot slot = std::get<StackSlot>(location);
            int slotByteOffset = paramOffsetBytes + (slot.index + 1) * 8;
            if (slotByteOffset <= paramOffsetBytes) slotByteOffset = paramOffsetBytes + 8;
            func.setStackSlotForVreg(vreg, slotByteOffset);
            int slotSize = 8;
            if (vreg && vreg->getType()) {
                if (auto* vt = dynamic_cast<const ir::VectorType*>(vreg->getType())) {
                    slotSize = vt->getSize();
                }
            }
            if (slotByteOffset + slotSize > stack_frame_size) {
                stack_frame_size = slotByteOffset + slotSize;
            }
        } else if (std::holds_alternative<PhysicalReg>(location)) {
            PhysicalReg reg = std::get<PhysicalReg>(location);
            vreg->setPhysicalRegister(reg.index);
        }
    }

    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instr : bb->getInstructions()) {
            if (!instr->getType() || instr->getType()->isVoidTy()) continue;
            if (dynamic_cast<ir::Parameter*>(instr.get())) continue;
            if (!func.hasStackSlot(instr.get()) && !instr->hasPhysicalRegister()) {
                int nextSlotIdx = func.getStackSlots().size();
                int slotByteOffset = paramOffsetBytes + (nextSlotIdx + 1) * 8;
                func.setStackSlotForVreg(instr.get(), slotByteOffset);
            }
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
                        if (slot > 0) {
                            if (!operand_vreg->getOperands().empty() && operand_vreg->getOperands()[0]) {
                                if (auto* c = dynamic_cast<ir::ConstantInt*>(operand_vreg->getOperands()[0]->get())) {
                                    use->set(c);
                                    continue;
                                } else if (auto* cfp = dynamic_cast<ir::ConstantFP*>(operand_vreg->getOperands()[0]->get())) {
                                    use->set(cfp);
                                    continue;
                                }
                            }
                            builder.setInsertPoint(bb.get(), it);
                            ir::Instruction* load = builder.createLoadStack(operand_vreg->getType(), slot);
                            use->setOriginalValue(operand_vreg);
                            use->set(load);
                        }
                    }
                }
            }

            // Rewrite definitions
            if (!dynamic_cast<ir::Parameter*>(instr) && location_map.count(instr) && std::holds_alternative<StackSlot>(location_map.at(instr))) {
                int slot = func.getStackSlotForVreg(instr);
                if (slot > 0) {
                    auto next_it = std::next(it);
                    builder.setInsertPoint(bb.get(), next_it);
                    builder.createStoreStack(instr, slot);
                }
            }

            ++it;
        }
    }

    return true;
}

} // namespace transforms
