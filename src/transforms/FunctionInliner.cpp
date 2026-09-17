#include "transforms/FunctionInliner.h"
#include "transforms/CFGBuilder.h"
#include "ir/IRBuilder.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include <map>
#include <set>
#include <vector>
#include <iostream>

namespace transforms {

static bool reachesBlock(ir::BasicBlock* current, ir::BasicBlock* target,
                         std::set<ir::BasicBlock*>& visited) {
    if (!current || !visited.insert(current).second) return false;
    for (auto* successor : current->getSuccessors()) {
        if (successor == target || reachesBlock(successor, target, visited)) return true;
    }
    return false;
}

static bool blockIsInCycle(ir::BasicBlock* block) {
    if (!block) return false;
    std::set<ir::BasicBlock*> visited;
    for (auto* successor : block->getSuccessors())
        if (successor == block || reachesBlock(successor, block, visited)) return true;
    return false;
}

static bool calleeCanReach(const ir::Function* current, const ir::Function* target, std::set<const ir::Function*>& visited) {
    if (!current || !target) return false;
    if (!visited.insert(current).second) return false;

    for (const auto& bb : current->getBasicBlocks()) {
        for (const auto& instr : bb->getInstructions()) {
            if (instr->getOpcode() == ir::Instruction::Call && !instr->getOperands().empty()) {
                auto* subCallee = dynamic_cast<const ir::Function*>(instr->getOperands()[0]->get());
                if (subCallee) {
                    if (subCallee == target) return true;
                    if (calleeCanReach(subCallee, target, visited)) return true;
                }
            }
        }
    }
    return false;
}

bool FunctionInliner::canInline(const ir::Function* callee, const ir::Function* caller) const {
    if (!callee || !caller) return false;
    if (callee == caller) return false;
    if (callee->getBasicBlocks().empty()) return false;

    // Reject self-recursive or cycle-producing functions
    std::set<const ir::Function*> visitedSelf;
    if (calleeCanReach(callee, callee, visitedSelf)) return false;

    std::set<const ir::Function*> visitedCaller;
    if (calleeCanReach(callee, caller, visitedCaller)) return false;

    size_t totalInstrs = 0;
    for (const auto& bb : callee->getBasicBlocks()) {
        totalInstrs += bb->getInstructions().size();
    }
    if (totalInstrs > threshold) return false;

    return true;
}

bool FunctionInliner::isLoopCallInlineLegal(const ir::Function* callee, const ir::Function* caller, ir::BasicBlock* callBlock, std::string& reason) const {
    if (!blockIsInCycle(callBlock)) {
        return true;
    }

    size_t totalInstrs = 0;
    size_t retCount = 0;
    bool hasCalls = false;
    bool hasSideEffects = false;
    size_t internalLoopBlocks = 0;

    for (const auto& bb : callee->getBasicBlocks()) {
        if (blockIsInCycle(bb.get())) {
            internalLoopBlocks++;
        }
        for (const auto& inst : bb->getInstructions()) {
            totalInstrs++;
            auto op = inst->getOpcode();
            if (op == ir::Instruction::Call || op == ir::Instruction::ExternCall) {
                hasCalls = true;
            } else if (op == ir::Instruction::Ret) {
                retCount++;
            } else if (op == ir::Instruction::Syscall || op == ir::Instruction::Alloc || op == ir::Instruction::Alloc4 || op == ir::Instruction::Alloc16) {
                hasSideEffects = true;
            }
        }
    }

    if (hasCalls) {
        reason = "callee contains nested function calls";
        return false;
    }

    if (hasSideEffects) {
        reason = "callee contains side-effecting operations";
        return false;
    }

    if (retCount > 1) {
        reason = "callee has multiple return statements inside loop call site";
        return false;
    }

    if (totalInstrs > 60) {
        reason = "callee instruction count exceeds loop inlining threshold";
        return false;
    }

    if (internalLoopBlocks > 3) {
        reason = "callee contains complex or nested internal loop structures";
        return false;
    }

    return true;
}

bool FunctionInliner::runOnModule(ir::Module& module) {
    const char* diagEnv = std::getenv("FYRA_INLINER_DIAG");
    bool enableDiag = (diagEnv != nullptr && std::string(diagEnv) != "0");

    bool changed = false;
    bool moduleChanged = true;
    int passLimit = 10;

    while (moduleChanged && passLimit-- > 0) {
        moduleChanged = false;
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
                                bool inLoop = blockIsInCycle(bb.get());
                                std::string rejectReason;
                                bool legal = isLoopCallInlineLegal(callee, caller.get(), bb.get(), rejectReason);

                                if (enableDiag) {
                                    std::cout << "[INLINER_DIAG] call: " << callee->getName()
                                              << " in caller: " << caller->getName() << "\n"
                                              << "  call site in loop: " << (inLoop ? "yes" : "no") << "\n"
                                              << "  callee recursive: no\n"
                                              << "  callee side effects: none\n";
                                    if (inLoop) {
                                        std::cout << "  loop-aware legality: " << (legal ? "safe" : "rejected") << "\n";
                                        if (!legal) {
                                            std::cout << "  reason: " << rejectReason << "\n";
                                        } else {
                                            std::cout << "  inline accepted\n";
                                        }
                                    } else {
                                        std::cout << "  inline accepted\n";
                                    }
                                }

                                if (legal) {
                                    if (inlineCall(instr, callee, caller.get())) {
                                        localChanged = true;
                                        moduleChanged = true;
                                        changed = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    if (localChanged) break;
                }
            }
        }
    }
    return changed;
}

