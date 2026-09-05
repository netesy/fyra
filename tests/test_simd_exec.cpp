#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/SIMDInstruction.h"
#include "ir/PhiNode.h"
#include "codegen/CodeGen.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "codegen/regalloc/RegAllocRewriter.h"
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
    Type* f32Ty = ctx->getFloatType();
    Type* f64Ty = ctx->getDoubleType();

    VectorType* vec16i8Ty = ctx->getVectorType(i8Ty, 16);
    VectorType* vec8i16Ty = ctx->getVectorType(i16Ty, 8);
    VectorType* vec4i32Ty = ctx->getVectorType(i32Ty, 4);
    VectorType* vec2i64Ty = ctx->getVectorType(i64Ty, 2);
    VectorType* vec4f32Ty = ctx->getVectorType(f32Ty, 4);
    VectorType* vec2f64Ty = ctx->getVectorType(f64Ty, 2);

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

    // Function for <4 x f32>
    Function* funcF32 = builder.createFunction("test_simd_func_f32", ctx->getVoidType(), {i64Ty, i64Ty, i64Ty, i64Ty, i64Ty, i64Ty});
    {
        const auto& params = funcF32->getParameters();
        auto pIt = params.begin();
        Value* pInA = (pIt++)->get();
        Value* pInB = (pIt++)->get();
        Value* pOutAdd = (pIt++)->get();
        Value* pOutSub = (pIt++)->get();
        Value* pOutMul = (pIt++)->get();
        Value* pOutDiv = (pIt++)->get();

        BasicBlock* entry = builder.createBasicBlock("entry", funcF32);
        builder.setInsertPoint(entry);

        VectorInstruction* vA = builder.createVLoad(vec4f32Ty, pInA);
        VectorInstruction* vB = builder.createVLoad(vec4f32Ty, pInB);

        VectorInstruction* vAdd = new VectorInstruction(vec4f32Ty, Instruction::VFAdd, {vA, vB}, 128);
        VectorInstruction* vSub = new VectorInstruction(vec4f32Ty, Instruction::VFSub, {vA, vB}, 128);
        VectorInstruction* vMul = new VectorInstruction(vec4f32Ty, Instruction::VFMul, {vA, vB}, 128);
        VectorInstruction* vDiv = new VectorInstruction(vec4f32Ty, Instruction::VFDiv, {vA, vB}, 128);

        entry->addInstruction(std::unique_ptr<Instruction>(vAdd));
        entry->addInstruction(std::unique_ptr<Instruction>(vSub));
        entry->addInstruction(std::unique_ptr<Instruction>(vMul));
        entry->addInstruction(std::unique_ptr<Instruction>(vDiv));

        builder.createVStore(vAdd, pOutAdd);
        builder.createVStore(vSub, pOutSub);
        builder.createVStore(vMul, pOutMul);
        builder.createVStore(vDiv, pOutDiv);
        builder.createRet(nullptr);

        transforms::CFGBuilder::run(*funcF32);
        transforms::LinearScanAllocator allocator;
        allocator.run(*funcF32);
    }

    // Function for <2 x f64>
    Function* funcF64 = builder.createFunction("test_simd_func_f64", ctx->getVoidType(), {i64Ty, i64Ty, i64Ty, i64Ty, i64Ty, i64Ty});
    {
        const auto& params = funcF64->getParameters();
        auto pIt = params.begin();
        Value* pInA = (pIt++)->get();
        Value* pInB = (pIt++)->get();
        Value* pOutAdd = (pIt++)->get();
        Value* pOutSub = (pIt++)->get();
        Value* pOutMul = (pIt++)->get();
        Value* pOutDiv = (pIt++)->get();

        BasicBlock* entry = builder.createBasicBlock("entry", funcF64);
        builder.setInsertPoint(entry);

        VectorInstruction* vA = builder.createVLoad(vec2f64Ty, pInA);
        VectorInstruction* vB = builder.createVLoad(vec2f64Ty, pInB);

        VectorInstruction* vAdd = new VectorInstruction(vec2f64Ty, Instruction::VFAdd, {vA, vB}, 128);
        VectorInstruction* vSub = new VectorInstruction(vec2f64Ty, Instruction::VFSub, {vA, vB}, 128);
        VectorInstruction* vMul = new VectorInstruction(vec2f64Ty, Instruction::VFMul, {vA, vB}, 128);
        VectorInstruction* vDiv = new VectorInstruction(vec2f64Ty, Instruction::VFDiv, {vA, vB}, 128);

        entry->addInstruction(std::unique_ptr<Instruction>(vAdd));
        entry->addInstruction(std::unique_ptr<Instruction>(vSub));
        entry->addInstruction(std::unique_ptr<Instruction>(vMul));
        entry->addInstruction(std::unique_ptr<Instruction>(vDiv));

        builder.createVStore(vAdd, pOutAdd);
        builder.createVStore(vSub, pOutSub);
        builder.createVStore(vMul, pOutMul);
        builder.createVStore(vDiv, pOutDiv);
        builder.createRet(nullptr);

        transforms::CFGBuilder::run(*funcF64);
        transforms::LinearScanAllocator allocator;
        allocator.run(*funcF64);
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
    assert(asmCode.find("addps") != std::string::npos);
    assert(asmCode.find("subps") != std::string::npos);
    assert(asmCode.find("mulps") != std::string::npos);
    assert(asmCode.find("divps") != std::string::npos);
    assert(asmCode.find("addpd") != std::string::npos);
    assert(asmCode.find("subpd") != std::string::npos);
    assert(asmCode.find("mulpd") != std::string::npos);
    assert(asmCode.find("divpd") != std::string::npos);

    // Direct memory addressing assertions (verifies removal of redundant movq %rdi, %rax copies)
    assert(asmCode.find("movdqu (%rdi), %xmm0") != std::string::npos);
    assert(asmCode.find("movdqu %xmm2, (%rdx)") != std::string::npos);
    assert(asmCode.find("movq %rdi, %rax") == std::string::npos);

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
#include <stdbool.h>
#include <math.h>
#include <assert.h>

extern void test_simd_func_i32(const int32_t* inA, const int32_t* inB, int32_t* outAdd, int32_t* outSub, int32_t* outMul);
extern void test_simd_func_i8(const uint8_t* inA, const uint8_t* inB, uint8_t* outAdd, uint8_t* outSub);
extern void test_simd_func_i16(const uint16_t* inA, const uint16_t* inB, uint16_t* outAdd, uint16_t* outSub, uint16_t* outMul);
extern void test_simd_func_i64(const uint64_t* inA, const uint64_t* inB, uint64_t* outAdd, uint64_t* outSub);
extern void test_simd_func_f32(const float* inA, const float* inB, float* outAdd, float* outSub, float* outMul, float* outDiv);
extern void test_simd_func_f64(const double* inA, const double* inB, double* outAdd, double* outSub, double* outMul, double* outDiv);

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
        assert(outAdd[0] == 0 && outAdd[1] == 15);
        assert(outSub[0] == 254 && outSub[1] == 5);
    }

    // Test i16 (including 0xFFFF + 1 wraparound)
    {
        uint16_t inA[8] = {0xFFFF, 1000, 2000, 3000, 4000, 5000, 6000, 7000};
        uint16_t inB[8] = {1,      500,  500,  500,  500,  500,  500,  500};
        uint16_t outAdd[8] = {0}, outSub[8] = {0}, outMul[8] = {0};
        test_simd_func_i16(inA, inB, outAdd, outSub, outMul);
        assert(outAdd[0] == 0 && outAdd[1] == 1500);
        assert(outSub[0] == 0xFFFE && outSub[1] == 500);
        assert(outMul[1] == 500000 % 65536);
    }

    // Test i64 (carry across 32-bit boundary)
    {
        uint64_t inA[2] = {0x00000000FFFFFFFFULL, 100000000000ULL};
        uint64_t inB[2] = {1ULL,                   50000000000ULL};
        uint64_t outAdd[2] = {0}, outSub[2] = {0};
        test_simd_func_i64(inA, inB, outAdd, outSub);
        assert(outAdd[0] == 0x0000000100000000ULL);
        assert(outSub[0] == 0x00000000FFFFFFFEULL);
    }

    // Test <4 x f32> (asymmetric subtraction and non-reciprocal division)
    {
        float inA[4] = {10.0f, 20.0f, 30.0f, 40.0f};
        float inB[4] = {1.0f, 2.0f, 3.0f, 4.0f};
        float outAdd[4] = {0}, outSub[4] = {0}, outMul[4] = {0}, outDiv[4] = {0};
        test_simd_func_f32(inA, inB, outAdd, outSub, outMul, outDiv);
        assert(fabsf(outAdd[0] - 11.0f) < 1e-5f && fabsf(outAdd[3] - 44.0f) < 1e-5f);
        assert(fabsf(outSub[0] - 9.0f) < 1e-5f && fabsf(outSub[3] - 36.0f) < 1e-5f); // Proves lhs - rhs, not reversed
        assert(fabsf(outMul[0] - 10.0f) < 1e-5f && fabsf(outMul[3] - 160.0f) < 1e-5f);
        assert(fabsf(outDiv[0] - 10.0f) < 1e-5f && fabsf(outDiv[3] - 10.0f) < 1e-5f); // Proves lhs / rhs, not reversed
    }

    // Test <2 x f64> (asymmetric subtraction and non-reciprocal division)
    {
        double inA[2] = {100.0, 50.0};
        double inB[2] = {4.0, 5.0};
        double outAdd[2] = {0}, outSub[2] = {0}, outMul[2] = {0}, outDiv[2] = {0};
        test_simd_func_f64(inA, inB, outAdd, outSub, outMul, outDiv);
        assert(fabs(outAdd[0] - 104.0) < 1e-9 && fabs(outAdd[1] - 55.0) < 1e-9);
        assert(fabs(outSub[0] - 96.0) < 1e-9 && fabs(outSub[1] - 45.0) < 1e-9); // Proves lhs - rhs
        assert(fabs(outMul[0] - 400.0) < 1e-9 && fabs(outMul[1] - 250.0) < 1e-9);
        assert(fabs(outDiv[0] - 25.0) < 1e-9 && fabs(outDiv[1] - 10.0) < 1e-9); // Proves lhs / rhs
    }

    printf("SIMD_FP_RUNTIME_EXECUTION_SUCCESS\n");
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
    assert(resultOutput.find("SIMD_FP_RUNTIME_EXECUTION_SUCCESS") != std::string::npos);

    std::cout << "--- End-to-End SIMD Runtime Execution Test PASSED ---" << std::endl;
}

