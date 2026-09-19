#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Constant.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include "transforms/CFGBuilder.h"
#include "transforms/ScalarEvolution.h"
#include "transforms/SCCP.h"
#include "transforms/FunctionInliner.h"
#include <cassert>
#include <iostream>
#include <memory>

static ir::Function* build_loop_sum_function(ir::IRBuilder& builder, ir::Module& module, int64_t boundVal, int64_t initAccum = 0) {
    auto ctx = module.getContextShared();
    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::IntegerType* i64Ty = ctx->getIntegerType(64);

    ir::Function* func = builder.createFunction("test_loop_sum", i64Ty, {});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    ir::BasicBlock* header = builder.createBasicBlock("header", func);
    ir::BasicBlock* body = builder.createBasicBlock("body", func);
    ir::BasicBlock* exit = builder.createBasicBlock("exit", func);

    builder.setInsertPoint(entry);
    builder.createJmp(header);

    builder.setInsertPoint(header);
    auto phiI = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, header);
    ir::PhiNode* rawPhiI = phiI.get();
    header->getInstructions().push_back(std::move(phiI));

    auto phiSum = std::make_unique<ir::PhiNode>(i64Ty, 0, nullptr, header);
    ir::PhiNode* rawPhiSum = phiSum.get();
    header->getInstructions().push_back(std::move(phiSum));

    rawPhiI->addIncoming(ctx->getConstantInt(i32Ty, 0), entry);
    rawPhiSum->addIncoming(ctx->getConstantInt(i64Ty, initAccum), entry);

    ir::Instruction* cond = builder.createCslt(rawPhiI, ctx->getConstantInt(i32Ty, boundVal));
    builder.createBr(cond, body, exit);

    builder.setInsertPoint(body);
    ir::Instruction* term = builder.createMul(rawPhiI, ctx->getConstantInt(i32Ty, 2));
    ir::Instruction* termExt = builder.createExtSW(term, i64Ty);
    ir::Instruction* sumNext = builder.createAdd(rawPhiSum, termExt);
    ir::Instruction* iNext = builder.createAdd(rawPhiI, ctx->getConstantInt(i32Ty, 1));

    rawPhiI->addIncoming(iNext, body);
    rawPhiSum->addIncoming(sumNext, body);
    builder.createJmp(header);

    builder.setInsertPoint(exit);
    builder.createRet(rawPhiSum);

    transforms::CFGBuilder::run(*func);
    return func;
}

void test_oracle_value_2000000() {
    std::cout << "--- Testing Closed-Form Reduction Oracle (N = 2,000,000) ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_oracle_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Function* func = build_loop_sum_function(builder, module, 2000000);

    transforms::ScalarEvolution scev;
    bool changed = scev.run(*func);
    assert(changed && "SCEV should optimize N=2,000,000 loop into closed form");

    ir::BasicBlock* exit = func->getBasicBlocks().back().get();
    ir::Instruction* retInst = exit->getInstructions().back().get();
    assert(retInst->getOpcode() == ir::Instruction::Ret);

    auto* retConst = dynamic_cast<ir::ConstantInt*>(retInst->getOperands()[0]->get());
    assert(retConst != nullptr);
    assert(retConst->getValue() == 3999998000000LL && "Single test_loop_sum(2000000) must equal 3,999,998,000,000");

    std::cout << "  Passed: test_loop_sum(2000000) = " << retConst->getValue() << std::endl;
}

void test_boundary_cases() {
    std::cout << "--- Testing Closed-Form Reduction Boundary Cases ---" << std::endl;
    struct TestCase {
        int64_t N;
        int64_t expected;
    } cases[] = {
        {1, 0},
        {2, 2},
        {3, 6},
        {4, 12},
        {5, 20},
        {10, 90}
    };

    for (const auto& tc : cases) {
        auto ctx = std::make_shared<ir::IRContext>();
        ir::Module module("test_boundary_mod", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        ir::Function* func = build_loop_sum_function(builder, module, tc.N);

        transforms::ScalarEvolution scev;
        bool changed = scev.run(*func);
        assert(changed);

        ir::BasicBlock* exit = func->getBasicBlocks().back().get();
        ir::Instruction* retInst = exit->getInstructions().back().get();
        auto* retConst = dynamic_cast<ir::ConstantInt*>(retInst->getOperands()[0]->get());
        assert(retConst != nullptr);
        assert(retConst->getValue() == tc.expected);
        std::cout << "  Passed: N=" << tc.N << " -> " << retConst->getValue() << std::endl;
    }
}

void test_nonzero_initial_accumulator() {
    std::cout << "--- Testing Non-Zero Initial Accumulator (INIT = 37, N = 10) ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_init_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Function* func = build_loop_sum_function(builder, module, 10, 37);

    transforms::ScalarEvolution scev;
    bool changed = scev.run(*func);
    assert(changed);

    ir::BasicBlock* exit = func->getBasicBlocks().back().get();
    ir::Instruction* retInst = exit->getInstructions().back().get();
    auto* retConst = dynamic_cast<ir::ConstantInt*>(retInst->getOperands()[0]->get());
    assert(retConst != nullptr);
    assert(retConst->getValue() == 127 && "37 + 90 should be 127");
    std::cout << "  Passed: INIT=37, N=10 -> " << retConst->getValue() << std::endl;
}

void test_boundary_limit_accepted() {
    std::cout << "--- Testing Boundary Limit Accepted (N = 1,073,741,824) ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_limit_acc_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Function* func = build_loop_sum_function(builder, module, 1073741824LL);

    transforms::ScalarEvolution scev;
    bool changed = scev.run(*func);
    assert(changed && "N=1,073,741,824 is within safe i32 non-overflow limit for K=2");
    std::cout << "  Passed: N=1,073,741,824 successfully reduced to closed-form" << std::endl;
}

void test_boundary_limit_overflow_rejected() {
    std::cout << "--- Testing Boundary Limit Overflow Rejected (N = 1,073,741,825) ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_limit_rej_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::Function* func = build_loop_sum_function(builder, module, 1073741825LL);

    transforms::ScalarEvolution scev;
    bool changed = scev.run(*func);
    assert(!changed && "N=1,073,741,825 must be rejected to prevent i32 overflow");
    std::cout << "  Passed: N=1,073,741,825 safely rejected" << std::endl;
}

int main() {
    test_oracle_value_2000000();
    test_boundary_cases();
    test_nonzero_initial_accumulator();
    test_boundary_limit_accepted();
    test_boundary_limit_overflow_rejected();
    std::cout << "=== All Closed-Form Reduction Tests Passed Successfully ===" << std::endl;
    return 0;
}
