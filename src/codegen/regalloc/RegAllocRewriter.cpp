#include "codegen/regalloc/RegAllocRewriter.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "target/core/TargetInfo.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/PhiNode.h"
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
    // Materialize register-passed parameters as ordinary virtual registers before
    // allocation.  Parameters otherwise keep living in their ABI registers, but
    // are absent from LiveIntervalAnalysis (which tracks instructions).  That let
    // the allocator reuse an argument register while the parameter was still live
    // -- for example, a loop temporary could overwrite its trip-count argument.
    // Copies make the complete lifetime visible to the allocator and also give it
    // freedom to coalesce short-lived arguments into caller-saved registers.
    if (!func.getBasicBlocks().empty()) {
        ir::BasicBlock* entry = func.getBasicBlocks().front().get();
        auto insertAt = entry->getInstructions().begin();
        ir::IRBuilder parameterBuilder(func.getParent()->getContextShared());
        parameterBuilder.setModule(func.getParent());

        for (auto& parameterOwner : func.getParameters()) {
            ir::Parameter* parameter = parameterOwner.get();
            if (!parameter || parameter->use_empty()) continue;

            // Snapshot the original uses before creating the copy so that the
            // copy's own source operand is not rewritten into a self-reference.
            std::vector<ir::Use*> originalUses(parameter->getUseList().begin(),
                                                parameter->getUseList().end());
            parameterBuilder.setInsertPoint(entry, insertAt);
            ir::Instruction* home = parameterBuilder.createCopy(parameter);
            home->setName(parameter->getName().empty()
                              ? "arg.home"
                              : parameter->getName() + ".home");
            for (ir::Use* use : originalUses) {
                use->set(home);
            }
        }
    }

    // 1. Run the allocator
    LinearScanAllocator allocator;
    allocator.run(func, targetInfo);
    const auto& location_map = allocator.getRegisterMap();

    // 2. Update the function's stack frame information
    int numRegParams = std::max(1, std::min(static_cast<int>(func.getParameters().size()), 6));
    int paramOffsetBytes = numRegParams * 8;
    int baseSpillOffset = (paramOffsetBytes + 63) & ~63;
    if (baseSpillOffset == 0) baseSpillOffset = 64;

    int stack_frame_size = baseSpillOffset;
    for (const auto& [vreg, location] : location_map) {
        if (dynamic_cast<const ir::Parameter*>(vreg)) continue;
        if (std::holds_alternative<StackSlot>(location)) {
            StackSlot slot = std::get<StackSlot>(location);
            int slotByteOffset = baseSpillOffset + slot.byteOffset;
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
                size_t align = 8;
                if (instr->getType()) {
                    if (auto* vt = dynamic_cast<const ir::VectorType*>(instr->getType())) {
                        size_t bits = vt->getSize() * 8;
                        if (bits >= 512) align = 64;
                        else if (bits >= 256) align = 32;
                        else if (bits >= 128) align = 16;
                    }
                }
                if (stack_frame_size % align != 0) {
                    stack_frame_size += (align - (stack_frame_size % align));
                }
                func.setStackSlotForVreg(instr.get(), stack_frame_size);
                size_t slotBytes = 8;
                if (instr->getType()) {
                    if (auto* vt = dynamic_cast<const ir::VectorType*>(instr->getType())) {
                        slotBytes = vt->getSize();
                    }
                }
                stack_frame_size += slotBytes;
            }
        }
    }
    func.setStackFrameSize(stack_frame_size);

    // 3. Rewrite the IR
    ir::IRBuilder builder(func.getParent()->getContextShared());
    builder.setModule(func.getParent());

    for (auto& bb : func.getBasicBlocks()) {
        auto& instrs = bb->getInstructions();
        for (auto it = instrs.begin(); it != instrs.end(); ) {
            ir::Instruction* instr = it->get();

            if (dynamic_cast<ir::PhiNode*>(instr)) {
                ++it;
                continue;
            }

            // Rewrite operands (handle uses)
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
                            if (operand_vreg->getOpcode() == ir::Instruction::Copy && !operand_vreg->getOperands().empty() && operand_vreg->getOperands()[0]) {
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
            if (!dynamic_cast<ir::Parameter*>(instr) && func.hasStackSlot(instr) && !instr->hasPhysicalRegister()) {
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
