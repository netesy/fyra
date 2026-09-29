#include "ir/IRBuilder.h"
#include "ir/IRLinker.h"
#include "fyra/BackendBuilder.h"
#include <cassert>
#include <iostream>

void test_basic_ir_linker() {
    std::cout << "--- Testing IR Linker Basic Module Merging ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module modA("modA", ctx);
    ir::Module modB("modB", ctx);

    ir::IRBuilder builderA(ctx);
    ir::IRBuilder builderB(ctx);

    // Module A: int add_helper(int x, int y) { return x + y; }
    auto* i32Ty = ctx->getIntegerType(32);
    
    auto funcA_ptr = std::make_unique<ir::Function>(i32Ty, "add_helper", &modA);
    auto* funcA = funcA_ptr.get();
    auto p1A = std::make_unique<ir::Parameter>(i32Ty, "x");
    auto p2A = std::make_unique<ir::Parameter>(i32Ty, "y");
    auto* p1A_raw = p1A.get();
    auto* p2A_raw = p2A.get();
    funcA->addParameter(std::move(p1A));
    funcA->addParameter(std::move(p2A));
    modA.addFunction(std::move(funcA_ptr));

    auto bbA_ptr = std::make_unique<ir::BasicBlock>(funcA, "entry");
    auto* bbA = bbA_ptr.get();
    funcA->addBasicBlock(std::move(bbA_ptr));

    builderA.setInsertPoint(bbA);
    auto* sum = builderA.createAdd(p1A_raw, p2A_raw);
    builderA.createRet(sum);

    // Module B: int main_func(int a) { return add_helper(a, 10); }
    // Declares add_helper as a prototype in Mod B
    auto funcA_decl_ptr = std::make_unique<ir::Function>(i32Ty, "add_helper", &modB);
    auto* funcA_declInB = funcA_decl_ptr.get();
    modB.addFunction(std::move(funcA_decl_ptr));

    auto mainFunc_ptr = std::make_unique<ir::Function>(i32Ty, "main_func", &modB);
    auto* mainFunc = mainFunc_ptr.get();
    auto p1B = std::make_unique<ir::Parameter>(i32Ty, "a");
    auto* p1B_raw = p1B.get();
    mainFunc->addParameter(std::move(p1B));
    modB.addFunction(std::move(mainFunc_ptr));

    auto bbB_ptr = std::make_unique<ir::BasicBlock>(mainFunc, "entry");
    auto* bbB = bbB_ptr.get();
    mainFunc->addBasicBlock(std::move(bbB_ptr));

    builderB.setInsertPoint(bbB);
    auto* ten = ctx->getConstantInt(i32Ty, 10);
    auto* callRes = builderB.createCall(funcA_declInB, {p1B_raw, ten});
    builderB.createRet(callRes);

    std::string err;
    bool linkSuccess = ir::IRLinker::linkModules(modB, std::make_unique<ir::Module>(std::move(modA)), err);
    assert(linkSuccess && "IRLinker failed to merge modA into modB");

    assert(modB.getFunction("add_helper") != nullptr);
    assert(!modB.getFunction("add_helper")->getBasicBlocks().empty() && "add_helper definition must replace declaration in modB");
    assert(modB.getFunction("main_func") != nullptr);

    std::cout << "--- Basic IR Linker Test PASSED ---" << std::endl;
}

void test_backend_builder_lto() {
    std::cout << "--- Testing BackendBuilder LTO Pipeline Across Targets ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();

    // Main module
    auto mainMod = std::make_unique<ir::Module>("main_mod", ctx);
    ir::IRBuilder mainBuilder(ctx);

    auto* i32Ty = ctx->getIntegerType(32);
    auto helperDecl_ptr = std::make_unique<ir::Function>(i32Ty, "external_helper", mainMod.get());
    auto* helperDecl = helperDecl_ptr.get();
    mainMod->addFunction(std::move(helperDecl_ptr));

    auto mainFunc_ptr = std::make_unique<ir::Function>(i32Ty, "main", mainMod.get());
    auto* mainFunc = mainFunc_ptr.get();
    mainMod->addFunction(std::move(mainFunc_ptr));

    auto bbMain_ptr = std::make_unique<ir::BasicBlock>(mainFunc, "entry");
    auto* bbMain = bbMain_ptr.get();
    mainFunc->addBasicBlock(std::move(bbMain_ptr));

    mainBuilder.setInsertPoint(bbMain);

    auto* five = ctx->getConstantInt(i32Ty, 5);
    auto* callRes = mainBuilder.createCall(helperDecl, {five});
    mainBuilder.createRet(callRes);

    // Secondary module with helper implementation
    auto subMod = std::make_unique<ir::Module>("sub_mod", ctx);
    ir::IRBuilder subBuilder(ctx);

    auto helperImpl_ptr = std::make_unique<ir::Function>(i32Ty, "external_helper", subMod.get());
    auto* helperImpl = helperImpl_ptr.get();
    auto p1Sub = std::make_unique<ir::Parameter>(i32Ty, "val");
    auto* p1Sub_raw = p1Sub.get();
    helperImpl->addParameter(std::move(p1Sub));
    subMod->addFunction(std::move(helperImpl_ptr));

    auto bbSub_ptr = std::make_unique<ir::BasicBlock>(helperImpl, "entry");
    auto* bbSub = bbSub_ptr.get();
    helperImpl->addBasicBlock(std::move(bbSub_ptr));

    subBuilder.setInsertPoint(bbSub);
    auto* two = ctx->getConstantInt(i32Ty, 2);
    auto* mul = subBuilder.createMul(p1Sub_raw, two);
    subBuilder.createRet(mul);

    // Build LTO across targets
    fyra::BackendBuilder builder(*mainMod);
    builder.addModule(std::move(subMod));
    builder.optimize(fyra::OptimizationLevel::O2);

    std::vector<std::string> targets = {"x64-linux", "aarch64-linux", "riscv64-linux", "wasm32-wasi"};
    for (const auto& tgt : targets) {
        builder.target(tgt);
        auto res = builder.emitAssembly("/tmp/test_lto_" + tgt + ".s");
        if (!res.success) {
            std::cerr << "LTO Error for target " << tgt << ":" << std::endl;
            for (const auto& e : res.errors) {
                std::cerr << "  - " << e << std::endl;
            }
        }
        assert(res.success && "LTO emitAssembly failed for target");
    }

    std::cout << "--- BackendBuilder LTO Pipeline Test PASSED ---" << std::endl;
}

int main() {
    test_basic_ir_linker();
    test_backend_builder_lto();
    std::cout << "=== All IR Linker & LTO Tests PASSED ===" << std::endl;
    return 0;
}
