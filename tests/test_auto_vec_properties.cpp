#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/SIMDInstruction.h"
#include "ir/PhiNode.h"
#include "codegen/CodeGen.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "target/architecture/x64/X64Architecture.h"
#include "target/os/linux/LinuxOS.h"
#include "target/core/CompositeTargetInfo.h"
#include "transforms/CFGBuilder.h"
#include "transforms/LoopVectorizer.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <memory>
#include <sstream>

using namespace ir;

void test_dependence_rejection() {
    std::cout << "--- Testing Memory Dependence Rejection (a[i] = a[i-1] + 1) ---" << std::endl;

    auto ctx = std::make_shared<IRContext>();
    Module module("test_dep_module", ctx);
    IRBuilder builder(ctx);
    builder.setModule(&module);

    Type* i32Ty = ctx->getIntegerType(32);
    Type* i64Ty = ctx->getIntegerType(64);

    Function* func = builder.createFunction("dep_loop", ctx->getVoidType(), {i64Ty, i32Ty});
    auto pIt = func->getParameters().begin();
    Value* pA = (pIt++)->get();
    Value* pN = (pIt++)->get();

    BasicBlock* entry = builder.createBasicBlock("entry", func);
    BasicBlock* loopHeader = builder.createBasicBlock("loop", func);
    BasicBlock* loopBody = builder.createBasicBlock("body", func);
    BasicBlock* exit = builder.createBasicBlock("exit", func);

    builder.setInsertPoint(entry);
    builder.createJmp(loopHeader);

    builder.setInsertPoint(loopHeader);
    auto phiI = std::make_unique<PhiNode>(i32Ty, 0, nullptr, loopHeader);
    PhiNode* rawPhiI = phiI.get();
    loopHeader->getInstructions().push_back(std::move(phiI));

    rawPhiI->addIncoming(ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 1), entry);

    Instruction* cond = builder.createCslt(rawPhiI, pN);
    builder.createBr(cond, loopBody, exit);

    builder.setInsertPoint(loopBody);
    Instruction* i64I = builder.createExtSW(rawPhiI, i64Ty);
    Instruction* i64Prev = builder.createSub(i64I, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 1));

    Instruction* offsetPrev = builder.createMul(i64Prev, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 4));
    Instruction* ptrPrev = builder.createAdd(pA, offsetPrev);
    Instruction* valPrev = builder.createLoaduw(ptrPrev);

    Instruction* valCurr = builder.createAdd(valPrev, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 1));

    Instruction* offsetCurr = builder.createMul(i64I, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 4));
    Instruction* ptrCurr = builder.createAdd(pA, offsetCurr);
    builder.createStore(valCurr, ptrCurr);

    Instruction* iNext = builder.createAdd(rawPhiI, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 1));
    rawPhiI->addIncoming(iNext, loopBody);

    builder.createJmp(loopHeader);

    builder.setInsertPoint(exit);
    builder.createRet(nullptr);

    transforms::CFGBuilder::run(*func);

    transforms::LoopVectorizer vectorizer;
    bool vectorized = vectorizer.performTransformation(*func);

    std::cout << "Dependence vectorization result: " << (vectorized ? "VECTORIZED (BUG!)" : "REJECTED (CORRECT)") << std::endl;
    assert(!vectorized && "Loop-carried dependence MUST be rejected by vectorizer!");
}

