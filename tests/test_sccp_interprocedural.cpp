#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Constant.h"
#include "ir/Use.h"
#include "ir/PhiNode.h"
#include "transforms/CFGBuilder.h"
#include "transforms/SCCP.h"
#include <cassert>
#include <iostream>
#include <memory>

void test_pure_factorial_eval() {
    std::cout << "--- Test 1: Pure Recursive Factorial Evaluation ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod1", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i64Ty = ctx->getIntegerType(64);

    // long long tail_factorial(long long n, long long acc)
    ir::Function* factFunc = builder.createFunction("tail_factorial", i64Ty, {i64Ty, i64Ty});
    auto pIt = factFunc->getParameters().begin();
    ir::Parameter* paramN = (pIt++)->get();
    ir::Parameter* paramAcc = pIt->get();

    ir::BasicBlock* bb_entry = builder.createBasicBlock("entry", factFunc);
    ir::BasicBlock* bb_base = builder.createBasicBlock("base", factFunc);
    ir::BasicBlock* bb_recur = builder.createBasicBlock("recur", factFunc);

    builder.setInsertPoint(bb_entry);
    ir::Instruction* cond = builder.createCsle(paramN, ctx->getConstantInt(ctx->getIntegerType(64), 1));
    builder.createBr(cond, bb_base, bb_recur);

    builder.setInsertPoint(bb_base);
    builder.createRet(paramAcc);

    builder.setInsertPoint(bb_recur);
    ir::Instruction* n_next = builder.createSub(paramN, ctx->getConstantInt(ctx->getIntegerType(64), 1));
    ir::Instruction* acc_next = builder.createMul(paramAcc, paramN);
    ir::Instruction* recCall = builder.createCall(factFunc, {n_next, acc_next});
    builder.createRet(recCall);

    // caller: int main() { return tail_factorial(5, 1); }
    ir::Type* i32Ty = ctx->getIntegerType(32);
    ir::Function* mainFunc = builder.createFunction("main", i32Ty, {});
    ir::BasicBlock* m_entry = builder.createBasicBlock("m_entry", mainFunc);
    builder.setInsertPoint(m_entry);

    ir::Constant* c5 = ctx->getConstantInt(ctx->getIntegerType(64), 5);
    ir::Constant* c1 = ctx->getConstantInt(ctx->getIntegerType(64), 1);
    ir::Instruction* mainCall = builder.createCall(factFunc, {c5, c1});
    ir::Instruction* truncRes = builder.createTruncD(mainCall, i32Ty);
    builder.createRet(truncRes);

    transforms::CFGBuilder::run(*factFunc);
    transforms::CFGBuilder::run(*mainFunc);

    transforms::SCCP sccp;
    bool changed = sccp.run(*mainFunc);
    assert(changed);

    // Check that main's call instruction was removed and constant 120 is returned
    bool callRemoved = true;
    bool foundConst120 = false;
    for (const auto& inst : m_entry->getInstructions()) {
        if (inst->getOpcode() == ir::Instruction::Call) {
            callRemoved = false;
        }
        if (inst->getOpcode() == ir::Instruction::Ret && !inst->getOperands().empty()) {
            if (auto* ci = dynamic_cast<ir::ConstantInt*>(inst->getOperands()[0]->get())) {
                if (ci->getValue() == 120) foundConst120 = true;
            }
        }
        if (inst->getOpcode() == ir::Instruction::TruncD && !inst->getOperands().empty()) {
            if (auto* ci = dynamic_cast<ir::ConstantInt*>(inst->getOperands()[0]->get())) {
                if (ci->getValue() == 120) foundConst120 = true;
            }
        }
    }
    assert(callRemoved);
    assert(foundConst120);

    std::cout << "--- Test 1 Passed: Pure Recursive Factorial Folded to Constant 120 ---" << std::endl;
}

