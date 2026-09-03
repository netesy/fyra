#include "transforms/ScalarEvolution.h"
#include "transforms/CFGBuilder.h"
#include "transforms/DominatorTree.h"
#include "ir/IRBuilder.h"
#include "ir/Constant.h"
#include "ir/Instruction.h"
#include "ir/BasicBlock.h"
#include "ir/Function.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include <algorithm>
#include <vector>
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

static bool parseLinearTerm(ir::Value* val, ir::Value* indVarPhi, int64_t& coeff, int64_t& constAdd) {
    val = stripExtensions(val);
    if (!val) return false;

    if (val == indVarPhi) {
        coeff += 1;
        return true;
    }

    if (auto* ci = dynamic_cast<ir::ConstantInt*>(val)) {
        constAdd += ci->getValue();
        return true;
    }

    if (auto* inst = dynamic_cast<ir::Instruction*>(val)) {
        ir::Instruction::Opcode op = inst->getOpcode();
        if (op == ir::Instruction::Mul) {
            if (inst->getOperands().size() >= 2 && inst->getOperands()[0] && inst->getOperands()[1]) {
                ir::Value* op0 = stripExtensions(inst->getOperands()[0]->get());
                ir::Value* op1 = stripExtensions(inst->getOperands()[1]->get());

                if (op0 == indVarPhi) {
                    if (auto* c1 = dynamic_cast<ir::ConstantInt*>(op1)) {
                        coeff += c1->getValue();
                        return true;
                    }
                } else if (op1 == indVarPhi) {
                    if (auto* c0 = dynamic_cast<ir::ConstantInt*>(op0)) {
                        coeff += c0->getValue();
                        return true;
                    }
                }
            }
        } else if (op == ir::Instruction::Add) {
            if (inst->getOperands().size() >= 2 && inst->getOperands()[0] && inst->getOperands()[1]) {
                return parseLinearTerm(inst->getOperands()[0]->get(), indVarPhi, coeff, constAdd) &&
                       parseLinearTerm(inst->getOperands()[1]->get(), indVarPhi, coeff, constAdd);
            }
        }
    }

    return false;
}

bool ScalarEvolution::run(ir::Function& func) {
    if (func.getBasicBlocks().empty()) return false;

    LoopInvariantCodeMotion licm;
    std::vector<std::unique_ptr<Loop>> loops;
    licm.findLoops(func, loops);

    for (auto& loopPtr : loops) {
        if (processLoop(*loopPtr, func)) {
            CFGBuilder::run(func);
            return true;
        }
    }
    return false;
}

