#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Constant.h"
#include "ir/Use.h"
#include "transforms/CFGBuilder.h"
#include "transforms/DominatorTree.h"
#include "transforms/GVN.h"
#include <cassert>
#include <iostream>
#include <memory>

void test_gvn_cross_block_and_commutative() {
    std::cout << "--- Running GVN Cross-Block & Commutative Test ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("gvn_module", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("gvn_func", i32Ty, {i32Ty, i32Ty});
    ir::Parameter* paramA = func->getParameters().front().get();
    ir::Parameter* paramB = (*std::next(func->getParameters().begin())).get();

    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    ir::BasicBlock* nextBB = builder.createBasicBlock("nextBB", func);

    // entry:
    // t1 = add a, b
    // jmp nextBB
    builder.setInsertPoint(entry);
    ir::Instruction* t1 = builder.createAdd(paramA, paramB);
    builder.createJmp(nextBB);

    // nextBB:
    // t2 = add b, a (commutative equivalent of t1!)
    // res = add t2, 10
    // ret res
    builder.setInsertPoint(nextBB);
    ir::Instruction* t2 = builder.createAdd(paramB, paramA);
    ir::Instruction* res = builder.createAdd(t2, ctx->getConstantInt(ctx->getIntegerType(32), 10));
    builder.createRet(res);

    transforms::CFGBuilder::run(*func);

    transforms::GVN gvn;
    bool changed = gvn.run(*func);

    assert(changed);
    // t2 should have been replaced with t1
    assert(res->getOperands()[0]->get() == t1);

    std::cout << "--- GVN Cross-Block & Commutative Test Passed ---" << std::endl;
}

void test_gvn_constant_fold() {
    std::cout << "--- Running GVN Constant Fold Test ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("gvn_fold_module", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("gvn_fold_func", i32Ty, {});

    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    ir::Instruction* constAdd = builder.createAdd(ctx->getConstantInt(ctx->getIntegerType(32), 15), ctx->getConstantInt(ctx->getIntegerType(32), 25));
    builder.createRet(constAdd);

    transforms::CFGBuilder::run(*func);

    transforms::GVN gvn;
    bool changed = gvn.run(*func);

    assert(changed);
    // Return value should now be ConstantInt(40)
    ir::Instruction* retInst = entry->getInstructions().back().get();
    auto* retVal = dynamic_cast<ir::ConstantInt*>(retInst->getOperands()[0]->get());
    assert(retVal != nullptr);
    assert(retVal->getValue() == 40);

    std::cout << "--- GVN Constant Fold Test Passed ---" << std::endl;
}

int main() {
    test_gvn_cross_block_and_commutative();
    test_gvn_constant_fold();
    return 0;
}