void test_dynamic_arg_rejection() {
    std::cout << "--- Test 2: Dynamic Argument Rejection ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod2", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i64Ty = ctx->getIntegerType(64);
    ir::Function* pureFunc = builder.createFunction("pure_func", i64Ty, {i64Ty});
    ir::Parameter* pX = pureFunc->getParameters().front().get();

    ir::BasicBlock* p_bb = builder.createBasicBlock("entry", pureFunc);
    builder.setInsertPoint(p_bb);
    ir::Instruction* res = builder.createAdd(pX, ctx->getConstantInt(ctx->getIntegerType(64), 10));
    builder.createRet(res);

    ir::Function* caller = builder.createFunction("caller", i64Ty, {i64Ty});
    ir::Parameter* pArg = caller->getParameters().front().get();
    ir::BasicBlock* c_bb = builder.createBasicBlock("entry", caller);
    builder.setInsertPoint(c_bb);

    ir::Instruction* callInst = builder.createCall(pureFunc, {pArg});
    builder.createRet(callInst);

    transforms::CFGBuilder::run(*pureFunc);
    transforms::CFGBuilder::run(*caller);

    transforms::SCCP sccp;
    bool changed = sccp.run(*caller);
    assert(!changed); // Call must remain because argument is dynamic

    std::cout << "--- Test 2 Passed: Dynamic Argument Call Correctly Not Folded ---" << std::endl;
}

void test_side_effect_rejection() {
    std::cout << "--- Test 3: Side-Effecting Store Rejection ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod3", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i64Ty = ctx->getIntegerType(64);
    ir::Function* impureFunc = builder.createFunction("impure_func", i64Ty, {i64Ty});
    ir::Parameter* pX = impureFunc->getParameters().front().get();

    ir::BasicBlock* ip_bb = builder.createBasicBlock("entry", impureFunc);
    builder.setInsertPoint(ip_bb);
    ir::Instruction* allocInst = builder.createAlloc(ctx->getConstantInt(ctx->getIntegerType(64), 8), i64Ty);
    builder.createStore(pX, allocInst);
    builder.createRet(pX);

    ir::Function* caller = builder.createFunction("caller", i64Ty, {});
    ir::BasicBlock* c_bb = builder.createBasicBlock("entry", caller);
    builder.setInsertPoint(c_bb);

    ir::Instruction* callInst = builder.createCall(impureFunc, {ctx->getConstantInt(ctx->getIntegerType(64), 42)});
    builder.createRet(callInst);

    transforms::CFGBuilder::run(*impureFunc);
    transforms::CFGBuilder::run(*caller);

    transforms::SCCP sccp;
    bool changed = sccp.run(*caller);
    assert(!changed); // Impure call with Alloc/Store must not be evaluated

    std::cout << "--- Test 3 Passed: Side-Effecting Call Correctly Not Folded ---" << std::endl;
}

void test_infinite_loop_budget_rejection() {
    std::cout << "--- Test 4: Infinite Loop Budget Exceeded Rejection ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod4", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i64Ty = ctx->getIntegerType(64);
    ir::Function* infFunc = builder.createFunction("inf_loop", i64Ty, {i64Ty});

    ir::BasicBlock* bb_loop = builder.createBasicBlock("loop", infFunc);
    builder.setInsertPoint(bb_loop);
    builder.createJmp(bb_loop); // infinite loop

    ir::Function* caller = builder.createFunction("caller", i64Ty, {});
    ir::BasicBlock* c_bb = builder.createBasicBlock("entry", caller);
    builder.setInsertPoint(c_bb);

    ir::Instruction* callInst = builder.createCall(infFunc, {ctx->getConstantInt(ctx->getIntegerType(64), 1)});
    builder.createRet(callInst);

    transforms::CFGBuilder::run(*infFunc);
    transforms::CFGBuilder::run(*caller);

    transforms::SCCP sccp;
    bool changed = sccp.run(*caller);
    assert(!changed); // Infinite loop must hit step budget limit and fail safely

    std::cout << "--- Test 4 Passed: Step Budget Exceeded Handled Safely ---" << std::endl;
}

