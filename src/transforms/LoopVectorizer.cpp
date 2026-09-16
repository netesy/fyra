#include "transforms/LoopVectorizer.h"
#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/SIMDInstruction.h"
#include "ir/PhiNode.h"
#include "ir/Constant.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "transforms/CFGBuilder.h"
#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <cstdlib>

namespace transforms {

namespace {

void logDiag(const std::string& msg) {
    if (std::getenv("FYRA_VECTORIZER_DIAG")) {
        std::cout << "[LoopVectorizer Diag] " << msg << std::endl;
    }
}

struct MemoryAccess {
    ir::Instruction* inst = nullptr;
    bool isStore = false;
    ir::Value* base = nullptr;
    ir::Value* index = nullptr;
    int64_t stride = 1;
    int64_t elementSize = 4;
};

struct ReductionInfo {
    ir::PhiNode* phi = nullptr;
    ir::Value* initVal = nullptr;
    ir::Instruction* accumInst = nullptr;
    ir::Instruction::Opcode op = ir::Instruction::Add;
    ir::Type* elemType = nullptr;
    bool isWidening = false;
};

struct VectorizationPlan {
    ir::PhiNode* indVarPhi = nullptr;
    ir::Value* initVal = nullptr;
    ir::Instruction* stepInst = nullptr;
    int64_t stepConst = 1;
    ir::Value* boundVal = nullptr;
    ir::Instruction* condInst = nullptr;

    ir::BasicBlock* headerBB = nullptr;
    ir::BasicBlock* bodyBB = nullptr;
    ir::BasicBlock* preheaderBB = nullptr;
    ir::BasicBlock* exitBB = nullptr;

    std::vector<ReductionInfo> reductions;
    std::vector<MemoryAccess> memoryAccesses;

