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

    std::string asmFilePath = "/tmp/test_mem_sum.s";
    std::string binFilePath = "/tmp/test_mem_sum_runner";
    {
        std::ofstream asmFile(asmFilePath);
        asmFile << asmCode;
    }

    std::string harnessPath = "/tmp/test_mem_sum_harness.c";
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
}

int main() {
    test_memory_sum_reduction(0);
    test_memory_sum_reduction(7);
    test_memory_sum_reduction(-9);
    return 0;
}