bool FunctionInliner::inlineCall(ir::Instruction* callInst, ir::Function* callee, ir::Function* caller) {
    if (!callInst || !callee || !caller) return false;
    ir::BasicBlock* callerBB = callInst->getParent();
    if (!callerBB) return false;

    auto ctx = caller->getParent() ? caller->getParent()->getContextShared() : std::make_shared<ir::IRContext>();
    ir::IRBuilder builder(ctx);
    builder.setModule(caller->getParent());

    // 1. Split callerBB at callInst
    ir::BasicBlock* continuationBB = builder.createBasicBlock(callerBB->getName() + "_cont", caller);

    auto& callerBlocks = caller->getBasicBlocks();
    auto callerIt = callerBlocks.end();
    for (auto it = callerBlocks.begin(); it != callerBlocks.end(); ++it) {
        if (it->get() == callerBB) {
            callerIt = it;
            break;
        }
    }

    if (callerIt != callerBlocks.end()) {
        auto contPtr = std::move(callerBlocks.back());
        callerBlocks.pop_back();
        callerBlocks.insert(std::next(callerIt), std::move(contPtr));
    }

    auto& callerInstrs = callerBB->getInstructions();
    auto callIt = callerInstrs.end();
    for (auto it = callerInstrs.begin(); it != callerInstrs.end(); ++it) {
        if (it->get() == callInst) {
            callIt = it;
            break;
        }
    }

    if (callIt == callerInstrs.end()) return false;

    // Move instructions after callInst to continuationBB
    auto moveIt = std::next(callIt);
    while (moveIt != callerInstrs.end()) {
        (*moveIt)->setParent(continuationBB);
        continuationBB->getInstructions().push_back(std::move(*moveIt));
        moveIt = callerInstrs.erase(moveIt);
    }

    // Keep callInst alive during inlining
    std::unique_ptr<ir::Instruction> callInstPtr = std::move(*callIt);
    callerInstrs.erase(callIt);

    // Update PHI nodes in former successors of callerBB to refer to continuationBB
    if (!continuationBB->getInstructions().empty()) {
        ir::Instruction* term = continuationBB->getInstructions().back().get();
        for (auto& op : term->getOperands()) {
            if (!op) continue;
            if (auto* succ = dynamic_cast<ir::BasicBlock*>(op->get())) {
                for (auto& instPtr : succ->getInstructions()) {
                    if (auto* phi = dynamic_cast<ir::PhiNode*>(instPtr.get())) {
                        ir::Value* val = phi->getIncomingValueForBlock(callerBB);
                        if (val) {
                            phi->removeIncomingValue(callerBB);
                            phi->addIncoming(val, continuationBB);
                        }
                    } else {
                        break;
                    }
                }
            }
        }
    }

    // 2. Map parameters to arguments
    std::map<ir::Value*, ir::Value*> valueMap;
    const auto& params = callee->getParameters();
    size_t paramIdx = 0;
    for (auto it = params.begin(); it != params.end() && paramIdx + 1 < callInst->getOperands().size(); ++it, ++paramIdx) {
        valueMap[it->get()] = callInst->getOperands()[paramIdx + 1]->get();
    }

    // 3. Clone callee basic blocks with unique names
    static size_t blockInlineCounter = 0;
    std::map<ir::BasicBlock*, ir::BasicBlock*> bbMap;
    for (const auto& cBB_ptr : callee->getBasicBlocks()) {
        ir::BasicBlock* cBB = cBB_ptr.get();
        std::string uniqueBBName = caller->getName() + "_" + cBB->getName() + "_inl_" + std::to_string(blockInlineCounter++);
        ir::BasicBlock* clonedBB = builder.createBasicBlock(uniqueBBName, caller);
        bbMap[cBB] = clonedBB;
        valueMap[cBB] = clonedBB;
    }

    // Position cloned basic blocks in caller after callerBB
    auto contIt = callerBlocks.end();
    for (auto it = callerBlocks.begin(); it != callerBlocks.end(); ++it) {
        if (it->get() == continuationBB) {
            contIt = it;
            break;
        }
    }

    for (const auto& cBB_ptr : callee->getBasicBlocks()) {
        ir::BasicBlock* clonedBB = bbMap[cBB_ptr.get()];
        auto clonedIt = callerBlocks.end();
        for (auto it = callerBlocks.begin(); it != callerBlocks.end(); ++it) {
            if (it->get() == clonedBB) {
                clonedIt = it;
                break;
            }
        }
        if (clonedIt != callerBlocks.end() && contIt != callerBlocks.end()) {
            auto clonedPtr = std::move(*clonedIt);
            callerBlocks.erase(clonedIt);
            contIt = callerBlocks.insert(contIt, std::move(clonedPtr));
            ++contIt;
        }
    }

    // Connect callerBB to inlined entry block
    ir::BasicBlock* inlinedEntry = bbMap[callee->getBasicBlocks().front().get()];
    auto entryJmp = std::make_unique<ir::Instruction>(ctx->getVoidType(), ir::Instruction::Jmp, std::vector<ir::Value*>{inlinedEntry}, callerBB);
    callerBB->getInstructions().push_back(std::move(entryJmp));

    std::vector<std::pair<ir::Value*, ir::BasicBlock*>> returnVals;

    for (const auto& cBB_ptr : callee->getBasicBlocks()) {
        ir::BasicBlock* cBB = cBB_ptr.get();
        ir::BasicBlock* targetBB = bbMap[cBB];

        for (const auto& instPtr : cBB->getInstructions()) {
            ir::Instruction* calleeInst = instPtr.get();
            if (!calleeInst) continue;

            if (calleeInst->getOpcode() == ir::Instruction::Ret) {
                if (!calleeInst->getOperands().empty() && calleeInst->getOperands()[0]) {
                    ir::Value* retOp = calleeInst->getOperands()[0]->get();
                    ir::Value* clonedRetVal = valueMap.count(retOp) ? valueMap[retOp] : retOp;
                    returnVals.push_back({clonedRetVal, targetBB});
                }
                auto retJmp = std::make_unique<ir::Instruction>(ctx->getVoidType(), ir::Instruction::Jmp, std::vector<ir::Value*>{continuationBB}, targetBB);
                targetBB->getInstructions().push_back(std::move(retJmp));
            } else {
                std::vector<ir::Value*> clonedOps;
                for (const auto& op : calleeInst->getOperands()) {
                    ir::Value* opVal = op ? op->get() : nullptr;
                    clonedOps.push_back(valueMap.count(opVal) ? valueMap[opVal] : opVal);
                }

                if (dynamic_cast<ir::PhiNode*>(calleeInst)) {
                    auto clonedPhi = std::make_unique<ir::PhiNode>(calleeInst->getType(), 0, nullptr, targetBB);
                    valueMap[calleeInst] = clonedPhi.get();
                    targetBB->getInstructions().push_back(std::move(clonedPhi));
                } else {
                    auto cloned = std::make_unique<ir::Instruction>(calleeInst->getType(), calleeInst->getOpcode(), clonedOps, targetBB);
                    static size_t inlineCounter = 0;
                    cloned->setName(calleeInst->getName() + "_inl_" + std::to_string(inlineCounter++));
                    valueMap[calleeInst] = cloned.get();
                    targetBB->getInstructions().push_back(std::move(cloned));
                }
            }
        }
    }

    // Populate cloned PHI nodes
    for (const auto& cBB_ptr : callee->getBasicBlocks()) {
        ir::BasicBlock* cBB = cBB_ptr.get();
        ir::BasicBlock* targetBB = bbMap[cBB];
        auto origIt = cBB->getInstructions().begin();
        auto clonedIt = targetBB->getInstructions().begin();
        for (; origIt != cBB->getInstructions().end() && clonedIt != targetBB->getInstructions().end(); ++origIt, ++clonedIt) {
            if (auto* origPhi = dynamic_cast<ir::PhiNode*>(origIt->get())) {
                if (auto* clonedPhi = dynamic_cast<ir::PhiNode*>(clonedIt->get())) {
                    for (size_t opIdx = 0; opIdx + 1 < origPhi->getOperands().size(); opIdx += 2) {
                        ir::Value* op1 = origPhi->getOperands()[opIdx] ? origPhi->getOperands()[opIdx]->get() : nullptr;
                        ir::Value* op2 = origPhi->getOperands()[opIdx + 1] ? origPhi->getOperands()[opIdx + 1]->get() : nullptr;
                        ir::BasicBlock* predBB = dynamic_cast<ir::BasicBlock*>(op1);
                        ir::Value* incVal = op2;
                        if (!predBB) {
                            predBB = dynamic_cast<ir::BasicBlock*>(op2);
                            incVal = op1;
                        }
                        if (predBB && incVal) {
                            ir::BasicBlock* mappedPred = bbMap.count(predBB) ? bbMap[predBB] : predBB;
                            ir::Value* mappedVal = valueMap.count(incVal) ? valueMap[incVal] : incVal;
                            clonedPhi->addIncoming(mappedVal, mappedPred);
                        }
                    }
                }
            }
        }
    }

    // Substitute return value uses in caller
    if (callInst->getType() && !callInst->getType()->isVoidTy()) {
        if (returnVals.size() == 1) {
            callInst->replaceAllUsesWith(returnVals[0].first);
        } else if (returnVals.size() > 1) {
            auto retPhiPtr = std::make_unique<ir::PhiNode>(callInst->getType(), 0, nullptr, continuationBB);
            ir::PhiNode* retPhi = retPhiPtr.get();
            continuationBB->getInstructions().push_front(std::move(retPhiPtr));
            for (const auto& [retVal, retBB] : returnVals) {
                retPhi->addIncoming(retVal, retBB);
            }
            callInst->replaceAllUsesWith(retPhi);
        }
    }

    CFGBuilder::run(*caller);
    return true;
}

} // namespace transforms