void test_simd_loop_liveness() {
    std::cout << "--- Running SIMD Loop Liveness Regression Test ---" << std::endl;

    auto ctx = std::make_shared<IRContext>();
    Module module("test_simd_loop_module", ctx);
    IRBuilder builder(ctx);
    builder.setModule(&module);

    Type* i32Ty = ctx->getIntegerType(32);
    Type* i64Ty = ctx->getIntegerType(64);
    VectorType* vec4i32Ty = ctx->getVectorType(i32Ty, 4);

    Function* func = builder.createFunction("test_simd_loop_func", ctx->getVoidType(), {i64Ty, i64Ty, i64Ty});
    const auto& params = func->getParameters();
    auto pIt = params.begin();
    Value* pInA = (pIt++)->get();
    Value* pInB = (pIt++)->get();
    Value* pOut = (pIt++)->get();

    BasicBlock* entry = builder.createBasicBlock("entry", func);
    BasicBlock* loopHead = builder.createBasicBlock("loop_head", func);
    BasicBlock* loopBody = builder.createBasicBlock("loop_body", func);
    BasicBlock* loopExit = builder.createBasicBlock("loop_exit", func);

    // Entry (preheader): load vector A and vector B, compute preheader invariant vector
    builder.setInsertPoint(entry);
    VectorInstruction* vA = builder.createVLoad(vec4i32Ty, pInA);
    VectorInstruction* vB = builder.createVLoad(vec4i32Ty, pInB);
    VectorInstruction* vInvariant = builder.createVAdd(vA, vB); // Preheader SIMD invariant
    builder.createJmp(loopHead);

    // Loop head
    builder.setInsertPoint(loopHead);
    auto phiI_ptr = std::make_unique<PhiNode>(i32Ty, 0, nullptr, loopHead);
    PhiNode* phiI = phiI_ptr.get();
    loopHead->getInstructions().push_back(std::move(phiI_ptr));

    auto phiAcc_ptr = std::make_unique<PhiNode>(vec4i32Ty, 0, nullptr, loopHead);
    PhiNode* phiAcc = phiAcc_ptr.get();
    loopHead->getInstructions().push_back(std::move(phiAcc_ptr));

    phiI->addIncoming(ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 0), entry);
    phiAcc->addIncoming(vA, entry);

    Instruction* cond = builder.createCslt(phiI, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 10));
    builder.createBr(cond, loopBody, loopExit);

    // Loop body: Accumulate vInvariant 10 times across loop recurrences
    builder.setInsertPoint(loopBody);
    VectorInstruction* vAccNext = builder.createVAdd(phiAcc, vInvariant);
    Instruction* iNext = builder.createAdd(phiI, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 1));

    phiI->addIncoming(iNext, loopBody);
    phiAcc->addIncoming(vAccNext, loopBody);
    builder.createJmp(loopHead);

    // Loop exit: Store accumulated SIMD result
    builder.setInsertPoint(loopExit);
    builder.createVStore(phiAcc, pOut);
    builder.createRet(nullptr);

    transforms::CFGBuilder::run(*func);
    transforms::LivenessAnalysis liveness_vextract;
    liveness_vextract.run(*func);
    transforms::RegAllocRewriter rewriter_vextract;
    rewriter_vextract.run(*func);

    auto x64Arch = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    auto linuxOS = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> targetInfo = std::make_unique<target::CompositeTargetInfo>(std::move(x64Arch), std::move(linuxOS));

    std::ostringstream asmStream;
    codegen::CodeGen cg(module, std::move(targetInfo), &asmStream);
    cg.emit(false);

    std::string asmCode = asmStream.str();
    assert(asmCode.find("paddd") != std::string::npos);

    std::string asmFilePath = "/tmp/test_simd_loop_generated.s";
    std::string binFilePath = "/tmp/test_simd_loop_runner";
    {
        std::ofstream asmFile(asmFilePath);
        asmFile << asmCode;
    }

    std::string harnessPath = "/tmp/test_simd_loop_harness.c";
    {
        std::ofstream hFile(harnessPath);
        hFile << R"(
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

extern void test_simd_loop_func(const int32_t* inA, const int32_t* inB, int32_t* out);

int main() {
    int32_t inA[4] = {10, 20, 30, 40};
    int32_t inB[4] = {1,  2,  3,  4};
    int32_t out[4] = {0};

    // inA + 10 * (inA + inB) = [10, 20, 30, 40] + 10 * [11, 22, 33, 44] = [120, 240, 360, 480]
    test_simd_loop_func(inA, inB, out);
    assert(out[0] == 120 && out[1] == 240 && out[2] == 360 && out[3] == 480);
    printf("SIMD_LOOP_LIVENESS_SUCCESS\n");
    return 0;
}
)";
    }

    std::string compileCmd = "gcc -no-pie " + asmFilePath + " " + harnessPath + " -o " + binFilePath;
    int compileRc = std::system(compileCmd.c_str());
    assert(compileRc == 0 && "Compilation of SIMD loop test assembly failed");

    FILE* pipe = popen(binFilePath.c_str(), "r");
    assert(pipe != nullptr);
    char buffer[128];
    std::string resultOutput = "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        resultOutput += buffer;
    }
    int execRc = pclose(pipe);

    assert(execRc == 0 && "Execution of SIMD loop test binary failed");
    assert(resultOutput.find("SIMD_LOOP_LIVENESS_SUCCESS") != std::string::npos);

    std::cout << "--- SIMD Loop Liveness Regression Test PASSED ---" << std::endl;
}

