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

    Type* i8Ty = ctx->getIntegerType(8);
    Type* i16Ty = ctx->getIntegerType(16);
    Type* i32Ty = ctx->getIntegerType(32);
    Type* i64Ty = ctx->getIntegerType(64);

    VectorType* vec16i8Ty = ctx->getVectorType(i8Ty, 16);
    VectorType* vec8i16Ty = ctx->getVectorType(i16Ty, 8);
    VectorType* vec4i32Ty = ctx->getVectorType(i32Ty, 4);
    VectorType* vec2i64Ty = ctx->getVectorType(i64Ty, 2);

    // Function for i32
    Function* func32 = builder.createFunction("test_simd_func_i32", ctx->getVoidType(), {i64Ty, i64Ty, i64Ty, i64Ty, i64Ty});
    {
        const auto& params = func32->getParameters();
        auto pIt = params.begin();
        Value* pInA = (pIt++)->get();
        Value* pInB = (pIt++)->get();
        Value* pOutAdd = (pIt++)->get();
        Value* pOutSub = (pIt++)->get();
        Value* pOutMul = (pIt++)->get();

        BasicBlock* entry = builder.createBasicBlock("entry", func32);
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

        transforms::CFGBuilder::run(*func32);
        transforms::LinearScanAllocator allocator;
        allocator.run(*func32);
    }

    // Function for i8
    Function* func8 = builder.createFunction("test_simd_func_i8", ctx->getVoidType(), {i64Ty, i64Ty, i64Ty, i64Ty});
    {
        const auto& params = func8->getParameters();
        auto pIt = params.begin();
        Value* pInA = (pIt++)->get();
        Value* pInB = (pIt++)->get();
        Value* pOutAdd = (pIt++)->get();
        Value* pOutSub = (pIt++)->get();

        BasicBlock* entry = builder.createBasicBlock("entry", func8);
        builder.setInsertPoint(entry);

        VectorInstruction* vA = builder.createVLoad(vec16i8Ty, pInA);
        VectorInstruction* vB = builder.createVLoad(vec16i8Ty, pInB);

        VectorInstruction* vAdd = builder.createVAdd(vA, vB);
        VectorInstruction* vSub = builder.createVSub(vA, vB);

        builder.createVStore(vAdd, pOutAdd);
        builder.createVStore(vSub, pOutSub);
        builder.createRet(nullptr);

        transforms::CFGBuilder::run(*func8);
        transforms::LinearScanAllocator allocator;
        allocator.run(*func8);
    }

    // Function for i16
    Function* func16 = builder.createFunction("test_simd_func_i16", ctx->getVoidType(), {i64Ty, i64Ty, i64Ty, i64Ty, i64Ty});
    {
        const auto& params = func16->getParameters();
        auto pIt = params.begin();
        Value* pInA = (pIt++)->get();
        Value* pInB = (pIt++)->get();
        Value* pOutAdd = (pIt++)->get();
        Value* pOutSub = (pIt++)->get();
        Value* pOutMul = (pIt++)->get();

        BasicBlock* entry = builder.createBasicBlock("entry", func16);
        builder.setInsertPoint(entry);

        VectorInstruction* vA = builder.createVLoad(vec8i16Ty, pInA);
        VectorInstruction* vB = builder.createVLoad(vec8i16Ty, pInB);

        VectorInstruction* vAdd = builder.createVAdd(vA, vB);
        VectorInstruction* vSub = builder.createVSub(vA, vB);
        VectorInstruction* vMul = builder.createVMul(vA, vB);

        builder.createVStore(vAdd, pOutAdd);
        builder.createVStore(vSub, pOutSub);
        builder.createVStore(vMul, pOutMul);
        builder.createRet(nullptr);

        transforms::CFGBuilder::run(*func16);
        transforms::LinearScanAllocator allocator;
        allocator.run(*func16);
    }

    // Function for i64
    Function* func64 = builder.createFunction("test_simd_func_i64", ctx->getVoidType(), {i64Ty, i64Ty, i64Ty, i64Ty});
    {
        const auto& params = func64->getParameters();
        auto pIt = params.begin();
        Value* pInA = (pIt++)->get();
        Value* pInB = (pIt++)->get();
        Value* pOutAdd = (pIt++)->get();
        Value* pOutSub = (pIt++)->get();

        BasicBlock* entry = builder.createBasicBlock("entry", func64);
        builder.setInsertPoint(entry);

        VectorInstruction* vA = builder.createVLoad(vec2i64Ty, pInA);
        VectorInstruction* vB = builder.createVLoad(vec2i64Ty, pInB);

        VectorInstruction* vAdd = builder.createVAdd(vA, vB);
        VectorInstruction* vSub = builder.createVSub(vA, vB);

        builder.createVStore(vAdd, pOutAdd);
        builder.createVStore(vSub, pOutSub);
        builder.createRet(nullptr);

        transforms::CFGBuilder::run(*func64);
        transforms::LinearScanAllocator allocator;
        allocator.run(*func64);
    }

    auto x64Arch = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    auto linuxOS = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> targetInfo = std::make_unique<target::CompositeTargetInfo>(std::move(x64Arch), std::move(linuxOS));

    std::ostringstream asmStream;
    codegen::CodeGen cg(module, std::move(targetInfo), &asmStream);
    cg.emit(false);

    std::string asmCode = asmStream.str();
    std::cout << "Generated SIMD Assembly:\n" << asmCode << std::endl;

    assert(asmCode.find("paddb") != std::string::npos);
    assert(asmCode.find("psubb") != std::string::npos);
    assert(asmCode.find("paddw") != std::string::npos);
    assert(asmCode.find("psubw") != std::string::npos);
    assert(asmCode.find("pmullw") != std::string::npos);
    assert(asmCode.find("paddd") != std::string::npos);
    assert(asmCode.find("psubd") != std::string::npos);
    assert(asmCode.find("pmulld") != std::string::npos);
    assert(asmCode.find("paddq") != std::string::npos);
    assert(asmCode.find("psubq") != std::string::npos);

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