void test_fp_reduction_vectorization() {
    std::cout << "--- Testing Floating-Point Reduction Vectorization ---" << std::endl;

    auto ctx = std::make_shared<IRContext>();
    Module module("test_fp_red_module", ctx);
    IRBuilder builder(ctx);
    builder.setModule(&module);

    Type* f32Ty = ctx->getFloatType();
    Type* i32Ty = ctx->getIntegerType(32);
    Type* i64Ty = ctx->getIntegerType(64);

    Function* func = builder.createFunction("fp_sum", f32Ty, {i64Ty, i32Ty, f32Ty});
    auto pIt = func->getParameters().begin();
    Value* pA = (pIt++)->get();
    Value* pN = (pIt++)->get();
    Value* pInit = pIt->get();

    BasicBlock* entry = builder.createBasicBlock("entry", func);
    BasicBlock* loopHeader = builder.createBasicBlock("loop", func);
    BasicBlock* loopBody = builder.createBasicBlock("body", func);
    BasicBlock* exit = builder.createBasicBlock("exit", func);

    builder.setInsertPoint(entry);
    builder.createJmp(loopHeader);

    builder.setInsertPoint(loopHeader);
    auto phiI = std::make_unique<PhiNode>(i32Ty, 0, nullptr, loopHeader);
    PhiNode* rawPhiI = phiI.get();
    loopHeader->getInstructions().push_back(std::move(phiI));

    auto phiSum = std::make_unique<PhiNode>(f32Ty, 0, nullptr, loopHeader);
    PhiNode* rawPhiSum = phiSum.get();
    loopHeader->getInstructions().push_back(std::move(phiSum));

    rawPhiI->addIncoming(ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 0), entry);
    rawPhiSum->addIncoming(pInit, entry);

    Instruction* cond = builder.createCslt(rawPhiI, pN);
    builder.createBr(cond, loopBody, exit);

    builder.setInsertPoint(loopBody);
    Instruction* i64I = builder.createExtSW(rawPhiI, i64Ty);
    Instruction* offset = builder.createMul(i64I, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 4));
    Instruction* ptrA = builder.createAdd(pA, offset);
    Instruction* valA = builder.createLoads(ptrA);

    Instruction* sumNext = builder.createFAdd(rawPhiSum, valA);
    Instruction* iNext = builder.createAdd(rawPhiI, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 1));

    rawPhiI->addIncoming(iNext, loopBody);
    rawPhiSum->addIncoming(sumNext, loopBody);

    builder.createJmp(loopHeader);

    builder.setInsertPoint(exit);
    builder.createRet(rawPhiSum);

    transforms::CFGBuilder::run(*func);

    transforms::LoopVectorizer vectorizer;
    bool vectorized = vectorizer.performTransformation(*func);

    std::cout << "FP sum reduction result: " << (vectorized ? "VECTORIZED (CORRECT)" : "REJECTED") << std::endl;
    assert(vectorized && "Floating-point sum reduction loop MUST be vectorized!");
}

void test_negative_step_vectorization() {
    std::cout << "--- Testing Negative Step Induction Vectorization (i--) ---" << std::endl;

    auto ctx = std::make_shared<IRContext>();
    Module module("test_neg_step_module", ctx);
    IRBuilder builder(ctx);
    builder.setModule(&module);

    Type* i32Ty = ctx->getIntegerType(32);
    Type* i64Ty = ctx->getIntegerType(64);

    Function* func = builder.createFunction("neg_step_sum", i32Ty, {i64Ty, i32Ty, i32Ty, i32Ty});
    auto pIt = func->getParameters().begin();
    Value* pA = (pIt++)->get();
    Value* pStart = (pIt++)->get();
    Value* pBound = (pIt++)->get();
    Value* pInit = pIt->get();

    BasicBlock* entry = builder.createBasicBlock("entry", func);
    BasicBlock* loopHeader = builder.createBasicBlock("loop", func);
    BasicBlock* loopBody = builder.createBasicBlock("body", func);
    BasicBlock* exit = builder.createBasicBlock("exit", func);

    builder.setInsertPoint(entry);
    builder.createJmp(loopHeader);

    builder.setInsertPoint(loopHeader);
    auto phiI = std::make_unique<PhiNode>(i32Ty, 0, nullptr, loopHeader);
    PhiNode* rawPhiI = phiI.get();
    loopHeader->getInstructions().push_back(std::move(phiI));

    auto phiSum = std::make_unique<PhiNode>(i32Ty, 0, nullptr, loopHeader);
    PhiNode* rawPhiSum = phiSum.get();
    loopHeader->getInstructions().push_back(std::move(phiSum));

    rawPhiI->addIncoming(pStart, entry);
    rawPhiSum->addIncoming(pInit, entry);

    Instruction* cond = builder.createCsgt(rawPhiI, pBound);
    builder.createBr(cond, loopBody, exit);

    builder.setInsertPoint(loopBody);
    Instruction* i64I = builder.createExtSW(rawPhiI, i64Ty);
    Instruction* offset = builder.createMul(i64I, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 4));
    Instruction* ptrA = builder.createAdd(pA, offset);
    Instruction* valA = builder.createLoaduw(ptrA);

    Instruction* sumNext = builder.createAdd(rawPhiSum, valA);
    Instruction* iNext = builder.createSub(rawPhiI, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 1));

    rawPhiI->addIncoming(iNext, loopBody);
    rawPhiSum->addIncoming(sumNext, loopBody);

    builder.createJmp(loopHeader);

    builder.setInsertPoint(exit);
    builder.createRet(rawPhiSum);

    transforms::CFGBuilder::run(*func);

    transforms::LoopVectorizer vectorizer;
    bool vectorized = vectorizer.performTransformation(*func);

    std::cout << "Negative step vectorization result: " << (vectorized ? "VECTORIZED (CORRECT)" : "REJECTED") << std::endl;
    assert(vectorized && "Negative step induction variable loop MUST be vectorized!");
}

int main() {
    test_dependence_rejection();
    test_fp_reduction_vectorization();
    test_negative_step_vectorization();
    return 0;
}
