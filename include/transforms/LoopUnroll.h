#pragma once

#include "transforms/TransformPass.h"
#include "transforms/Loop.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "ir/Instruction.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include <map>
#include <set>
#include <vector>
#include <memory>

namespace transforms {

class LoopUnroll : public TransformPass {
public:
    explicit LoopUnroll(std::shared_ptr<ErrorReporter> error_reporter = nullptr)
        : TransformPass("Loop Unrolling (2x)", error_reporter) {}

    virtual ~LoopUnroll() override = default;

    /**
     * @brief Utility class for remapping operands and cloning instructions.
     */
    class ValueCloner {
    public:
        explicit ValueCloner(const std::map<ir::Value*, ir::Value*>& valueMap)
            : valueMap_(valueMap) {}

        ir::Value* getMappedValue(ir::Value* orig) const {
            if (!orig) return nullptr;
            auto it = valueMap_.find(orig);
            if (it != valueMap_.end()) return it->second;
            return orig;
        }

        std::unique_ptr<ir::Instruction> cloneInstruction(ir::Instruction* orig, ir::BasicBlock* targetBB) const {
            std::vector<ir::Value*> newOperands;
            for (auto& op : orig->getOperands()) {
                ir::Value* v = op ? op->get() : nullptr;
                newOperands.push_back(getMappedValue(v));
            }

            std::unique_ptr<ir::Instruction> cloned;
            if (auto* sys = dynamic_cast<ir::SyscallInstruction*>(orig)) {
                cloned = std::make_unique<ir::SyscallInstruction>(sys->getType(), newOperands, sys->getSyscallId(), targetBB);
            } else if (auto* ext = dynamic_cast<ir::ExternCallInstruction*>(orig)) {
                cloned = std::make_unique<ir::ExternCallInstruction>(ext->getType(), newOperands, ext->getCapability(), targetBB);
            } else if (auto* phi = dynamic_cast<ir::PhiNode*>(orig)) {
                auto newPhi = std::make_unique<ir::PhiNode>(phi->getType(), newOperands.size(), phi->getVariable(), targetBB);
                for (size_t k = 0; k < newOperands.size(); k += 2) {
                    if (k + 1 < newOperands.size()) {
                        newPhi->addIncoming(newOperands[k + 1], dynamic_cast<ir::BasicBlock*>(newOperands[k]));
                    }
                }
                cloned = std::move(newPhi);
            } else {
                cloned = std::make_unique<ir::Instruction>(orig->getType(), orig->getOpcode(), newOperands, targetBB);
            }
            cloned->setName(orig->getName() + ".unroll");
            cloned->setSourceLine(orig->getSourceLine());
            return cloned;
        }

    private:
        const std::map<ir::Value*, ir::Value*>& valueMap_;
    };

protected:
    bool performTransformation(ir::Function& func) override;

private:
    struct IndVarInfo {
        ir::PhiNode* phi = nullptr;
        ir::Instruction* stepInst = nullptr;
        ir::Value* initVal = nullptr;
        int64_t stepVal = 0;
        ir::Instruction* condInst = nullptr;
        ir::Value* boundVal = nullptr;
        ir::Instruction::Opcode cmpOpcode = ir::Instruction::Cslt;
        bool stepAddedToIV = true;
        bool condUsesIV = true;
    };

    bool analyzeLoopLegality(Loop& loop, ir::Function& func, IndVarInfo& ivInfo);
    bool unrollLoop(Loop& loop, ir::Function& func, const IndVarInfo& ivInfo);
};

} // namespace transforms