bool ScalarEvolution::analyzeInductionVariable(Loop& loop, IndVar& indVar) {
    if (!loop.header) return false;

    for (auto& instPtr : loop.header->getInstructions()) {
        auto* phi = dynamic_cast<ir::PhiNode*>(instPtr.get());
        if (!phi) break; // PHIs are at start

        // Check if this PHI is an induction variable
        ir::Value* initVal = nullptr;
        ir::Value* stepNextVal = nullptr;

        for (size_t i = 0; i + 1 < phi->getOperands().size(); i += 2) {
            ir::Value* pBlock = phi->getOperands()[i] ? phi->getOperands()[i]->get() : nullptr;
            ir::Value* pVal = phi->getOperands()[i + 1] ? phi->getOperands()[i + 1]->get() : nullptr;
            if (!pBlock) {
                pBlock = phi->getOperands()[i + 1] ? phi->getOperands()[i + 1]->get() : nullptr;
                pVal = phi->getOperands()[i] ? phi->getOperands()[i]->get() : nullptr;
            }
            ir::BasicBlock* predBB = dynamic_cast<ir::BasicBlock*>(pBlock);
            if (!predBB || !pVal) continue;

            if (loop.blocks.find(predBB) == loop.blocks.end()) {
                initVal = pVal;
            } else {
                stepNextVal = pVal;
            }
        }

        if (!initVal || !stepNextVal) continue;

        auto* initConst = dynamic_cast<ir::ConstantInt*>(stripExtensions(initVal));
        if (!initConst) continue;

        auto* stepInst = dynamic_cast<ir::Instruction*>(stripExtensions(stepNextVal));
        if (!stepInst) continue;

        ir::Instruction::Opcode op = stepInst->getOpcode();
        int64_t stepVal = 0;
        if (op == ir::Instruction::Add && stepInst->getOperands().size() >= 2) {
            ir::Value* op0 = stripExtensions(stepInst->getOperands()[0]->get());
            ir::Value* op1 = stripExtensions(stepInst->getOperands()[1]->get());
            if (op0 == phi) {
                if (auto* c1 = dynamic_cast<ir::ConstantInt*>(op1)) stepVal = c1->getValue();
            } else if (op1 == phi) {
                if (auto* c0 = dynamic_cast<ir::ConstantInt*>(op0)) stepVal = c0->getValue();
            }
        }

        if (stepVal == 0) continue;

        // Check header condition (slt or sle) against indVar
        for (auto& headerInstPtr : loop.header->getInstructions()) {
            ir::Instruction* hInst = headerInstPtr.get();
            if (!hInst) continue;

            ir::Instruction::Opcode hOp = hInst->getOpcode();
            if (hOp == ir::Instruction::Cslt || hOp == ir::Instruction::Cult ||
                hOp == ir::Instruction::Csle || hOp == ir::Instruction::Cule) {
                if (hInst->getOperands().size() >= 2 && hInst->getOperands()[0] && hInst->getOperands()[1]) {
                    ir::Value* condOp0 = stripExtensions(hInst->getOperands()[0]->get());
                    ir::Value* condOp1 = stripExtensions(hInst->getOperands()[1]->get());

                    if (condOp0 == phi) {
                        indVar.phi = phi;
                        indVar.stepInst = stepInst;
                        indVar.initVal = initConst->getValue();
                        indVar.stepVal = stepVal;
                        indVar.boundVal = condOp1;
                        if (auto* bConst = dynamic_cast<ir::ConstantInt*>(condOp1)) {
                            indVar.constantBound = bConst->getValue();
                            indVar.isConstantBound = true;
                        }
                        indVar.isSlt = (hOp == ir::Instruction::Cslt || hOp == ir::Instruction::Cult);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool ScalarEvolution::analyzeRecurrence(Loop& loop, const IndVar& indVar, LoopRecurrence& rec) {
    if (!loop.header) return false;

    for (auto& instPtr : loop.header->getInstructions()) {
        auto* phi = dynamic_cast<ir::PhiNode*>(instPtr.get());
        if (!phi || phi == indVar.phi) continue;

        ir::Value* initVal = nullptr;
        ir::Value* stepNextVal = nullptr;

        for (size_t i = 0; i + 1 < phi->getOperands().size(); i += 2) {
            ir::Value* pBlock = phi->getOperands()[i] ? phi->getOperands()[i]->get() : nullptr;
            ir::Value* pVal = phi->getOperands()[i + 1] ? phi->getOperands()[i + 1]->get() : nullptr;
            if (!pBlock) {
                pBlock = phi->getOperands()[i + 1] ? phi->getOperands()[i + 1]->get() : nullptr;
                pVal = phi->getOperands()[i] ? phi->getOperands()[i]->get() : nullptr;
            }
            ir::BasicBlock* predBB = dynamic_cast<ir::BasicBlock*>(pBlock);
            if (!predBB || !pVal) continue;

            if (loop.blocks.find(predBB) == loop.blocks.end()) {
                initVal = pVal;
            } else {
                stepNextVal = pVal;
            }
        }

        if (!initVal || !stepNextVal) continue;

        auto* nextInst = dynamic_cast<ir::Instruction*>(stripExtensions(stepNextVal));
        if (!nextInst || nextInst->getOpcode() != ir::Instruction::Add) continue;
        if (nextInst->getOperands().size() < 2) continue;

        ir::Value* op0 = stripExtensions(nextInst->getOperands()[0]->get());
        ir::Value* op1 = stripExtensions(nextInst->getOperands()[1]->get());

        ir::Value* termVal = nullptr;
        if (op0 == phi) termVal = op1;
        else if (op1 == phi) termVal = op0;
        else continue;

        termVal = stripExtensions(termVal);

        // Pattern 1: Linear f(i) = b*i + c
        int64_t coeffB = 0;
        int64_t coeffC = 0;
        if (parseLinearTerm(termVal, indVar.phi, coeffB, coeffC)) {
            rec.sumPhi = phi;
            rec.sumNextInst = nextInst;
            rec.initSumVal = initVal;
            rec.coeffA = 0;
            rec.coeffB = coeffB;
            rec.coeffC = coeffC;
            rec.isValid = true;
            return true;
        }

        // Pattern 2: Quadratic f(i) = (A*i + B)(C*i + D) = AC*i^2 + (AD+BC)*i + BD
        if (auto* mulInst = dynamic_cast<ir::Instruction*>(termVal)) {
            if (mulInst->getOpcode() == ir::Instruction::Mul && mulInst->getOperands().size() >= 2) {
                ir::Value* m0 = stripExtensions(mulInst->getOperands()[0]->get());
                ir::Value* m1 = stripExtensions(mulInst->getOperands()[1]->get());

                int64_t linA1 = 0, constB1 = 0;
                int64_t linA2 = 0, constB2 = 0;

                if (parseLinearTerm(m0, indVar.phi, linA1, constB1) &&
                    parseLinearTerm(m1, indVar.phi, linA2, constB2)) {
                    rec.sumPhi = phi;
                    rec.sumNextInst = nextInst;
                    rec.initSumVal = initVal;
                    rec.coeffA = linA1 * linA2;
                    rec.coeffB = (linA1 * constB2) + (constB1 * linA2);
                    rec.coeffC = constB1 * constB2;
                    rec.isValid = true;
                    return true;
                }
            }
        }
    }

    return false;
}

bool ScalarEvolution::isSafeToEliminate(Loop& loop) {
    for (ir::BasicBlock* bb : loop.blocks) {
        if (!bb) return false;
        for (auto& instPtr : bb->getInstructions()) {
            ir::Instruction* inst = instPtr.get();
            if (!inst) continue;

            ir::Instruction::Opcode op = inst->getOpcode();
            if (op == ir::Instruction::Call || op == ir::Instruction::Syscall ||
                op == ir::Instruction::ExternCall || op == ir::Instruction::Store ||
                op == ir::Instruction::Stored || op == ir::Instruction::Stores ||
                op == ir::Instruction::Storel || op == ir::Instruction::Storeh ||
                op == ir::Instruction::Storeb || op == ir::Instruction::Alloc ||
                op == ir::Instruction::Alloc4 || op == ir::Instruction::Alloc16 ||
                op == ir::Instruction::Load || op == ir::Instruction::Loadd) {
                return false;
            }
        }
    }
    return true;
}

ir::Value* ScalarEvolution::generateClosedForm(ir::Function& func, ir::BasicBlock* preheader, const IndVar& indVar, const LoopRecurrence& rec) {
    auto ctx = func.getParent() ? func.getParent()->getContextShared() : std::make_shared<ir::IRContext>();
    ir::IRBuilder builder(ctx);
    builder.setModule(func.getParent());
    builder.setInsertPoint(preheader);

    ir::IntegerType* i64Ty = ctx->getIntegerType(64);

    if (indVar.isConstantBound && indVar.stepVal == 1) {
        int64_t bound = indVar.constantBound;
        int64_t initI = indVar.initVal;
        int64_t numIterations = 0;

        if (indVar.isSlt) {
            if (bound > initI) numIterations = bound - initI;
        } else {
            if (bound >= initI) numIterations = bound - initI + 1;
        }

        if (numIterations <= 0) return nullptr;

        uint64_t N = static_cast<uint64_t>(numIterations);
        uint64_t I0 = static_cast<uint64_t>(initI);

        uint64_t initSum = 0;
        if (auto* cInit = dynamic_cast<ir::ConstantInt*>(stripExtensions(rec.initSumVal))) {
            initSum = static_cast<uint64_t>(cInit->getValue());
        }

        // sumI_0 = N * (2*I0 + N - 1) / 2
        uint64_t sumI = 0;
        uint64_t term2I0_N1 = (2 * I0) + N - 1;
        if (N % 2 == 0) {
            sumI = (N / 2) * term2I0_N1;
        } else {
            sumI = N * (term2I0_N1 / 2);
        }

        // sumI2_0 = sum_{k=0}^{N-1} (k + I0)^2 = sum k^2 + 2*I0*sum k + I0^2 * N
        uint64_t sumK = (N * (N - 1)) / 2;
        uint64_t sumK2 = 0;
        if (N % 2 == 0) {
            sumK2 = (N / 2) * (N - 1);
        } else {
            sumK2 = N * ((N - 1) / 2);
        }
        uint64_t term2N1 = (2 * N) - 1;
        if (sumK2 % 3 == 0) {
            sumK2 = (sumK2 / 3) * term2N1;
        } else {
            sumK2 = sumK2 * (term2N1 / 3);
        }

        uint64_t sumI2 = sumK2 + (2 * I0 * sumK) + (I0 * I0 * N);

        uint64_t sumConst = 0;
        sumConst += static_cast<uint64_t>(rec.coeffC) * N;
        sumConst += static_cast<uint64_t>(rec.coeffB) * sumI;
        sumConst += static_cast<uint64_t>(rec.coeffA) * sumI2;

        uint64_t totalFinalSum = initSum + sumConst;
        return ctx->getConstantInt(i64Ty, totalFinalSum);
    }

    return nullptr;
}

void ScalarEvolution::eliminateLoop(Loop& loop, ir::Value* closedFormVal, ir::Function& func) {
    ir::BasicBlock* preheader = loop.preheader;
    if (!preheader || !closedFormVal) return;

    // Find exit block outside loop
    ir::BasicBlock* exitBB = nullptr;
    for (ir::BasicBlock* exit : loop.exits) {
        for (ir::BasicBlock* succ : exit->getSuccessors()) {
            if (loop.blocks.find(succ) == loop.blocks.end()) {
                exitBB = succ;
                break;
            }
        }
        if (exitBB) break;
    }

    if (!exitBB) return;

    // Replace all uses of rec.sumPhi and sumNextInst with closedFormVal
    for (ir::BasicBlock* bb : loop.blocks) {
        for (auto& instPtr : bb->getInstructions()) {
            if (instPtr) {
                instPtr->replaceAllUsesWith(closedFormVal);
            }
        }
    }

    // Update PHIs in exitBB
    for (auto& instPtr : exitBB->getInstructions()) {
        if (auto* phi = dynamic_cast<ir::PhiNode*>(instPtr.get())) {
            phi->replaceAllUsesWith(closedFormVal);
        } else {
            break;
        }
    }

    // Redirect preheader terminator to jump directly to exitBB
    auto& preheaderInstrs = preheader->getInstructions();
    if (!preheaderInstrs.empty() && preheaderInstrs.back()->getOpcode() == ir::Instruction::Jmp) {
        preheaderInstrs.back()->getOperands()[0]->set(exitBB);
    }

    // Erase loop blocks from function
    auto& blocks = func.getBasicBlocks();
    auto it = blocks.begin();
    while (it != blocks.end()) {
        if (loop.blocks.find(it->get()) != loop.blocks.end()) {
            it->get()->replaceAllUsesWith(exitBB);
            it = blocks.erase(it);
        } else {
            ++it;
        }
    }
}

bool ScalarEvolution::processLoop(Loop& loop, ir::Function& func) {
    if (!loop.preheader) {
        LoopInvariantCodeMotion licm;
        licm.getOrCreatePreheader(loop, func);
    }

    if (!loop.preheader) return false;
    if (!isSafeToEliminate(loop)) return false;

    IndVar indVar;
    if (!analyzeInductionVariable(loop, indVar)) return false;

    LoopRecurrence rec;
    if (!analyzeRecurrence(loop, indVar, rec)) return false;

    ir::Value* closedFormVal = generateClosedForm(func, loop.preheader, indVar, rec);
    if (!closedFormVal) return false;

    eliminateLoop(loop, closedFormVal, func);
    return true;
}

} // namespace transforms
