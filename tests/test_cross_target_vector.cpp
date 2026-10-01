#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include "fyra/CompilerPipeline.h"
#include "target/core/TargetResolver.h"
#include "target/core/CompositeTargetInfo.h"
#include "target/architecture/x64/X64Architecture.h"
#include "target/architecture/wasm32/WasmModule.h"
#include "target/os/linux/LinuxOS.h"
#include "codegen/CodeGen.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <vector>
#include <memory>
#include <unistd.h>

using namespace ir;

// Helper to build a vectorizable loop: C[i] = A[i] + B[i]
static Function* buildAddKernel(IRBuilder& b, Module& m, const std::string& name) {
    auto ctx = m.getContextShared();
    IntegerType* i32 = ctx->getIntegerType(32);
    IntegerType* i64 = ctx->getIntegerType(64);

    Function* f = b.createFunction(name, ctx->getVoidType(), {i64, i64, i64, i32});
    auto p = f->getParameters().begin();
    Value* pC = (p++)->get();
    Value* pA = (p++)->get();
    Value* pB = (p++)->get();
    Value* pN = p->get();

    BasicBlock* entry = b.createBasicBlock("entry", f);
    BasicBlock* header = b.createBasicBlock("header", f);
    BasicBlock* body = b.createBasicBlock("body", f);
    BasicBlock* exit = b.createBasicBlock("exit", f);

    b.setInsertPoint(entry);
    b.createJmp(header);

    b.setInsertPoint(header);
    auto phiI = std::make_unique<PhiNode>(i32, 0, nullptr, header);
    PhiNode* rawI = phiI.get();
    header->getInstructions().push_back(std::move(phiI));
    rawI->addIncoming(ctx->getConstantInt(i32, 0), entry);

    Instruction* cond = b.createCslt(rawI, pN);
    b.createBr(cond, body, exit);

    b.setInsertPoint(body);
    Instruction* i64I = b.createExtSW(rawI, i64);
    Instruction* offset = b.createMul(i64I, ctx->getConstantInt(i64, 4));

    Instruction* ptrA = b.createAdd(pA, offset);
    Instruction* valA = b.createLoaduw(ptrA);

    Instruction* ptrB = b.createAdd(pB, offset);
    Instruction* valB = b.createLoaduw(ptrB);

    Instruction* valC = b.createAdd(valA, valB);

    Instruction* ptrC = b.createAdd(pC, offset);
    b.createStore(valC, ptrC);

    Instruction* nextI = b.createAdd(rawI, ctx->getConstantInt(i32, 1));
    rawI->addIncoming(nextI, body);
    b.createJmp(header);

    b.setInsertPoint(exit);
    b.createRet(nullptr);

    return f;
}

// Helper to build a vectorizable reduction: sum += A[i]
static Function* buildSumKernel(IRBuilder& b, Module& m, const std::string& name) {
    auto ctx = m.getContextShared();
    IntegerType* i32 = ctx->getIntegerType(32);
    IntegerType* i64 = ctx->getIntegerType(64);

    Function* f = b.createFunction(name, i32, {i64, i32, i32});
    auto p = f->getParameters().begin();
    Value* pA = (p++)->get();
    Value* pN = (p++)->get();
    Value* pInit = p->get();

    BasicBlock* entry = b.createBasicBlock("entry", f);
    BasicBlock* header = b.createBasicBlock("header", f);
    BasicBlock* body = b.createBasicBlock("body", f);
    BasicBlock* exit = b.createBasicBlock("exit", f);

    b.setInsertPoint(entry);
    b.createJmp(header);

    b.setInsertPoint(header);
    auto phiI = std::make_unique<PhiNode>(i32, 0, nullptr, header);
    PhiNode* rawI = phiI.get();
    header->getInstructions().push_back(std::move(phiI));
    rawI->addIncoming(ctx->getConstantInt(i32, 0), entry);

    auto phiSum = std::make_unique<PhiNode>(i32, 0, nullptr, header);
    PhiNode* rawSum = phiSum.get();
    header->getInstructions().push_back(std::move(phiSum));
    rawSum->addIncoming(pInit, entry);

    Instruction* cond = b.createCslt(rawI, pN);
    b.createBr(cond, body, exit);

    b.setInsertPoint(body);
    Instruction* i64I = b.createExtSW(rawI, i64);
    Instruction* offset = b.createMul(i64I, ctx->getConstantInt(i64, 4));
    Instruction* ptrA = b.createAdd(pA, offset);
    Instruction* valA = b.createLoaduw(ptrA);

    Instruction* nextSum = b.createAdd(rawSum, valA);
    Instruction* nextI = b.createAdd(rawI, ctx->getConstantInt(i32, 1));

    rawI->addIncoming(nextI, body);
    rawSum->addIncoming(nextSum, body);
    b.createJmp(header);

    b.setInsertPoint(exit);
    b.createRet(rawSum);

    return f;
}

