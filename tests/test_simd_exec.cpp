#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/SIMDInstruction.h"
#include "codegen/CodeGen.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "target/architecture/x64/X64Architecture.h"
#include "target/os/linux/LinuxOS.h"
#include "target/core/CompositeTargetInfo.h"
#include "transforms/CFGBuilder.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <memory>
#include <sstream>

using namespace ir;

void test_simd_runtime_execution() {
    std::cout << "--- Running End-to-End SIMD Runtime Execution Test ---" << std::endl;

    auto ctx = std::make_shared<IRContext>();
    Module module("test_simd_module", ctx);
    IRBuilder builder(ctx);
    builder.setModule(&module);

    Type* i32Ty = ctx->getIntegerType(32);
    Type* i64Ty = ctx->getIntegerType(64);
    VectorType* vec4i32Ty = ctx->getVectorType(i32Ty, 4);

    Function* func = builder.createFunction("test_simd_func", ctx->getVoidType(), {i64Ty, i64Ty, i64Ty, i64Ty, i64Ty});
    const auto& params = func->getParameters();
    auto pIt = params.begin();
    Value* pInA = (pIt++)->get();
    Value* pInB = (pIt++)->get();
    Value* pOutAdd = (pIt++)->get();
    Value* pOutSub = (pIt++)->get();
    Value* pOutMul = (pIt++)->get();

    BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    VectorInstruction* vA = builder.createVLoad(vec4i32Ty, pInA);
    VectorInstruction* vB = builder.createVLoad(vec4i32Ty, pInB);

    VectorInstruction* vAdd = builder.createVAdd(vA, vB);
    VectorInstruction* vSub = builder.createVSub(vA, vB);
    VectorInstruction* vMul = builder.createVMul(vA, vB);

    builder.createVStore(vAdd, pOutAdd);
    builder.createVStore(vSub, pOutSub);
    builder.createVStore(vMul, pOutMul);

    builder.createRet(nullptr);

    transforms::CFGBuilder::run(*func);

    transforms::LinearScanAllocator allocator;
    allocator.run(*func);


    assert(vA->hasPhysicalRegister() && vA->getPhysicalRegister() >= 100);
    assert(vB->hasPhysicalRegister() && vB->getPhysicalRegister() >= 100);
    assert(vAdd->hasPhysicalRegister() && vAdd->getPhysicalRegister() >= 100);
    assert(vSub->hasPhysicalRegister() && vSub->getPhysicalRegister() >= 100);
    assert(vMul->hasPhysicalRegister() && vMul->getPhysicalRegister() >= 100);

    auto x64Arch = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    auto linuxOS = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> targetInfo = std::make_unique<target::CompositeTargetInfo>(std::move(x64Arch), std::move(linuxOS));

    std::ostringstream asmStream;
    codegen::CodeGen cg(module, std::move(targetInfo), &asmStream);
    cg.emit(false);

    std::string asmCode = asmStream.str();
    std::cout << "Generated SIMD Assembly:\n" << asmCode << std::endl;
    std::cout << "Assembly emitted successfully." << std::endl;

    assert(asmCode.find("movdqu") != std::string::npos);
    assert(asmCode.find("paddd") != std::string::npos);
    assert(asmCode.find("psubd") != std::string::npos);
    assert(asmCode.find("pmulld") != std::string::npos);
    assert(asmCode.find("%xmm") != std::string::npos);

    std::string asmFilePath = "/tmp/test_simd_generated.s";
    std::string binFilePath = "/tmp/test_simd_runner";
    {
        std::ofstream asmFile(asmFilePath);
        asmFile << asmCode;
    }

    std::string harnessPath = "/tmp/test_simd_harness.c";
    {
        std::ofstream hFile(harnessPath);
        hFile << R"(
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

extern void test_simd_func(const int32_t* inA, const int32_t* inB, int32_t* outAdd, int32_t* outSub, int32_t* outMul);

int main() {
    int32_t inA[4] = {100, 200, 300, 400};
    int32_t inB[4] = {10, 20, 30, 40};
    int32_t outAdd[4] = {0};
    int32_t outSub[4] = {0};
    int32_t outMul[4] = {0};

    test_simd_func(inA, inB, outAdd, outSub, outMul);

    assert(outAdd[0] == 110 && outAdd[1] == 220 && outAdd[2] == 330 && outAdd[3] == 440);
    assert(outSub[0] == 90 && outSub[1] == 180 && outSub[2] == 270 && outSub[3] == 360);
    assert(outMul[0] == 1000 && outMul[1] == 4000 && outMul[2] == 9000 && outMul[3] == 16000);

    printf("SIMD_RUNTIME_EXECUTION_SUCCESS\n");
    return 0;
}
)";
    }

    std::string compileCmd = "gcc -no-pie " + asmFilePath + " " + harnessPath + " -o " + binFilePath;
    int compileRc = std::system(compileCmd.c_str());
    assert(compileRc == 0 && "Compilation of SIMD test assembly failed");

    FILE* pipe = popen(binFilePath.c_str(), "r");
    assert(pipe != nullptr);
    char buffer[128];
    std::string resultOutput = "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        resultOutput += buffer;
    }
    int execRc = pclose(pipe);

    assert(execRc == 0 && "Execution of SIMD test binary failed");
    assert(resultOutput.find("SIMD_RUNTIME_EXECUTION_SUCCESS") != std::string::npos);

    std::cout << "--- End-to-End SIMD Runtime Execution Test PASSED ---" << std::endl;
}

int main() {
    test_simd_runtime_execution();
    return 0;
}
