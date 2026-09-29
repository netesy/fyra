#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/PhiNode.h"
#include "ir/Constant.h"
#include "transforms/CFGBuilder.h"
#include "transforms/FunctionInliner.h"
#include <cassert>
#include <iostream>
#include <memory>

void test_multiblock_inlining() {
    std::cout << "--- Running Multi-Block Inliner Test ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("inliner_module", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);

    // Create callee:
    // int callee(int x) {
    //   if (x < 10) return x + 1;
    //   else return x + 2;
    // }
    ir::Function* callee = builder.createFunction("callee_func", i32Ty, {i32Ty});
    ir::Parameter* paramX = callee->getParameters().front().get();

    ir::BasicBlock* c_entry = builder.createBasicBlock("c_entry", callee);
    ir::BasicBlock* c_then = builder.createBasicBlock("c_then", callee);
    ir::BasicBlock* c_else = builder.createBasicBlock("c_else", callee);

    builder.setInsertPoint(c_entry);
    ir::Instruction* cond = builder.createCslt(paramX, ctx->getConstantInt(ctx->getIntegerType(32), 10));
    builder.createBr(cond, c_then, c_else);

    builder.setInsertPoint(c_then);
    ir::Instruction* res1 = builder.createAdd(paramX, ctx->getConstantInt(ctx->getIntegerType(32), 1));
    builder.createRet(res1);

    builder.setInsertPoint(c_else);
    ir::Instruction* res2 = builder.createAdd(paramX, ctx->getConstantInt(ctx->getIntegerType(32), 2));
    builder.createRet(res2);

    // Create caller:
    // int caller(int arg) {
    //   int val = callee(arg);
    //   return val * 2;
    // }
    ir::Function* caller = builder.createFunction("caller_func", i32Ty, {i32Ty});
    ir::Parameter* argA = caller->getParameters().front().get();

    ir::BasicBlock* k_entry = builder.createBasicBlock("k_entry", caller);
    builder.setInsertPoint(k_entry);

    ir::Instruction* callInst = builder.createCall(callee, {argA});
    ir::Instruction* mult = builder.createMul(callInst, ctx->getConstantInt(ctx->getIntegerType(32), 2));
    builder.createRet(mult);

    transforms::CFGBuilder::run(*callee);
    transforms::CFGBuilder::run(*caller);

    std::cout << "[TEST] Running inliner..." << std::endl;
    transforms::FunctionInliner inliner(30);
    bool changed = inliner.runOnModule(module);
    std::cout << "[TEST] Inliner returned " << changed << std::endl;

    assert(changed);

    // Verify call instruction was removed from caller
    bool foundCall = false;
    for (const auto& bb : caller->getBasicBlocks()) {
        for (const auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == ir::Instruction::Call) {
                foundCall = true;
            }
        }
    }
    assert(!foundCall);

    std::cout << "--- Multi-Block Inliner Test Passed ---" << std::endl;
}

void test_recursion_rejection() {
    std::cout << "--- Running Recursion Rejection Test ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("rec_module", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);

    // Create recursive function:
    // int rec(int n) { if (n <= 0) return 0; else return rec(n - 1); }
    ir::Function* recFunc = builder.createFunction("rec_func", i32Ty, {i32Ty});
    ir::Parameter* paramN = recFunc->getParameters().front().get();

    ir::BasicBlock* r_entry = builder.createBasicBlock("r_entry", recFunc);
    ir::BasicBlock* r_base = builder.createBasicBlock("r_base", recFunc);
    ir::BasicBlock* r_step = builder.createBasicBlock("r_step", recFunc);

    builder.setInsertPoint(r_entry);
    ir::Instruction* cond = builder.createCsle(paramN, ctx->getConstantInt(ctx->getIntegerType(32), 0));
    builder.createBr(cond, r_base, r_step);

    builder.setInsertPoint(r_base);
    builder.createRet(ctx->getConstantInt(ctx->getIntegerType(32), 0));

    builder.setInsertPoint(r_step);
    ir::Instruction* subN = builder.createSub(paramN, ctx->getConstantInt(ctx->getIntegerType(32), 1));
    ir::Instruction* recCall = builder.createCall(recFunc, {subN});
    builder.createRet(recCall);

    transforms::CFGBuilder::run(*recFunc);

    transforms::FunctionInliner inliner(30);
    bool changed = inliner.runOnModule(module);

    assert(!changed); // Recursion must be rejected

    std::cout << "--- Recursion Rejection Test Passed ---" << std::endl;
}

