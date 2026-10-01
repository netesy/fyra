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
#include <algorithm>
#include <memory>
#include <unistd.h>

using namespace ir;

[[noreturn]] static void fail(const std::string& message) {
    std::cerr << "CROSS_TARGET_VECTOR_TEST_FAILURE: " << message << std::endl;
    std::exit(1);
}

static void require(bool condition, const std::string& message) {
    if (!condition) fail(message);
}

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

// Helper to build a vectorizable reduction: sum += A[i] * B[i]
static Function* buildDotKernel(IRBuilder& b, Module& m, const std::string& name) {
    auto ctx = m.getContextShared();
    auto* i32 = ctx->getIntegerType(32); auto* i64 = ctx->getIntegerType(64);
    Function* f = b.createFunction(name, i32, {i64, i64, i32, i32});
    auto p = f->getParameters().begin(); Value* a = (p++)->get();
    Value* bb = (p++)->get(); Value* n = (p++)->get(); Value* init = p->get();
    BasicBlock* entry = b.createBasicBlock("entry", f);
    BasicBlock* header = b.createBasicBlock("header", f);
    BasicBlock* body = b.createBasicBlock("body", f);
    BasicBlock* exit = b.createBasicBlock("exit", f);
    b.setInsertPoint(entry); b.createJmp(header); b.setInsertPoint(header);
    auto iOwner = std::make_unique<PhiNode>(i32, 0, nullptr, header);
    auto* i = iOwner.get(); header->getInstructions().push_back(std::move(iOwner));
    i->addIncoming(ctx->getConstantInt(i32, 0), entry);
    auto sumOwner = std::make_unique<PhiNode>(i32, 0, nullptr, header);
    auto* sum = sumOwner.get(); header->getInstructions().push_back(std::move(sumOwner));
    sum->addIncoming(init, entry); b.createBr(b.createCslt(i, n), body, exit);
    b.setInsertPoint(body);
    Value* off = b.createMul(b.createExtSW(i, i64), ctx->getConstantInt(i64, 4));
    Value* av = b.createLoaduw(b.createAdd(a, off));
    Value* bv = b.createLoaduw(b.createAdd(bb, off));
    auto* nextSum = b.createAdd(sum, b.createMul(av, bv));
    auto* nextI = b.createAdd(i, ctx->getConstantInt(i32, 1));
    i->addIncoming(nextI, body); sum->addIncoming(nextSum, body); b.createJmp(header);
    b.setInsertPoint(exit); b.createRet(sum); return f;
}

static const std::vector<int> AWKWARD_TRIP_COUNTS = {0, 1, 3, 7, 15, 16, 17, 31, 32, 33, 63, 64, 65};

