#include "transforms/LoopUnroll.h"
#include "transforms/LoopInvariantCodeMotion.h"
#include "transforms/CFGBuilder.h"
#include "ir/IRBuilder.h"
#include "ir/Constant.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include "ir/Validator.h"
#include <algorithm>
#include <vector>
#include <set>
#include <map>
#include <iostream>

namespace transforms {

static ir::Value* stripExtensions(ir::Value* val) {
    while (auto* inst = dynamic_cast<ir::Instruction*>(val)) {
        ir::Instruction::Opcode op = inst->getOpcode();
        if (op == ir::Instruction::ExtSW || op == ir::Instruction::ExtUW) {
            if (!inst->getOperands().empty() && inst->getOperands()[0]) {
                val = inst->getOperands()[0]->get();
            } else break;
        } else break;
    }
    return val;
}

bool LoopUnroll::performTransformation(ir::Function& func) {
    if (func.getBasicBlocks().empty()) return false;

    LoopInvariantCodeMotion licm;
    std::vector<std::unique_ptr<Loop>> loops;
    licm.findLoops(func, loops);

    bool changed = false;
    for (auto& loopPtr : loops) {
        IndVarInfo ivInfo;
        if (analyzeLoopLegality(*loopPtr, func, ivInfo)) {
            if (unrollLoop(*loopPtr, func, ivInfo)) {
                changed = true;
                break; // Transform one loop per pass iteration for CFG safety
            }
        }
    }

    if (changed) {
        CFGBuilder::run(func);
    }
    return changed;
}

bool LoopUnroll::analyzeLoopLegality(Loop& loop, ir::Function& func, IndVarInfo& ivInfo) {
    if (!loop.header) return false;

    // Ensure preheader exists or can be created
    if (!loop.preheader) {
        LoopInvariantCodeMotion licm;
        licm.getOrCreatePreheader(loop, func);
    }
    if (!loop.preheader) return false;

    // Single exit requirement
    if (loop.exits.size() != 1) return false;

    // Conservative contract: single natural loop body (at most 2 blocks: header and latch)
    if (loop.blocks.size() > 2) return false;

    // Check for nested child loops
    if (!loop.children.empty()) return false;

    // Safety checks on all instructions in loop blocks
    for (ir::BasicBlock* bb : loop.blocks) {
        if (!bb) return false;
        for (auto& instPtr : bb->getInstructions()) {
            ir::Instruction* inst = instPtr.get();
            if (!inst) continue;

            ir::Instruction::Opcode op = inst->getOpcode();
            // Disallow calls and unsupported side effects
            if (op == ir::Instruction::Call || op == ir::Instruction::Syscall ||
                op == ir::Instruction::ExternCall) {
                return false;
            }
        }
    }

    // Identify induction variable PHI in header
    for (auto& instPtr : loop.header->getInstructions()) {
        auto* phi = dynamic_cast<ir::PhiNode*>(instPtr.get());
        if (!phi) break; // PHIs are at beginning of block

        ir::Value* initVal = nullptr;
        ir::Value* stepNextVal = nullptr;

        for (size_t i = 0; i + 1 < phi->getOperands().size(); i += 2) {
            ir::Value* pBlock = phi->getOperands()[i] ? phi->getOperands()[i]->get() : nullptr;
            ir::Value* pVal = phi->getOperands()[i + 1] ? phi->getOperands()[i + 1]->get() : nullptr;
            if (!pBlock) {
                pBlock = phi->getOperands()[i + 1] ? phi->getOperands()[i + 1]->get() : nullptr;
                pVal = phi->getOperands()[i] ? phi->getOperands()[i]->get() : nullptr;
            }
            auto* predBB = dynamic_cast<ir::BasicBlock*>(pBlock);
            if (!predBB || !pVal) continue;

            if (loop.blocks.find(predBB) == loop.blocks.end()) {
                initVal = pVal;
            } else {
                stepNextVal = pVal;
            }
        }

        if (!initVal || !stepNextVal) continue;

        auto* stepInst = dynamic_cast<ir::Instruction*>(stripExtensions(stepNextVal));
        if (!stepInst) continue;

        ir::Instruction::Opcode op = stepInst->getOpcode();
        int64_t stepVal = 0;
        bool stepAddedToIV = true;

        if (op == ir::Instruction::Add && stepInst->getOperands().size() >= 2) {
            ir::Value* op0 = stripExtensions(stepInst->getOperands()[0]->get());
            ir::Value* op1 = stripExtensions(stepInst->getOperands()[1]->get());
            if (op0 == phi) {
                if (auto* c1 = dynamic_cast<ir::ConstantInt*>(op1)) {
                    stepVal = c1->getValue();
                    stepAddedToIV = true;
                }
            } else if (op1 == phi) {
                if (auto* c0 = dynamic_cast<ir::ConstantInt*>(op0)) {
                    stepVal = c0->getValue();
                    stepAddedToIV = true;
                }
            }
        } else if (op == ir::Instruction::Sub && stepInst->getOperands().size() >= 2) {
            ir::Value* op0 = stripExtensions(stepInst->getOperands()[0]->get());
            ir::Value* op1 = stripExtensions(stepInst->getOperands()[1]->get());
            if (op0 == phi) {
                if (auto* c1 = dynamic_cast<ir::ConstantInt*>(op1)) {
                    stepVal = -c1->getValue();
                    stepAddedToIV = true;
                }
            }
        }

        if (stepVal == 0) continue;

        // Check for comparison in header or latch against this IV
        for (ir::BasicBlock* bb : loop.blocks) {
            for (auto& instPtr : bb->getInstructions()) {
                ir::Instruction* hInst = instPtr.get();
                if (!hInst) continue;

                ir::Instruction::Opcode hOp = hInst->getOpcode();
                if (hOp == ir::Instruction::Cslt || hOp == ir::Instruction::Cult ||
                    hOp == ir::Instruction::Csle || hOp == ir::Instruction::Cule ||
                    hOp == ir::Instruction::Csgt || hOp == ir::Instruction::Cuge ||
                    hOp == ir::Instruction::Csge || hOp == ir::Instruction::Cugt) {

                    if (hInst->getOperands().size() >= 2 && hInst->getOperands()[0] && hInst->getOperands()[1]) {
                        ir::Value* condOp0 = stripExtensions(hInst->getOperands()[0]->get());
                        ir::Value* condOp1 = stripExtensions(hInst->getOperands()[1]->get());

                        bool usesIV = (condOp0 == phi);
                        bool usesStepInst = (condOp0 == stepInst);

                        if (usesIV || usesStepInst) {
                            // Verify boundVal is defined outside loop
                            ir::Value* boundVal = condOp1;
                            if (auto* bInst = dynamic_cast<ir::Instruction*>(boundVal)) {
                                if (loop.blocks.find(bInst->getParent()) != loop.blocks.end()) {
                                    continue; // Bound is defined inside loop
                                }
                            }

                            ivInfo.phi = phi;
                            ivInfo.stepInst = stepInst;
                            ivInfo.initVal = initVal;
                            ivInfo.stepVal = stepVal;
                            ivInfo.condInst = hInst;
                            ivInfo.boundVal = boundVal;
                            ivInfo.cmpOpcode = hOp;
                            ivInfo.stepAddedToIV = stepAddedToIV;
                            ivInfo.condUsesIV = usesIV;
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

bool LoopUnroll::unrollLoop(Loop& loop, ir::Function& func, const IndVarInfo& ivInfo) {
    ir::BasicBlock* header = loop.header;
    ir::BasicBlock* preheader = loop.preheader;

    // Determine exit block
    ir::BasicBlock* origExitBB = nullptr;
    ir::BasicBlock* latch = nullptr;

    for (ir::BasicBlock* exitCandidate : loop.exits) {
        for (ir::BasicBlock* succ : exitCandidate->getSuccessors()) {
            if (loop.blocks.find(succ) == loop.blocks.end()) {
                origExitBB = succ;
                break;
            }
        }
        if (origExitBB) break;
    }

    if (!origExitBB) return false;

    // Identify latch block (the block with back-edge to header)
    for (ir::BasicBlock* bb : loop.blocks) {
        for (ir::BasicBlock* succ : bb->getSuccessors()) {
            if (succ == header) {
                latch = bb;
                break;
            }
        }
        if (latch) break;
    }
    if (!latch) latch = header;

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

    // Map each header PHI to its initial value from preheader and step value from latch
    std::map<ir::PhiNode*, ir::Value*> phiInitMap;
    std::map<ir::PhiNode*, ir::Value*> phiLatchMap;

    for (ir::PhiNode* phi : headerPhis) {
        ir::Value* initV = nullptr;
        ir::Value* latchV = nullptr;

        for (size_t i = 0; i + 1 < phi->getOperands().size(); i += 2) {
            ir::Value* pBlock = phi->getOperands()[i] ? phi->getOperands()[i]->get() : nullptr;
            ir::Value* pVal = phi->getOperands()[i + 1] ? phi->getOperands()[i + 1]->get() : nullptr;
            if (!pBlock) {
                pBlock = phi->getOperands()[i + 1] ? phi->getOperands()[i + 1]->get() : nullptr;
                pVal = phi->getOperands()[i] ? phi->getOperands()[i]->get() : nullptr;
            }
            auto* predBB = dynamic_cast<ir::BasicBlock*>(pBlock);
            if (!predBB || !pVal) continue;

            if (loop.blocks.find(predBB) == loop.blocks.end()) {
                initV = pVal;
            } else {
                latchV = pVal;
            }
        }

        if (!initV || !latchV) return false;
        phiInitMap[phi] = initV;
        phiLatchMap[phi] = latchV;
    }

    // Create new basic blocks
    std::string prefix = header->getName() + ".unroll";
    auto unrolledHeader = std::make_unique<ir::BasicBlock>(&func, prefix + ".header");
    auto body0 = std::make_unique<ir::BasicBlock>(&func, prefix + ".body0");
    auto body1 = std::make_unique<ir::BasicBlock>(&func, prefix + ".body1");
    auto epilogueHeader = std::make_unique<ir::BasicBlock>(&func, prefix + ".epi.header");
    auto epilogueBody = std::make_unique<ir::BasicBlock>(&func, prefix + ".epi.body");
    auto finalExit = std::make_unique<ir::BasicBlock>(&func, prefix + ".final.exit");

    ir::BasicBlock* uHeaderPtr = unrolledHeader.get();
    ir::BasicBlock* b0Ptr = body0.get();
    ir::BasicBlock* b1Ptr = body1.get();
    ir::BasicBlock* epiHeaderPtr = epilogueHeader.get();
    ir::BasicBlock* epiBodyPtr = epilogueBody.get();
    ir::BasicBlock* finalExitPtr = finalExit.get();

    // 1. Setup PHIs in unrolledHeader
    std::map<ir::PhiNode*, ir::PhiNode*> uHeaderPhiMap;
    for (ir::PhiNode* origPhi : headerPhis) {
        auto uPhi = std::make_unique<ir::PhiNode>(origPhi->getType(), 0, origPhi->getVariable(), uHeaderPtr);
        uPhi->setName(origPhi->getName() + ".2x");
        uPhi->addIncoming(phiInitMap[origPhi], preheader);
        uHeaderPhiMap[origPhi] = uPhi.get();
        uHeaderPtr->addInstruction(std::move(uPhi));
    }

    // 2. Compute 2x exit condition in unrolledHeader
    ir::PhiNode* uIVPhi = uHeaderPhiMap[ivInfo.phi];
    ir::Type* ivType = uIVPhi->getType();
    auto* intIvType = dynamic_cast<ir::IntegerType*>(ivType);
    if (!intIvType) intIvType = ir::IntegerType::get(32);
    ir::ConstantInt* stepConst = ir::ConstantInt::get(intIvType, ivInfo.stepVal);

    // Calculate second iteration's IV value for condition checking
    auto addStepInst = std::make_unique<ir::Instruction>(ivType, ir::Instruction::Add,
                                                         std::vector<ir::Value*>{uIVPhi, stepConst}, uHeaderPtr);
    addStepInst->setName(uIVPhi->getName() + ".step1");
    ir::Value* condIVVal = addStepInst.get();
    if (!ivInfo.condUsesIV) {
        // If condition used stepInst, calculate i + 2*step
        ir::ConstantInt* twoStepConst = ir::ConstantInt::get(intIvType, ivInfo.stepVal * 2);
        auto add2StepInst = std::make_unique<ir::Instruction>(ivType, ir::Instruction::Add,
                                                             std::vector<ir::Value*>{uIVPhi, twoStepConst}, uHeaderPtr);
        add2StepInst->setName(uIVPhi->getName() + ".step2");
        condIVVal = add2StepInst.get();
        uHeaderPtr->addInstruction(std::move(add2StepInst));
    } else {
        uHeaderPtr->addInstruction(std::move(addStepInst));
    }

    auto cond2xInst = std::make_unique<ir::Instruction>(ir::IntegerType::get(1), ivInfo.cmpOpcode,
                                                         std::vector<ir::Value*>{condIVVal, ivInfo.boundVal}, uHeaderPtr);
    cond2xInst->setName(ivInfo.condInst->getName() + ".2x");
    ir::Instruction* cond2xPtr = cond2xInst.get();
    uHeaderPtr->addInstruction(std::move(cond2xInst));

    auto br2x = std::make_unique<ir::Instruction>(ir::VoidType::get(), ir::Instruction::Jnz,
                                                   std::vector<ir::Value*>{cond2xPtr, b0Ptr, epiHeaderPtr}, uHeaderPtr);
    uHeaderPtr->addInstruction(std::move(br2x));

    // 3. Clone Body 0
    std::map<ir::Value*, ir::Value*> map0;
    for (ir::PhiNode* origPhi : headerPhis) {
        map0[origPhi] = uHeaderPhiMap[origPhi];
    }
    ValueCloner cloner0(map0);

    for (ir::BasicBlock* bb : std::vector<ir::BasicBlock*>{header, latch}) {
        if (!bb) continue;
        for (auto& instPtr : bb->getInstructions()) {
            ir::Instruction* inst = instPtr.get();
            if (!inst || dynamic_cast<ir::PhiNode*>(inst)) continue;
            ir::Instruction::Opcode op = inst->getOpcode();
            if (op == ir::Instruction::Jmp || op == ir::Instruction::Jnz ||
                op == ir::Instruction::Jz  || op == ir::Instruction::Br) continue;

            auto cloned = cloner0.cloneInstruction(inst, b0Ptr);
            map0[inst] = cloned.get();
            b0Ptr->addInstruction(std::move(cloned));
        }
        if (header == latch) break;
    }
    auto jmpToB1 = std::make_unique<ir::Instruction>(ir::VoidType::get(), ir::Instruction::Jmp,
                                                      std::vector<ir::Value*>{b1Ptr}, b0Ptr);
    b0Ptr->addInstruction(std::move(jmpToB1));

    // 4. Clone Body 1
    std::map<ir::Value*, ir::Value*> map1;
    for (ir::PhiNode* origPhi : headerPhis) {
        ir::Value* latchVal0 = map0[phiLatchMap[origPhi]];
        map1[origPhi] = latchVal0 ? latchVal0 : phiLatchMap[origPhi];
    }
    ValueCloner cloner1(map1);

    for (ir::BasicBlock* bb : std::vector<ir::BasicBlock*>{header, latch}) {
        if (!bb) continue;
        for (auto& instPtr : bb->getInstructions()) {
            ir::Instruction* inst = instPtr.get();
            if (!inst || dynamic_cast<ir::PhiNode*>(inst)) continue;
            ir::Instruction::Opcode op = inst->getOpcode();
            if (op == ir::Instruction::Jmp || op == ir::Instruction::Jnz ||
                op == ir::Instruction::Jz  || op == ir::Instruction::Br) continue;

            auto cloned = cloner1.cloneInstruction(inst, b1Ptr);
            map1[inst] = cloned.get();
            b1Ptr->addInstruction(std::move(cloned));
        }
        if (header == latch) break;
    }
    auto jmpToHeader = std::make_unique<ir::Instruction>(ir::VoidType::get(), ir::Instruction::Jmp,
                                                          std::vector<ir::Value*>{uHeaderPtr}, b1Ptr);
    b1Ptr->addInstruction(std::move(jmpToHeader));

    // Complete back-edge incoming operands for uHeader PHIs
    for (ir::PhiNode* origPhi : headerPhis) {
        ir::PhiNode* uPhi = uHeaderPhiMap[origPhi];
        ir::Value* latchVal1 = map1[phiLatchMap[origPhi]];
        uPhi->addIncoming(latchVal1 ? latchVal1 : phiLatchMap[origPhi], b1Ptr);
    }

    // 5. Setup Epilogue Header
    std::map<ir::PhiNode*, ir::PhiNode*> epiPhiMap;
    for (ir::PhiNode* origPhi : headerPhis) {
        auto epiPhi = std::make_unique<ir::PhiNode>(origPhi->getType(), 0, origPhi->getVariable(), epiHeaderPtr);
        epiPhi->setName(origPhi->getName() + ".epi");
        epiPhi->addIncoming(uHeaderPhiMap[origPhi], uHeaderPtr);
        epiPhiMap[origPhi] = epiPhi.get();
        epiHeaderPtr->addInstruction(std::move(epiPhi));
    }

    ir::PhiNode* epiIVPhi = epiPhiMap[ivInfo.phi];
    ir::Value* epiCondIVVal = epiIVPhi;
    if (!ivInfo.condUsesIV) {
        auto addEpiStep = std::make_unique<ir::Instruction>(ivType, ir::Instruction::Add,
                                                            std::vector<ir::Value*>{epiIVPhi, stepConst}, epiHeaderPtr);
        addEpiStep->setName(epiIVPhi->getName() + ".epi.step");
        epiCondIVVal = addEpiStep.get();
        epiHeaderPtr->addInstruction(std::move(addEpiStep));
    }

    auto condEpiInst = std::make_unique<ir::Instruction>(ir::IntegerType::get(1), ivInfo.cmpOpcode,
                                                          std::vector<ir::Value*>{epiCondIVVal, ivInfo.boundVal}, epiHeaderPtr);
    condEpiInst->setName(ivInfo.condInst->getName() + ".epi");
    ir::Instruction* condEpiPtr = condEpiInst.get();
    epiHeaderPtr->addInstruction(std::move(condEpiInst));

    auto brEpi = std::make_unique<ir::Instruction>(ir::VoidType::get(), ir::Instruction::Jnz,
                                                    std::vector<ir::Value*>{condEpiPtr, epiBodyPtr, finalExitPtr}, epiHeaderPtr);
    epiHeaderPtr->addInstruction(std::move(brEpi));

    // 6. Clone Epilogue Body
    std::map<ir::Value*, ir::Value*> mapEpi;
    for (ir::PhiNode* origPhi : headerPhis) {
        mapEpi[origPhi] = epiPhiMap[origPhi];
    }
    ValueCloner clonerEpi(mapEpi);

    for (ir::BasicBlock* bb : std::vector<ir::BasicBlock*>{header, latch}) {
        if (!bb) continue;
        for (auto& instPtr : bb->getInstructions()) {
            ir::Instruction* inst = instPtr.get();
            if (!inst || dynamic_cast<ir::PhiNode*>(inst)) continue;
            ir::Instruction::Opcode op = inst->getOpcode();
            if (op == ir::Instruction::Jmp || op == ir::Instruction::Jnz ||
                op == ir::Instruction::Jz  || op == ir::Instruction::Br) continue;

            auto cloned = clonerEpi.cloneInstruction(inst, epiBodyPtr);
            mapEpi[inst] = cloned.get();
            epiBodyPtr->addInstruction(std::move(cloned));
        }
        if (header == latch) break;
    }
    auto jmpToFinalExit = std::make_unique<ir::Instruction>(ir::VoidType::get(), ir::Instruction::Jmp,
                                                             std::vector<ir::Value*>{finalExitPtr}, epiBodyPtr);
    epiBodyPtr->addInstruction(std::move(jmpToFinalExit));

    // 7. Setup Final Exit Block and PHI values for outside uses
    std::map<ir::Value*, ir::PhiNode*> finalExitPhiMap;
    std::set<ir::Value*> definedInLoop;
    for (ir::BasicBlock* bb : loop.blocks) {
        for (auto& instPtr : bb->getInstructions()) {
            if (instPtr) definedInLoop.insert(instPtr.get());
        }
    }

    for (ir::Value* val : definedInLoop) {
        // Check if val is used outside loop
        bool usedOutside = false;
        for (ir::Use* u : val->getUseList()) {
            if (!u || !u->getUser()) continue;
            auto* userInst = dynamic_cast<ir::Instruction*>(u->getUser());
            if (userInst && userInst->getParent()) {
                if (loop.blocks.find(userInst->getParent()) == loop.blocks.end()) {
                    usedOutside = true;
                    break;
                }
            }
        }

        if (usedOutside) {
            auto finalPhi = std::make_unique<ir::PhiNode>(val->getType(), 0, nullptr, finalExitPtr);
            finalPhi->setName(val->getName() + ".final");

            ir::Value* valEpiHeader = nullptr;
            if (auto* phiVal = dynamic_cast<ir::PhiNode*>(val)) {
                valEpiHeader = epiPhiMap[phiVal];
            } else if (map1.count(val)) {
                valEpiHeader = map1[val];
            } else if (map0.count(val)) {
                valEpiHeader = map0[val];
            } else {
                valEpiHeader = val;
            }

            ir::Value* valEpiBody = nullptr;
            if (mapEpi.count(val)) {
                valEpiBody = mapEpi[val];
            } else if (auto* phiVal = dynamic_cast<ir::PhiNode*>(val)) {
                valEpiBody = epiPhiMap[phiVal];
            } else {
                valEpiBody = valEpiHeader;
            }

            finalPhi->addIncoming(valEpiHeader, epiHeaderPtr);
            finalPhi->addIncoming(valEpiBody, epiBodyPtr);
            finalExitPhiMap[val] = finalPhi.get();
            finalExitPtr->addInstruction(std::move(finalPhi));
        }
    }

    auto jmpToOrigExit = std::make_unique<ir::Instruction>(ir::VoidType::get(), ir::Instruction::Jmp,
                                                            std::vector<ir::Value*>{origExitBB}, finalExitPtr);
    finalExitPtr->addInstruction(std::move(jmpToOrigExit));

    // Update outside uses of loop values
    for (auto& pair : finalExitPhiMap) {
        ir::Value* origVal = pair.first;
        ir::PhiNode* finalPhi = pair.second;

        std::vector<ir::Use*> usesToReplace;
        for (ir::Use* u : origVal->getUseList()) {
            if (!u || !u->getUser()) continue;
            auto* userInst = dynamic_cast<ir::Instruction*>(u->getUser());
            if (userInst && userInst->getParent()) {
                if (loop.blocks.find(userInst->getParent()) == loop.blocks.end() &&
                    userInst->getParent() != finalExitPtr) {
                    usesToReplace.push_back(u);
                }
            }
        }

        for (ir::Use* u : usesToReplace) {
            u->set(finalPhi);
        }
    }

    // Redirect preheader terminator to unrolledHeader
    auto& preheaderInstrs = preheader->getInstructions();
    if (!preheaderInstrs.empty()) {
        ir::Instruction* term = preheaderInstrs.back().get();
        for (auto& op : term->getOperands()) {
            if (op && op->get() == header) {
                op->set(uHeaderPtr);
            }
        }
    }

    // Update origExitBB PHIs if any reference header or latch
    for (auto& instPtr : origExitBB->getInstructions()) {
        if (auto* origExitPhi = dynamic_cast<ir::PhiNode*>(instPtr.get())) {
            for (size_t i = 0; i + 1 < origExitPhi->getOperands().size(); i += 2) {
                ir::Value* pBlock = origExitPhi->getOperands()[i] ? origExitPhi->getOperands()[i]->get() : nullptr;
                if (pBlock == header || pBlock == latch || (pBlock && loop.blocks.find(dynamic_cast<ir::BasicBlock*>(pBlock)) != loop.blocks.end())) {
                    origExitPhi->getOperands()[i]->set(finalExitPtr);
                }
            }
        } else {
            break;
        }
    }

    // Remove old loop blocks from function
    auto& blocks = func.getBasicBlocks();
    for (auto it = blocks.begin(); it != blocks.end(); ) {
        if (loop.blocks.find(it->get()) != loop.blocks.end()) {
            it = blocks.erase(it);
        } else {
            ++it;
        }
    }

    // Add new blocks to function
    func.addBasicBlock(std::move(unrolledHeader));
    func.addBasicBlock(std::move(body0));
    func.addBasicBlock(std::move(body1));
    func.addBasicBlock(std::move(epilogueHeader));
    func.addBasicBlock(std::move(epilogueBody));
    func.addBasicBlock(std::move(finalExit));

    return true;
}

} // namespace transforms
