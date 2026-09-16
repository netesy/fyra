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
#include <atomic>
#include <unistd.h>
#include <cstdio>

using namespace ir;

// Scalar reference implementation for memory sum with non-zero initial value
int32_t scalar_mem_sum_ref(const int32_t* a, int32_t n, int32_t init) {
    int32_t sum = init;
    for (int32_t i = 0; i < n; i++) {
        sum += a[i];
    }
    return sum;
}

void test_memory_sum_reduction(int32_t initVal) {
    std::cout << "--- Testing Memory Sum Reduction Execution (Init = " << initVal << ") ---" << std::endl;

    auto ctx = std::make_shared<IRContext>();
    Module module("test_sum_module", ctx);
    IRBuilder builder(ctx);
    builder.setModule(&module);

    Type* i32Ty = ctx->getIntegerType(32);
    Type* i64Ty = ctx->getIntegerType(64);

    // int32_t mem_sum(int32_t* a, int32_t n, int32_t init)
    Function* func = builder.createFunction("mem_sum", i32Ty, {i64Ty, i32Ty, i32Ty});
    auto pIt = func->getParameters().begin();
    Value* pA = (pIt++)->get();
    Value* pN = (pIt++)->get();
    Value* pInit = (pIt++)->get();

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

    rawPhiI->addIncoming(ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 0), entry);
    rawPhiSum->addIncoming(pInit, entry);

    Instruction* cond = builder.createCslt(rawPhiI, pN);
    builder.createBr(cond, loopBody, exit);

    builder.setInsertPoint(loopBody);
    Instruction* i64I = builder.createExtSW(rawPhiI, i64Ty);
    Instruction* offset = builder.createMul(i64I, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 4));

    Instruction* ptrA = builder.createAdd(pA, offset);
    Instruction* valA = builder.createLoaduw(ptrA);

    Instruction* sumNext = builder.createAdd(rawPhiSum, valA);
    Instruction* iNext = builder.createAdd(rawPhiI, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 1));

    rawPhiI->addIncoming(iNext, loopBody);
    rawPhiSum->addIncoming(sumNext, loopBody);

    builder.createJmp(loopHeader);

    builder.setInsertPoint(exit);
    builder.createRet(rawPhiSum);

    transforms::CFGBuilder::run(*func);

    transforms::LoopVectorizer vectorizer;
    bool vectorized = vectorizer.performTransformation(*func);

    assert(vectorized && "Memory sum reduction loop MUST be vectorized!");

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
    std::string asmFilePath = "/tmp/test_mem_sum_" + idStr + ".s";
    std::string binFilePath = "/tmp/test_mem_sum_runner_" + idStr;
    std::string harnessPath = "/tmp/test_mem_sum_harness_" + idStr + ".c";
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

extern int32_t mem_sum(int32_t* a, int32_t n, int32_t init);

int main(int argc, char** argv) {
    if (argc < 3) return 1;
    int32_t n = atoi(argv[1]);
    int32_t init = atoi(argv[2]);
    int32_t data[64];
    for (int i = 0; i < 64; ++i) data[i] = i + 1;
    const int32_t upper_half_only[8] = {0, 0, 0, 0, 10, 20, 30, 40};
    for (int i = 0; i < 8; ++i) data[i] = upper_half_only[i];
    int32_t res = mem_sum(data, n, init);
    printf("RES:%d\n", res);
    return 0;
}
)";
    }

    std::string compileCmd = "gcc -no-pie " + asmFilePath + " " + harnessPath + " -o " + binFilePath;
    int compileRc = std::system(compileCmd.c_str());
    assert(compileRc == 0 && "Compilation of vectorized memory sum assembly failed");

    int32_t testData[64];
    for (int i = 0; i < 64; ++i) testData[i] = i + 1;
    const int32_t upperHalfOnly[8] = {0, 0, 0, 0, 10, 20, 30, 40};
    for (int i = 0; i < 8; ++i) testData[i] = upperHalfOnly[i];

    for (int nVal : { 0, 1, 7, 8, 9, 15, 16, 17, 31 }) {
        std::string runCmd = binFilePath + " " + std::to_string(nVal) + " " + std::to_string(initVal);
        FILE* pipe = popen(runCmd.c_str(), "r");
        assert(pipe != nullptr);
        char buffer[128];
        std::string resultOutput = "";
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            resultOutput += buffer;
        }
        pclose(pipe);

        int32_t expected = scalar_mem_sum_ref(testData, nVal, initVal);
        std::string expectedStr = "RES:" + std::to_string(expected);
        if (resultOutput.find(expectedStr) == std::string::npos) {
            std::cout << "DEBUG nVal=" << nVal << " init=" << initVal << " expected=" << expectedStr << " got=" << resultOutput << std::endl;
        }
        assert(resultOutput.find(expectedStr) != std::string::npos && "Memory sum execution result mismatch!");
        std::cout << "Executed & verified N=" << nVal << ", init=" << initVal << " -> RES=" << expected << std::endl;
    }

    std::remove(asmFilePath.c_str());
    std::remove(harnessPath.c_str());
    std::remove(binFilePath.c_str());
}