int main() {
    std::cout << "=========================================================\n";
    std::cout << " Cross-Target Compile Evidence + Native x64 Semantic Suite\n";
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
        buildDotKernel(builder, module, "vec_dot");

        fyra::CompilerPipeline pipeline;
        fyra::PipelineConfig config;
        config.targetTriple = target;
        config.optLevel = fyra::OptimizationLevel::O2;
        config.enableLoopVectorization = true;
        config.enableSLP = true;
        config.enableLoopUnroll = false;

        fyra::PipelineResult result = pipeline.run(module, config);
        if (!result.success) {
            for (const auto& err : result.errors) std::cerr << "Pipeline error (" << target << "): " << err << std::endl;
        }
        require(result.success, "CompilerPipeline failed for " + target);

        size_t vectorIRCount = 0;
        for (const auto& function : module.getFunctions())
            for (const auto& block : function->getBasicBlocks())
                for (const auto& instruction : block->getInstructions())
                    if ((instruction->getType() && instruction->getType()->isVectorTy()) ||
                        std::any_of(instruction->getOperands().begin(), instruction->getOperands().end(),
                            [](const auto& operand) { return operand && operand->get() &&
                                operand->get()->getType() && operand->get()->getType()->isVectorTy(); }))
                        ++vectorIRCount;

        // Lower to target assembly or binary module
        auto desc = target::TargetDescriptor::fromString(target);
        require(desc.has_value(), "invalid target descriptor for " + target);
        auto targetInfo = target::TargetResolver::resolve(*desc);
        require(targetInfo != nullptr, "target resolver failed for " + target);

        std::ostringstream asmOut;
        codegen::CodeGen cg(module, std::move(targetInfo), &asmOut);
        cg.emit();

        std::string text = asmOut.str();
        bool artifactValidated = false;
        if (target.find("wasm") != std::string::npos) {
            auto binaryTarget = target::TargetResolver::resolve(*desc);
            codegen::CodeGen binaryCodeGen(module, std::move(binaryTarget));
            binaryCodeGen.emit();
            const auto& bytes = binaryCodeGen.getAssembler().getCode();
            artifactValidated = bytes.size() >= 8 && bytes[0] == 0x00 && bytes[1] == 0x61 &&
                                bytes[2] == 0x73 && bytes[3] == 0x6d;
        } else {
            artifactValidated = !text.empty();
        }
        require(artifactValidated, "target artifact was not validated for " + target);
        std::cout << "  -> " << target << ": " << vectorIRCount
                  << " vector-typed IR instructions; artifact validated (not executed)." << std::endl;
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
    buildDotKernel(builderExec, moduleExec, "vec_dot_exec");

    fyra::CompilerPipeline pipelineExec;
    fyra::PipelineConfig configExec;
    configExec.targetTriple = "x64-linux";
    configExec.optLevel = fyra::OptimizationLevel::O2;
    configExec.enableLoopVectorization = true;
    configExec.enableLoopUnroll = false; // Test scalar/vector loops without unrolling mutation issues

    fyra::PipelineResult resExec = pipelineExec.run(moduleExec, configExec);
    require(resExec.success, "native x64 optimization pipeline failed");

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
    std::string instrumentedAssembly = asmExec.str();
    auto instrumentBlock = [&](const std::string& label, const std::string& counter) {
        const std::string marker = label + ":\n";
        const size_t position = instrumentedAssembly.find(marker);
        require(position != std::string::npos, "missing generated block " + label);
        instrumentedAssembly.insert(position + marker.size(),
            "  incq " + counter + "(%rip)\n");
    };
    instrumentBlock("vec_add_exec_v_loop_body", "fyra_vector_main_hits");
    instrumentBlock("vec_add_exec_epi_body", "fyra_scalar_tail_hits");
    instrumentedAssembly +=
        ".data\n.align 8\n.globl fyra_vector_main_hits\n"
        "fyra_vector_main_hits: .quad 0\n"
        ".globl fyra_scalar_tail_hits\n"
        "fyra_scalar_tail_hits: .quad 0\n";
    std::ofstream(base + ".s") << instrumentedAssembly;
    std::ofstream(base + ".c") << R"(
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

extern void vec_add_exec(int* c, const int* a, const int* b, int n);
extern int vec_sum_exec(const int* a, int n, int init);
extern void vec_shift_exec(int* c, const int* a, int n);
extern int vec_dot_exec(const int* a, const int* b, int n, int init);
extern long fyra_vector_main_hits;
extern long fyra_scalar_tail_hits;

static const int TRIPS[] = {0, 1, 3, 7, 15, 16, 17, 31, 32, 33, 63, 64, 65};

int main(void) {
    int aStorage[130], bStorage[130], cStorage[130];
    int *a = &aStorage[1], *b = &bStorage[1], *c = &cStorage[1];
    for (int t = 0; t < 13; ++t) {
        int n = TRIPS[t];
        aStorage[0] = bStorage[0] = cStorage[0] = 0x13572468;
        aStorage[129] = bStorage[129] = cStorage[129] = 0x24681357;
        for (int i = 0; i < 128; ++i) {
            a[i] = i * 3 + 1;
            b[i] = i * 2 - 5;
            c[i] = -999;
        }

        printf("Testing t=%d n=%d Add...\n", t, n); fflush(stdout);
        fyra_vector_main_hits = fyra_scalar_tail_hits = 0;
        vec_add_exec(c, a, b, n);
        for (int i = 0; i < 128; ++i) {
            if (i < n) assert(c[i] == a[i] + b[i]);
            else assert(c[i] == -999);
        }
        assert(fyra_vector_main_hits == n / 8);
        assert(fyra_scalar_tail_hits == n % 8);
        assert(cStorage[0] == 0x13572468 && cStorage[129] == 0x24681357);
        if (n < 128) assert(c[n] == -999);

        printf("Testing t=%d n=%d Sum...\n", t, n); fflush(stdout);
        int expectedSum = 100;
        for (int i = 0; i < n; ++i) expectedSum += a[i];
        int actualSum = vec_sum_exec(a, n, 100);
        printf("t=%d n=%d actual=%d expected=%d\n", t, n, actualSum, expectedSum);
        fflush(stdout);
        assert(actualSum == expectedSum);

        int expectedDot = 11;
        for (int i = 0; i < n; ++i) expectedDot += a[i] * b[i];
        assert(vec_dot_exec(a, b, n, 11) == expectedDot);

        printf("Testing t=%d n=%d Shift...\n", t, n); fflush(stdout);
        for (int i = 0; i < 128; ++i) c[i] = -999;
        vec_shift_exec(c, a, n);
        for (int i = 0; i < 128; ++i) {
            if (i < n) assert(c[i] == (a[i] << 3));
            else assert(c[i] == -999);
        }
        assert(cStorage[0] == 0x13572468 && cStorage[129] == 0x24681357);
    }

    /* Aliasing must take the scalar fallback rather than the vector path. */
    for (int i = 0; i < 128; ++i) { a[i] = i + 1; b[i] = 3 * i - 7; }
    fyra_vector_main_hits = fyra_scalar_tail_hits = 0;
    vec_add_exec(a, a, b, 17);
    for (int i = 0; i < 17; ++i) assert(a[i] == (i + 1) + (3 * i - 7));
    assert(fyra_vector_main_hits == 0);
    printf("CROSS_TARGET_VECTOR_TEST_SUCCESS\n");
    return 0;
}
)";

    std::string cmd = "cc -mavx2 -no-pie " + base + ".s " + base + ".c -o " + base + " && " + base;
    int rc = std::system(cmd.c_str());
    require(rc == 0, "native x64 execution or scalar-reference comparison failed");

    std::remove((base + ".s").c_str());
    std::remove((base + ".c").c_str());
    std::remove(base.c_str());

    std::cout << "[SUCCESS] Four targets compiled; native x64 semantic test passed!\n";
    return 0;
}
