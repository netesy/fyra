#include "transforms/FunctionInliner.h"
#include "ir/IRBuilder.h"
#include "ir/Use.h"
#include <map>
#include <vector>

namespace transforms {

bool FunctionInliner::canInline(const ir::Function* callee, const ir::Function* caller) const {
    if (!callee || !caller) return false;
    if (callee == caller) return false; // Never inline recursive calls into themselves
    if (callee->getBasicBlocks().empty()) return false;

    size_t totalInstrs = 0;
    for (const auto& bb : callee->getBasicBlocks()) {
        totalInstrs += bb->getInstructions().size();
    }
    if (totalInstrs > threshold) return false;

    return true;
}

bool FunctionInliner::runOnModule(ir::Module& module) {
    bool changed = false;
    for (auto& caller : module.getFunctions()) {
        if (!caller || caller->getBasicBlocks().empty()) continue;

        bool localChanged = true;
        while (localChanged) {
            localChanged = false;
            for (auto& bb : caller->getBasicBlocks()) {
                auto& instrs = bb->getInstructions();
                for (auto it = instrs.begin(); it != instrs.end(); ++it) {
                    ir::Instruction* instr = it->get();
                    if (instr->getOpcode() == ir::Instruction::Call && !instr->getOperands().empty()) {
                        auto* callee = dynamic_cast<ir::Function*>(instr->getOperands()[0]->get());
                        if (callee && canInline(callee, caller.get())) {
                            if (inlineCall(instr, callee, caller.get())) {
                                localChanged = true;
                                changed = true;
                                break;
                            }
                        }
                    }
                }
                if (localChanged) break;
            }
        }
    }
    return changed;
}

bool FunctionInliner::inlineCall(ir::Instruction* callInst, ir::Function* callee, ir::Function* caller) {
    if (callee->getBasicBlocks().size() != 1) return false; // Support single-block callees

    ir::BasicBlock* calleeBB = callee->getBasicBlocks().front().get();
    ir::BasicBlock* callerBB = callInst->getParent();

    std::map<ir::Value*, ir::Value*> valueMap;
    const auto& params = callee->getParameters();
    size_t i = 0;
    for (auto it = params.begin(); it != params.end() && i + 1 < callInst->getOperands().size(); ++it, ++i) {
        valueMap[it->get()] = callInst->getOperands()[i + 1]->get();
    }

    ir::Value* returnVal = nullptr;
    std::vector<std::unique_ptr<ir::Instruction>> clonedInstrs;

    for (const auto& instrPtr : calleeBB->getInstructions()) {
        ir::Instruction* calleeInst = instrPtr.get();
        if (calleeInst->getOpcode() == ir::Instruction::Ret) {
            if (!calleeInst->getOperands().empty()) {
                ir::Value* retOp = calleeInst->getOperands()[0]->get();
                returnVal = valueMap.count(retOp) ? valueMap[retOp] : retOp;
            }
            break;
        }

        std::vector<ir::Value*> clonedOps;
        for (const auto& op : calleeInst->getOperands()) {
            ir::Value* opVal = op->get();
            clonedOps.push_back(valueMap.count(opVal) ? valueMap[opVal] : opVal);
        }

        auto cloned = std::make_unique<ir::Instruction>(calleeInst->getType(), calleeInst->getOpcode(), clonedOps, callerBB);
        cloned->setName(calleeInst->getName() + "_inlined");
        valueMap[calleeInst] = cloned.get();
        clonedInstrs.push_back(std::move(cloned));
    }

    auto& callerInstrs = callerBB->getInstructions();
    auto callIt = callerInstrs.end();
    for (auto it = callerInstrs.begin(); it != callerInstrs.end(); ++it) {
        if (it->get() == callInst) {
            callIt = it;
            break;
        }
    }

    if (callIt != callerInstrs.end()) {
        if (returnVal && callInst->getType() && !callInst->getType()->isVoidTy()) {
            callInst->replaceAllUsesWith(returnVal);
        }

        for (auto& cloned : clonedInstrs) {
            callerInstrs.insert(callIt, std::move(cloned));
        }

        callerInstrs.erase(callIt);
        return true;
    }

    return false;
}

} // namespace transforms