extern void test_simd_func_i32(const int32_t* inA, const int32_t* inB, int32_t* outAdd, int32_t* outSub, int32_t* outMul);
extern void test_simd_func_i8(const uint8_t* inA, const uint8_t* inB, uint8_t* outAdd, uint8_t* outSub);
extern void test_simd_func_i16(const uint16_t* inA, const uint16_t* inB, uint16_t* outAdd, uint16_t* outSub, uint16_t* outMul);
extern void test_simd_func_i64(const uint64_t* inA, const uint64_t* inB, uint64_t* outAdd, uint64_t* outSub);

int main() {
    // Test i32
    {
        int32_t inA[4] = {100, 200, 300, 400};
        int32_t inB[4] = {10, 20, 30, 40};
        int32_t outAdd[4] = {0}, outSub[4] = {0}, outMul[4] = {0};
        test_simd_func_i32(inA, inB, outAdd, outSub, outMul);
        assert(outAdd[0] == 110 && outAdd[1] == 220 && outAdd[2] == 330 && outAdd[3] == 440);
        assert(outSub[0] == 90 && outSub[1] == 180 && outSub[2] == 270 && outSub[3] == 360);
        assert(outMul[0] == 1000 && outMul[1] == 4000 && outMul[2] == 9000 && outMul[3] == 16000);
    }

    // Test i8 (including 0xFF + 1 wraparound)
    {
        uint8_t inA[16] = {255, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 150};
        uint8_t inB[16] = {1,   5,  5,  5,  5,  5,  5,  5,  5,  5,  5,   5,   5,   5,   5,   5};
        uint8_t outAdd[16] = {0}, outSub[16] = {0};
        test_simd_func_i8(inA, inB, outAdd, outSub);
        assert(outAdd[0] == 0 && outAdd[1] == 15); // Proven 8-bit wraparound: 255 + 1 = 0, next lane 10 + 5 = 15 uncorrupted
        assert(outSub[0] == 254 && outSub[1] == 5);
    }

    // Test i16 (including 0xFFFF + 1 wraparound)
    {
        uint16_t inA[8] = {0xFFFF, 1000, 2000, 3000, 4000, 5000, 6000, 7000};
        uint16_t inB[8] = {1,      500,  500,  500,  500,  500,  500,  500};
        uint16_t outAdd[8] = {0}, outSub[8] = {0}, outMul[8] = {0};
        test_simd_func_i16(inA, inB, outAdd, outSub, outMul);
        assert(outAdd[0] == 0 && outAdd[1] == 1500); // Proven 16-bit wraparound
        assert(outSub[0] == 0xFFFE && outSub[1] == 500);
        assert(outMul[1] == 500000 % 65536);
    }

    // Test i64 (carry across 32-bit boundary)
    {
        uint64_t inA[2] = {0x00000000FFFFFFFFULL, 100000000000ULL};
        uint64_t inB[2] = {1ULL,                   50000000000ULL};
        uint64_t outAdd[2] = {0}, outSub[2] = {0};
        test_simd_func_i64(inA, inB, outAdd, outSub);
        assert(outAdd[0] == 0x0000000100000000ULL); // Proven 64-bit carry propagation across low dword to high dword
        assert(outSub[0] == 0x00000000FFFFFFFEULL);
    }

    printf("SIMD_TYPED_RUNTIME_EXECUTION_SUCCESS\n");
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
    assert(resultOutput.find("SIMD_TYPED_RUNTIME_EXECUTION_SUCCESS") != std::string::npos);

    std::cout << "--- End-to-End SIMD Runtime Execution Test PASSED ---" << std::endl;
}

