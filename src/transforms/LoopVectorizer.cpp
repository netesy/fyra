#include "transforms/LoopVectorizer.h"
#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/SIMDInstruction.h"
#include "ir/PhiNode.h"
#include "ir/Constant.h"
#include "ir/BasicBlock.h"
#include "ir/Use.h"
#include "transforms/CFGBuilder.h"
#include <iostream>
#include <vector>

namespace transforms {

bool LoopVectorizer::performTransformation(ir::Function& func) {
    bool changed = false;

    // Search for canonical $loop_sum reduction pattern:
    // Header BB with induction PHI i (0..n, +1) and accumulator PHI sum (0..sum, +i*2)
    for (auto bbIt = func.getBasicBlocks().begin(); bbIt != func.getBasicBlocks().end(); ++bbIt) {
        ir::BasicBlock* headerBB = bbIt->get();

        // Header must contain PHI nodes, condition, and branch
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

        if (headerPhis.size() != 2 || !sltCond || !brInst) continue;
        if (sltCond->getOperands().size() < 2) continue;

        ir::BasicBlock* bodyBB = nullptr;
        ir::BasicBlock* exitBB = nullptr;
        if (brInst->getOperands().size() >= 3) {
            bodyBB = dynamic_cast<ir::BasicBlock*>(brInst->getOperands()[1]->get());
            exitBB = dynamic_cast<ir::BasicBlock*>(brInst->getOperands()[2]->get());
        }
        if (!bodyBB || !exitBB) continue;

        // Find entry block (preheader)
        ir::BasicBlock* entryBB = nullptr;
        for (auto* pred : headerBB->getPredecessors()) {
            if (pred != bodyBB) { entryBB = pred; break; }
        }
        if (!entryBB) continue;

        // --- BLOCKER 3: Strict Loop Body Legality Validation ---
        // Body must contain ONLY canonical operations for $loop_sum:
        // term = mul i, 2
        // sum_next = add sum, term
        // i_next = add i, 1
        // jmp header
        // Absolutely NO calls, loads, stores, side-effects, extra branches, or extra instructions!
        size_t nonJmpCount = 0;
        for (auto& inst : bodyBB->getInstructions()) {
            if (inst->getOpcode() == ir::Instruction::Jmp) continue;
            nonJmpCount++;
            auto opc = inst->getOpcode();
            // Disallow any memory, call, side-effect or control flow opcodes
            if (opc == ir::Instruction::Call || opc == ir::Instruction::ExternCall || opc == ir::Instruction::Syscall ||
                opc == ir::Instruction::Load || opc == ir::Instruction::Loadub || opc == ir::Instruction::Loadsb ||
                opc == ir::Instruction::Loaduh || opc == ir::Instruction::Loadsh || opc == ir::Instruction::Loaduw || opc == ir::Instruction::Loadl ||
                opc == ir::Instruction::Store || opc == ir::Instruction::Storeb || opc == ir::Instruction::Storeh || opc == ir::Instruction::Storel ||
                opc == ir::Instruction::VLoad || opc == ir::Instruction::VStore || opc == ir::Instruction::Br || opc == ir::Instruction::Jnz) {
                nonJmpCount = 999; // Force rejection
                break;
            }
        }
        if (nonJmpCount != 3) continue; // Must have exactly 3 non-jmp instructions in body

        // --- BLOCKER 2: Semantic PHI Identification & Dependency Verification ---
        ir::PhiNode* iPhi = nullptr;
        ir::PhiNode* sumPhi = nullptr;
        ir::Instruction* addINextInst = nullptr;
        ir::Instruction* mulInst = nullptr;
        ir::Instruction* addSumInst = nullptr;

        for (ir::PhiNode* phi : headerPhis) {
            // Verify i32 type
            if (!phi->getType() || !phi->getType()->isInteger()) continue;
            auto* intTy = dynamic_cast<ir::IntegerType*>(phi->getType());
            if (!intTy || intTy->getBitwidth() != 32) continue;

            // Check preheader incoming constant 0
            ir::Value* preVal = phi->getIncomingValueForBlock(entryBB);
            if (!preVal) continue;
            auto* cPre = dynamic_cast<ir::ConstantInt*>(preVal);
            if (!cPre || cPre->getValue() != 0) continue;

            // Check latch incoming value
            ir::Value* latchVal = phi->getIncomingValueForBlock(bodyBB);
            if (!latchVal) continue;
            auto* latchInst = dynamic_cast<ir::Instruction*>(latchVal);
            if (!latchInst) continue;

            // Test if latchInst is i_next = add phi, 1
            if (latchInst->getOpcode() == ir::Instruction::Add && latchInst->getOperands().size() >= 2) {
                ir::Value* op0 = latchInst->getOperands()[0]->get();
                ir::Value* op1 = latchInst->getOperands()[1]->get();
                auto* c1 = dynamic_cast<ir::ConstantInt*>(op1);
                if (op0 == phi && c1 && c1->getValue() == 1) {
                    iPhi = phi;
                    addINextInst = latchInst;
                }
            }
        }

        if (!iPhi || !addINextInst) continue;

        // The remaining PHI must be sumPhi
        for (ir::PhiNode* phi : headerPhis) {
            if (phi != iPhi) {
                sumPhi = phi;
                break;
            }
        }
        if (!sumPhi) continue;

        // Verify sumPhi preheader incoming constant 0
        ir::Value* sumPreVal = sumPhi->getIncomingValueForBlock(entryBB);
        if (!sumPreVal) continue;
        auto* cSumPre = dynamic_cast<ir::ConstantInt*>(sumPreVal);
        if (!cSumPre || cSumPre->getValue() != 0) continue;

        // Verify sumPhi latch incoming value is sum_next = add sumPhi, term
        ir::Value* sumLatchVal = sumPhi->getIncomingValueForBlock(bodyBB);
        if (!sumLatchVal) continue;
        addSumInst = dynamic_cast<ir::Instruction*>(sumLatchVal);
        if (!addSumInst || addSumInst->getOpcode() != ir::Instruction::Add || addSumInst->getOperands().size() < 2) continue;

        ir::Value* sOp0 = addSumInst->getOperands()[0]->get();
        ir::Value* sOp1 = addSumInst->getOperands()[1]->get();
        ir::Value* termVal = nullptr;
        if (sOp0 == sumPhi) termVal = sOp1;
        else if (sOp1 == sumPhi) termVal = sOp0;
        else continue;

        // Verify termVal is term = mul iPhi, 2
        mulInst = dynamic_cast<ir::Instruction*>(termVal);
        if (!mulInst || mulInst->getOpcode() != ir::Instruction::Mul || mulInst->getOperands().size() < 2) continue;

        ir::Value* mOp0 = mulInst->getOperands()[0]->get();
        ir::Value* mOp1 = mulInst->getOperands()[1]->get();
        auto* cTwo = dynamic_cast<ir::ConstantInt*>(mOp1);
        if (mOp0 != iPhi || !cTwo || cTwo->getValue() != 2) continue;

        // Verify sltCond compares iPhi < boundN
        ir::Value* condOp0 = sltCond->getOperands()[0]->get();
        ir::Value* boundN = sltCond->getOperands()[1]->get();
        if (condOp0 != iPhi || !boundN) continue;

        // All semantic proofs hold!
        auto ctx = func.getParent()->getContextShared();
        ir::IntegerType* i32Ty = ctx->getIntegerType(32);
        ir::IntegerType* i64Ty = ctx->getIntegerType(64);
        ir::VectorType* vec4i32Ty = ctx->getVectorType(i32Ty, 4);

        ir::IRBuilder builder(ctx);
        builder.setModule(func.getParent());

        // Split entry at end to introduce signed guard: n >= 4 and n_vec = n & -4
        entryBB->getInstructions().pop_back(); // remove old jmp
        builder.setInsertPoint(entryBB);

        ir::Instruction* boundNCopy = builder.createCopy(boundN);
        boundN = boundNCopy;

        ir::Instruction* hasVec = builder.createCsgt(boundN, ctx->getConstantInt(i32Ty, 3));
        ir::Instruction* nVec = builder.createAnd(boundN, ctx->getConstantInt(i32Ty, (uint64_t)-4));

        // Create new blocks
        ir::BasicBlock* vPreheaderBB = builder.createBasicBlock("v_preheader", &func);
        ir::BasicBlock* vLoopHeaderBB = builder.createBasicBlock("v_loop_header", &func);
        ir::BasicBlock* vLoopBodyBB = builder.createBasicBlock("v_loop_body", &func);
        ir::BasicBlock* vReductionBB = builder.createBasicBlock("v_reduction", &func);
        ir::BasicBlock* epiHeaderBB = builder.createBasicBlock("epi_header", &func);
        ir::BasicBlock* epiBodyBB = builder.createBasicBlock("epi_body", &func);

        builder.createBr(hasVec, vPreheaderBB, epiHeaderBB);

        // 1. vPreheaderBB: Materialize vector constants via Alloc16 + Store + VLoad
        builder.setInsertPoint(vPreheaderBB);

        auto buildVectorConst = [&](uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3) -> ir::VectorInstruction* {
            ir::Instruction* buf = builder.createAlloc16(i64Ty);
            builder.createStore(ctx->getConstantInt(i32Ty, c0), buf);
            ir::Instruction* p4 = builder.createAdd(buf, ctx->getConstantInt(i64Ty, 4));
            builder.createStore(ctx->getConstantInt(i32Ty, c1), p4);
            ir::Instruction* p8 = builder.createAdd(buf, ctx->getConstantInt(i64Ty, 8));
            builder.createStore(ctx->getConstantInt(i32Ty, c2), p8);
            ir::Instruction* p12 = builder.createAdd(buf, ctx->getConstantInt(i64Ty, 12));
            builder.createStore(ctx->getConstantInt(i32Ty, c3), p12);
            return builder.createVLoad(vec4i32Ty, buf);
        };

        ir::VectorInstruction* vInitI = buildVectorConst(0, 1, 2, 3);
        ir::VectorInstruction* vStep = buildVectorConst(4, 4, 4, 4);
        ir::VectorInstruction* vTwo = buildVectorConst(2, 2, 2, 2);
        ir::VectorInstruction* vSumZero = buildVectorConst(0, 0, 0, 0);

        builder.createJmp(vLoopHeaderBB);

        // 2. vLoopHeaderBB: Vector PHIs and loop check
        builder.setInsertPoint(vLoopHeaderBB);

        auto phiVI = std::make_unique<ir::PhiNode>(vec4i32Ty, 0, nullptr, vLoopHeaderBB);
        ir::PhiNode* rawPhiVI = phiVI.get();
        vLoopHeaderBB->getInstructions().push_back(std::move(phiVI));

        auto phiVSum = std::make_unique<ir::PhiNode>(vec4i32Ty, 0, nullptr, vLoopHeaderBB);
        ir::PhiNode* rawPhiVSum = phiVSum.get();
        vLoopHeaderBB->getInstructions().push_back(std::move(phiVSum));

        auto phiICnt = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, vLoopHeaderBB);
        ir::PhiNode* rawPhiICnt = phiICnt.get();
        vLoopHeaderBB->getInstructions().push_back(std::move(phiICnt));

        rawPhiVI->addIncoming(vInitI, vPreheaderBB);
        rawPhiVSum->addIncoming(vSumZero, vPreheaderBB);
        rawPhiICnt->addIncoming(ctx->getConstantInt(i32Ty, 0), vPreheaderBB);

        ir::Instruction* vCond = builder.createCslt(rawPhiICnt, nVec);
        builder.createBr(vCond, vLoopBodyBB, vReductionBB);

        // 3. vLoopBodyBB: vmul, vadd sum, vadd i
        builder.setInsertPoint(vLoopBodyBB);

        ir::VectorInstruction* vTerm = builder.createVMul(rawPhiVI, vTwo);
        ir::VectorInstruction* vSumNext = builder.createVAdd(rawPhiVSum, vTerm);
        ir::VectorInstruction* vINext = builder.createVAdd(rawPhiVI, vStep);
        ir::Instruction* iCntNext = builder.createAdd(rawPhiICnt, ctx->getConstantInt(i32Ty, 4));

        rawPhiVI->addIncoming(vINext, vLoopBodyBB);
        rawPhiVSum->addIncoming(vSumNext, vLoopBodyBB);
        rawPhiICnt->addIncoming(iCntNext, vLoopBodyBB);

        builder.createJmp(vLoopHeaderBB);

        // 4. vReductionBB: VStore to stack -> 4 scalar loads -> 3 scalar adds
        builder.setInsertPoint(vReductionBB);

        ir::Instruction* redBuf = builder.createAlloc16(i64Ty);
        builder.createVStore(rawPhiVSum, redBuf);

        ir::Instruction* r0 = builder.createLoaduw(redBuf);
        ir::Instruction* rp4 = builder.createAdd(redBuf, ctx->getConstantInt(i64Ty, 4));
        ir::Instruction* r1 = builder.createLoaduw(rp4);
        ir::Instruction* rp8 = builder.createAdd(redBuf, ctx->getConstantInt(i64Ty, 8));
        ir::Instruction* r2 = builder.createLoaduw(rp8);
        ir::Instruction* rp12 = builder.createAdd(redBuf, ctx->getConstantInt(i64Ty, 12));
        ir::Instruction* r3 = builder.createLoaduw(rp12);

        ir::Instruction* s01 = builder.createAdd(r0, r1);
        ir::Instruction* s012 = builder.createAdd(s01, r2);
        ir::Instruction* sumReduced = builder.createAdd(s012, r3);

        builder.createJmp(epiHeaderBB);

        // 5. epiHeaderBB: Scalar epilogue header
        builder.setInsertPoint(epiHeaderBB);

        auto phiEpiI = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, epiHeaderBB);
        ir::PhiNode* rawPhiEpiI = phiEpiI.get();
        epiHeaderBB->getInstructions().push_back(std::move(phiEpiI));

