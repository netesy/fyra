#include "target/core/TargetInfo.h"
#include "transforms/InstructionScheduler.h"
#include "fyra/BackendBuilder.h"
#include "ir/IRBuilder.h"
#include <cassert>
#include <iostream>

void test_target_features() {
    std::cout << "--- Testing Compile-Time Target Feature Flags ---" << std::endl;
    target::TargetFeatureFlags flags;
    flags.parseFeatures("+avx512,+dotprod,-avx2");

    assert(flags.hasFeature("avx512"));
    assert(flags.hasFeature("dotprod"));
    assert(!flags.hasFeature("avx2"));

    std::cout << "--- Target Feature Flags Test PASSED ---" << std::endl;
}

void test_instruction_scheduler() {
    std::cout << "--- Testing Basic-Block Instruction Scheduler ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module mod("test_sched", ctx);

    auto* i32Ty = ctx->getIntegerType(32);
    auto func_ptr = std::make_unique<ir::Function>(i32Ty, "sched_fn", &mod);
    auto* func = func_ptr.get();
    auto p1 = std::make_unique<ir::Parameter>(i32Ty, "a");
    auto p2 = std::make_unique<ir::Parameter>(i32Ty, "b");
    auto* p1_raw = p1.get();
    auto* p2_raw = p2.get();
    func->addParameter(std::move(p1));
    func->addParameter(std::move(p2));
    mod.addFunction(std::move(func_ptr));

    auto bb_ptr = std::make_unique<ir::BasicBlock>(func, "entry");
    auto* bb = bb_ptr.get();
    func->addBasicBlock(std::move(bb_ptr));

    ir::IRBuilder builder(ctx);
    builder.setInsertPoint(bb);

    auto* mul = builder.createMul(p1_raw, p2_raw); // High latency
    auto* add = builder.createAdd(p1_raw, p2_raw); // Low latency
    auto* sub = builder.createSub(add, mul);
    builder.createRet(sub);

    bool scheduled = transforms::InstructionScheduler::scheduleBasicBlock(*bb);
    assert(scheduled && "Instruction scheduler should process non-trivial basic block");

    std::cout << "--- Basic-Block Instruction Scheduler Test PASSED ---" << std::endl;
}

void test_backend_builder_target_features() {
    std::cout << "--- Testing BackendBuilder Target Feature Integration ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module mod("feat_mod", ctx);

    auto* i32Ty = ctx->getIntegerType(32);
    auto func_ptr = std::make_unique<ir::Function>(i32Ty, "main", &mod);
    auto* func = func_ptr.get();
    mod.addFunction(std::move(func_ptr));

    auto bb_ptr = std::make_unique<ir::BasicBlock>(func, "entry");
    auto* bb = bb_ptr.get();
    func->addBasicBlock(std::move(bb_ptr));

    ir::IRBuilder builder(ctx);
    builder.setInsertPoint(bb);
    auto* c10 = ctx->getConstantInt(i32Ty, 10);
    builder.createRet(c10);

    fyra::BackendBuilder b(mod);
    b.target("x64-linux");
    b.targetFeature("+avx512");
    auto res = b.emitAssembly("/tmp/test_feat_x64.s");
    assert(res.success && "BackendBuilder failed with target feature +avx512");

    std::cout << "--- BackendBuilder Target Feature Test PASSED ---" << std::endl;
}

int main() {
    test_target_features();
    test_instruction_scheduler();
    test_backend_builder_target_features();
    std::cout << "=== All Target Feature Flags & Scheduler Tests PASSED ===" << std::endl;
    return 0;
}