void test_simd_rejection() {
    std::cout << "--- Running SIMD Negative Rejection Tests ---" << std::endl;
    auto x64Arch = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);

    auto ctx = std::make_shared<IRContext>();
    Type* i8Ty = ctx->getIntegerType(8);
    Type* i64Ty = ctx->getIntegerType(64);
    Type* f32Ty = ctx->getFloatType();
    Type* f64Ty = ctx->getDoubleType();

    VectorType* vec16i8 = ctx->getVectorType(i8Ty, 16);
    VectorType* vec2i64 = ctx->getVectorType(i64Ty, 2);
    VectorType* vec3i32 = ctx->getVectorType(ctx->getIntegerType(32), 3);
    VectorType* vec3f32 = ctx->getVectorType(f32Ty, 3);
    VectorType* vec4f64 = ctx->getVectorType(f64Ty, 4);

    // supportsVectorType checks
    assert(x64Arch->supportsVectorType(vec16i8) == true);
    assert(x64Arch->supportsVectorType(vec2i64) == true);
    assert(x64Arch->supportsVectorType(vec3i32) == false); // Unsupported lane count
    assert(x64Arch->supportsVectorType(vec3f32) == false); // Unsupported float lane count
    assert(x64Arch->supportsVectorType(vec4f64) == false); // Unsupported double lane count

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