int64_t scalar_widening_sum_ref(const int32_t* a, int32_t n, int32_t start, int64_t init) {
    int64_t sum = init;
    for (int32_t i = start; i < n; i++) {
        sum += (int64_t)a[i];
    }
    return sum;
}

void test_signed_widening_vectorized(int32_t startVal, int64_t initVal) {
    std::cout << "--- Testing Signed i32 -> i64 Widening Reduction (Start = " << startVal << ", Init = " << initVal << ") ---" << std::endl;

    auto ctx = std::make_shared<IRContext>();
    Module module("test_widening_module", ctx);
    IRBuilder builder(ctx);
    builder.setModule(&module);

    auto* i32Ty = ctx->getIntegerType(32);
    auto* i64Ty = ctx->getIntegerType(64);
    Function* func = builder.createFunction("widening_sum", i64Ty, {i64Ty, i32Ty, i32Ty, i64Ty});
    auto parameter = func->getParameters().begin();
    Value* base = (parameter++)->get();
    Value* bound = (parameter++)->get();
    Value* start = (parameter++)->get();
    Value* init = parameter->get();

    BasicBlock* entry = builder.createBasicBlock("entry", func);
    BasicBlock* header = builder.createBasicBlock("loop", func);
    BasicBlock* body = builder.createBasicBlock("body", func);
    BasicBlock* exit = builder.createBasicBlock("exit", func);
    builder.setInsertPoint(entry);
    builder.createJmp(header);

    builder.setInsertPoint(header);
    auto inductionOwner = std::make_unique<PhiNode>(i32Ty, 0, nullptr, header);
    auto* induction = inductionOwner.get();
    header->getInstructions().push_back(std::move(inductionOwner));
    auto sumOwner = std::make_unique<PhiNode>(i64Ty, 0, nullptr, header);
    auto* sum = sumOwner.get();
    header->getInstructions().push_back(std::move(sumOwner));
    induction->addIncoming(start, entry);
    sum->addIncoming(init, entry);
    builder.createBr(builder.createCslt(induction, bound), body, exit);

    builder.setInsertPoint(body);
    auto* wideIndex = builder.createExtSW(induction, i64Ty);
    auto* offset = builder.createMul(wideIndex, ctx->getConstantInt(i64Ty, 4));
    auto* loaded = builder.createLoaduw(builder.createAdd(base, offset));
    auto* widened = builder.createExtSW(loaded, i64Ty);
    auto* sumNext = builder.createAdd(sum, widened);
    auto* inductionNext = builder.createAdd(induction, ctx->getConstantInt(i32Ty, 1));
    induction->addIncoming(inductionNext, body);
    sum->addIncoming(sumNext, body);
    builder.createJmp(header);
    builder.setInsertPoint(exit);
    builder.createRet(sum);

    transforms::CFGBuilder::run(*func);
    transforms::LoopVectorizer vectorizer;
    bool vectorized = vectorizer.performTransformation(*func);
    assert(vectorized && "signed i32 -> i64 widening sum MUST vectorize!");

    transforms::LinearScanAllocator allocator;
    allocator.run(*func);

    auto x64Arch = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    auto linuxOS = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> targetInfo = std::make_unique<target::CompositeTargetInfo>(std::move(x64Arch), std::move(linuxOS));

    std::ostringstream asmStream;
    codegen::CodeGen cg(module, std::move(targetInfo), &asmStream);
    cg.emit(false);

    std::string asmCode = asmStream.str();

    // Verify assembly instructions vpmovsxdq and vpaddq
    assert(asmCode.find("vpmovsxdq") != std::string::npos && "Assembly MUST contain vpmovsxdq!");
    assert(asmCode.find("vpaddq") != std::string::npos && "Assembly MUST contain vpaddq!");

    pid_t pid = getpid();
    static std::atomic<uint64_t> counter{0};
    uint64_t uid = counter.fetch_add(1);
    std::string idStr = std::to_string(pid) + "_" + std::to_string(uid);
    std::string asmFilePath = "/tmp/test_widening_" + idStr + ".s";
    std::string binFilePath = "/tmp/test_widening_runner_" + idStr;
    std::string harnessPath = "/tmp/test_widening_harness_" + idStr + ".c";
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

extern int64_t widening_sum(int32_t* a, int32_t n, int32_t start, int64_t init);

int main(int argc, char** argv) {
    if (argc < 5) return 1;
    int32_t n = atoi(argv[1]);
    int32_t start = atoi(argv[2]);
    int64_t init = atoll(argv[3]);
    int test_mode = atoi(argv[4]);

    int32_t data[64];
    if (test_mode == 1) {
        // Negative values test array
        int32_t neg_vals[8] = {-2147483648, -123456, -2, -1, 0, 1, 123456, 2147483647};
        for (int i = 0; i < 64; ++i) data[i] = neg_vals[i % 8];
    } else if (test_mode == 2) {
        // High-half only values test array
        for (int i = 0; i < 64; ++i) data[i] = 0;
        data[4] = -100000; data[5] = 200000; data[6] = -300000; data[7] = 400000;
    } else {
        for (int i = 0; i < 64; ++i) data[i] = (i + 1) * 10;
    }

    int64_t res = widening_sum(data, n, start, init);
    printf("RES:%lld\n", (long long)res);
    return 0;
}
)";
    }

    std::string compileCmd = "gcc -no-pie " + asmFilePath + " " + harnessPath + " -o " + binFilePath;
    int compileRc = std::system(compileCmd.c_str());
    assert(compileRc == 0 && "Compilation of vectorized widening assembly failed");

    for (int testMode : {0, 1, 2}) {
        int32_t testData[64];
        if (testMode == 1) {
            int32_t negVals[8] = {-2147483648, -123456, -2, -1, 0, 1, 123456, 2147483647};
            for (int i = 0; i < 64; ++i) testData[i] = negVals[i % 8];
        } else if (testMode == 2) {
            for (int i = 0; i < 64; ++i) testData[i] = 0;
            testData[4] = -100000; testData[5] = 200000; testData[6] = -300000; testData[7] = 400000;
        } else {
            for (int i = 0; i < 64; ++i) testData[i] = (i + 1) * 10;
        }

        for (int nVal : { 0, 1, 7, 8, 9, 15, 16, 17, 31 }) {
            if (nVal < startVal) continue;
            std::string runCmd = binFilePath + " " + std::to_string(nVal) + " " + std::to_string(startVal) + " " + std::to_string(initVal) + " " + std::to_string(testMode);
            FILE* pipe = popen(runCmd.c_str(), "r");
            assert(pipe != nullptr);
            char buffer[128];
            std::string resultOutput = "";
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                resultOutput += buffer;
            }
            pclose(pipe);

            int64_t expected = scalar_widening_sum_ref(testData, nVal, startVal, initVal);
            std::string expectedStr = "RES:" + std::to_string(expected);
            if (resultOutput.find(expectedStr) == std::string::npos) {
                std::cout << "DEBUG nVal=" << nVal << " start=" << startVal << " init=" << initVal << " mode=" << testMode << " expected=" << expectedStr << " got=" << resultOutput << std::endl;
            }
            assert(resultOutput.find(expectedStr) != std::string::npos && "Widening sum execution result mismatch!");
        }
    }

    std::remove(asmFilePath.c_str());
    std::remove(harnessPath.c_str());
    std::remove(binFilePath.c_str());
    std::cout << "Signed widening execution test PASSED!" << std::endl;
}