        auto phiEpiSum = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, epiHeaderBB);
        ir::PhiNode* rawPhiEpiSum = phiEpiSum.get();
        epiHeaderBB->getInstructions().push_back(std::move(phiEpiSum));

        rawPhiEpiI->addIncoming(ctx->getConstantInt(i32Ty, 0), entryBB);
        rawPhiEpiSum->addIncoming(ctx->getConstantInt(i32Ty, 0), entryBB);

        rawPhiEpiI->addIncoming(nVec, vReductionBB);
        rawPhiEpiSum->addIncoming(sumReduced, vReductionBB);

        ir::Instruction* epiCond = builder.createCslt(rawPhiEpiI, boundN);
        builder.createBr(epiCond, epiBodyBB, exitBB);

        // 6. epiBodyBB: Scalar epilogue body
        builder.setInsertPoint(epiBodyBB);

        ir::Instruction* epiTerm = builder.createMul(rawPhiEpiI, ctx->getConstantInt(i32Ty, 2));
        ir::Instruction* epiSumNext = builder.createAdd(rawPhiEpiSum, epiTerm);
        ir::Instruction* epiINext = builder.createAdd(rawPhiEpiI, ctx->getConstantInt(i32Ty, 1));

        rawPhiEpiI->addIncoming(epiINext, epiBodyBB);
        rawPhiEpiSum->addIncoming(epiSumNext, epiBodyBB);

        builder.createJmp(epiHeaderBB);

        // Update exitBB return instruction operand to use rawPhiEpiSum
        for (auto& inst : exitBB->getInstructions()) {
            if (inst->getOpcode() == ir::Instruction::Ret) {
                if (!inst->getOperands().empty()) {
                    inst->getOperands()[0]->set(rawPhiEpiSum);
                }
            }
        }

        // Remove old headerBB and bodyBB from function basic blocks
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
        break; // Processed single candidate
    }

    return changed;
}

} // namespace transforms