// Helper to build shift kernel: C[i] = A[i] << 3
static Function* buildShiftKernel(IRBuilder& b, Module& m, const std::string& name) {
    auto ctx = m.getContextShared();
    IntegerType* i32 = ctx->getIntegerType(32);
    IntegerType* i64 = ctx->getIntegerType(64);

    Function* f = b.createFunction(name, ctx->getVoidType(), {i64, i64, i32});
    auto p = f->getParameters().begin();
    Value* pC = (p++)->get();
    Value* pA = (p++)->get();
    Value* pN = p->get();

    BasicBlock* entry = b.createBasicBlock("entry", f);
    BasicBlock* header = b.createBasicBlock("header", f);
    BasicBlock* body = b.createBasicBlock("body", f);
    BasicBlock* exit = b.createBasicBlock("exit", f);

    b.setInsertPoint(entry);
    b.createJmp(header);

    b.setInsertPoint(header);
    auto phiI = std::make_unique<PhiNode>(i32, 0, nullptr, header);
    PhiNode* rawI = phiI.get();
    header->getInstructions().push_back(std::move(phiI));
    rawI->addIncoming(ctx->getConstantInt(i32, 0), entry);

    Instruction* cond = b.createCslt(rawI, pN);
    b.createBr(cond, body, exit);

    b.setInsertPoint(body);
    Instruction* i64I = b.createExtSW(rawI, i64);
    Instruction* offset = b.createMul(i64I, ctx->getConstantInt(i64, 4));

    Instruction* ptrA = b.createAdd(pA, offset);
    Instruction* valA = b.createLoaduw(ptrA);
    Instruction* valShift = b.createShl(valA, ctx->getConstantInt(i32, 3));

    Instruction* ptrC = b.createAdd(pC, offset);
    b.createStore(valShift, ptrC);

    Instruction* nextI = b.createAdd(rawI, ctx->getConstantInt(i32, 1));
    rawI->addIncoming(nextI, body);
    b.createJmp(header);

    b.setInsertPoint(exit);
    b.createRet(nullptr);

    return f;
}

static const std::vector<int> AWKWARD_TRIP_COUNTS = {0, 1, 3, 7, 15, 16, 17, 31, 32, 33, 63, 64, 65};