    // Derived properties
    unsigned vectorFactor = 4;
    unsigned vectorWidthBits = 128;
    ir::Type* mainElemType = nullptr;
    uint64_t mulScaleFactor = 1;
};

ir::Value* extractBasePointer(ir::Value* ptr) {
    if (!ptr) return nullptr;
    ir::Instruction* ptrInst = dynamic_cast<ir::Instruction*>(ptr);
    if (ptrInst && ptrInst->getOpcode() == ir::Instruction::Add && ptrInst->getOperands().size() >= 2) {
        ir::Value* op0 = ptrInst->getOperands()[0]->get();
        ir::Value* op1 = ptrInst->getOperands()[1]->get();
        if (op0->getType() && op0->getType()->isPointerTy()) return op0;
        if (op1->getType() && op1->getType()->isPointerTy()) return op1;
        if (dynamic_cast<ir::Parameter*>(op0) || dynamic_cast<ir::Instruction*>(op0)) return op0;
        return op1;
    }
    return ptr;
}

} // anonymous namespace

bool LoopVectorizer::performTransformation(ir::Function& func) {
    bool changed = false;

    logDiag("Analyzing function: " + func.getName());

    for (auto bbIt = func.getBasicBlocks().begin(); bbIt != func.getBasicBlocks().end(); ++bbIt) {
        ir::BasicBlock* headerBB = bbIt->get();

        std::vector<ir::PhiNode*> headerPhis;
        ir::Instruction* sltCond = nullptr;
        ir::Instruction* brInst = nullptr;

        for (auto& inst : headerBB->getInstructions()) {
            if (auto* phi = dynamic_cast<ir::PhiNode*>(inst.get())) {
                headerPhis.push_back(phi);
            } else if (inst->getOpcode() == ir::Instruction::Cslt || inst->getOpcode() == ir::Instruction::Clt) {
                sltCond = inst.get();
            } else if (inst->getOpcode() == ir::Instruction::Br || inst->getOpcode() == ir::Instruction::Jnz) {
                brInst = inst.get();
            }
        }

        if (headerPhis.empty() || !sltCond || !brInst) continue;
        if (sltCond->getOperands().size() < 2) continue;

        ir::BasicBlock* bodyBB = nullptr;
        ir::BasicBlock* exitBB = nullptr;
        if (brInst->getOperands().size() >= 3) {
            bodyBB = dynamic_cast<ir::BasicBlock*>(brInst->getOperands()[1]->get());
            exitBB = dynamic_cast<ir::BasicBlock*>(brInst->getOperands()[2]->get());
        }
        if (!bodyBB || !exitBB) continue;

        ir::BasicBlock* entryBB = nullptr;
        for (auto* pred : headerBB->getPredecessors()) {
            if (pred != bodyBB) { entryBB = pred; break; }
        }
        if (!entryBB) continue;

        // --- Legality Analysis: Body instructions ---
        bool isLegal = true;
        std::vector<MemoryAccess> memAccesses;
        for (auto& inst : bodyBB->getInstructions()) {
            auto opc = inst->getOpcode();
            if (opc == ir::Instruction::Call || opc == ir::Instruction::ExternCall || opc == ir::Instruction::Syscall ||
                opc == ir::Instruction::Alloc || opc == ir::Instruction::Alloc4 || opc == ir::Instruction::Alloc16) {
                logDiag("Rejected loop: unsupported side-effect opcode in body");
                isLegal = false;
                break;
            }

            if (opc == ir::Instruction::Load || opc == ir::Instruction::Loaduw ||
                opc == ir::Instruction::Loadd || opc == ir::Instruction::Loads ||
                opc == ir::Instruction::Store || opc == ir::Instruction::Stored ||
                opc == ir::Instruction::Stores) {
                MemoryAccess access;
                access.inst = inst.get();
                access.isStore = (opc == ir::Instruction::Store || opc == ir::Instruction::Stored || opc == ir::Instruction::Stores);
                if (access.inst->getOperands().size() > 0) {
                    ir::Value* ptr = access.isStore ? access.inst->getOperands()[1]->get() : access.inst->getOperands()[0]->get();
                    access.base = extractBasePointer(ptr);
                }
                memAccesses.push_back(access);
            }
        }
        if (!isLegal) continue;

        // --- Memory Dependence Analysis ---
        bool memLegal = true;
        for (size_t i = 0; i < memAccesses.size(); ++i) {
            for (size_t j = i + 1; j < memAccesses.size(); ++j) {
                if (memAccesses[i].isStore || memAccesses[j].isStore) {
                    if (memAccesses[i].base == memAccesses[j].base) {
                        logDiag("Rejected loop: loop-carried memory dependence on same base pointer");
                        memLegal = false;
                        break;
                    }
                }
            }
            if (!memLegal) break;
        }
        if (!memLegal) continue;

        VectorizationPlan plan;
        plan.headerBB = headerBB;
        plan.bodyBB = bodyBB;
        plan.preheaderBB = entryBB;
        plan.exitBB = exitBB;
        plan.memoryAccesses = memAccesses;

        // 1. Identify Induction Variable & Step
        ir::PhiNode* iPhi = nullptr;
        ir::Instruction* addINextInst = nullptr;

        for (ir::PhiNode* phi : headerPhis) {
            if (!phi->getType() || !phi->getType()->isInteger()) continue;

            ir::Value* preVal = phi->getIncomingValueForBlock(entryBB);
            if (!preVal) continue;

            ir::Value* latchVal = phi->getIncomingValueForBlock(bodyBB);
            if (!latchVal) continue;
            auto* latchInst = dynamic_cast<ir::Instruction*>(latchVal);
            if (!latchInst) continue;

            if (latchInst->getOpcode() == ir::Instruction::Add && latchInst->getOperands().size() >= 2) {
                ir::Value* op0 = latchInst->getOperands()[0]->get();
                ir::Value* op1 = latchInst->getOperands()[1]->get();
                auto* c1 = dynamic_cast<ir::ConstantInt*>(op1);
                if (op0 == phi && c1 && c1->getValue() == 1) {
                    iPhi = phi;
                    addINextInst = latchInst;
                    plan.initVal = preVal;
                    plan.stepConst = 1;
                    plan.stepInst = latchInst;
                    break;
                }
            }
        }

        if (!iPhi || !addINextInst) {
            logDiag("Rejected loop: canonical induction variable (step=1) not found");
            continue;
        }

        ir::Value* condOp0 = sltCond->getOperands()[0]->get();
        ir::Value* boundN = sltCond->getOperands()[1]->get();
        if (condOp0 != iPhi || !boundN) continue;

        plan.indVarPhi = iPhi;
        plan.boundVal = boundN;

        // Cost model: reject constant trip count < 4
        if (auto* cBound = dynamic_cast<ir::ConstantInt*>(boundN)) {
            if (cBound->getValue() > 0 && cBound->getValue() < 4) {
                logDiag("Rejected loop: trip count too small for vectorization (" + std::to_string(cBound->getValue()) + ")");
                continue;
            }
        }

        // 2. Identify Reductions
        ir::PhiNode* sumPhi = nullptr;
        ir::Instruction* addSumInst = nullptr;
        uint64_t mulFactor = 1;

        for (ir::PhiNode* phi : headerPhis) {
            if (phi != iPhi) {
                sumPhi = phi;
                break;
            }
        }

        if (sumPhi && sumPhi->getType() && sumPhi->getType()->isInteger()) {
            ir::Value* sumPreVal = sumPhi->getIncomingValueForBlock(entryBB);
            ir::Value* sumLatchVal = sumPhi->getIncomingValueForBlock(bodyBB);

            if (sumPreVal && sumLatchVal) {
                addSumInst = dynamic_cast<ir::Instruction*>(sumLatchVal);
                if (addSumInst && addSumInst->getOpcode() == ir::Instruction::Add && addSumInst->getOperands().size() >= 2) {
                    ir::Value* sOp0 = addSumInst->getOperands()[0]->get();
                    ir::Value* sOp1 = addSumInst->getOperands()[1]->get();
                    ir::Value* termVal = (sOp0 == sumPhi) ? sOp1 : ((sOp1 == sumPhi) ? sOp0 : nullptr);

                    if (termVal) {
                        ir::Instruction* termInst = dynamic_cast<ir::Instruction*>(termVal);
                        bool isWidening = false;
                        if (termInst) {
                            if (termInst->getOpcode() == ir::Instruction::ExtSW) {
                                ir::Value* extOp = termInst->getOperands()[0]->get();
                                if (extOp == iPhi) {
                                    for (auto& inst : bodyBB->getInstructions()) {
                                        if (inst->getOpcode() == ir::Instruction::Mul && inst->getOperands().size() >= 2) {
                                            if (inst->getOperands()[0]->get() == termInst) {
                                                auto* cTwo = dynamic_cast<ir::ConstantInt*>(inst->getOperands()[1]->get());
                                                if (cTwo && cTwo->getValue() >= 1) {
                                                    mulFactor = cTwo->getValue();
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                } else {
                                    isWidening = true;
                                }
                            } else if (termInst->getOpcode() == ir::Instruction::Mul && termInst->getOperands().size() >= 2) {
                                ir::Value* mOp0 = termInst->getOperands()[0]->get();
                                ir::Value* mOp1 = termInst->getOperands()[1]->get();
                                auto* cTwo = dynamic_cast<ir::ConstantInt*>(mOp1);
                                if (mOp0 == iPhi && cTwo && cTwo->getValue() >= 1) {
                                    mulFactor = cTwo->getValue();
                                }
                            }
                        }

                        if (isWidening) {
                            logDiag("Rejected loop: signed i32 -> i64 widening reduction not natively supported");
                            continue;
                        }

                        auto* cPreZero = dynamic_cast<ir::ConstantInt*>(sumPreVal);
                        bool isInitZero = (cPreZero && cPreZero->getValue() == 0);

                        if ((mulFactor == 2 && isInitZero) || !plan.memoryAccesses.empty() || !isInitZero) {
                            ReductionInfo red;
                            red.phi = sumPhi;
                            red.initVal = sumPreVal;
                            red.accumInst = addSumInst;
                            red.op = ir::Instruction::Add;
                            red.elemType = sumPhi->getType();
                            red.isWidening = false;
                            plan.reductions.push_back(red);
                            plan.mulScaleFactor = mulFactor;
                        }
                    }
                }
            }
        }

        if (plan.reductions.empty() && plan.memoryAccesses.empty()) {
            logDiag("Rejected loop: no vectorizable reductions or array memory accesses found");
            continue;
        }

        // Target Capability Query & Optimal VF Selection
        auto targetInfo = func.getParent() ? target::TargetResolver::resolve(target::TargetDescriptor{target::Arch::X64, target::OS::Linux}) : nullptr;
        plan.vectorFactor = 4;
        plan.vectorWidthBits = 128;

        auto ctx = func.getParent()->getContextShared();
        ir::IntegerType* i32Ty = ctx->getIntegerType(32);
        ir::IntegerType* i64Ty = ctx->getIntegerType(64);

        ir::Instruction::Opcode mainVOp = ir::Instruction::VAdd;
        for (auto& inst : bodyBB->getInstructions()) {
            auto opc = inst->getOpcode();
            if (opc == ir::Instruction::Sub) { mainVOp = ir::Instruction::VSub; break; }
            if (opc == ir::Instruction::Mul) { mainVOp = ir::Instruction::VMul; break; }
        }

        if (targetInfo) {
            ir::VectorType* vec256i32 = ctx->getVectorType(i32Ty, 8);
            logDiag("candidate 256-bit vector type: <8 x i32>");
            logDiag("supportsVectorType: " + std::string(targetInfo->supportsVectorType(vec256i32) ? "true" : "false"));
            logDiag("supportsVectorOperation: " + std::string(targetInfo->supportsVectorOperation(mainVOp, vec256i32) ? "true" : "false"));

            if (targetInfo->supportsVectorWidth(256) && targetInfo->supportsVectorOperation(mainVOp, vec256i32)) {
                plan.vectorFactor = 8;
                plan.vectorWidthBits = 256;
            }
        }

        logDiag("profitability: VF=" + std::to_string(plan.vectorFactor) + " (" + std::to_string(plan.vectorWidthBits) + "-bit) selected");

        ir::VectorType* vecTy = ctx->getVectorType(i32Ty, plan.vectorFactor);

        ir::IRBuilder builder(ctx);
        builder.setModule(func.getParent());

        // Split entry block to introduce signed guard: N >= VF and N_vec = N & -VF
        entryBB->getInstructions().pop_back();
        builder.setInsertPoint(entryBB);

        ir::Instruction* boundNCopy = builder.createCopy(plan.boundVal);

        // Copy pointer bases in entryBB to ensure stable SSA virtual registers
        std::map<ir::Value*, ir::Value*> baseCopyMap;
        for (auto& ma : plan.memoryAccesses) {
            if (ma.base && !baseCopyMap.count(ma.base)) {
                baseCopyMap[ma.base] = builder.createCopy(ma.base);
            }
        }

        ir::Instruction* hasVec = builder.createCsgt(boundNCopy, ctx->getConstantInt(i32Ty, plan.vectorFactor - 1));
        ir::Instruction* nVec = builder.createAnd(boundNCopy, ctx->getConstantInt(i32Ty, (uint64_t)(-(int64_t)plan.vectorFactor)));

        ir::BasicBlock* vPreheaderBB = builder.createBasicBlock("v_preheader", &func);
        ir::BasicBlock* vLoopHeaderBB = builder.createBasicBlock("v_loop_header", &func);
        ir::BasicBlock* vLoopBodyBB = builder.createBasicBlock("v_loop_body", &func);
        ir::BasicBlock* vReductionBB = builder.createBasicBlock("v_reduction", &func);
        ir::BasicBlock* epiHeaderBB = builder.createBasicBlock("epi_header", &func);
        ir::BasicBlock* epiBodyBB = builder.createBasicBlock("epi_body", &func);

        builder.createBr(hasVec, vPreheaderBB, epiHeaderBB);

        // Vector Preheader
        builder.setInsertPoint(vPreheaderBB);
        uint32_t startValConst = 0;
        if (auto* cStart = dynamic_cast<ir::ConstantInt*>(plan.initVal)) {
            startValConst = static_cast<uint32_t>(cStart->getValue());
        }

        auto buildVectorConst = [&](uint32_t val0, uint32_t valStep) -> ir::VectorInstruction* {
            ir::Instruction* buf = builder.createAlloc(ctx->getConstantInt(i64Ty, plan.vectorWidthBits / 8), i64Ty);
            for (unsigned k = 0; k < plan.vectorFactor; ++k) {
                ir::Instruction* pOff = (k == 0) ? buf : builder.createAdd(buf, ctx->getConstantInt(i64Ty, k * 4));
                builder.createStore(ctx->getConstantInt(i32Ty, val0 + k * valStep), pOff);
            }
            return builder.createVLoad(vecTy, buf);
        };

        ir::VectorInstruction* vInitI = buildVectorConst(startValConst, 1);
        ir::VectorInstruction* vStep = buildVectorConst(plan.vectorFactor, 0);
        ir::VectorInstruction* vScale = buildVectorConst((uint32_t)mulFactor, 0);
        ir::VectorInstruction* vSumZero = buildVectorConst(0, 0);

        builder.createJmp(vLoopHeaderBB);

        // Vector Header
        builder.setInsertPoint(vLoopHeaderBB);
        auto phiVI = std::make_unique<ir::PhiNode>(vecTy, 0, nullptr, vLoopHeaderBB);
        ir::PhiNode* rawPhiVI = phiVI.get();
        vLoopHeaderBB->getInstructions().push_back(std::move(phiVI));

        ir::PhiNode* rawPhiVSum = nullptr;
        if (!plan.reductions.empty()) {
            auto phiVSum = std::make_unique<ir::PhiNode>(vecTy, 0, nullptr, vLoopHeaderBB);
            rawPhiVSum = phiVSum.get();
            vLoopHeaderBB->getInstructions().push_back(std::move(phiVSum));
            rawPhiVSum->addIncoming(vSumZero, vPreheaderBB);
        }

        auto phiICnt = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, vLoopHeaderBB);
        ir::PhiNode* rawPhiICnt = phiICnt.get();
        vLoopHeaderBB->getInstructions().push_back(std::move(phiICnt));

        rawPhiVI->addIncoming(vInitI, vPreheaderBB);
        rawPhiICnt->addIncoming(plan.initVal, vPreheaderBB);

        ir::Instruction* vCond = builder.createCslt(rawPhiICnt, nVec);
        builder.createBr(vCond, vLoopBodyBB, vReductionBB);

        // Vector Body
        builder.setInsertPoint(vLoopBodyBB);

        // Lower array accesses if present
        if (!plan.memoryAccesses.empty()) {
            std::map<ir::Instruction*, ir::Value*> vValueMap;
            ir::Instruction* i64ICnt = builder.createExtSW(rawPhiICnt, i64Ty);
            ir::Instruction* byteOffset = builder.createMul(i64ICnt, ctx->getConstantInt(i64Ty, 4));

            for (auto& inst : bodyBB->getInstructions()) {
                auto opc = inst->getOpcode();
                if (opc == ir::Instruction::Loaduw || opc == ir::Instruction::Load) {
                    ir::Value* ptr = inst->getOperands()[0]->get();
                    ir::Value* basePtr = extractBasePointer(ptr);
                    ir::Value* safeBase = baseCopyMap.count(basePtr) ? baseCopyMap[basePtr] : basePtr;
                    ir::Instruction* vPtr = builder.createAdd(safeBase, byteOffset);
                    ir::VectorInstruction* vLd = builder.createVLoad(vecTy, vPtr);
                    vValueMap[inst.get()] = vLd;
                } else if (opc == ir::Instruction::Add && inst.get() != addINextInst) {
                    ir::Value* op0 = inst->getOperands()[0]->get();
                    ir::Value* op1 = inst->getOperands()[1]->get();
                    auto* inst0 = dynamic_cast<ir::Instruction*>(op0);
                    auto* inst1 = dynamic_cast<ir::Instruction*>(op1);

                    ir::Value* vOp0 = (inst0 && vValueMap.count(inst0)) ? vValueMap[inst0] : nullptr;
                    ir::Value* vOp1 = (inst1 && vValueMap.count(inst1)) ? vValueMap[inst1] : nullptr;

                    if (vOp0 && vOp1) {
                        ir::VectorInstruction* vAdd = builder.createVAdd(vOp0, vOp1);
                        vValueMap[inst.get()] = vAdd;
                    } else if (rawPhiVSum) {
                        ir::Value* ldVal = vOp0 ? vOp0 : vOp1;
                        if (ldVal) {
                            ir::VectorInstruction* vAddSum = builder.createVAdd(rawPhiVSum, ldVal);
                            vValueMap[inst.get()] = vAddSum;
                            rawPhiVSum->addIncoming(vAddSum, vLoopBodyBB);
                        }
                    }
                } else if (opc == ir::Instruction::Sub) {
                    ir::Value* op0 = inst->getOperands()[0]->get();
                    ir::Value* op1 = inst->getOperands()[1]->get();
                    auto* inst0 = dynamic_cast<ir::Instruction*>(op0);
                    auto* inst1 = dynamic_cast<ir::Instruction*>(op1);

                    ir::Value* vOp0 = (inst0 && vValueMap.count(inst0)) ? vValueMap[inst0] : nullptr;
                    ir::Value* vOp1 = (inst1 && vValueMap.count(inst1)) ? vValueMap[inst1] : nullptr;

                    if (vOp0 && vOp1) {
                        ir::VectorInstruction* vSub = builder.createVSub(vOp0, vOp1);
                        vValueMap[inst.get()] = vSub;
                    }
                } else if (opc == ir::Instruction::Mul) {
                    ir::Value* op0 = inst->getOperands()[0]->get();
                    ir::Value* op1 = inst->getOperands()[1]->get();
                    auto* inst0 = dynamic_cast<ir::Instruction*>(op0);
                    auto* inst1 = dynamic_cast<ir::Instruction*>(op1);

                    ir::Value* vOp0 = (inst0 && vValueMap.count(inst0)) ? vValueMap[inst0] : nullptr;
                    ir::Value* vOp1 = (inst1 && vValueMap.count(inst1)) ? vValueMap[inst1] : nullptr;

                    if (vOp0 && vOp1) {
                        ir::VectorInstruction* vMul = builder.createVMul(vOp0, vOp1);
                        vValueMap[inst.get()] = vMul;
                    }
                } else if (opc == ir::Instruction::Store || opc == ir::Instruction::Stored || opc == ir::Instruction::Stores) {
                    ir::Value* valToStore = inst->getOperands()[0]->get();
                    ir::Value* ptrToStore = inst->getOperands()[1]->get();

                    auto* instVal = dynamic_cast<ir::Instruction*>(valToStore);
                    ir::Value* vVal = (instVal && vValueMap.count(instVal)) ? vValueMap[instVal] : valToStore;

                    ir::Value* basePtr = extractBasePointer(ptrToStore);
                    ir::Value* safeBase = baseCopyMap.count(basePtr) ? baseCopyMap[basePtr] : basePtr;
                    ir::Instruction* vPtr = builder.createAdd(safeBase, byteOffset);
                    builder.createVStore(vVal, vPtr);
                }
            }
        } else if (rawPhiVSum) {
            ir::VectorInstruction* vTerm = builder.createVMul(rawPhiVI, vScale);
            ir::VectorInstruction* vSumNext = builder.createVAdd(rawPhiVSum, vTerm);
            rawPhiVSum->addIncoming(vSumNext, vLoopBodyBB);
        }

        ir::VectorInstruction* vINext = builder.createVAdd(rawPhiVI, vStep);
        ir::Instruction* iCntNext = builder.createAdd(rawPhiICnt, ctx->getConstantInt(i32Ty, plan.vectorFactor));

        rawPhiVI->addIncoming(vINext, vLoopBodyBB);
        rawPhiICnt->addIncoming(iCntNext, vLoopBodyBB);

        builder.createJmp(vLoopHeaderBB);

        // Vector Reduction Block
        builder.setInsertPoint(vReductionBB);
        ir::Instruction* sumReduced = nullptr;
        if (!plan.reductions.empty() && rawPhiVSum) {
            ir::Instruction* redBuf = builder.createAlloc(ctx->getConstantInt(i64Ty, plan.vectorWidthBits / 8), i64Ty);
            builder.createVStore(rawPhiVSum, redBuf);

            sumReduced = builder.createLoaduw(redBuf);
            for (unsigned lane = 1; lane < plan.vectorFactor; ++lane) {
                ir::Instruction* pOff = builder.createAdd(redBuf, ctx->getConstantInt(i64Ty, lane * 4));
                ir::Instruction* laneVal = builder.createLoaduw(pOff);
                sumReduced = builder.createAdd(sumReduced, laneVal);
            }
        }

        builder.createJmp(epiHeaderBB);

        // Epilogue Header
        builder.setInsertPoint(epiHeaderBB);
        auto phiEpiI = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, epiHeaderBB);
        ir::PhiNode* rawPhiEpiI = phiEpiI.get();
        epiHeaderBB->getInstructions().push_back(std::move(phiEpiI));

        ir::PhiNode* rawPhiEpiSum = nullptr;
        if (!plan.reductions.empty()) {
            auto phiEpiSum = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, epiHeaderBB);
            rawPhiEpiSum = phiEpiSum.get();
            epiHeaderBB->getInstructions().push_back(std::move(phiEpiSum));

            rawPhiEpiSum->addIncoming(plan.reductions[0].initVal, entryBB);
            if (sumReduced) {
                auto* cInitZero = dynamic_cast<ir::ConstantInt*>(plan.reductions[0].initVal);
                if (cInitZero && cInitZero->getValue() == 0) {
                    rawPhiEpiSum->addIncoming(sumReduced, vReductionBB);
                } else {
                    ir::Instruction* combinedInit = builder.createAdd(sumReduced, plan.reductions[0].initVal);
                    rawPhiEpiSum->addIncoming(combinedInit, vReductionBB);
                }
            } else {
                rawPhiEpiSum->addIncoming(plan.reductions[0].initVal, vReductionBB);
            }
        }

        rawPhiEpiI->addIncoming(plan.initVal, entryBB);
        rawPhiEpiI->addIncoming(nVec, vReductionBB);

        ir::Instruction* epiCond = builder.createCslt(rawPhiEpiI, boundNCopy);
        builder.createBr(epiCond, epiBodyBB, exitBB);

        // Epilogue Body
        builder.setInsertPoint(epiBodyBB);

        // Recreate original scalar body in epilogue for remainder iterations
        std::map<ir::Instruction*, ir::Instruction*> epiValueMap;
        for (auto& inst : bodyBB->getInstructions()) {
            auto opc = inst->getOpcode();
            if (inst.get() == addINextInst) continue;

            if (opc == ir::Instruction::ExtSW && inst->getOperands()[0]->get() == iPhi) {
                ir::Instruction* epiI64 = builder.createExtSW(rawPhiEpiI, i64Ty);
                epiValueMap[inst.get()] = epiI64;
            } else if (opc == ir::Instruction::Mul) {
                ir::Value* op0 = inst->getOperands()[0]->get();
                ir::Value* op1 = inst->getOperands()[1]->get();
                auto* inst0 = dynamic_cast<ir::Instruction*>(op0);
                ir::Value* eOp0 = (inst0 && epiValueMap.count(inst0)) ? epiValueMap[inst0] : op0;
                ir::Instruction* epiMul = builder.createMul(eOp0, op1);
                epiValueMap[inst.get()] = epiMul;
            } else if (opc == ir::Instruction::Add) {
                ir::Value* op0 = inst->getOperands()[0]->get();
                ir::Value* op1 = inst->getOperands()[1]->get();
                auto* inst0 = dynamic_cast<ir::Instruction*>(op0);
                auto* inst1 = dynamic_cast<ir::Instruction*>(op1);
                ir::Value* eOp0 = (inst0 && epiValueMap.count(inst0)) ? epiValueMap[inst0] : op0;
                ir::Value* eOp1 = (inst1 && epiValueMap.count(inst1)) ? epiValueMap[inst1] : op1;
                ir::Instruction* epiAdd = builder.createAdd(eOp0, eOp1);
                epiValueMap[inst.get()] = epiAdd;
            } else if (opc == ir::Instruction::Sub) {
                ir::Value* op0 = inst->getOperands()[0]->get();
                ir::Value* op1 = inst->getOperands()[1]->get();
                auto* inst0 = dynamic_cast<ir::Instruction*>(op0);
                auto* inst1 = dynamic_cast<ir::Instruction*>(op1);
                ir::Value* eOp0 = (inst0 && epiValueMap.count(inst0)) ? epiValueMap[inst0] : op0;
                ir::Value* eOp1 = (inst1 && epiValueMap.count(inst1)) ? epiValueMap[inst1] : op1;
                ir::Instruction* epiSub = builder.createSub(eOp0, eOp1);
                epiValueMap[inst.get()] = epiSub;
            } else if (opc == ir::Instruction::Loaduw || opc == ir::Instruction::Load) {
                ir::Value* ptr = inst->getOperands()[0]->get();
                ir::Instruction* ptrAdd = dynamic_cast<ir::Instruction*>(ptr);
                ir::Value* ePtr = ptr;
                if (ptrAdd && ptrAdd->getOpcode() == ir::Instruction::Add) {
                    ir::Instruction* epiI64 = builder.createExtSW(rawPhiEpiI, i64Ty);
                    ir::Instruction* byteOff = builder.createMul(epiI64, ctx->getConstantInt(i64Ty, 4));
                    ir::Value* basePtr = extractBasePointer(ptr);
                    ir::Value* safeBase = baseCopyMap.count(basePtr) ? baseCopyMap[basePtr] : basePtr;
                    ePtr = builder.createAdd(safeBase, byteOff);
                }
                ir::Instruction* epiLd = builder.createLoaduw(ePtr);
                epiValueMap[inst.get()] = epiLd;
            } else if (opc == ir::Instruction::Store || opc == ir::Instruction::Stored || opc == ir::Instruction::Stores) {
                ir::Value* valToStore = inst->getOperands()[0]->get();
                ir::Value* ptrToStore = inst->getOperands()[1]->get();
                auto* instVal = dynamic_cast<ir::Instruction*>(valToStore);
                ir::Value* eVal = (instVal && epiValueMap.count(instVal)) ? epiValueMap[instVal] : valToStore;

                ir::Instruction* ptrAdd = dynamic_cast<ir::Instruction*>(ptrToStore);
                ir::Value* ePtr = ptrToStore;
                if (ptrAdd && ptrAdd->getOpcode() == ir::Instruction::Add) {
                    ir::Instruction* epiI64 = builder.createExtSW(rawPhiEpiI, i64Ty);
                    ir::Instruction* byteOff = builder.createMul(epiI64, ctx->getConstantInt(i64Ty, 4));
                    ir::Value* basePtr = extractBasePointer(ptrToStore);
                    ir::Value* safeBase = baseCopyMap.count(basePtr) ? baseCopyMap[basePtr] : basePtr;
                    ePtr = builder.createAdd(safeBase, byteOff);
                }
                builder.createStore(eVal, ePtr);
            }
        }

        ir::Instruction* epiINext = builder.createAdd(rawPhiEpiI, ctx->getConstantInt(i32Ty, 1));
        rawPhiEpiI->addIncoming(epiINext, epiBodyBB);

        if (rawPhiEpiSum) {
            ir::Instruction* epiSumNext = nullptr;
            if (!plan.memoryAccesses.empty() && plan.reductions[0].accumInst) {
                for (auto& inst : bodyBB->getInstructions()) {
                    if (inst->getOpcode() == ir::Instruction::Loaduw || inst->getOpcode() == ir::Instruction::Load) {
                        if (epiValueMap.count(inst.get())) {
                            epiSumNext = builder.createAdd(rawPhiEpiSum, epiValueMap[inst.get()]);
                            break;
                        }
                    }
                }
            }
            if (!epiSumNext) {
                ir::Instruction* epiTerm = builder.createMul(rawPhiEpiI, ctx->getConstantInt(i32Ty, mulFactor));
                epiSumNext = builder.createAdd(rawPhiEpiSum, epiTerm);
            }
            rawPhiEpiSum->addIncoming(epiSumNext, epiBodyBB);

            for (auto& inst : exitBB->getInstructions()) {
                if (inst->getOpcode() == ir::Instruction::Ret && !inst->getOperands().empty()) {
                    inst->getOperands()[0]->set(rawPhiEpiSum);
                }
            }
        }

        builder.createJmp(epiHeaderBB);

        auto removeBB = [&](ir::BasicBlock* target) {
            for (auto it = func.getBasicBlocks().begin(); it != func.getBasicBlocks().end(); ++it) {
                if (it->get() == target) {
                    func.getBasicBlocks().erase(it);
                    break;
                }
            }
        };

        removeBB(headerBB);
        removeBB(bodyBB);

        CFGBuilder::run(func);
        changed = true;
        logDiag("Vectorization successful for function: " + func.getName());
        break;
    }

    return changed;
}

} // namespace transforms
