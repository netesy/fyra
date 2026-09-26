#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/Module.h"
#include "ir/PhiNode.h"
#include "transforms/LoopStrengthReduction.h"
#include "transforms/ScalarEvolution.h"
#include "transforms/CFGBuilder.h"
#include <cassert>
#include <iostream>
#include <memory>

using namespace ir;

static void testAffineAddressingScalesAndInduction() {
    auto context = std::make_shared<IRContext>();
    Module module("lsr_test", context);
    IRBuilder builder(context);
    builder.setModule(&module);

    auto* i32 = context->getIntegerType(32);
    auto* i64 = context->getIntegerType(64);
    auto* ptrTy = context->getPointerType(i32);

    Function* function = builder.createFunction("test_lsr_kernel", i32, {ptrTy, i32});
    auto paramIt = function->getParameters().begin();
    Value* basePtr = paramIt->get(); ++paramIt;
    Value* nVal = paramIt->get();

    BasicBlock* entry = builder.createBasicBlock("entry", function);
    BasicBlock* header = builder.createBasicBlock("header", function);
    BasicBlock* body = builder.createBasicBlock("body", function);
    BasicBlock* latch = builder.createBasicBlock("latch", function);
    BasicBlock* exit = builder.createBasicBlock("exit", function);

    builder.setInsertPoint(entry);
    builder.createJmp(header);

    builder.setInsertPoint(header);
    auto phiIOwner = std::make_unique<PhiNode>(i32, 0, nullptr, header);
    PhiNode* phiI = phiIOwner.get();
    header->getInstructions().push_back(std::move(phiIOwner));
    phiI->addIncoming(context->getConstantInt(i32, 0), entry);

    auto phiSumOwner = std::make_unique<PhiNode>(i32, 0, nullptr, header);
    PhiNode* phiSum = phiSumOwner.get();
    header->getInstructions().push_back(std::move(phiSumOwner));
    phiSum->addIncoming(context->getConstantInt(i32, 0), entry);

    Instruction* cond = builder.createCslt(phiI, nVal);
    builder.createBr(cond, body, exit);

    builder.setInsertPoint(body);
    // Affine expression: base + i * 4 + 8
    Instruction* idx64 = builder.createExtSW(phiI, i64);
    Instruction* mulScale = builder.createMul(idx64, context->getConstantInt(i64, 4));
    Instruction* offset = builder.createAdd(mulScale, context->getConstantInt(i64, 8));
    Instruction* addr = builder.createAdd(basePtr, offset);
    Instruction* val = builder.createLoad(addr);
    Instruction* nextSum = builder.createAdd(phiSum, val);

    builder.createJmp(latch);

    builder.setInsertPoint(latch);
    Instruction* nextI = builder.createAdd(phiI, context->getConstantInt(i32, 1));
    phiI->addIncoming(nextI, latch);
    phiSum->addIncoming(nextSum, latch);
    builder.createJmp(header);

    builder.setInsertPoint(exit);
    builder.createRet(phiSum);

    transforms::CFGBuilder::run(*function);
    transforms::LoopStrengthReduction lsr;
    bool transformed = lsr.performTransformation(*function);

    assert(transformed && "LSR must reduce affine induction expression");

    // Verify derived PHI was inserted in header
    bool foundDerivedPhi = false;
    for (auto& inst : header->getInstructions()) {
        if (auto* phi = dynamic_cast<PhiNode*>(inst.get())) {
            if (phi != phiI && phi != phiSum) {
                foundDerivedPhi = true;
                break;
            }
        }
    }
    assert(foundDerivedPhi && "Derived PHI must be present in loop header");
    std::cout << "testAffineAddressingScalesAndInduction passed\n";
}

int main() {
    testAffineAddressingScalesAndInduction();
    std::cout << "All LSR tests passed!\n";
    return 0;
}