void test_simd_vbroadcast() {
    std::cout << "--- Running SIMD VBroadcast Execution & Assembly Assertion Test ---" << std::endl;

    auto ctx = std::make_shared<IRContext>();
    Module module("test_simd_vbroadcast_module", ctx);
    IRBuilder builder(ctx);
    builder.setModule(&module);

    Type* i8Ty = ctx->getIntegerType(8);
    Type* i16Ty = ctx->getIntegerType(16);
    Type* i32Ty = ctx->getIntegerType(32);
    Type* i64Ty = ctx->getIntegerType(64);
    Type* f32Ty = ctx->getFloatType();
    Type* f64Ty = ctx->getDoubleType();

    VectorType* vec16i8Ty = ctx->getVectorType(i8Ty, 16);
    VectorType* vec8i16Ty = ctx->getVectorType(i16Ty, 8);
    VectorType* vec4i32Ty = ctx->getVectorType(i32Ty, 4);
    VectorType* vec2i64Ty = ctx->getVectorType(i64Ty, 2);
    VectorType* vec4f32Ty = ctx->getVectorType(f32Ty, 4);
    VectorType* vec2f64Ty = ctx->getVectorType(f64Ty, 2);

    Function* func = builder.createFunction("test_vbroadcast_func", ctx->getVoidType(), {i64Ty});
    const auto& params = func->getParameters();
    Value* pArgs = params.front().get();

    BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    Value* ptr0 = builder.createCopy(pArgs);

    // 1. i32 VBroadcast
    Value* pIn32 = builder.createLoadl(ptr0);
    Value* ptr4 = builder.createAdd(ptr0, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 32));
    Value* pOut32 = builder.createLoadl(ptr4);
    Instruction* val32 = builder.createLoaduw(pIn32);
    VectorInstruction* v32 = new VectorInstruction(vec4i32Ty, Instruction::VBroadcast, {val32}, 128);
    entry->addInstruction(std::unique_ptr<Instruction>(v32));
    builder.createVStore(v32, pOut32);

    // Scalar preservation check: use val32 in scalar arithmetic
    Value* ptr10 = builder.createAdd(ptr0, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 80));
    Value* pOutPreserve = builder.createLoadl(ptr10);
    Value* val32Add = builder.createAdd(val32, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 100));
    builder.createStore(val32Add, pOutPreserve);

    // 2. i64 VBroadcast
    Value* ptr1 = builder.createAdd(ptr0, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 8));
    Value* pIn64 = builder.createLoadl(ptr1);
    Value* ptr5 = builder.createAdd(ptr0, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 40));
    Value* pOut64 = builder.createLoadl(ptr5);
    Instruction* val64 = builder.createLoadl(pIn64);
    VectorInstruction* v64 = new VectorInstruction(vec2i64Ty, Instruction::VBroadcast, {val64}, 128);
    entry->addInstruction(std::unique_ptr<Instruction>(v64));
    builder.createVStore(v64, pOut64);

    // 3. i16 VBroadcast
    Value* ptr6 = builder.createAdd(ptr0, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 48));
    Value* pOut16 = builder.createLoadl(ptr6);
    Value* val16 = builder.createTruncD(val32, i16Ty);
    VectorInstruction* v16 = new VectorInstruction(vec8i16Ty, Instruction::VBroadcast, {val16}, 128);
    entry->addInstruction(std::unique_ptr<Instruction>(v16));
    builder.createVStore(v16, pOut16);

    // 4. i8 VBroadcast
    Value* ptr7 = builder.createAdd(ptr0, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 56));
    Value* pOut8 = builder.createLoadl(ptr7);
    Value* val8 = builder.createTruncD(val32, i8Ty);
    VectorInstruction* v8 = new VectorInstruction(vec16i8Ty, Instruction::VBroadcast, {val8}, 128);
    entry->addInstruction(std::unique_ptr<Instruction>(v8));
    builder.createVStore(v8, pOut8);

    // 5. f32 VBroadcast
    Value* ptr2 = builder.createAdd(ptr0, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 16));
    Value* pInF32 = builder.createLoadl(ptr2);
    Value* ptr8 = builder.createAdd(ptr0, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 64));
    Value* pOutF32 = builder.createLoadl(ptr8);
    Instruction* valF32 = builder.createLoadd(pInF32);
    VectorInstruction* vF32 = new VectorInstruction(vec4f32Ty, Instruction::VBroadcast, {valF32}, 128);
    entry->addInstruction(std::unique_ptr<Instruction>(vF32));
    builder.createVStore(vF32, pOutF32);

    // 6. f64 VBroadcast
    Value* ptr3 = builder.createAdd(ptr0, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 24));
    Value* pInF64 = builder.createLoadl(ptr3);
    Value* ptr9 = builder.createAdd(ptr0, ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), 72));
    Value* pOutF64 = builder.createLoadl(ptr9);
    Instruction* valF64 = builder.createLoadl(pInF64);
    VectorInstruction* vF64 = new VectorInstruction(vec2f64Ty, Instruction::VBroadcast, {valF64}, 128);
    entry->addInstruction(std::unique_ptr<Instruction>(vF64));
    builder.createVStore(vF64, pOutF64);
    builder.createRet(nullptr);

    transforms::CFGBuilder::run(*func);
    transforms::LivenessAnalysis liveness;
    liveness.run(*func);
    transforms::RegAllocRewriter rewriter;
    rewriter.run(*func);

    auto x64Arch = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    auto linuxOS = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> targetInfo = std::make_unique<target::CompositeTargetInfo>(std::move(x64Arch), std::move(linuxOS));

    std::ostringstream asmStream;
    codegen::CodeGen cg(module, std::move(targetInfo), &asmStream);
    cg.emit(false);

    std::string asmCode = asmStream.str();
    std::cout << "Generated VBroadcast Assembly:\n" << asmCode << std::endl;

    assert(asmCode.find("pshufd") != std::string::npos && "pshufd required for i32 broadcast");
    assert(asmCode.find("punpcklqdq") != std::string::npos && "punpcklqdq required for i64 broadcast");
    assert(asmCode.find("pshuflw") != std::string::npos && "pshuflw required for i16 broadcast");
    assert(asmCode.find("punpcklbw") != std::string::npos && "punpcklbw required for i8 broadcast");
    assert(asmCode.find("shufps") != std::string::npos && "shufps required for f32 broadcast");
    assert(asmCode.find("movddup") != std::string::npos && "movddup required for f64 broadcast");

    std::string asmFilePath = "/tmp/test_vbroadcast_generated.s";
    std::string binFilePath = "/tmp/test_vbroadcast_runner";
    {
        std::ofstream asmFile(asmFilePath);
        asmFile << asmCode;
    }

    std::string harnessPath = "/tmp/test_vbroadcast_harness.c";
    {
        std::ofstream hFile(harnessPath);
        hFile << R"(
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>

struct VBroadcastArgs {
    uint64_t pVal32;
    uint64_t pVal64;
    uint64_t pValF32;
    uint64_t pValF64;
    uint64_t pOut32;
    uint64_t pOut64;
    uint64_t pOut16;
    uint64_t pOut8;
    uint64_t pOutF32;
    uint64_t pOutF64;
    uint64_t pOutPreserve;
};

extern void test_vbroadcast_func(const struct VBroadcastArgs* args);

int main() {
    int32_t val32 = -1234567;
    int64_t val64 = 0x123456789ABCDEF0LL;
    float valF32 = 3.14159265f;
    double valF64 = 2.718281828459045;

    int32_t out32[4] = {0};
    int64_t out64[2] = {0};
    int16_t out16[8] = {0};
    int8_t  out8[16] = {0};
    float   outF32[4] = {0};
    double  outF64[2] = {0};
    int32_t outPreserve[1] = {0};

    struct VBroadcastArgs args = {
        (uint64_t)&val32,
        (uint64_t)&val64,
        (uint64_t)&valF32,
        (uint64_t)&valF64,
        (uint64_t)out32,
        (uint64_t)out64,
        (uint64_t)out16,
        (uint64_t)out8,
        (uint64_t)outF32,
        (uint64_t)outF64,
        (uint64_t)outPreserve
    };

    test_vbroadcast_func(&args);

    // Verify i32 broadcast
    for (int i = 0; i < 4; i++) {
        assert(out32[i] == -1234567);
    }

    // Verify i64 broadcast
    for (int i = 0; i < 2; i++) {
        assert(out64[i] == 0x123456789ABCDEF0LL);
    }

    // Verify i16 broadcast
    int16_t expected16 = (int16_t)(-1234567 & 0xFFFF);
    for (int i = 0; i < 8; i++) {
        assert(out16[i] == expected16);
    }

    // Verify i8 broadcast
    int8_t expected8 = (int8_t)(-1234567 & 0xFF);
    for (int i = 0; i < 16; i++) {
        assert(out8[i] == expected8);
    }

    // Verify f32 broadcast
    for (int i = 0; i < 4; i++) {
        assert(fabsf(outF32[i] - 3.14159265f) < 1e-6f);
    }

    // Verify f64 broadcast
    for (int i = 0; i < 2; i++) {
        assert(fabs(outF64[i] - 2.718281828459045) < 1e-12);
    }

    // Verify scalar preservation
    assert(outPreserve[0] == -1234567 + 100);

    printf("VBROADCAST_RUNTIME_EXECUTION_SUCCESS\n");
    return 0;
}
)";
    }

    std::string compileCmd = "gcc -no-pie " + asmFilePath + " " + harnessPath + " -o " + binFilePath;
    int compileRc = std::system(compileCmd.c_str());
    assert(compileRc == 0 && "Compilation of VBroadcast test assembly failed");

    FILE* pipe = popen(binFilePath.c_str(), "r");
    assert(pipe != nullptr);
    char buffer[128];
    std::string resultOutput = "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        resultOutput += buffer;
    }
    int execRc = pclose(pipe);

    assert(execRc == 0 && "Execution of VBroadcast test binary failed");
    assert(resultOutput.find("VBROADCAST_RUNTIME_EXECUTION_SUCCESS") != std::string::npos);

    std::cout << "--- SIMD VBroadcast Execution & Assembly Assertion Test PASSED ---" << std::endl;
}