#include "codegen/regalloc/LivenessAnalysis.h"
#include "codegen/regalloc/LiveIntervalAnalysis.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"

void test_safe_leaf_helper_in_loop() {
    std::cout << "--- Testing Safe Leaf Helper Inlining Inside Loop ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("leaf_loop_module", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);

    // Leaf helper
    ir::Function* leaf = builder.createFunction("leaf_helper", i32Ty, {i32Ty, i32Ty});
    auto pIt = leaf->getParameters().begin();
    ir::Value* pA = (pIt++)->get();
    ir::Value* pB = (pIt++)->get();
    ir::BasicBlock* l_entry = builder.createBasicBlock("l_entry", leaf);
    builder.setInsertPoint(l_entry);
    ir::Instruction* res = builder.createAdd(pA, pB);
    builder.createRet(res);

    // Loop caller
    ir::Function* caller = builder.createFunction("leaf_caller", i32Ty, {i32Ty});
    ir::Value* pN = caller->getParameters().front().get();
    ir::BasicBlock* c_entry = builder.createBasicBlock("c_entry", caller);
    ir::BasicBlock* c_loop = builder.createBasicBlock("c_loop", caller);
    ir::BasicBlock* c_body = builder.createBasicBlock("c_body", caller);
    ir::BasicBlock* c_exit = builder.createBasicBlock("c_exit", caller);

    builder.setInsertPoint(c_entry);
    builder.createJmp(c_loop);

    builder.setInsertPoint(c_loop);
    ir::PhiNode* phiI = builder.createPhi(i32Ty, 0, nullptr);
    ir::PhiNode* phiSum = builder.createPhi(i32Ty, 0, nullptr);
    ir::Instruction* cond = builder.createCslt(phiI, pN);
    builder.createBr(cond, c_body, c_exit);

    builder.setInsertPoint(c_body);
    ir::Instruction* callInst = builder.createCall(leaf, {phiI, phiSum});
    ir::Instruction* iNext = builder.createAdd(phiI, ctx->getConstantInt(ctx->getIntegerType(32), 1));
    builder.createJmp(c_loop);

    phiI->addIncoming(ctx->getConstantInt(ctx->getIntegerType(32), 0), c_entry);
    phiI->addIncoming(iNext, c_body);

    phiSum->addIncoming(ctx->getConstantInt(ctx->getIntegerType(32), 0), c_entry);
    phiSum->addIncoming(callInst, c_body);

    builder.setInsertPoint(c_exit);
    builder.createRet(phiSum);

    transforms::CFGBuilder::run(*leaf);
    transforms::CFGBuilder::run(*caller);

    transforms::FunctionInliner inliner(30);
    bool changed = inliner.runOnModule(module);
    assert(changed && "Safe leaf helper call inside loop MUST be inlined!");

    // Verify call instruction eliminated
    for (const auto& bb : caller->getBasicBlocks()) {
        for (const auto& inst : bb->getInstructions()) {
            assert(inst->getOpcode() != ir::Instruction::Call);
        }
    }
    std::cout << "--- Safe Leaf Helper Inlining Inside Loop Passed ---" << std::endl;
}

