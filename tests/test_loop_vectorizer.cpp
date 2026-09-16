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
#include "ir/Use.h"
#include "transforms/CFGBuilder.h"
#include "transforms/LoopVectorizer.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <atomic>
#include <unistd.h>
#include <cstdio>

using namespace ir;

// Scalar reference implementation for sum = sum + 2*i for i in 0..n-1
int32_t scalar_loop_sum_ref(int32_t n, int32_t start = 0) {
    int32_t sum = 0;
    for (int32_t i = start; i < n; i++) {
        sum += i * 2;
    }
    return sum;
}

void test_loop_vectorizer_case(int32_t n_val, bool reversePhis = false,
                               int32_t start = 0, bool reverseCompare = false) {
    auto ctx = std::make_shared<IRContext>();
    Module module("test_vec_module", ctx);
    IRBuilder builder(ctx);
    builder.setModule(&module);

    Type* i32Ty = ctx->getIntegerType(32);
    Function* func = builder.createFunction("test_vec_func", i32Ty, {i32Ty});
    Value* pN = func->getParameters().front().get();

    BasicBlock* entry = builder.createBasicBlock("entry", func);
    BasicBlock* loopHeader = builder.createBasicBlock("loop", func);
    BasicBlock* loopBody = builder.createBasicBlock("body", func);
    BasicBlock* exit = builder.createBasicBlock("exit", func);

    builder.setInsertPoint(entry);
    builder.createJmp(loopHeader);

    builder.setInsertPoint(loopHeader);
    auto phiI = std::make_unique<PhiNode>(i32Ty, 0, nullptr, loopHeader);
    PhiNode* rawPhiI = phiI.get();

    auto phiSum = std::make_unique<PhiNode>(i32Ty, 0, nullptr, loopHeader);
    PhiNode* rawPhiSum = phiSum.get();

    if (reversePhis) {
        loopHeader->getInstructions().push_back(std::move(phiSum));
        loopHeader->getInstructions().push_back(std::move(phiI));
    } else {
        loopHeader->getInstructions().push_back(std::move(phiI));
        loopHeader->getInstructions().push_back(std::move(phiSum));
    }

    rawPhiI->addIncoming(ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), start), entry);
    rawPhiSum->addIncoming(ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 0), entry);

    Instruction* cond = reverseCompare ? builder.createCsgt(pN, rawPhiI)
                                       : builder.createCslt(rawPhiI, pN);
    builder.createBr(cond, loopBody, exit);

    builder.setInsertPoint(loopBody);
    Instruction* term = builder.createMul(rawPhiI, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 2));
    Instruction* sumNext = builder.createAdd(rawPhiSum, term);
    Instruction* iNext = builder.createAdd(rawPhiI, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 1));

    rawPhiI->addIncoming(iNext, loopBody);
    rawPhiSum->addIncoming(sumNext, loopBody);

    builder.createJmp(loopHeader);

    builder.setInsertPoint(exit);
    builder.createRet(rawPhiSum);

    transforms::CFGBuilder::run(*func);

    // Run LoopVectorizer
    transforms::LoopVectorizer vectorizer;
    bool vectorized = vectorizer.performTransformation(*func);

    if (n_val >= 4) {
        assert(vectorized && "Loop should have been vectorized for n >= 4");
    }

    transforms::LinearScanAllocator allocator;
    allocator.run(*func);

    auto x64Arch = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    auto linuxOS = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> targetInfo = std::make_unique<target::CompositeTargetInfo>(std::move(x64Arch), std::move(linuxOS));

    std::ostringstream asmStream;
    codegen::CodeGen cg(module, std::move(targetInfo), &asmStream);
    cg.emit(false);

    std::string asmCode = asmStream.str();

    pid_t pid = getpid();
    static std::atomic<uint64_t> counter{0};
    uint64_t uid = counter.fetch_add(1);
    std::string idStr = std::to_string(pid) + "_" + std::to_string(uid);
    std::string asmFilePath = "/tmp/test_vec_gen_" + idStr + ".s";
    std::string binFilePath = "/tmp/test_vec_runner_" + idStr;
    std::string harnessPath = "/tmp/test_vec_harness_" + idStr + ".c";
    {
        std::ofstream asmFile(asmFilePath);
        asmFile << asmCode;
    }
    {
        std::ofstream hFile(harnessPath);
        hFile << R"(
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

extern int32_t test_vec_func(int32_t n);

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    int32_t n = atoi(argv[1]);
    int32_t res = test_vec_func(n);
    printf("RES:%d\n", res);
    return 0;
}
)";
    }

    std::string compileCmd = "gcc -no-pie " + asmFilePath + " " + harnessPath + " -o " + binFilePath;
    int compileRc = std::system(compileCmd.c_str());
    assert(compileRc == 0 && "Compilation of vectorized assembly failed");

    std::string runCmd = binFilePath + " " + std::to_string(n_val);
    FILE* pipe = popen(runCmd.c_str(), "r");
    assert(pipe != nullptr);
    char buffer[128];
    std::string resultOutput = "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        resultOutput += buffer;
    }
    int execRc = pclose(pipe);

    std::remove(asmFilePath.c_str());
    std::remove(harnessPath.c_str());
    std::remove(binFilePath.c_str());

    assert(execRc == 0 && "Execution of vectorized test binary failed");

    int32_t expectedRes = scalar_loop_sum_ref(n_val, start);
    std::string expectedStr = "RES:" + std::to_string(expectedRes);
    if (resultOutput.find(expectedStr) == std::string::npos) {
        std::cout << "DEBUG n_val=" << n_val << " expected=" << expectedStr << " got=" << resultOutput << std::endl;
    }
    assert(resultOutput.find(expectedStr) != std::string::npos && "Vectorized result mismatch!");

    std::cout << "Test start=" << start << " n=" << n_val
              << (reversePhis ? " (reversed PHIs)" : "")
              << (reverseCompare ? " (reversed compare)" : "")
              << " PASSED (res=" << expectedRes << ")" << std::endl;
}