void test_widened_multiply_rejected() {
    auto ctx = std::make_shared<IRContext>();
    Module module("test_widened_mul_rejection", ctx);
    IRBuilder builder(ctx);
    builder.setModule(&module);

    auto* i32Ty = ctx->getIntegerType(32);
    auto* i64Ty = ctx->getIntegerType(64);
    Function* func = builder.createFunction("widened_mul_sum", i64Ty, {i64Ty, i64Ty, i32Ty});
    auto parameter = func->getParameters().begin();
    Value* baseA = (parameter++)->get();
    Value* baseB = (parameter++)->get();
    Value* bound = parameter->get();

    BasicBlock* entry = builder.createBasicBlock("entry", func);
    BasicBlock* header = builder.createBasicBlock("loop", func);
    BasicBlock* body = builder.createBasicBlock("body", func);
    BasicBlock* exit = builder.createBasicBlock("exit", func);
    builder.setInsertPoint(entry);
    builder.createJmp(header);

    builder.setInsertPoint(header);
    auto inductionOwner = std::make_unique<PhiNode>(i32Ty, 0, nullptr, header);
    auto* induction = inductionOwner.get();
    header->getInstructions().push_back(std::move(inductionOwner));
    auto sumOwner = std::make_unique<PhiNode>(i64Ty, 0, nullptr, header);
    auto* sum = sumOwner.get();
    header->getInstructions().push_back(std::move(sumOwner));
    induction->addIncoming(ctx->getConstantInt(i32Ty, 0), entry);
    sum->addIncoming(ctx->getConstantInt(i64Ty, 0), entry);
    builder.createBr(builder.createCslt(induction, bound), body, exit);

    builder.setInsertPoint(body);
    auto* wideIndex = builder.createExtSW(induction, i64Ty);
    auto* offset = builder.createMul(wideIndex, ctx->getConstantInt(i64Ty, 4));
    auto* loadedA = builder.createLoaduw(builder.createAdd(baseA, offset));
    auto* loadedB = builder.createLoaduw(builder.createAdd(baseB, offset));
    auto* wideA = builder.createExtSW(loadedA, i64Ty);
    auto* wideB = builder.createExtSW(loadedB, i64Ty);
    auto* prod = builder.createMul(wideA, wideB);
    auto* sumNext = builder.createAdd(sum, prod);
    auto* inductionNext = builder.createAdd(induction, ctx->getConstantInt(i32Ty, 1));
    induction->addIncoming(inductionNext, body);
    sum->addIncoming(sumNext, body);
    builder.createJmp(header);
    builder.setInsertPoint(exit);
    builder.createRet(sum);

    transforms::CFGBuilder::run(*func);
    transforms::LoopVectorizer vectorizer;
    assert(!vectorizer.performTransformation(*func) &&
           "widened multiply pattern MUST be rejected when i64 packed multiply is unsupported");
    std::cout << "Widened multiply pattern rejection test PASSED!" << std::endl;
}

int main() {
    test_memory_sum_reduction(0);
    test_memory_sum_reduction(7);
    test_memory_sum_reduction(-9);

    test_signed_widening_vectorized(0, 0);
    test_signed_widening_vectorized(0, 7);
    test_signed_widening_vectorized(0, -9);
    test_signed_widening_vectorized(0, 123456789LL);
    test_signed_widening_vectorized(3, 0);
    test_signed_widening_vectorized(3, 100);

    test_widened_multiply_rejected();
    return 0;
}
