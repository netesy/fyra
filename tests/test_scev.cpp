#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Constant.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include "transforms/CFGBuilder.h"
#include "transforms/ScalarEvolution.h"
#include <cassert>
#include <iostream>
#include <memory>

void test_scev_linear_sum() {
    std::cout << "--- Testing SCEV Linear Sum Reduction ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("scev_linear_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::IntegerType* i64Ty = ctx->getIntegerType(64);

    ir::Function* func = builder.createFunction("linear_sum", i64Ty, {});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    ir::BasicBlock* header = builder.createBasicBlock("header", func);
    ir::BasicBlock* body = builder.createBasicBlock("body", func);
    ir::BasicBlock* exit = builder.createBasicBlock("exit", func);

    builder.setInsertPoint(entry);
    builder.createJmp(header);

    builder.setInsertPoint(header);
    auto phiI = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, header);
    ir::PhiNode* rawPhiI = phiI.get();
    header->getInstructions().push_back(std::move(phiI));

    auto phiSum = std::make_unique<ir::PhiNode>(i64Ty, 0, nullptr, header);
    ir::PhiNode* rawPhiSum = phiSum.get();
    header->getInstructions().push_back(std::move(phiSum));

    rawPhiI->addIncoming(ctx->getConstantInt(i32Ty, 0), entry);
    rawPhiSum->addIncoming(ctx->getConstantInt(i64Ty, 0), entry);

    ir::Instruction* cond = builder.createCslt(rawPhiI, ctx->getConstantInt(i32Ty, 10));
    builder.createBr(cond, body, exit);

    builder.setInsertPoint(body);
    ir::Instruction* term = builder.createMul(rawPhiI, ctx->getConstantInt(i32Ty, 2));
    ir::Instruction* termExt = builder.createExtSW(term, i64Ty);
    ir::Instruction* sumNext = builder.createAdd(rawPhiSum, termExt);
    ir::Instruction* iNext = builder.createAdd(rawPhiI, ctx->getConstantInt(i32Ty, 1));

    rawPhiI->addIncoming(iNext, body);
    rawPhiSum->addIncoming(sumNext, body);
    builder.createJmp(header);

    builder.setInsertPoint(exit);
    builder.createRet(rawPhiSum);

    transforms::CFGBuilder::run(*func);

    transforms::ScalarEvolution scev;
    assert(scev.run(*func));

    // sum_{i=0}^9 (2i) = 90
    ir::Instruction* retInst = exit->getInstructions().back().get();
    assert(retInst->getOpcode() == ir::Instruction::Ret);
    auto* retConst = dynamic_cast<ir::ConstantInt*>(retInst->getOperands()[0]->get());
    assert(retConst != nullptr);
    assert(retConst->getValue() == 90);

    std::cout << "--- SCEV Linear Sum Reduction Passed ---" << std::endl;
}

void test_scev_negative_side_effect() {
    std::cout << "--- Testing SCEV Negative Side Effect Safety ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("scev_neg_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::IntegerType* i64Ty = ctx->getIntegerType(64);

    ir::Function* dummyCallee = builder.createFunction("dummy", ctx->getVoidType(), {});
    ir::BasicBlock* d_entry = builder.createBasicBlock("d_entry", dummyCallee);
    builder.setInsertPoint(d_entry);
    builder.createRet(nullptr);

    ir::Function* func = builder.createFunction("side_effect_loop", i64Ty, {});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    ir::BasicBlock* header = builder.createBasicBlock("header", func);
    ir::BasicBlock* body = builder.createBasicBlock("body", func);
    ir::BasicBlock* exit = builder.createBasicBlock("exit", func);

    builder.setInsertPoint(entry);
    builder.createJmp(header);

    builder.setInsertPoint(header);
    auto phiI = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, header);
    ir::PhiNode* rawPhiI = phiI.get();
    header->getInstructions().push_back(std::move(phiI));

    rawPhiI->addIncoming(ctx->getConstantInt(i32Ty, 0), entry);
    ir::Instruction* cond = builder.createCslt(rawPhiI, ctx->getConstantInt(i32Ty, 10));
    builder.createBr(cond, body, exit);

    builder.setInsertPoint(body);
    builder.createCall(dummyCallee, {}); // Call has side effects!
    ir::Instruction* iNext = builder.createAdd(rawPhiI, ctx->getConstantInt(i32Ty, 1));
    rawPhiI->addIncoming(iNext, body);
    builder.createJmp(header);

    builder.setInsertPoint(exit);
    builder.createRet(rawPhiI);

    transforms::CFGBuilder::run(*func);

    transforms::ScalarEvolution scev;
    assert(!scev.run(*func)); // Must NOT run on loop with call side effect!

    std::cout << "--- SCEV Negative Side Effect Safety Passed ---" << std::endl;
}

int main() {
    test_scev_linear_sum();
    test_scev_negative_side_effect();
    return 0;
}
