#include "transforms/LoopRotate.h"
#include "transforms/LoopInvariantCodeMotion.h"
#include "transforms/CFGBuilder.h"
#include "ir/IRBuilder.h"
#include "ir/Constant.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include <algorithm>
#include <vector>
#include <set>
#include <map>

namespace transforms {

bool LoopRotate::run(ir::Function& func) {
    return performTransformation(func);
}

bool LoopRotate::performTransformation(ir::Function& func) {
    if (func.getBasicBlocks().empty()) return false;

    LoopInvariantCodeMotion licm;
    std::vector<std::unique_ptr<Loop>> loops;
    licm.findLoops(func, loops);

    bool changed = false;
    for (auto& loopPtr : loops) {
        if (rotateLoop(*loopPtr, func)) {
            changed = true;
            break; // Transform one loop per iteration to ensure CFG/Dominator safety
        }
    }

    if (changed) {
        CFGBuilder::run(func);
    }
    return changed;
}

bool LoopRotate::rotateLoop(Loop& loop, ir::Function& func) {
    if (!loop.header) return false;

    // Ensure preheader exists
    if (!loop.preheader) {
        LoopInvariantCodeMotion licm;
        licm.getOrCreatePreheader(loop, func);
    }
    ir::BasicBlock* preheader = loop.preheader;
    if (!preheader) return false;

    ir::BasicBlock* header = loop.header;
    if (header->getInstructions().empty()) return false;

    // Do not rotate complex vector/versioned loops with > 3 blocks
    if (loop.blocks.size() > 3) return false;

    // Do not rotate vector loops, vector epilogues, or scalar fallbacks created by LoopVectorizer
    std::string headerName = header->getName();
    if (headerName.find("v_") != std::string::npos ||
        headerName.find("epi_") != std::string::npos ||
        headerName.find("alias.") != std::string::npos) {
        return false;
    }

    ir::Instruction* headerTerm = header->getInstructions().back().get();
    if (!headerTerm) return false;

    ir::Instruction::Opcode termOp = headerTerm->getOpcode();
    if (termOp != ir::Instruction::Jnz && termOp != ir::Instruction::Jz && termOp != ir::Instruction::Br) {
        return false; // Header terminator must be a top-tested conditional branch
    }

    if (headerTerm->getOperands().size() < 3) return false;
    ir::Value* condVal = headerTerm->getOperands()[0]->get();
    auto* trueDest = dynamic_cast<ir::BasicBlock*>(headerTerm->getOperands()[1]->get());
    auto* falseDest = dynamic_cast<ir::BasicBlock*>(headerTerm->getOperands()[2]->get());
    if (!condVal || !trueDest || !falseDest) return false;

    bool trueInLoop = (loop.blocks.count(trueDest) > 0);
    bool falseInLoop = (loop.blocks.count(falseDest) > 0);

    // Exactly one target must be inside loop, one outside
    if (trueInLoop == falseInLoop) return false;

    ir::BasicBlock* bodyBlock = trueInLoop ? trueDest : falseDest;
    ir::BasicBlock* exitBlock = trueInLoop ? falseDest : trueDest;

    // Identify latch block (the block in loop that jumps to header)
    ir::BasicBlock* latchBlock = nullptr;
    int latchCount = 0;
    for (ir::BasicBlock* bb : loop.blocks) {
        if (!bb || bb->getInstructions().empty()) continue;
        ir::Instruction* bbTerm = bb->getInstructions().back().get();
        if (bbTerm && bbTerm->getOpcode() == ir::Instruction::Jmp && bbTerm->getOperands().size() == 1) {
            if (bbTerm->getOperands()[0]->get() == header) {
                latchBlock = bb;
                latchCount++;
            }
        }
    }

    if (latchCount != 1 || !latchBlock) return false;

    // Reject loops where header == latchBlock or bodyBlock == latchBlock or header == bodyBlock
    if (header == latchBlock || bodyBlock == latchBlock || header == bodyBlock) return false;

    // Safety check on non-PHI instructions in header
    std::vector<ir::Instruction*> headerNonPhis;
    for (auto& instPtr : header->getInstructions()) {
        ir::Instruction* inst = instPtr.get();
        if (!inst || dynamic_cast<ir::PhiNode*>(inst)) continue;
        if (inst == headerTerm) break;

        ir::Instruction::Opcode op = inst->getOpcode();
        if (op == ir::Instruction::Call || op == ir::Instruction::Syscall ||
            op == ir::Instruction::ExternCall || op == ir::Instruction::Store ||
            op == ir::Instruction::Stored || op == ir::Instruction::Stores ||
            op == ir::Instruction::Storel || op == ir::Instruction::Storeh ||
            op == ir::Instruction::Storeb || op == ir::Instruction::Alloc ||
            op == ir::Instruction::Alloc4 || op == ir::Instruction::Alloc16) {
            return false; // Disallow side-effecting instructions in header condition calculation
        }
        headerNonPhis.push_back(inst);
    }

    // Collect header PHIs
    std::vector<ir::PhiNode*> headerPhis;
    for (auto& instPtr : header->getInstructions()) {
        if (auto* phi = dynamic_cast<ir::PhiNode*>(instPtr.get())) {
            headerPhis.push_back(phi);
        } else {
            break;
        }
    }
    if (headerPhis.empty()) return false;

    // Map initial values (from preheader) and loop-carried values (from latchBlock)
    std::map<ir::PhiNode*, ir::Value*> phiInitMap;
    std::map<ir::PhiNode*, ir::Value*> phiLatchMap;

    for (ir::PhiNode* phi : headerPhis) {
        ir::Value* initVal = nullptr;
        ir::Value* latchVal = nullptr;

        for (size_t i = 0; i + 1 < phi->getOperands().size(); i += 2) {
            ir::Value* pBlock = phi->getOperands()[i] ? phi->getOperands()[i]->get() : nullptr;
            ir::Value* pVal = phi->getOperands()[i + 1] ? phi->getOperands()[i + 1]->get() : nullptr;
            if (!pBlock) {
                pBlock = phi->getOperands()[i + 1] ? phi->getOperands()[i + 1]->get() : nullptr;
                pVal = phi->getOperands()[i] ? phi->getOperands()[i]->get() : nullptr;
            }
            auto* predBB = dynamic_cast<ir::BasicBlock*>(pBlock);
            if (!predBB || !pVal) continue;

            if (predBB == preheader) {
                initVal = pVal;
            } else if (loop.blocks.count(predBB) > 0) {
                latchVal = pVal;
            }
        }
        if (!initVal || !latchVal) return false;
        phiInitMap[phi] = initVal;
        phiLatchMap[phi] = latchVal;
    }

    // --- STEP 1: Guard in Preheader ---
    if (!preheader->getInstructions().empty()) {
        preheader->getInstructions().pop_back();
    }

    std::map<ir::Value*, ir::Value*> preheaderMap;
    for (ir::PhiNode* phi : headerPhis) {
        preheaderMap[phi] = phiInitMap[phi];
    }

    for (ir::Instruction* inst : headerNonPhis) {
        std::vector<ir::Value*> newOps;
        for (const auto& opUse : inst->getOperands()) {
            ir::Value* origOp = opUse ? opUse->get() : nullptr;
            ir::Value* newOp = (origOp && preheaderMap.count(origOp)) ? preheaderMap[origOp] : origOp;
            newOps.push_back(newOp);
        }
        auto cloned = std::make_unique<ir::Instruction>(inst->getType(), inst->getOpcode(), newOps, preheader);
        cloned->setName(inst->getName() + ".init");
        preheaderMap[inst] = cloned.get();
        preheader->getInstructions().push_back(std::move(cloned));
    }

    ir::Value* condInit = (preheaderMap.count(condVal)) ? preheaderMap[condVal] : condVal;
    ir::BasicBlock* preheaderTrue = trueInLoop ? bodyBlock : exitBlock;
    ir::BasicBlock* preheaderFalse = trueInLoop ? exitBlock : bodyBlock;

    auto guardBr = std::make_unique<ir::Instruction>(
        ir::VoidType::get(), termOp, std::vector<ir::Value*>{condInit, preheaderTrue, preheaderFalse}, preheader);
    preheader->getInstructions().push_back(std::move(guardBr));

    // --- STEP 2: Create PHIs in bodyBlock ---
    std::map<ir::PhiNode*, ir::PhiNode*> newBodyPhiMap;
    std::vector<std::unique_ptr<ir::Instruction>> newBodyPhis;

    for (ir::PhiNode* origPhi : headerPhis) {
        auto newPhi = std::make_unique<ir::PhiNode>(origPhi->getType(), 0, origPhi->getVariable(), bodyBlock);
        newPhi->setName(origPhi->getName());
        newPhi->addIncoming(phiInitMap[origPhi], preheader);
        newPhi->addIncoming(phiLatchMap[origPhi], latchBlock);
        newBodyPhiMap[origPhi] = newPhi.get();
        newBodyPhis.push_back(std::move(newPhi));
    }

    // Insert new PHIs at top of bodyBlock
    for (auto it = newBodyPhis.rbegin(); it != newBodyPhis.rend(); ++it) {
        bodyBlock->getInstructions().insert(bodyBlock->getInstructions().begin(), std::move(*it));
    }

    // --- STEP 3: Latch Back-Edge Conditional Branch ---
    if (!latchBlock->getInstructions().empty()) {
        latchBlock->getInstructions().pop_back();
    }

    std::map<ir::Value*, ir::Value*> latchMap;
    for (ir::PhiNode* origPhi : headerPhis) {
        latchMap[origPhi] = phiLatchMap[origPhi];
    }

    for (ir::Instruction* inst : headerNonPhis) {
        std::vector<ir::Value*> newOps;
        for (const auto& opUse : inst->getOperands()) {
            ir::Value* origOp = opUse ? opUse->get() : nullptr;
            ir::Value* newOp = origOp;
            if (origOp && latchMap.count(origOp)) {
                newOp = latchMap[origOp];
            } else if (auto* origPhi = dynamic_cast<ir::PhiNode*>(origOp)) {
                if (phiLatchMap.count(origPhi)) {
                    newOp = phiLatchMap[origPhi];
                }
            }
            newOps.push_back(newOp);
        }
        auto cloned = std::make_unique<ir::Instruction>(inst->getType(), inst->getOpcode(), newOps, latchBlock);
        cloned->setName(inst->getName() + ".latch");
        latchMap[inst] = cloned.get();
        latchBlock->getInstructions().push_back(std::move(cloned));
    }

    ir::Value* condLatch = (latchMap.count(condVal)) ? latchMap[condVal] : condVal;
    ir::BasicBlock* latchTrue = trueInLoop ? bodyBlock : exitBlock;
    ir::BasicBlock* latchFalse = trueInLoop ? exitBlock : bodyBlock;

    auto latchBr = std::make_unique<ir::Instruction>(
        ir::VoidType::get(), termOp, std::vector<ir::Value*>{condLatch, latchTrue, latchFalse}, latchBlock);
    latchBlock->getInstructions().push_back(std::move(latchBr));

    // --- STEP 4: Rewrite Exit PHIs and Live-Out Uses ---
    std::map<ir::Value*, ir::PhiNode*> exitPhiMap;

    auto getOrCreateExitPhi = [&](ir::Value* val, ir::Value* initV, ir::Value* latchV) -> ir::PhiNode* {
        if (exitPhiMap.count(val)) return exitPhiMap[val];
        auto exitPhi = std::make_unique<ir::PhiNode>(val->getType(), 0, nullptr, exitBlock);
        exitPhi->setName(val->getName() + ".exit");
        exitPhi->addIncoming(initV, preheader);
        exitPhi->addIncoming(latchV, latchBlock);
        ir::PhiNode* exitPhiPtr = exitPhi.get();
        exitBlock->getInstructions().insert(exitBlock->getInstructions().begin(), std::move(exitPhi));
        exitPhiMap[val] = exitPhiPtr;
        return exitPhiPtr;
    };

    // Update existing PHIs in exitBlock
    for (auto& instPtr : exitBlock->getInstructions()) {
        if (auto* exitPhi = dynamic_cast<ir::PhiNode*>(instPtr.get())) {
            for (size_t i = 0; i + 1 < exitPhi->getOperands().size(); i += 2) {
                ir::Value* pBlock = exitPhi->getOperands()[i] ? exitPhi->getOperands()[i]->get() : nullptr;
                if (pBlock == header) {
                    ir::Value* val = exitPhi->getOperands()[i + 1] ? exitPhi->getOperands()[i + 1]->get() : nullptr;
                    ir::Value* initV = val;
                    ir::Value* latchV = val;

                    if (preheaderMap.count(val)) initV = preheaderMap[val];
                    if (latchMap.count(val)) latchV = latchMap[val];

                    if (auto* phiVal = dynamic_cast<ir::PhiNode*>(val)) {
                        if (phiInitMap.count(phiVal)) initV = phiInitMap[phiVal];
                        if (phiLatchMap.count(phiVal)) latchV = phiLatchMap[phiVal];
                    }

                    exitPhi->getOperands()[i]->set(latchBlock);
                    exitPhi->getOperands()[i + 1]->set(latchV);
                    exitPhi->addIncoming(initV, preheader);
                    break;
                }
            }
        } else {
            break;
        }
    }

    // Rewrite all remaining uses of header PHIs
    for (ir::PhiNode* origPhi : headerPhis) {
        std::vector<ir::Use*> uses(origPhi->getUseList().begin(), origPhi->getUseList().end());
        ir::Value* initV = phiInitMap[origPhi];
        ir::Value* latchV = phiLatchMap[origPhi];
        ir::PhiNode* newBodyPhi = newBodyPhiMap[origPhi];

        for (ir::Use* u : uses) {
            if (!u || !u->getUser()) continue;
            auto* userInst = dynamic_cast<ir::Instruction*>(u->getUser());
            if (!userInst) continue;
            ir::BasicBlock* userBB = userInst->getParent();

            if (userBB == preheader) {
                u->set(initV);
            } else if (userBB == latchBlock && latchMap.count(userInst)) {
                u->set(latchV);
            } else if (loop.blocks.count(userBB) > 0) {
                u->set(newBodyPhi);
            } else {
                // Live-out use outside loop
                ir::PhiNode* exitPhi = getOrCreateExitPhi(origPhi, initV, latchV);
                u->set(exitPhi);
            }
        }
    }

    // Rewrite all remaining uses of headerNonPhis
    for (ir::Instruction* inst : headerNonPhis) {
        std::vector<ir::Use*> uses(inst->getUseList().begin(), inst->getUseList().end());
        ir::Value* preVal = preheaderMap.count(inst) ? preheaderMap[inst] : nullptr;
        ir::Value* latVal = latchMap.count(inst) ? latchMap[inst] : nullptr;

        ir::PhiNode* bodyPhi = nullptr;
        ir::PhiNode* exitPhi = nullptr;

        for (ir::Use* u : uses) {
            if (!u || !u->getUser()) continue;
            auto* userInst = dynamic_cast<ir::Instruction*>(u->getUser());
            if (!userInst) continue;
            ir::BasicBlock* userBB = userInst->getParent();

            if (userBB == preheader) {
                if (preVal) u->set(preVal);
            } else if (userBB == latchBlock && latchMap.count(userInst)) {
                if (latVal) u->set(latVal);
            } else if (loop.blocks.count(userBB) > 0) {
                if (preVal && latVal) {
                    if (!bodyPhi) {
                        auto bPhi = std::make_unique<ir::PhiNode>(inst->getType(), 0, nullptr, bodyBlock);
                        bPhi->setName(inst->getName() + ".bodyphi");
                        bPhi->addIncoming(preVal, preheader);
                        bPhi->addIncoming(latVal, latchBlock);
                        bodyPhi = bPhi.get();
                        auto insIt = bodyBlock->getInstructions().begin();
                        std::advance(insIt, newBodyPhis.size());
                        bodyBlock->getInstructions().insert(insIt, std::move(bPhi));
                    }
                    u->set(bodyPhi);
                }
            } else {
                if (preVal && latVal) {
                    if (!exitPhi) exitPhi = getOrCreateExitPhi(inst, preVal, latVal);
                    u->set(exitPhi);
                }
            }
        }
    }

    // Update any pre-existing PHI nodes in bodyBlock or exitBlock that reference header as an incoming block
    for (ir::BasicBlock* targetBB : {bodyBlock, exitBlock}) {
        for (auto& instPtr : targetBB->getInstructions()) {
            if (auto* phi = dynamic_cast<ir::PhiNode*>(instPtr.get())) {
                for (size_t i = 0; i + 1 < phi->getOperands().size(); i += 2) {
                    ir::Value* pBlock = phi->getOperands()[i] ? phi->getOperands()[i]->get() : nullptr;
                    if (pBlock == header) {
                        phi->getOperands()[i]->set(preheader);
                    }
                }
            } else {
                break;
            }
        }
    }

    // --- STEP 5: Erase header from function & loop blocks ---
    auto& blocks = func.getBasicBlocks();
    for (auto it = blocks.begin(); it != blocks.end(); ++it) {
        if (it->get() == header) {
            blocks.erase(it);
            break;
        }
    }
    loop.blocks.erase(header);
    loop.header = bodyBlock;

    return true;
}

} // namespace transforms
