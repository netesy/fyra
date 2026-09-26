#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/Module.h"
#include "transforms/LoopVectorizer.h"
#include "transforms/CFGBuilder.h"
#include "ir/PhiNode.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>

using namespace ir;

static void testLoopVectorizerUnrollingAndFPReassociation() {
    // 1. Strict FP mode test
    {
        auto context = std::make_shared<IRContext>();
        Module module("strict_fp", context);
        IRBuilder builder(context);
        builder.setModule(&module);

        auto* f32 = context->getFloatType();
        auto* i32 = context->getIntegerType(32);
        auto* ptrTy = context->getPointerType(f32);

        Function* function = builder.createFunction("strict_fp_reduction", f32, {ptrTy, i32});
        auto paramIt = function->getParameters().begin();
        Value* arr = paramIt->get(); ++paramIt;
        Value* n = paramIt->get();

        BasicBlock* entry = builder.createBasicBlock("entry", function);
        BasicBlock* header = builder.createBasicBlock("header", function);
        BasicBlock* body = builder.createBasicBlock("body", function);
        BasicBlock* exit = builder.createBasicBlock("exit", function);

        builder.setInsertPoint(entry);
        builder.createJmp(header);

        builder.setInsertPoint(header);
        auto phiIOwner = std::make_unique<PhiNode>(i32, 0, nullptr, header);
        PhiNode* phiI = phiIOwner.get();
        header->getInstructions().push_back(std::move(phiIOwner));
        phiI->addIncoming(context->getConstantInt(i32, 0), entry);

        auto phiSumOwner = std::make_unique<PhiNode>(f32, 0, nullptr, header);
        PhiNode* phiSum = phiSumOwner.get();
        header->getInstructions().push_back(std::move(phiSumOwner));
        phiSum->addIncoming(context->getConstantFP(f32, 0.0), entry);

        Instruction* cond = builder.createCslt(phiI, n);
        builder.createBr(cond, body, exit);

        builder.setInsertPoint(body);
        Instruction* i64Idx = builder.createExtSW(phiI, context->getIntegerType(64));
        Instruction* byteOff = builder.createMul(i64Idx, context->getConstantInt(context->getIntegerType(64), 4));
        Instruction* ptr = builder.createAdd(arr, byteOff);
        Instruction* val = builder.createLoads(ptr);
        Instruction* nextSum = builder.createFAdd(phiSum, val);
        Instruction* nextI = builder.createAdd(phiI, context->getConstantInt(i32, 1));

        phiI->addIncoming(nextI, body);
        phiSum->addIncoming(nextSum, body);
        builder.createJmp(header);

        builder.setInsertPoint(exit);
        builder.createRet(phiSum);

        transforms::CFGBuilder::run(*function);

        // Under strict FP mode (default), FP reduction vectorization is rejected to preserve IEEE order
        transforms::LoopVectorizer strictVectorizer(nullptr, {target::Arch::X64, target::OS::Linux}, false);
        bool strictVectorized = strictVectorizer.performTransformation(*function);
        assert(!strictVectorized && "Strict FP reduction must not be silently reassociated");

        // Under reassociable FP mode, vectorization is allowed
        transforms::LoopVectorizer fastVectorizer(nullptr, {target::Arch::X64, target::OS::Linux}, true);
        bool fastVectorized = fastVectorizer.performTransformation(*function);
        assert(fastVectorized && "Fast FP mode must permit FP reduction vectorization");
    }

    std::cout << "testLoopVectorizerUnrollingAndFPReassociation passed!\n";
}

int main() {
    testLoopVectorizerUnrollingAndFPReassociation();
    std::cout << "All advanced loop vectorizer tests passed!\n";
    return 0;
}