int main() {
    std::cout << "=========================================================\n";
    std::cout << " Cross-Target Semantic Vector Test Suite\n";
    std::cout << "=========================================================\n";

    // 1. Cross-target compilation tests across all 4 SIMD target triples
    const std::vector<std::string> targetTriples = {
        "x64-linux",
        "aarch64-linux",
        "riscv64-linux",
        "wasm32-wasi"
    };

    for (const auto& target : targetTriples) {
        std::cout << "[Target Test] Compiling vector kernels for " << target << "..." << std::endl;
        auto ctx = std::make_shared<IRContext>();
        Module module("cross_vector_" + target, ctx);
        IRBuilder builder(ctx);
        builder.setModule(&module);

        buildAddKernel(builder, module, "vec_add");
        buildSumKernel(builder, module, "vec_sum");
        buildShiftKernel(builder, module, "vec_shift");

        fyra::CompilerPipeline pipeline;
        fyra::PipelineConfig config;
        config.targetTriple = target;
        config.optLevel = fyra::OptimizationLevel::O2;
        config.enableLoopVectorization = true;
        config.enableSLP = true;

        fyra::PipelineResult result = pipeline.run(module, config);
        if (!result.success) {
            for (const auto& err : result.errors) std::cerr << "Pipeline error (" << target << "): " << err << std::endl;
        }
        assert(result.success && "CompilerPipeline failed for target!");

        // Lower to target assembly or binary module
        auto desc = target::TargetDescriptor::fromString(target);
        assert(desc.has_value());
        auto targetInfo = target::TargetResolver::resolve(*desc);
        assert(targetInfo != nullptr);

        std::ostringstream asmOut;
        codegen::CodeGen cg(module, std::move(targetInfo), &asmOut);
        cg.emit();

        std::string text = asmOut.str();
        assert(!text.empty() || target.find("wasm") != std::string::npos);
        std::cout << "  -> " << target << " compilation succeeded." << std::endl;
    }

    // 2. Executable semantic verification on host for all awkward trip counts
    std::cout << "\n[Semantic Exec Test] Running kernels for trip counts: ";
    for (size_t i = 0; i < AWKWARD_TRIP_COUNTS.size(); ++i) {
        std::cout << AWKWARD_TRIP_COUNTS[i] << (i + 1 < AWKWARD_TRIP_COUNTS.size() ? ", " : "\n");
    }

    auto ctxExec = std::make_shared<IRContext>();
    Module moduleExec("exec_vector", ctxExec);
    IRBuilder builderExec(ctxExec);
    builderExec.setModule(&moduleExec);

    buildAddKernel(builderExec, moduleExec, "vec_add_exec");
    buildSumKernel(builderExec, moduleExec, "vec_sum_exec");
    buildShiftKernel(builderExec, moduleExec, "vec_shift_exec");

    fyra::CompilerPipeline pipelineExec;
    fyra::PipelineConfig configExec;
    configExec.targetTriple = "x64-linux";
    configExec.optLevel = fyra::OptimizationLevel::O2;
    configExec.enableLoopVectorization = true;
    configExec.enableLoopUnroll = false; // Test scalar/vector loops without unrolling mutation issues

    fyra::PipelineResult resExec = pipelineExec.run(moduleExec, configExec);
    assert(resExec.success);

    auto targetInfoExec = std::make_unique<target::CompositeTargetInfo>(
        std::make_unique<target::X64Architecture>(target::X64ABI::SystemV),
        std::make_unique<target::LinuxOS>()
    );

    for (const auto& f : moduleExec.getFunctions()) {
        std::cout << "--- Function IR: " << f->getName() << " ---\n";
        for (const auto& bb : f->getBasicBlocks()) {
            std::cout << "Block " << bb->getName() << ":\n";
            for (const auto& inst : bb->getInstructions()) {
                std::cout << "  op=" << inst->getOpcode() << " " << inst->getName() << " ops=[";
                for (const auto& u : inst->getOperands()) {
                    if (u && u->get()) std::cout << u->get()->getName() << "(" << u->get()->getType()->toString() << ") ";
                }
                std::cout << "]\n";
            }
        }
        std::cout.flush();
    }

    std::ostringstream asmExec;
    codegen::CodeGen cgExec(moduleExec, std::move(targetInfoExec), &asmExec);
    cgExec.emit(false);

    std::string base = "/tmp/fyra_cross_vec_" + std::to_string(::getpid());
    std::ofstream(base + ".s") << asmExec.str();
    std::ofstream(base + ".c") << R"(
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

extern void vec_add_exec(int* c, const int* a, const int* b, int n);
extern int vec_sum_exec(const int* a, int n, int init);
extern void vec_shift_exec(int* c, const int* a, int n);

static const int TRIPS[] = {0, 1, 3, 7, 15, 16, 17, 31, 32, 33, 63, 64, 65};

int main(void) {
    int a[128], b[128], c[128];
    for (int t = 0; t < 13; ++t) {
        int n = TRIPS[t];
        for (int i = 0; i < 128; ++i) {
            a[i] = i * 3 + 1;
            b[i] = i * 2 - 5;
            c[i] = -999;
        }

        printf("Testing t=%d n=%d Add...\n", t, n); fflush(stdout);
        vec_add_exec(c, a, b, n);
        for (int i = 0; i < 128; ++i) {
            if (i < n) assert(c[i] == a[i] + b[i]);
            else assert(c[i] == -999);
        }

        printf("Testing t=%d n=%d Sum...\n", t, n); fflush(stdout);
        int expectedSum = 100;
        for (int i = 0; i < n; ++i) expectedSum += a[i];
        int actualSum = vec_sum_exec(a, n, 100);
        printf("t=%d n=%d actual=%d expected=%d\n", t, n, actualSum, expectedSum);
        fflush(stdout);
        assert(actualSum == expectedSum);

        printf("Testing t=%d n=%d Shift...\n", t, n); fflush(stdout);
        for (int i = 0; i < 128; ++i) c[i] = -999;
        vec_shift_exec(c, a, n);
        for (int i = 0; i < 128; ++i) {
            if (i < n) assert(c[i] == (a[i] << 3));
            else assert(c[i] == -999);
        }
    }
    printf("CROSS_TARGET_VECTOR_TEST_SUCCESS\n");
    return 0;
}
)";

    std::string cmd = "cc -mavx2 -no-pie " + base + ".s " + base + ".c -o " + base + " && " + base;
    int rc = std::system(cmd.c_str());
    assert(rc == 0 && "Execution of cross-target vector test failed!");

    std::remove((base + ".s").c_str());
    std::remove((base + ".c").c_str());
    std::remove(base.c_str());

    std::cout << "[SUCCESS] All cross-target semantic vector tests passed!\n";
    return 0;
}