void test_simd_rejection() {
    std::cout << "--- Running SIMD Negative Rejection Tests ---" << std::endl;
    auto x64Arch = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);

    auto ctx = std::make_shared<IRContext>();
    Type* i8Ty = ctx->getIntegerType(8);
    Type* i64Ty = ctx->getIntegerType(64);

    VectorType* vec16i8 = ctx->getVectorType(i8Ty, 16);
    VectorType* vec2i64 = ctx->getVectorType(i64Ty, 2);
    VectorType* vec3i32 = ctx->getVectorType(ctx->getIntegerType(32), 3);

    // supportsVectorType checks
    assert(x64Arch->supportsVectorType(vec16i8) == true);
    assert(x64Arch->supportsVectorType(vec2i64) == true);
    assert(x64Arch->supportsVectorType(vec3i32) == false); // Unsupported lane count

    // VMul rejection check for <2 x i64>
    Module module("test_simd_reject", ctx);
    IRBuilder builder(ctx);
    builder.setModule(&module);
    Function* func = builder.createFunction("test_reject_func", ctx->getVoidType(), {i64Ty, i64Ty, i64Ty});
    const auto& params = func->getParameters();
    auto pIt = params.begin();
    Value* pInA = (pIt++)->get();
    Value* pInB = (pIt++)->get();
    Value* pOut = (pIt++)->get();

    BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    VectorInstruction* vA = builder.createVLoad(vec2i64, pInA);
    VectorInstruction* vB = builder.createVLoad(vec2i64, pInB);
    VectorInstruction* vMul = builder.createVMul(vA, vB);
    builder.createVStore(vMul, pOut);
    builder.createRet(nullptr);

    transforms::CFGBuilder::run(*func);
    transforms::LinearScanAllocator allocator;
    allocator.run(*func);

    auto linuxOS = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> targetInfo = std::make_unique<target::CompositeTargetInfo>(std::move(x64Arch), std::move(linuxOS));
    std::ostringstream asmStream;
    codegen::CodeGen cg(module, std::move(targetInfo), &asmStream);

    bool caughtError = false;
    try {
        cg.emit(false);
    } catch (const std::runtime_error& e) {
        caughtError = true;
        std::cout << "Caught expected rejection: " << e.what() << std::endl;
    }
    assert(caughtError && "VMul for <2 x i64> must be rejected!");

    std::cout << "--- SIMD Negative Rejection Tests PASSED ---" << std::endl;
}

int main() {
    test_simd_runtime_execution();
    test_simd_rejection();
    return 0;
}