void test_simd_vextract() {
    std::cout << "--- Running SIMD VExtract Execution & Assembly Assertion Test ---" << std::endl;

    auto ctx = std::make_shared<IRContext>();
    Module module("test_simd_vextract_module", ctx);
    IRBuilder builder(ctx);
    builder.setModule(&module);

    Type* i8Ty = ctx->getIntegerType(8);
    Type* i16Ty = ctx->getIntegerType(16);
    Type* i32Ty = ctx->getIntegerType(32);
    Type* i64Ty = ctx->getIntegerType(64);
    Type* f32Ty = ctx->getFloatType();
    Type* f64Ty = ctx->getDoubleType();

    VectorType* vec16i8Ty = ctx->getVectorType(i8Ty, 16);
    VectorType* vec8i16Ty = ctx->getVectorType(i16Ty, 8);
    VectorType* vec4i32Ty = ctx->getVectorType(i32Ty, 4);
    VectorType* vec2i64Ty = ctx->getVectorType(i64Ty, 2);
    VectorType* vec4f32Ty = ctx->getVectorType(f32Ty, 4);
    VectorType* vec2f64Ty = ctx->getVectorType(f64Ty, 2);

    Function* func = builder.createFunction("test_vextract_func", ctx->getVoidType(), {i64Ty});
    const auto& params = func->getParameters();
    Value* pArgs = params.front().get();

    BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    Instruction* slot = builder.createAlloc16(i64Ty);
    builder.createStore(pArgs, slot);

    auto loadPtrAt = [&](int byteOffset) -> Value* {
        Value* currentArgs = builder.createLoad(slot);
        Value* offVal = ctx->getConstantInt(dynamic_cast<IntegerType*>(i64Ty), byteOffset);
        Value* slotAddr = builder.createAdd(currentArgs, offVal);
        return builder.createLoadl(slotAddr);
    };

    // Group 1: i8 (lane 0, 7, 15)
    {
        Value* pIn8 = loadPtrAt(0);
        VectorInstruction* v8 = builder.createVLoad(vec16i8Ty, pIn8);

        VectorInstruction* ext8_0 = builder.createVExtract(v8, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 0));
        builder.createStore(ext8_0, loadPtrAt(48));

        VectorInstruction* ext8_7 = builder.createVExtract(v8, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 7));
        builder.createStore(ext8_7, loadPtrAt(56));

        VectorInstruction* ext8_15 = builder.createVExtract(v8, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 15));
        builder.createStore(ext8_15, loadPtrAt(64));
    }

    // Group 2: i16 (lane 0, 3, 7)
    {
        Value* pIn16 = loadPtrAt(8);
        VectorInstruction* v16 = builder.createVLoad(vec8i16Ty, pIn16);

        VectorInstruction* ext16_0 = builder.createVExtract(v16, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 0));
        builder.createStore(ext16_0, loadPtrAt(72));

        VectorInstruction* ext16_3 = builder.createVExtract(v16, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 3));
        builder.createStore(ext16_3, loadPtrAt(80));

        VectorInstruction* ext16_7 = builder.createVExtract(v16, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 7));
        builder.createStore(ext16_7, loadPtrAt(88));
    }

    // Group 3: i32 (lane 0, 1, 3, plus vector reuse)
    {
        Value* pIn32 = loadPtrAt(16);
        VectorInstruction* v32 = builder.createVLoad(vec4i32Ty, pIn32);

        VectorInstruction* ext32_0 = builder.createVExtract(v32, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 0));
        builder.createStore(ext32_0, loadPtrAt(96));

        VectorInstruction* ext32_1 = builder.createVExtract(v32, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 1));
        builder.createStore(ext32_1, loadPtrAt(104));

        VectorInstruction* ext32_3 = builder.createVExtract(v32, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 3));
        builder.createStore(ext32_3, loadPtrAt(112));

        VectorInstruction* v32Double = builder.createVAdd(v32, v32);
        builder.createVStore(v32Double, loadPtrAt(176));
    }

    // Group 4: i64 (lane 0, 1)
    {
        Value* pIn64 = loadPtrAt(24);
        VectorInstruction* v64 = builder.createVLoad(vec2i64Ty, pIn64);

        VectorInstruction* ext64_0 = builder.createVExtract(v64, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 0));
        builder.createStore(ext64_0, loadPtrAt(120));

        VectorInstruction* ext64_1 = builder.createVExtract(v64, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 1));
        builder.createStore(ext64_1, loadPtrAt(128));
    }

    // Group 5: f32 (lane 0, 1, 3)
    {
        Value* pInF32 = loadPtrAt(32);
        VectorInstruction* vF32 = builder.createVLoad(vec4f32Ty, pInF32);

        VectorInstruction* extF32_0 = builder.createVExtract(vF32, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 0));
        builder.createStore(extF32_0, loadPtrAt(136));

        VectorInstruction* extF32_1 = builder.createVExtract(vF32, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 1));
        builder.createStore(extF32_1, loadPtrAt(144));

        VectorInstruction* extF32_3 = builder.createVExtract(vF32, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 3));
        builder.createStore(extF32_3, loadPtrAt(152));
    }

    // Group 6: f64 (lane 0, 1)
    {
        Value* pInF64 = loadPtrAt(40);
        VectorInstruction* vF64 = builder.createVLoad(vec2f64Ty, pInF64);

        VectorInstruction* extF64_0 = builder.createVExtract(vF64, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 0));
        builder.createStore(extF64_0, loadPtrAt(160));

        VectorInstruction* extF64_1 = builder.createVExtract(vF64, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 1));
        builder.createStore(extF64_1, loadPtrAt(168));
    }

    builder.createRet(nullptr);

    transforms::CFGBuilder::run(*func);
    transforms::LivenessAnalysis liveness;
    liveness.run(*func);
    transforms::RegAllocRewriter rewriter;
    rewriter.run(*func);

    auto x64Arch = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    auto linuxOS = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> targetInfo = std::make_unique<target::CompositeTargetInfo>(std::move(x64Arch), std::move(linuxOS));

    std::ostringstream asmStream;
    codegen::CodeGen cg(module, std::move(targetInfo), &asmStream);
    cg.emit(false);

    std::string asmCode = asmStream.str();
    std::cout << "Generated VExtract Assembly:\n" << asmCode << std::endl;

    assert(asmCode.find("pextrb") != std::string::npos && "pextrb required for i8 extraction");
    assert(asmCode.find("pextrw") != std::string::npos && "pextrw required for i16 extraction");
    assert(asmCode.find("movd") != std::string::npos && "movd required for i32 lane 0 extraction");
    assert(asmCode.find("pextrd") != std::string::npos && "pextrd required for i32 extraction");
    assert(asmCode.find("movq") != std::string::npos && "movq required for i64 extraction");
    assert(asmCode.find("pextrq") != std::string::npos && "pextrq required for i64 lane 1 extraction");
    assert(asmCode.find("shufps") != std::string::npos && "shufps required for f32 extraction");
    assert(asmCode.find("movhlps") != std::string::npos && "movhlps required for f64 lane 1 extraction");

    std::string asmFilePath = "/tmp/test_vextract_generated.s";
    std::string binFilePath = "/tmp/test_vextract_runner";
    {
        std::ofstream asmFile(asmFilePath);
        asmFile << asmCode;
    }

    std::string harnessPath = "/tmp/test_vextract_harness.c";
    {
        std::ofstream hFile(harnessPath);
        hFile << R"(
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>

struct VExtractArgs {
    uint64_t pIn8;
    uint64_t pIn16;
    uint64_t pIn32;
    uint64_t pIn64;
    uint64_t pInF32;
    uint64_t pInF64;

    uint64_t pOut8_0;
    uint64_t pOut8_7;
    uint64_t pOut8_15;

    uint64_t pOut16_0;
    uint64_t pOut16_3;
    uint64_t pOut16_7;

    uint64_t pOut32_0;
    uint64_t pOut32_1;
    uint64_t pOut32_3;

    uint64_t pOut64_0;
    uint64_t pOut64_1;

    uint64_t pOutF32_0;
    uint64_t pOutF32_1;
    uint64_t pOutF32_3;

    uint64_t pOutF64_0;
    uint64_t pOutF64_1;

    uint64_t pOutReuse;
};

extern void test_vextract_func(const struct VExtractArgs* args);

int main() {
    int8_t  in8[16]  = { -1, 2, 3, 4, 5, 6, 7, 0x7F, -10, -20, -30, -40, -50, -60, -70, (int8_t)0x80 };
    int16_t in16[8]  = { -1000, 200, 300, 0x7FFF, 500, 600, 700, (int16_t)0x8000 };
    int32_t in32[4]  = { -1234567, 0x7FFFFFFF, 300000, (int32_t)0x80000000 };
    int64_t in64[2]  = { 0x123456789ABCDEF0LL, -100000000000LL };
    float   inF32[4] = { 3.14159f, -2.71828f, 100.5f, -0.001f };
    double  inF64[2] = { 2.718281828459045, -3.141592653589793 };

    int8_t  out8_0 = 0, out8_7 = 0, out8_15 = 0;
    int16_t out16_0 = 0, out16_3 = 0, out16_7 = 0;
    int32_t out32_0 = 0, out32_1 = 0, out32_3 = 0;
    int64_t out64_0 = 0, out64_1 = 0;
    float   outF32_0 = 0, outF32_1 = 0, outF32_3 = 0;
    double  outF64_0 = 0, outF64_1 = 0;
    int32_t outReuse[4] = {0};

    struct VExtractArgs args = {
        (uint64_t)in8, (uint64_t)in16, (uint64_t)in32, (uint64_t)in64, (uint64_t)inF32, (uint64_t)inF64,
        (uint64_t)&out8_0, (uint64_t)&out8_7, (uint64_t)&out8_15,
        (uint64_t)&out16_0, (uint64_t)&out16_3, (uint64_t)&out16_7,
        (uint64_t)&out32_0, (uint64_t)&out32_1, (uint64_t)&out32_3,
        (uint64_t)&out64_0, (uint64_t)&out64_1,
        (uint64_t)&outF32_0, (uint64_t)&outF32_1, (uint64_t)&outF32_3,
        (uint64_t)&outF64_0, (uint64_t)&outF64_1,
        (uint64_t)outReuse
    };

    test_vextract_func(&args);

    // Assert i8 extractions
    assert(out8_0 == -1);
    assert(out8_7 == 0x7F);
    assert(out8_15 == (int8_t)0x80);

    // Assert i16 extractions
    assert(out16_0 == -1000);
    assert(out16_3 == 0x7FFF);
    assert(out16_7 == (int16_t)0x8000);

    // Assert i32 extractions
    assert(out32_0 == -1234567);
    assert(out32_1 == 0x7FFFFFFF);
    assert(out32_3 == (int32_t)0x80000000);

    // Assert i64 extractions
    printf("DEBUG: out64_0 = 0x%llx, expected = 0x123456789abcdef0\n", (unsigned long long)out64_0);
    fflush(stdout);
    assert(out64_0 == 0x123456789ABCDEF0LL);
    assert(out64_1 == -100000000000LL);

    // Assert f32 extractions
    assert(fabsf(outF32_0 - 3.14159f) < 1e-5f);
    assert(fabsf(outF32_1 - (-2.71828f)) < 1e-5f);
    assert(fabsf(outF32_3 - (-0.001f)) < 1e-5f);

    // Assert f64 extractions
    assert(fabs(outF64_0 - 2.718281828459045) < 1e-12);
    assert(fabs(outF64_1 - (-3.141592653589793)) < 1e-12);

    // Assert vector re-use after extraction
    assert(outReuse[0] == -1234567 * 2);
    assert(outReuse[1] == (int32_t)(0x7FFFFFFFU * 2U));
    assert(outReuse[3] == (int32_t)(0x80000000 * 2));

    printf("VEXTRACT_RUNTIME_EXECUTION_SUCCESS\n");
    return 0;
}
)";
    }

    std::string compileCmd = "gcc -no-pie " + asmFilePath + " " + harnessPath + " -o " + binFilePath;
    int compileRc = std::system(compileCmd.c_str());
    assert(compileRc == 0 && "Compilation of VExtract test assembly failed");

    FILE* pipe = popen(binFilePath.c_str(), "r");
    assert(pipe != nullptr);
    char buffer[128];
    std::string resultOutput = "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        resultOutput += buffer;
    }
    int execRc = pclose(pipe);
    if (execRc != 0) {
        std::cout << "Runner Output on failure:\n" << resultOutput << std::endl;
    }

    assert(execRc == 0 && "Execution of VExtract test binary failed");
    assert(resultOutput.find("VEXTRACT_RUNTIME_EXECUTION_SUCCESS") != std::string::npos);

    std::cout << "--- SIMD VExtract Execution & Assembly Assertion Test PASSED ---" << std::endl;
}

int main() {
    test_simd_runtime_execution();
    test_simd_loop_liveness();
    test_simd_vbroadcast();
    test_simd_vextract();
    test_simd_rejection();
    return 0;
}