void test_canonical_loop_helper_in_loop() {
    std::cout << "--- Testing Canonical Loop Helper Inlining Inside Loop ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("canonical_loop_module", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);

    // Callee with canonical loop
    ir::Function* loopCallee = builder.createFunction("loop_callee", i32Ty, {i32Ty});
    ir::Value* pLimit = loopCallee->getParameters().front().get();
    ir::BasicBlock* h_entry = builder.createBasicBlock("h_entry", loopCallee);
    ir::BasicBlock* h_loop = builder.createBasicBlock("h_loop", loopCallee);
    ir::BasicBlock* h_body = builder.createBasicBlock("h_body", loopCallee);
    ir::BasicBlock* h_exit = builder.createBasicBlock("h_exit", loopCallee);

    builder.setInsertPoint(h_entry);
    builder.createJmp(h_loop);

    builder.setInsertPoint(h_loop);
    ir::PhiNode* phiJ = builder.createPhi(i32Ty, 0, nullptr);
    ir::PhiNode* phiAcc = builder.createPhi(i32Ty, 0, nullptr);
    ir::Instruction* hCond = builder.createCslt(phiJ, pLimit);
    builder.createBr(hCond, h_body, h_exit);

    builder.setInsertPoint(h_body);
    ir::Instruction* jNext = builder.createAdd(phiJ, ctx->getConstantInt(ctx->getIntegerType(32), 1));
    ir::Instruction* accNext = builder.createAdd(phiAcc, phiJ);
    builder.createJmp(h_loop);

    phiJ->addIncoming(ctx->getConstantInt(ctx->getIntegerType(32), 0), h_entry);
    phiJ->addIncoming(jNext, h_body);

    phiAcc->addIncoming(ctx->getConstantInt(ctx->getIntegerType(32), 0), h_entry);
    phiAcc->addIncoming(accNext, h_body);

    builder.setInsertPoint(h_exit);
    builder.createRet(phiAcc);

    // Outer Caller loop
    ir::Function* caller = builder.createFunction("outer_caller", i32Ty, {i32Ty});
    ir::Value* pN = caller->getParameters().front().get();
    ir::BasicBlock* c_entry = builder.createBasicBlock("c_entry", caller);
    ir::BasicBlock* c_loop = builder.createBasicBlock("c_loop", caller);
    ir::BasicBlock* c_body = builder.createBasicBlock("c_body", caller);
    ir::BasicBlock* c_exit = builder.createBasicBlock("c_exit", caller);

    builder.setInsertPoint(c_entry);
    builder.createJmp(c_loop);

    builder.setInsertPoint(c_loop);
    ir::PhiNode* phiI = builder.createPhi(i32Ty, 0, nullptr);
    ir::PhiNode* phiTotal = builder.createPhi(i32Ty, 0, nullptr);
    ir::Instruction* cCond = builder.createCslt(phiI, pN);
    builder.createBr(cCond, c_body, c_exit);

    builder.setInsertPoint(c_body);
    ir::Instruction* callInst = builder.createCall(loopCallee, {phiI});
    ir::Instruction* totalNext = builder.createAdd(phiTotal, callInst);
    ir::Instruction* iNext = builder.createAdd(phiI, ctx->getConstantInt(ctx->getIntegerType(32), 1));
    builder.createJmp(c_loop);

    phiI->addIncoming(ctx->getConstantInt(ctx->getIntegerType(32), 0), c_entry);
    phiI->addIncoming(iNext, c_body);

    phiTotal->addIncoming(ctx->getConstantInt(ctx->getIntegerType(32), 0), c_entry);
    phiTotal->addIncoming(totalNext, c_body);

    builder.setInsertPoint(c_exit);
    builder.createRet(phiTotal);

    transforms::CFGBuilder::run(*loopCallee);
    transforms::CFGBuilder::run(*caller);

    transforms::FunctionInliner inliner(40);
    bool changed = inliner.runOnModule(module);
    assert(changed && "Small canonical loop helper call inside loop MUST be inlined!");

    // Verify call instruction eliminated
    for (const auto& bb : caller->getBasicBlocks()) {
        for (const auto& inst : bb->getInstructions()) {
            assert(inst->getOpcode() != ir::Instruction::Call);
        }
    }
    std::cout << "--- Canonical Loop Helper Inlining Inside Loop Passed ---" << std::endl;
}

