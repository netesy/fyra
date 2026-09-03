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

int main() {
    test_multiblock_inlining();
    test_recursion_rejection();
    return 0;
}