void test_value_based_memoization() {
    std::cout << "--- Test 5: Value-Based Memoization Cache ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod5", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i64Ty = ctx->getIntegerType(64);
    ir::Function* squareFunc = builder.createFunction("square", i64Ty, {i64Ty});
    ir::Parameter* pX = squareFunc->getParameters().front().get();

    ir::BasicBlock* bb = builder.createBasicBlock("entry", squareFunc);
    builder.setInsertPoint(bb);
    ir::Instruction* res = builder.createMul(pX, pX);
    builder.createRet(res);

    ir::Function* caller = builder.createFunction("caller", i64Ty, {});
    ir::BasicBlock* c_bb = builder.createBasicBlock("entry", caller);
    builder.setInsertPoint(c_bb);

    // Create 3 separate ConstantInt instances with equal value (42)
    ir::IntegerType* ity = ctx->getIntegerType(64);
    ir::Constant* c1 = new ir::ConstantInt(ity, 42);
    ir::Constant* c2 = new ir::ConstantInt(ity, 42);
    ir::Constant* c3 = new ir::ConstantInt(ity, 42);

    ir::Instruction* call1 = builder.createCall(squareFunc, {c1});
    ir::Instruction* call2 = builder.createCall(squareFunc, {c2});
    ir::Instruction* call3 = builder.createCall(squareFunc, {c3});
    ir::Instruction* sum1 = builder.createAdd(call1, call2);
    ir::Instruction* sum2 = builder.createAdd(sum1, call3);
    builder.createRet(sum2);

    transforms::CFGBuilder::run(*squareFunc);
    transforms::CFGBuilder::run(*caller);

    transforms::SCCP sccp;
    bool changed = sccp.run(*caller);
    assert(changed);

    // Verify cache size in SCCP evaluation context is 1 because all 3 calls had value-equal parameters
    const auto& evalCtx = sccp.getEvaluationContext();
    assert(evalCtx.evalCache.size() == 1);

    // Clean up heap allocated test constants
    delete c1;
    delete c2;
    delete c3;

    std::cout << "--- Test 5 Passed: Value-Based Cache Hits for Separately Created Constants ---" << std::endl;
}

void test_non_second_block_header() {
    std::cout << "--- Test 6: Non-Second-Block Loop Header Iteration Counting ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod6", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i64Ty = ctx->getIntegerType(64);
    ir::Function* loopFunc = builder.createFunction("loop_func", i64Ty, {i64Ty});
    ir::Parameter* pN = loopFunc->getParameters().front().get();

    // CFG layout: entry -> dummy1 -> dummy2 -> preheader -> header -> body -> latch -> header / exit
    ir::BasicBlock* bb_entry = builder.createBasicBlock("entry", loopFunc);
    ir::BasicBlock* bb_dummy1 = builder.createBasicBlock("dummy1", loopFunc);
    ir::BasicBlock* bb_dummy2 = builder.createBasicBlock("dummy2", loopFunc);
    ir::BasicBlock* bb_preheader = builder.createBasicBlock("preheader", loopFunc);
    ir::BasicBlock* bb_header = builder.createBasicBlock("header", loopFunc);
    ir::BasicBlock* bb_body = builder.createBasicBlock("body", loopFunc);
    ir::BasicBlock* bb_latch = builder.createBasicBlock("latch", loopFunc);
    ir::BasicBlock* bb_exit = builder.createBasicBlock("exit", loopFunc);

    builder.setInsertPoint(bb_entry);
    builder.createJmp(bb_dummy1);

    builder.setInsertPoint(bb_dummy1);
    builder.createJmp(bb_dummy2);

    builder.setInsertPoint(bb_dummy2);
    builder.createJmp(bb_preheader);

    builder.setInsertPoint(bb_preheader);
    builder.createJmp(bb_header);

    builder.setInsertPoint(bb_header);
    auto phiI_owner = std::make_unique<ir::PhiNode>(i64Ty, 0, nullptr, bb_header);
    ir::PhiNode* phiI = phiI_owner.get();
    bb_header->getInstructions().push_back(std::move(phiI_owner));

    auto phiSum_owner = std::make_unique<ir::PhiNode>(i64Ty, 0, nullptr, bb_header);
    ir::PhiNode* phiSum = phiSum_owner.get();
    bb_header->getInstructions().push_back(std::move(phiSum_owner));

    phiI->addIncoming(ctx->getConstantInt(ctx->getIntegerType(64), 0), bb_preheader);
    phiSum->addIncoming(ctx->getConstantInt(ctx->getIntegerType(64), 0), bb_preheader);

    ir::Instruction* cond = builder.createCslt(phiI, pN);
    builder.createBr(cond, bb_body, bb_exit);

    builder.setInsertPoint(bb_body);
    ir::Instruction* sumNext = builder.createAdd(phiSum, phiI);
    ir::Instruction* iNext = builder.createAdd(phiI, ctx->getConstantInt(ctx->getIntegerType(64), 1));
    builder.createJmp(bb_latch);

    builder.setInsertPoint(bb_latch);
    phiI->addIncoming(iNext, bb_latch);
    phiSum->addIncoming(sumNext, bb_latch);
    builder.createJmp(bb_header);

    builder.setInsertPoint(bb_exit);
    builder.createRet(phiSum);

    ir::Function* caller = builder.createFunction("caller", i64Ty, {});
    ir::BasicBlock* c_bb = builder.createBasicBlock("entry", caller);
    builder.setInsertPoint(c_bb);

    ir::Instruction* callInst = builder.createCall(loopFunc, {ctx->getConstantInt(ctx->getIntegerType(64), 100)});
    builder.createRet(callInst);

    transforms::CFGBuilder::run(*loopFunc);
    transforms::CFGBuilder::run(*caller);

    transforms::SCCP sccp;
    bool changed = sccp.run(*caller);
    assert(changed);

    // Sum of 0..99 is 4950
    bool foundConst4950 = false;
    for (const auto& inst : c_bb->getInstructions()) {
        if (inst->getOpcode() == ir::Instruction::Ret && !inst->getOperands().empty()) {
            if (auto* ci = dynamic_cast<ir::ConstantInt*>(inst->getOperands()[0]->get())) {
                if (ci->getValue() == 4950) foundConst4950 = true;
            }
        }
    }
    assert(foundConst4950);

    std::cout << "--- Test 6 Passed: Non-Second-Block Header Backedge Counting Functioned Correctly ---" << std::endl;
}