void test_complex_multi_return_rejection_in_loop() {
    std::cout << "--- Testing Complex Multi-Return Rejection Inside Loop ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("multi_ret_module", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Type* i32Ty = ctx->getIntegerType(32);

    // Callee with multiple return statements
    ir::Function* multiRetCallee = builder.createFunction("multi_ret_func", i32Ty, {i32Ty});
    ir::Value* pX = multiRetCallee->getParameters().front().get();
    ir::BasicBlock* m_entry = builder.createBasicBlock("m_entry", multiRetCallee);
    ir::BasicBlock* m_then = builder.createBasicBlock("m_then", multiRetCallee);
    ir::BasicBlock* m_else = builder.createBasicBlock("m_else", multiRetCallee);

    builder.setInsertPoint(m_entry);
    ir::Instruction* cond = builder.createCslt(pX, ctx->getConstantInt(ctx->getIntegerType(32), 5));
    builder.createBr(cond, m_then, m_else);

    builder.setInsertPoint(m_then);
    builder.createRet(ctx->getConstantInt(ctx->getIntegerType(32), 10));

    builder.setInsertPoint(m_else);
    builder.createRet(ctx->getConstantInt(ctx->getIntegerType(32), 20));

    // Caller loop
    ir::Function* caller = builder.createFunction("loop_caller_multi_ret", i32Ty, {i32Ty});
    ir::Value* pN = caller->getParameters().front().get();
    ir::BasicBlock* c_entry = builder.createBasicBlock("c_entry", caller);
    ir::BasicBlock* c_loop = builder.createBasicBlock("c_loop", caller);
    ir::BasicBlock* c_body = builder.createBasicBlock("c_body", caller);
    ir::BasicBlock* c_exit = builder.createBasicBlock("c_exit", caller);

    builder.setInsertPoint(c_entry);
    builder.createJmp(c_loop);

    builder.setInsertPoint(c_loop);
    ir::PhiNode* phiI = builder.createPhi(i32Ty, 0, nullptr);
    ir::PhiNode* phiSum = builder.createPhi(i32Ty, 0, nullptr);
    ir::Instruction* cCond = builder.createCslt(phiI, pN);
    builder.createBr(cCond, c_body, c_exit);

    builder.setInsertPoint(c_body);
    ir::Instruction* callInst = builder.createCall(multiRetCallee, {phiI});
    ir::Instruction* sumNext = builder.createAdd(phiSum, callInst);
    ir::Instruction* iNext = builder.createAdd(phiI, ctx->getConstantInt(ctx->getIntegerType(32), 1));
    builder.createJmp(c_loop);

    phiI->addIncoming(ctx->getConstantInt(ctx->getIntegerType(32), 0), c_entry);
    phiI->addIncoming(iNext, c_body);

    phiSum->addIncoming(ctx->getConstantInt(ctx->getIntegerType(32), 0), c_entry);
    phiSum->addIncoming(sumNext, c_body);

    builder.setInsertPoint(c_exit);
    builder.createRet(phiSum);

    transforms::CFGBuilder::run(*multiRetCallee);
    transforms::CFGBuilder::run(*caller);

    transforms::FunctionInliner inliner(30);
    bool changed = inliner.runOnModule(module);
    assert(!changed && "Multi-return callee inside loop call site MUST be rejected!");

    std::cout << "--- Complex Multi-Return Rejection Inside Loop Passed ---" << std::endl;
}

