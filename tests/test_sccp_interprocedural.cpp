#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Constant.h"
#include "ir/Use.h"
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

    // Check that main's call instruction was replaced with constant 120 (5! = 120)
    bool callRemoved = true;
    bool foundConst120 = false;
    for (const auto& inst : m_entry->getInstructions()) {
        if (inst->getOpcode() == ir::Instruction::Call) {
            callRemoved = false;
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

int main() {
    test_pure_factorial_eval();
    test_dynamic_arg_rejection();
    test_side_effect_rejection();
    test_infinite_loop_budget_rejection();
    std::cout << "=== All Interprocedural SCCP Evaluation Tests Passed ===" << std::endl;
    return 0;
}
