#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/Module.h"
#include "ir/SIMDInstruction.h"
#include "transforms/SLPVectorizer.h"
#include <cassert>
#include <iostream>
#include <memory>

using namespace ir;

static void testMultiLevelSLPTreeAndCostModel() {
    auto context = std::make_shared<IRContext>();
    Module module("advanced_slp", context);
    IRBuilder builder(context);
    builder.setModule(&module);

    auto* i32 = context->getIntegerType(32);
    auto* ptrTy = context->getPointerType(i32);

    Function* function = builder.createFunction("multi_level_slp", context->getVoidType(), {ptrTy, ptrTy, ptrTy});
    auto paramIt = function->getParameters().begin();
    Value* srcA = paramIt->get(); ++paramIt;
    Value* srcB = paramIt->get(); ++paramIt;
    Value* dst = paramIt->get();

    BasicBlock* entry = builder.createBasicBlock("entry", function);
    builder.setInsertPoint(entry);

    auto addr = [&](Value* base, unsigned offset) {
        return builder.createAdd(base, context->getConstantInt(context->getIntegerType(64), offset), ptrTy);
    };

    // 4-lane multi-level SLP tree:
    // Level 1: load A[0..3] and load B[0..3]
    // Level 2: add = A + B
    // Level 3: mul = add * A
    // Level 4: store mul to dst[0..3]
    std::vector<Instruction*> mulResults;
    for (unsigned lane = 0; lane < 4; ++lane) {
        auto* aLd = builder.createLoad(addr(srcA, lane * 4));
        auto* bLd = builder.createLoad(addr(srcB, lane * 4));
        auto* addInst = builder.createAdd(aLd, bLd);
        auto* mulInst = builder.createMul(addInst, aLd);
        mulResults.push_back(mulInst);
        builder.createStore(mulInst, addr(dst, lane * 4));
    }
    builder.createRet(nullptr);

    transforms::SLPVectorizer slp;
    bool transformed = slp.performTransformation(*function);
    assert(transformed && "Multi-level SLP tree from contiguous stores must be vectorized");

    std::cout << "testMultiLevelSLPTreeAndCostModel passed!\n";
}

int main() {
    testMultiLevelSLPTreeAndCostModel();
    std::cout << "All advanced SLP tests passed!\n";
    return 0;
}