void test_nested_loop_unprofitable_rejection() {
    std::cout << "--- Testing Nested Loop Unprofitable Rejection ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("unprofitable_module", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = static_cast<ir::IntegerType*>(ctx->getIntegerType(32));
    ir::IntegerType* i64Ty = static_cast<ir::IntegerType*>(ctx->getIntegerType(64));

    // Inner loop callee with sign-extended quadratic product
    ir::Function* calcFunc = builder.createFunction("calc_func", i64Ty, {i32Ty});
    ir::Value* pN = calcFunc->getParameters().front().get();
    ir::BasicBlock* h_entry = builder.createBasicBlock("h_entry", calcFunc);
    ir::BasicBlock* h_loop = builder.createBasicBlock("h_loop", calcFunc);
    ir::BasicBlock* h_body = builder.createBasicBlock("h_body", calcFunc);
    ir::BasicBlock* h_exit = builder.createBasicBlock("h_exit", calcFunc);

    builder.setInsertPoint(h_entry);
    builder.createJmp(h_loop);

    builder.setInsertPoint(h_loop);
    ir::PhiNode* phiJ = builder.createPhi(i32Ty, 0, nullptr);
    ir::PhiNode* phiSum = builder.createPhi(i64Ty, 0, nullptr);
    ir::Instruction* cond = builder.createCslt(phiJ, pN);
    builder.createBr(cond, h_body, h_exit);

    builder.setInsertPoint(h_body);
    ir::Instruction* t1 = builder.createMul(phiJ, ctx->getConstantInt(i32Ty, 5));
    ir::Instruction* a_w = builder.createAdd(t1, ctx->getConstantInt(i32Ty, 3));
    ir::Instruction* a = builder.createCast(a_w, i64Ty);
    ir::Instruction* t2 = builder.createMul(phiJ, ctx->getConstantInt(i32Ty, 2));
    ir::Instruction* b_w = builder.createAdd(t2, ctx->getConstantInt(i32Ty, 7));
    ir::Instruction* b = builder.createCast(b_w, i64Ty);
    ir::Instruction* prod = builder.createMul(a, b);
    ir::Instruction* sumNext = builder.createAdd(phiSum, prod);
    ir::Instruction* jNext = builder.createAdd(phiJ, ctx->getConstantInt(i32Ty, 1));
    builder.createJmp(h_loop);

    phiJ->addIncoming(ctx->getConstantInt(i32Ty, 0), h_entry);
    phiJ->addIncoming(jNext, h_body);
    phiSum->addIncoming(ctx->getConstantInt(i64Ty, 0), h_entry);
    phiSum->addIncoming(sumNext, h_body);

    builder.setInsertPoint(h_exit);
    builder.createRet(phiSum);

    // Outer caller loop
    ir::Function* caller = builder.createFunction("outer_caller", i64Ty, {i32Ty});
    ir::Value* pK = caller->getParameters().front().get();
    ir::BasicBlock* c_entry = builder.createBasicBlock("c_entry", caller);
    ir::BasicBlock* c_loop = builder.createBasicBlock("c_loop", caller);
    ir::BasicBlock* c_body = builder.createBasicBlock("c_body", caller);
    ir::BasicBlock* c_exit = builder.createBasicBlock("c_exit", caller);

    builder.setInsertPoint(c_entry);
    builder.createJmp(c_loop);

    builder.setInsertPoint(c_loop);
    ir::PhiNode* phiK = builder.createPhi(i32Ty, 0, nullptr);
    ir::PhiNode* phiTotal = builder.createPhi(i64Ty, 0, nullptr);
    ir::Instruction* cCond = builder.createCslt(phiK, pK);
    builder.createBr(cCond, c_body, c_exit);

    builder.setInsertPoint(c_body);
    ir::Instruction* callInst = builder.createCall(calcFunc, {ctx->getConstantInt(i32Ty, 5000000)});
    ir::Instruction* totalNext = builder.createAdd(phiTotal, callInst);
    ir::Instruction* kNext = builder.createAdd(phiK, ctx->getConstantInt(i32Ty, 1));
    builder.createJmp(c_loop);

    phiK->addIncoming(ctx->getConstantInt(i32Ty, 0), c_entry);
    phiK->addIncoming(kNext, c_body);
    phiTotal->addIncoming(ctx->getConstantInt(i64Ty, 0), c_entry);
    phiTotal->addIncoming(totalNext, c_body);

    builder.setInsertPoint(c_exit);
    builder.createRet(phiTotal);

    transforms::CFGBuilder::run(*calcFunc);
    transforms::CFGBuilder::run(*caller);

    transforms::FunctionInliner inliner(40);
    bool changed = inliner.runOnModule(module);
    assert(!changed && "Unprofitable nested loop callee MUST NOT be inlined!");

    std::cout << "--- Nested Loop Unprofitable Rejection Passed ---" << std::endl;
}

int main() {
    test_multiblock_inlining();
    test_recursion_rejection();
    test_safe_leaf_helper_in_loop();
    test_canonical_loop_helper_in_loop();
    test_complex_multi_return_rejection_in_loop();
    test_nested_loop_unprofitable_rejection();
    return 0;
}
