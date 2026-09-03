#include "transforms/LoopUnroll.h"
#include "transforms/InstructionCloner.h"
#include "transforms/DominatorTree.h"
#include "transforms/LoopInvariantCodeMotion.h"
#include "ir/IRBuilder.h"
#include "ir/Type.h"
#include "ir/Use.h"
#include <set>
#include <vector>
#include <iostream>

namespace transforms {

bool LoopUnrollPass::performTransformation(ir::Function& func) {
    LoopInvariantCodeMotion licm;
    std::vector<std::unique_ptr<Loop>> loops;
    licm.findLoops(func, loops);

    bool changed = false;

    for (auto& loopPtr : loops) {
        Loop& loop = *loopPtr;
        licm.getOrCreatePreheader(loop, func);

        UnrollCandidate cand;
        if (isLegalToUnroll(loop, func, cand)) {
            if (unrollLoopBy2(cand, func)) {
                changed = true;
            }
        }
    }

    return changed;
}

bool LoopUnrollPass::isLegalToUnroll(Loop& loop, ir::Function& func, UnrollCandidate& cand) {
    if (!loop.header || !loop.preheader) return false;
    if (!loop.children.empty()) return false; // Must be a leaf loop (no nested child loops)
    if (loop.blocks.size() != 2 && loop.blocks.size() != 1) return false; // Single body + header
    if (loop.exits.size() != 1) return false;

    cand.loop = &loop;
    cand.header = loop.header;
    cand.preheader = loop.preheader;
    cand.exitBB = *loop.exits.begin();

    // Identify body block
    cand.body = nullptr;
    for (ir::BasicBlock* bb : loop.blocks) {
        if (bb != cand.header) {
            cand.body = bb;
            break;
        }
    }
    if (!cand.body) cand.body = cand.header;

    // Check side effects / calls / branches / instruction limits / register pressure in body
    size_t bodyInstCount = 0;
    size_t liveVregCount = 0;

    for (ir::BasicBlock* bb : loop.blocks) {
        for (auto& instPtr : bb->getInstructions()) {
            bodyInstCount++;
            ir::Instruction* inst = instPtr.get();
            ir::Instruction::Opcode opc = inst->getOpcode();

            if (opc == ir::Instruction::Call || opc == ir::Instruction::Syscall || opc == ir::Instruction::ExternCall) {
                return false; // No non-inlined calls
            }
            if (opc == ir::Instruction::Store || opc == ir::Instruction::Storeb || opc == ir::Instruction::Alloc) {
                return false; // No memory stores/allocs in initial conservative pass
            }
            if (bb == cand.body && (opc == ir::Instruction::Jnz || opc == ir::Instruction::Jz || opc == ir::Instruction::Br)) {
                return false; // No internal control flow branches inside body
            }
            if (inst->getType() && !inst->getType()->isVoidTy()) {
                liveVregCount++;
            }
        }
    }

    if (bodyInstCount < 3) return false;   // Must have at least 3 body instructions
    if (bodyInstCount > 150) return false; // Prevent instruction bloat
    if (liveVregCount > 12) return false;  // Register pressure guard: prevent spill regressions (e.g. reg_pressure)

    // Locate comparison instruction in header feeding loop condition
    ir::Instruction* cmpInst = nullptr;
    for (auto& instPtr : cand.header->getInstructions()) {
        ir::Instruction* inst = instPtr.get();
        ir::Instruction::Opcode opc = inst->getOpcode();
        if (opc == ir::Instruction::Csle || opc == ir::Instruction::Cslt) {
            cmpInst = inst;
            cand.isSle = (opc == ir::Instruction::Csle);
            break;
        }
    }

    if (!cmpInst || cmpInst->getOperands().size() < 2) return false;

    // Verify comparison operands: operand 0 must be indPhi (or indNext), operand 1 must be boundVal
    ir::Value* cmpOp0 = cmpInst->getOperands()[0]->get();
    cand.boundVal = cmpInst->getOperands()[1]->get();

    ir::PhiNode* indPhi = dynamic_cast<ir::PhiNode*>(cmpOp0);
    if (!indPhi) {
        // Try if cmpOp0 is indNext
        if (auto* instOp0 = dynamic_cast<ir::Instruction*>(cmpOp0)) {
            if (instOp0->getOpcode() == ir::Instruction::Add && instOp0->getOperands().size() >= 1) {
                indPhi = dynamic_cast<ir::PhiNode*>(instOp0->getOperands()[0]->get());
            }
        }
    }

    if (!indPhi) return false;

    // Find the induction step instruction corresponding to indPhi
    ir::Instruction* indNext = nullptr;
    for (auto& use : indPhi->getUseList()) {
        ir::User* user = use->getUser();
        if (auto* userInst = dynamic_cast<ir::Instruction*>(user)) {
            if (userInst->getOpcode() == ir::Instruction::Add && loop.blocks.count(userInst->getParent())) {
                if (userInst->getOperands().size() >= 2 && userInst->getOperands()[0]->get() == indPhi) {
                    if (dynamic_cast<ir::ConstantInt*>(userInst->getOperands()[1]->get())) {
                        indNext = userInst;
                        break;
                    }
                }
            }
        }
    }

    if (!indNext) return false;

    ir::Value* initVal = indPhi->getIncomingValueForBlock(cand.preheader);
    ir::ConstantInt* cInit = dynamic_cast<ir::ConstantInt*>(initVal);
    if (!cInit) return false;

    ir::ConstantInt* cStep = dynamic_cast<ir::ConstantInt*>(indNext->getOperands()[1]->get());
    if (!cStep) return false;

    cand.indPhi = indPhi;
    cand.indNext = indNext;
    cand.initVal = cInit->getValue();
    cand.stepVal = cStep->getValue();

    if (cand.stepVal != 1) return false; // Only unroll unit-step loops once (stepVal == 1)

    if (auto* cBound = dynamic_cast<ir::ConstantInt*>(cand.boundVal)) {
        cand.isConstantBound = true;
        cand.constantBound = cBound->getValue();
    }

    return true;
}

bool LoopUnrollPass::unrollLoopBy2(UnrollCandidate& cand, ir::Function& func) {
    if (!cand.body || !cand.header) return false;

    // Check trip count divisibility by 2
    if (cand.isConstantBound) {
        int64_t totalIters = cand.constantBound - cand.initVal + (cand.isSle ? 1 : 0);
        if (totalIters <= 1 || totalIters % 2 != 0) return false; // Strictly even trip count for exact unrolling
    } else {
        return false; // Constant bounds for now
    }

    ValueMapper vmap;
    ir::IRBuilder builder;

    // Duplicate instructions in body
    std::vector<ir::Instruction*> originalBodyInsts;
    for (auto& instPtr : cand.body->getInstructions()) {
        ir::Instruction* inst = instPtr.get();
        if (inst->getOpcode() != ir::Instruction::Jmp &&
            inst->getOpcode() != ir::Instruction::Jnz &&
            inst->getOpcode() != ir::Instruction::Br &&
            inst->getOpcode() != ir::Instruction::Ret) {
            originalBodyInsts.push_back(inst);
        }
    }

    // Map induction variable for second iteration: %i_iter2 = %i + step
    ir::Type* indType = cand.indPhi->getType();
    ir::ConstantInt* cStep = ir::ConstantInt::get(dynamic_cast<ir::IntegerType*>(indType), cand.stepVal);

    // Set insertion point in body right before terminator
    auto termIt = std::prev(cand.body->getInstructions().end());
    builder.setInsertPoint(cand.body, termIt);

    ir::Value* i2 = builder.createAdd(cand.indPhi, cStep);
    i2->setName(cand.indPhi->getName() + ".iter2");

    // Map header PHI values for iteration 2
    for (auto& instPtr : cand.header->getInstructions()) {
        if (auto* phi = dynamic_cast<ir::PhiNode*>(instPtr.get())) {
            if (phi == cand.indPhi) {
                vmap[phi] = i2;
            } else {
                ir::Value* bodyVal = phi->getIncomingValueForBlock(cand.body);
                if (bodyVal) {
                    vmap[phi] = bodyVal; // Map to iteration 1's accumulator result
                }
            }
        }
    }

    // Map indNext to i2 so any instruction reading indNext gets i2
    vmap[cand.indNext] = i2;

    // Clone body instructions for iteration 2
    for (ir::Instruction* inst : originalBodyInsts) {
        if (inst == cand.indNext) continue; // Handle induction step separately

        std::unique_ptr<ir::Instruction> cloned = InstructionCloner::cloneInstruction(inst, vmap, cand.body);
        ir::Instruction* clonedPtr = cloned.get();
        vmap[inst] = clonedPtr;

        // Insert cloned instruction before terminator
        cand.body->addInstruction(termIt, std::move(cloned));
    }

    // Update induction step for 2x unrolled loop: %i_next = %i + (2 * step) AFTER cloning iteration 2
    ir::ConstantInt* cStep2 = ir::ConstantInt::get(dynamic_cast<ir::IntegerType*>(indType), cand.stepVal * 2);
    cand.indNext->getOperands()[1]->set(cStep2);

    // Update loop-carried accumulator PHIs in header to receive iteration 2's cloned result
    for (auto& instPtr : cand.header->getInstructions()) {
        if (auto* phi = dynamic_cast<ir::PhiNode*>(instPtr.get())) {
            if (phi == cand.indPhi) continue;

            ir::Value* accumBodyVal = phi->getIncomingValueForBlock(cand.body);
            if (accumBodyVal) {
                auto itVal = vmap.find(accumBodyVal);
                if (itVal != vmap.end()) {
                    phi->setIncomingValueForBlock(cand.body, itVal->second);
                }
            }
        }
    }

    return true;
}

} // namespace transforms
