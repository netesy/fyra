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

    size_t bodyInstructionCount = 0;
    for (ir::BasicBlock* bb : loop.blocks) {
        if (bb) bodyInstructionCount += bb->getInstructions().size();
    }
    if (bodyInstructionCount > 25) return false;

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
            // Disallow FP instructions to ensure conservative preservation of FP semantics
            if (inst->getType()->isFloatTy() || inst->getType()->isDoubleTy()) {
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

    // Recognize reductions vs IV
    // A reduction PHI is a non-IV header PHI whose latch update is an Add where one operand is the PHI.
    struct ReductionInfo {
        ir::PhiNode* phi = nullptr;
        ir::Instruction* addInst = nullptr;
        ir::Value* contrib = nullptr; // value added in each iteration
        ir::Value* initVal = nullptr;
    };

    std::map<ir::PhiNode*, ReductionInfo> reductionMap;

    for (ir::PhiNode* phi : headerPhis) {
        if (phi == ivInfo.phi) continue;

        ir::Value* latchV = phiLatchMap[phi];
        auto* addInst = dynamic_cast<ir::Instruction*>(latchV);
        if (!addInst || addInst->getOpcode() != ir::Instruction::Add || addInst->getOperands().size() < 2) {
            continue;
        }

        ir::Value* op0 = addInst->getOperands()[0]->get();
        ir::Value* op1 = addInst->getOperands()[1]->get();
        ir::Value* contrib = nullptr;

        if (op0 == phi) {
            contrib = op1;
        } else if (op1 == phi) {
            contrib = op0;
        }

        if (contrib) {
            // Ensure reduction is not observed mid-loop in an order-sensitive way
            bool valid = true;
            for (ir::Use* u : phi->getUseList()) {
                if (!u || !u->getUser()) continue;
                auto* userInst = dynamic_cast<ir::Instruction*>(u->getUser());
                if (userInst && userInst != addInst) {
                    if (loop.blocks.find(userInst->getParent()) != loop.blocks.end()) {
                        valid = false;
                        break;
                    }
                }
            }
            if (valid) {
                ReductionInfo rinfo;
                rinfo.phi = phi;
                rinfo.addInst = addInst;
                rinfo.contrib = contrib;
                rinfo.initVal = phiInitMap[phi];
                reductionMap[phi] = rinfo;
            }
        }
    }

    const size_t UF = 4;

    // Create new basic blocks
    std::string prefix = header->getName() + ".unroll";
    auto unrolledHeader = std::make_unique<ir::BasicBlock>(&func, prefix + ".header");

    std::vector<std::unique_ptr<ir::BasicBlock>> bodyBlocks;
    std::vector<ir::BasicBlock*> bodyPtrs;
    for (size_t k = 0; k < UF; ++k) {
        auto b = std::make_unique<ir::BasicBlock>(&func, prefix + ".body" + std::to_string(k));
        bodyPtrs.push_back(b.get());
        bodyBlocks.push_back(std::move(b));
    }

    auto epiloguePrep = std::make_unique<ir::BasicBlock>(&func, prefix + ".epi.prep");
    auto epilogueHeader = std::make_unique<ir::BasicBlock>(&func, prefix + ".epi.header");
    auto epilogueBody = std::make_unique<ir::BasicBlock>(&func, prefix + ".epi.body");
    auto finalExit = std::make_unique<ir::BasicBlock>(&func, prefix + ".final.exit");

    ir::BasicBlock* uHeaderPtr = unrolledHeader.get();
    ir::BasicBlock* epiPrepPtr = epiloguePrep.get();
    ir::BasicBlock* epiHeaderPtr = epilogueHeader.get();
    ir::BasicBlock* epiBodyPtr = epilogueBody.get();
    ir::BasicBlock* finalExitPtr = finalExit.get();

    // 1. Setup PHIs in unrolledHeader
    std::map<ir::PhiNode*, std::vector<ir::PhiNode*>> uHeaderAccMap; // For reductions: UF accumulators
    std::map<ir::PhiNode*, ir::PhiNode*> uHeaderPhiMap;             // For IV / normal PHIs

    for (ir::PhiNode* origPhi : headerPhis) {
        if (reductionMap.count(origPhi)) {
            // Create UF accumulator PHIs
            ir::Value* zeroVal = ir::ConstantInt::get(dynamic_cast<ir::IntegerType*>(origPhi->getType()) ? static_cast<ir::IntegerType*>(origPhi->getType()) : ir::IntegerType::get(64), 0);
            for (size_t k = 0; k < UF; ++k) {
                auto accPhi = std::make_unique<ir::PhiNode>(origPhi->getType(), 0, origPhi->getVariable(), uHeaderPtr);
                accPhi->setName(origPhi->getName() + ".acc" + std::to_string(k));
                ir::Value* initV = (k == 0) ? phiInitMap[origPhi] : zeroVal;
                accPhi->addIncoming(initV, preheader);
                uHeaderAccMap[origPhi].push_back(accPhi.get());
                uHeaderPtr->addInstruction(std::move(accPhi));
            }
        } else {
            auto uPhi = std::make_unique<ir::PhiNode>(origPhi->getType(), 0, origPhi->getVariable(), uHeaderPtr);
            uPhi->setName(origPhi->getName() + "." + std::to_string(UF) + "x");
            uPhi->addIncoming(phiInitMap[origPhi], preheader);
            uHeaderPhiMap[origPhi] = uPhi.get();
            uHeaderPtr->addInstruction(std::move(uPhi));
        }
    }

    // 2. Compute UF exit condition in unrolledHeader
    ir::PhiNode* uIVPhi = uHeaderPhiMap[ivInfo.phi];
    ir::Type* ivType = uIVPhi->getType();
    auto* intIvType = dynamic_cast<ir::IntegerType*>(ivType);
    if (!intIvType) intIvType = ir::IntegerType::get(32);

    // Check if IV + (UF-1)*step satisfies loop bound
    ir::ConstantInt* ufMinus1StepConst = ir::ConstantInt::get(intIvType, ivInfo.stepVal * (int64_t)(UF - 1));
    auto addUFStepInst = std::make_unique<ir::Instruction>(ivType, ir::Instruction::Add,
                                                           std::vector<ir::Value*>{uIVPhi, ufMinus1StepConst}, uHeaderPtr);
    addUFStepInst->setName(uIVPhi->getName() + ".step.uf");
    ir::Value* condIVVal = addUFStepInst.get();
    if (!ivInfo.condUsesIV) {
        ir::ConstantInt* ufStepConst = ir::ConstantInt::get(intIvType, ivInfo.stepVal * (int64_t)UF);
        auto addUFStepInst2 = std::make_unique<ir::Instruction>(ivType, ir::Instruction::Add,
                                                              std::vector<ir::Value*>{uIVPhi, ufStepConst}, uHeaderPtr);
        addUFStepInst2->setName(uIVPhi->getName() + ".step.uf2");
        condIVVal = addUFStepInst2.get();
        uHeaderPtr->addInstruction(std::move(addUFStepInst2));
    } else {
        uHeaderPtr->addInstruction(std::move(addUFStepInst));
    }

    auto condUFInst = std::make_unique<ir::Instruction>(ir::IntegerType::get(1), ivInfo.cmpOpcode,
                                                         std::vector<ir::Value*>{condIVVal, ivInfo.boundVal}, uHeaderPtr);
    condUFInst->setName(ivInfo.condInst->getName() + "." + std::to_string(UF) + "x");
    ir::Instruction* condUFPtr = condUFInst.get();
    uHeaderPtr->addInstruction(std::move(condUFInst));

    auto brUF = std::make_unique<ir::Instruction>(ir::VoidType::get(), ir::Instruction::Jnz,
                                                   std::vector<ir::Value*>{condUFPtr, bodyPtrs[0], epiPrepPtr}, uHeaderPtr);
    uHeaderPtr->addInstruction(std::move(brUF));

    // 3. Clone Body 0..UF-1
    std::vector<std::map<ir::Value*, ir::Value*>> bodyMaps(UF);

    for (size_t k = 0; k < UF; ++k) {
        ir::BasicBlock* curBB = bodyPtrs[k];
        auto& curMap = bodyMaps[k];

        // Map PHI values for iteration k
        for (ir::PhiNode* origPhi : headerPhis) {
            if (reductionMap.count(origPhi)) {
                curMap[origPhi] = uHeaderAccMap[origPhi][k];
            } else if (origPhi == ivInfo.phi) {
                if (k == 0) {
                    curMap[origPhi] = uHeaderPhiMap[origPhi];
                } else {
                    ir::Value* prevIV = bodyMaps[k-1][phiLatchMap[origPhi]];
                    curMap[origPhi] = prevIV ? prevIV : bodyMaps[k-1][origPhi];
                }
            } else {
                if (k == 0) {
                    curMap[origPhi] = uHeaderPhiMap[origPhi];
                } else {
                    ir::Value* prevL = bodyMaps[k-1][phiLatchMap[origPhi]];
                    curMap[origPhi] = prevL ? prevL : bodyMaps[k-1][origPhi];
                }
            }
        }

        ValueCloner cloner(curMap);

        for (ir::BasicBlock* bb : std::vector<ir::BasicBlock*>{header, latch}) {
            if (!bb) continue;
            for (auto& instPtr : bb->getInstructions()) {
                ir::Instruction* inst = instPtr.get();
                if (!inst || dynamic_cast<ir::PhiNode*>(inst)) continue;
                ir::Instruction::Opcode op = inst->getOpcode();
                if (op == ir::Instruction::Jmp || op == ir::Instruction::Jnz ||
                    op == ir::Instruction::Jz  || op == ir::Instruction::Br) continue;

                auto cloned = cloner.cloneInstruction(inst, curBB);
                curMap[inst] = cloned.get();
                curBB->addInstruction(std::move(cloned));
            }
            if (header == latch) break;
        }

        ir::BasicBlock* nextBB = (k + 1 < UF) ? bodyPtrs[k + 1] : uHeaderPtr;
        auto jmpNext = std::make_unique<ir::Instruction>(ir::VoidType::get(), ir::Instruction::Jmp,
                                                         std::vector<ir::Value*>{nextBB}, curBB);
        curBB->addInstruction(std::move(jmpNext));
    }

    // Complete back-edge incoming operands for uHeader PHIs
    for (ir::PhiNode* origPhi : headerPhis) {
        if (reductionMap.count(origPhi)) {
            for (size_t k = 0; k < UF; ++k) {
                ir::PhiNode* accPhi = uHeaderAccMap[origPhi][k];
                ir::Value* latchAccVal = bodyMaps[k][phiLatchMap[origPhi]];
                accPhi->addIncoming(latchAccVal ? latchAccVal : phiLatchMap[origPhi], bodyPtrs[UF - 1]);
            }
        } else {
            ir::PhiNode* uPhi = uHeaderPhiMap[origPhi];
            ir::Value* latchValLast = bodyMaps[UF - 1][phiLatchMap[origPhi]];
            uPhi->addIncoming(latchValLast ? latchValLast : phiLatchMap[origPhi], bodyPtrs[UF - 1]);
        }
    }

    // 4. Combine accumulators in epiloguePrep
    std::map<ir::PhiNode*, ir::Value*> epiInitMap;

    for (ir::PhiNode* origPhi : headerPhis) {
        if (reductionMap.count(origPhi)) {
            // Combine acc0 + acc1 + ... + accUF-1 in epiPrep
            ir::Value* combined = uHeaderAccMap[origPhi][0];
            for (size_t k = 1; k < UF; ++k) {
                auto addCombine = std::make_unique<ir::Instruction>(origPhi->getType(), ir::Instruction::Add,
                                                                    std::vector<ir::Value*>{combined, uHeaderAccMap[origPhi][k]}, epiPrepPtr);
                addCombine->setName(origPhi->getName() + ".combine" + std::to_string(k));
                combined = addCombine.get();
                epiPrepPtr->addInstruction(std::move(addCombine));
            }
            epiInitMap[origPhi] = combined;
        } else {
            epiInitMap[origPhi] = uHeaderPhiMap[origPhi];
        }
    }

    auto jmpPrepToHeader = std::make_unique<ir::Instruction>(ir::VoidType::get(), ir::Instruction::Jmp,
                                                             std::vector<ir::Value*>{epiHeaderPtr}, epiPrepPtr);
    epiPrepPtr->addInstruction(std::move(jmpPrepToHeader));

    // 5. Setup Epilogue Header PHIs (1x tail loop)
    std::map<ir::PhiNode*, ir::PhiNode*> epiPhiMap;
    for (ir::PhiNode* origPhi : headerPhis) {
        auto epiPhi = std::make_unique<ir::PhiNode>(origPhi->getType(), 0, origPhi->getVariable(), epiHeaderPtr);
        epiPhi->setName(origPhi->getName() + ".epi");
        epiPhi->addIncoming(epiInitMap[origPhi], epiPrepPtr);
        epiPhiMap[origPhi] = epiPhi.get();
        epiHeaderPtr->addInstruction(std::move(epiPhi));
    }

    ir::PhiNode* epiIVPhi = epiPhiMap[ivInfo.phi];
    ir::Value* epiCondIVVal = epiIVPhi;
    ir::ConstantInt* stepConst = ir::ConstantInt::get(intIvType, ivInfo.stepVal);
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

    // Complete epilogue back-edge incoming operands
    for (ir::PhiNode* origPhi : headerPhis) {
        ir::PhiNode* epiPhi = epiPhiMap[origPhi];
        ir::Value* epiLatchVal = mapEpi[phiLatchMap[origPhi]];
        epiPhi->addIncoming(epiLatchVal ? epiLatchVal : phiLatchMap[origPhi], epiBodyPtr);
    }

    auto jmpToEpiHeader = std::make_unique<ir::Instruction>(ir::VoidType::get(), ir::Instruction::Jmp,
                                                            std::vector<ir::Value*>{epiHeaderPtr}, epiBodyPtr);
    epiBodyPtr->addInstruction(std::move(jmpToEpiHeader));

    // 7. Setup Final Exit Block and PHI values for outside uses
    std::map<ir::Value*, ir::PhiNode*> finalExitPhiMap;
    std::set<ir::Value*> definedInLoop;
    for (ir::BasicBlock* bb : loop.blocks) {
        for (auto& instPtr : bb->getInstructions()) {
            if (instPtr) definedInLoop.insert(instPtr.get());
        }
    }

    std::set<ir::BasicBlock*> allUnrolledBlocks = loop.blocks;
    allUnrolledBlocks.insert(uHeaderPtr);
    for (ir::BasicBlock* b : bodyPtrs) allUnrolledBlocks.insert(b);
    allUnrolledBlocks.insert(epiPrepPtr);
    allUnrolledBlocks.insert(epiHeaderPtr);
    allUnrolledBlocks.insert(epiBodyPtr);
    allUnrolledBlocks.insert(finalExitPtr);

    for (ir::Value* val : definedInLoop) {
        // Check if val is used outside loop
        bool usedOutside = false;
        for (ir::Use* u : val->getUseList()) {
            if (!u || !u->getUser()) continue;
            auto* userInst = dynamic_cast<ir::Instruction*>(u->getUser());
            if (userInst && userInst->getParent()) {
                if (allUnrolledBlocks.find(userInst->getParent()) == allUnrolledBlocks.end()) {
                    usedOutside = true;
                    break;
                }
            }
        }

        if (usedOutside) {
            auto finalPhi = std::make_unique<ir::PhiNode>(val->getType(), 0, nullptr, finalExitPtr);
            finalPhi->setName(val->getName() + ".final");

            ir::Value* valEpiExit = nullptr;
            if (auto* phiVal = dynamic_cast<ir::PhiNode*>(val)) {
                valEpiExit = epiPhiMap[phiVal];
            } else if (mapEpi.count(val)) {
                valEpiExit = mapEpi[val];
            } else {
                valEpiExit = val;
            }

            finalPhi->addIncoming(valEpiExit, epiHeaderPtr);
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

    // Redirect all incoming branches to header from outside the loop to uHeaderPtr
    auto headerUses = header->getUseList();
    for (ir::Use* u : headerUses) {
        if (!u || !u->getUser()) continue;
        auto* userInst = dynamic_cast<ir::Instruction*>(u->getUser());
        if (userInst && userInst->getParent()) {
            if (allUnrolledBlocks.find(userInst->getParent()) == allUnrolledBlocks.end()) {
                u->set(uHeaderPtr);
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
    for (auto& b : bodyBlocks) {
        func.addBasicBlock(std::move(b));
    }
    func.addBasicBlock(std::move(epiloguePrep));
    func.addBasicBlock(std::move(epilogueHeader));
    func.addBasicBlock(std::move(epilogueBody));
    func.addBasicBlock(std::move(finalExit));

    return true;
}

} // namespace transforms