void test_rejection_cases() {
    std::cout << "--- Testing Negative Rejection Cases ---" << std::endl;

    auto makeBaseModule = [](std::shared_ptr<IRContext>& ctx, Module*& module, Function*& func, BasicBlock*& entry, BasicBlock*& loopHeader, BasicBlock*& loopBody, BasicBlock*& exit, PhiNode*& rawPhiI, PhiNode*& rawPhiSum) {
        ctx = std::make_shared<IRContext>();
        module = new Module("test_rej_mod", ctx);
        IRBuilder builder(ctx);
        builder.setModule(module);

        Type* i32Ty = ctx->getIntegerType(32);
        func = builder.createFunction("test_func", i32Ty, {i32Ty});
        Value* pN = func->getParameters().front().get();

        entry = builder.createBasicBlock("entry", func);
        loopHeader = builder.createBasicBlock("loop", func);
        loopBody = builder.createBasicBlock("body", func);
        exit = builder.createBasicBlock("exit", func);

        builder.setInsertPoint(entry);
        builder.createJmp(loopHeader);

        builder.setInsertPoint(loopHeader);
        auto phiI = std::make_unique<PhiNode>(i32Ty, 0, nullptr, loopHeader);
        rawPhiI = phiI.get();
        loopHeader->getInstructions().push_back(std::move(phiI));

        auto phiSum = std::make_unique<PhiNode>(i32Ty, 0, nullptr, loopHeader);
        rawPhiSum = phiSum.get();
        loopHeader->getInstructions().push_back(std::move(phiSum));

        rawPhiI->addIncoming(ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 0), entry);
        rawPhiSum->addIncoming(ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 0), entry);

        Instruction* cond = builder.createCslt(rawPhiI, pN);
        builder.createBr(cond, loopBody, exit);
    };

    // 1. Side effect Alloc in body
    {
        std::shared_ptr<IRContext> ctx; Module* mod; Function* func; BasicBlock *entry, *header, *body, *exit; PhiNode *pI, *pSum;
        makeBaseModule(ctx, mod, func, entry, header, body, exit, pI, pSum);
        IRBuilder builder(ctx); builder.setModule(mod); builder.setInsertPoint(body);
        Instruction* dummyAlloc = builder.createAlloc4(ctx->getIntegerType(32));
        Instruction* term = builder.createMul(pI, ctx->getConstantInt(ctx->getIntegerType(32), 2));
        Instruction* sumNext = builder.createAdd(pSum, term);
        Instruction* iNext = builder.createAdd(pI, ctx->getConstantInt(ctx->getIntegerType(32), 1));
        pI->addIncoming(iNext, body); pSum->addIncoming(sumNext, body); builder.createJmp(header);
        builder.setInsertPoint(exit); builder.createRet(pSum);
        transforms::CFGBuilder::run(*func);
        transforms::LoopVectorizer vec; assert(!vec.performTransformation(*func) && "Must reject side effect alloc in body");
        delete mod;
    }

    // 2. Call in body
    {
        std::shared_ptr<IRContext> ctx; Module* mod; Function* func; BasicBlock *entry, *header, *body, *exit; PhiNode *pI, *pSum;
        makeBaseModule(ctx, mod, func, entry, header, body, exit, pI, pSum);
        IRBuilder builder(ctx); builder.setModule(mod); builder.setInsertPoint(body);
        builder.createExternCall("dummy_cap", {}, ctx->getVoidType());
        Instruction* term = builder.createMul(pI, ctx->getConstantInt(ctx->getIntegerType(32), 2));
        Instruction* sumNext = builder.createAdd(pSum, term);
        Instruction* iNext = builder.createAdd(pI, ctx->getConstantInt(ctx->getIntegerType(32), 1));
        pI->addIncoming(iNext, body); pSum->addIncoming(sumNext, body); builder.createJmp(header);
        builder.setInsertPoint(exit); builder.createRet(pSum);
        transforms::CFGBuilder::run(*func);
        transforms::LoopVectorizer vec; assert(!vec.performTransformation(*func) && "Must reject Call in body");
        delete mod;
    }

    std::cout << "All Rejection Test Cases Passed Successfully!" << std::endl;
}

int main() {
    std::cout << "=== Running Loop Vectorizer Tests ===" << std::endl;

    test_loop_vectorizer_case(-1);
    test_loop_vectorizer_case(-2147483647 - 1); // INT_MIN
    test_loop_vectorizer_case(0);
    test_loop_vectorizer_case(1);
    test_loop_vectorizer_case(2);
    test_loop_vectorizer_case(3);
    test_loop_vectorizer_case(4);
    test_loop_vectorizer_case(4, true); // Reverse PHI order test
    test_loop_vectorizer_case(5);
    test_loop_vectorizer_case(7);
    test_loop_vectorizer_case(8);
    test_loop_vectorizer_case(9);
    test_loop_vectorizer_case(10000);
    test_loop_vectorizer_case(100000); // i32 wraparound test
    test_loop_vectorizer_case(20, false, 3);
    test_loop_vectorizer_case(22, false, 3);
    test_loop_vectorizer_case(17, false, 0, true);

    test_rejection_cases();

    std::cout << "=== All Loop Vectorizer Unit Tests Passed Successfully ===" << std::endl;
    return 0;
}