void test_exact_wrapping_and_extsw() {
    std::cout << "--- Test 7: Exact i32 Wrapping and ExtSW Sign Extension ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod7", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);
    ir::Type* i64Ty = ctx->getIntegerType(64);
    ir::Function* wrapFunc = builder.createFunction("wrap_func", i64Ty, {});

    ir::BasicBlock* bb = builder.createBasicBlock("entry", wrapFunc);
    builder.setInsertPoint(bb);

    // INT32_MAX = 2147483647 (0x7FFFFFFF)
    // INT32_MAX + 1 in i32 wraps to -2147483648 (0x80000000)
    // extsw(-2147483648) -> -2147483648 in i64
    ir::Constant* c_max = ctx->getConstantInt(ctx->getIntegerType(32), 2147483647ULL);
    ir::Constant* c_one = ctx->getConstantInt(ctx->getIntegerType(32), 1ULL);
    ir::Instruction* addW = builder.createAdd(c_max, c_one);
    ir::Instruction* extS = builder.createExtSW(addW, i64Ty);
    builder.createRet(extS);

    transforms::CFGBuilder::run(*wrapFunc);

    transforms::SCCP sccp;
    bool changed = sccp.run(*wrapFunc);
    assert(changed);

    bool foundWrappedVal = false;
    for (const auto& inst : bb->getInstructions()) {
        if (inst->getOpcode() == ir::Instruction::Ret && !inst->getOperands().empty()) {
            if (auto* ci = dynamic_cast<ir::ConstantInt*>(inst->getOperands()[0]->get())) {
                // Check exact bit pattern 0xFFFFFFFF80000000ULL
                if (ci->getValue() == 0xFFFFFFFF80000000ULL) foundWrappedVal = true;
            }
        }
    }
    assert(foundWrappedVal);

    std::cout << "--- Test 7 Passed: Exact i32 Wrapping and ExtSW Preserved ---" << std::endl;
}

int main() {
    test_pure_factorial_eval();
    test_dynamic_arg_rejection();
    test_side_effect_rejection();
    test_infinite_loop_budget_rejection();
    test_value_based_memoization();
    test_non_second_block_header();
    test_exact_wrapping_and_extsw();
    std::cout << "=== All Interprocedural SCCP Evaluation Tests Passed ===" << std::endl;
    return 0;
}
