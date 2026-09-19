#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Constant.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include "transforms/CFGBuilder.h"
#include "transforms/ScalarEvolution.h"
#include <cassert>
#include <iostream>
#include <memory>

void test_unconstrained_runtime_bound_rejection() {
    std::cout << "--- Testing Unconstrained Runtime Signed Bound Rejection ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("unconstrained_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::IntegerType* i64Ty = ctx->getIntegerType(64);

    // function sum(n: i32) -> i64
    ir::Function* func = builder.createFunction("sum_runtime", i64Ty, {i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    ir::BasicBlock* header = builder.createBasicBlock("header", func);
    ir::BasicBlock* body = builder.createBasicBlock("body", func);
    ir::BasicBlock* exit = builder.createBasicBlock("exit", func);

    ir::Value* argN = func->getParameters().front().get();

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
    rawPhiSum->addIncoming(ctx->getConstantInt(i64Ty, 0), entry);

    ir::Instruction* cond = builder.createCslt(rawPhiI, argN);
    builder.createBr(cond, body, exit);

    builder.setInsertPoint(body);
    ir::Instruction* term32 = builder.createMul(rawPhiI, ctx->getConstantInt(i32Ty, 2));
    ir::Instruction* term64 = builder.createExtSW(term32, i64Ty);
    ir::Instruction* sumNext = builder.createAdd(rawPhiSum, term64);
    ir::Instruction* iNext = builder.createAdd(rawPhiI, ctx->getConstantInt(i32Ty, 1));

    rawPhiI->addIncoming(iNext, body);
    rawPhiSum->addIncoming(sumNext, body);
    builder.createJmp(header);

    builder.setInsertPoint(exit);
    builder.createRet(rawPhiSum);

    transforms::CFGBuilder::run(*func);

    transforms::ScalarEvolution scev;
    bool fired = scev.run(*func);

    // Transformation must NOT fire for unconstrained runtime bound!
    assert(!fired && "Closed-form transformation must be rejected for unconstrained runtime bound");
    // Original loop blocks must be retained
    assert(func->getBasicBlocks().size() == 4 && "Original loop blocks must be retained");

    std::cout << "--- Unconstrained Runtime Bound Rejection Passed ---" << std::endl;
}

void test_constant_bound_transformations() {
    std::cout << "--- Testing Constant Bound Transformations ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();

    auto run_constant_test = [&](int64_t N, int64_t expected_sum) {
        ir::Module module("const_bound_mod", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        ir::IntegerType* i32Ty = ctx->getIntegerType(32);
        ir::IntegerType* i64Ty = ctx->getIntegerType(64);

        ir::Function* func = builder.createFunction("sum_const", i64Ty, {});
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
        rawPhiSum->addIncoming(ctx->getConstantInt(i64Ty, 0), entry);

        ir::Instruction* cond = builder.createCslt(rawPhiI, ctx->getConstantInt(i32Ty, N));
        builder.createBr(cond, body, exit);

        builder.setInsertPoint(body);
        ir::Instruction* term32 = builder.createMul(rawPhiI, ctx->getConstantInt(i32Ty, 2));
        ir::Instruction* term64 = builder.createExtSW(term32, i64Ty);
        ir::Instruction* sumNext = builder.createAdd(rawPhiSum, term64);
        ir::Instruction* iNext = builder.createAdd(rawPhiI, ctx->getConstantInt(i32Ty, 1));

        rawPhiI->addIncoming(iNext, body);
        rawPhiSum->addIncoming(sumNext, body);
        builder.createJmp(header);

        builder.setInsertPoint(exit);
        builder.createRet(rawPhiSum);

        transforms::CFGBuilder::run(*func);

        transforms::ScalarEvolution scev;
        bool fired = scev.run(*func);
        assert(fired && "Closed-form transformation should fire for valid constant bounds");

        ir::Instruction* retInst = exit->getInstructions().back().get();
        assert(retInst->getOpcode() == ir::Instruction::Ret);
        auto* retConst = dynamic_cast<ir::ConstantInt*>(retInst->getOperands()[0]->get());
        assert(retConst != nullptr);
        assert(retConst->getValue() == expected_sum);
    };

    run_constant_test(1, 0);                 // sum_{i=0}^0 2i = 0
    run_constant_test(2, 2);                 // sum_{i=0}^1 2i = 2
    run_constant_test(10, 90);               // sum_{i=0}^9 2i = 90
    run_constant_test(2000000, 3999998000000LL); // sum_{i=0}^1999999 2i = 1999999 * 2000000 = 3999998000000

    std::cout << "--- Constant Bound Transformations Passed ---" << std::endl;
}

void test_safe_bound_legality() {
    std::cout << "--- Testing Safe Bound Legality Thresholds ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();

    auto check_legality = [&](int64_t N) -> bool {
        ir::Module module("legality_mod", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        ir::IntegerType* i32Ty = ctx->getIntegerType(32);
        ir::IntegerType* i64Ty = ctx->getIntegerType(64);

        ir::Function* func = builder.createFunction("sum_legality", i64Ty, {});
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
        rawPhiSum->addIncoming(ctx->getConstantInt(i64Ty, 0), entry);

        ir::Instruction* cond = builder.createCslt(rawPhiI, ctx->getConstantInt(i32Ty, N));
        builder.createBr(cond, body, exit);

        builder.setInsertPoint(body);
        ir::Instruction* term32 = builder.createMul(rawPhiI, ctx->getConstantInt(i32Ty, 2));
        ir::Instruction* term64 = builder.createExtSW(term32, i64Ty);
        ir::Instruction* sumNext = builder.createAdd(rawPhiSum, term64);
        ir::Instruction* iNext = builder.createAdd(rawPhiI, ctx->getConstantInt(i32Ty, 1));

        rawPhiI->addIncoming(iNext, body);
        rawPhiSum->addIncoming(sumNext, body);
        builder.createJmp(header);

        builder.setInsertPoint(exit);
        builder.createRet(rawPhiSum);

        transforms::CFGBuilder::run(*func);

        transforms::ScalarEvolution scev;
        return scev.run(*func);
    };

    assert(check_legality(1073741823) == true && "N = 1073741823 should be safe");
    assert(check_legality(1073741824) == true && "N = 1073741824 should be safe");
    assert(check_legality(1073741825) == false && "N = 1073741825 potentially wraps and must be rejected");

    std::cout << "--- Safe Bound Legality Thresholds Passed ---" << std::endl;
}

void test_negative_and_zero_constant_bounds() {
    std::cout << "--- Testing Negative and Zero Constant Bounds ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();

    auto run_zero_neg_test = [&](int64_t N, int64_t init_sum, int64_t expected_sum) {
        ir::Module module("zero_neg_mod", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        ir::IntegerType* i32Ty = ctx->getIntegerType(32);
        ir::IntegerType* i64Ty = ctx->getIntegerType(64);

        ir::Function* func = builder.createFunction("sum_zero_neg", i64Ty, {});
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
        rawPhiSum->addIncoming(ctx->getConstantInt(i64Ty, init_sum), entry);

        ir::Instruction* cond = builder.createCslt(rawPhiI, ctx->getConstantInt(i32Ty, N));
        builder.createBr(cond, body, exit);

        builder.setInsertPoint(body);
        ir::Instruction* term32 = builder.createMul(rawPhiI, ctx->getConstantInt(i32Ty, 2));
        ir::Instruction* term64 = builder.createExtSW(term32, i64Ty);
        ir::Instruction* sumNext = builder.createAdd(rawPhiSum, term64);
        ir::Instruction* iNext = builder.createAdd(rawPhiI, ctx->getConstantInt(i32Ty, 1));

        rawPhiI->addIncoming(iNext, body);
        rawPhiSum->addIncoming(sumNext, body);
        builder.createJmp(header);

        builder.setInsertPoint(exit);
        builder.createRet(rawPhiSum);

        transforms::CFGBuilder::run(*func);

        transforms::ScalarEvolution scev;
        bool fired = scev.run(*func);
        assert(fired && "Closed-form transformation should fire for non-positive bounds by bypassing loop");

        ir::Instruction* retInst = exit->getInstructions().back().get();
        assert(retInst->getOpcode() == ir::Instruction::Ret);
        auto* retConst = dynamic_cast<ir::ConstantInt*>(retInst->getOperands()[0]->get());
        assert(retConst != nullptr);
        assert(retConst->getValue() == expected_sum);
    };

    run_zero_neg_test(-10, 37, 37);
    run_zero_neg_test(-1, 37, 37);
    run_zero_neg_test(0, 37, 37);

    std::cout << "--- Negative and Zero Constant Bounds Passed ---" << std::endl;
}

#include "transforms/FunctionInliner.h"
#include "transforms/SCCP.h"

void test_constant_term_recurrence() {
    std::cout << "--- Testing Constant-Term Recurrence Reduction ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();

    auto run_constant_term_test = [&](int64_t N, int64_t C, int64_t init_acc, uint64_t expected_sum) {
        ir::Module module("const_term_mod", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        ir::IntegerType* i32Ty = ctx->getIntegerType(32);
        ir::IntegerType* i64Ty = ctx->getIntegerType(64);

        ir::Function* func = builder.createFunction("sum_const_term", i64Ty, {});
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
        rawPhiSum->addIncoming(ctx->getConstantInt(i64Ty, init_acc), entry);

        ir::Instruction* cond = builder.createCslt(rawPhiI, ctx->getConstantInt(i32Ty, N));
        builder.createBr(cond, body, exit);

        builder.setInsertPoint(body);
        ir::Instruction* sumNext = builder.createAdd(rawPhiSum, ctx->getConstantInt(i64Ty, C));
        ir::Instruction* iNext = builder.createAdd(rawPhiI, ctx->getConstantInt(i32Ty, 1));

        rawPhiI->addIncoming(iNext, body);
        rawPhiSum->addIncoming(sumNext, body);
        builder.createJmp(header);

        builder.setInsertPoint(exit);
        builder.createRet(rawPhiSum);

        transforms::CFGBuilder::run(*func);

        transforms::ScalarEvolution scev;
        bool fired = scev.run(*func);
        assert(fired && "Closed-form transformation MUST fire for constant-term recurrence!");

        ir::Instruction* retInst = exit->getInstructions().back().get();
        assert(retInst->getOpcode() == ir::Instruction::Ret);
        auto* retConst = dynamic_cast<ir::ConstantInt*>(retInst->getOperands()[0]->get());
        assert(retConst != nullptr);
        assert(static_cast<uint64_t>(retConst->getValue()) == expected_sum);
    };

    // tail_factorial case: acc += 1307674368000 for N=5000000
    run_constant_term_test(5000000, 1307674368000LL, 0, 6538371840000000000ULL);

    // Trip counts N = 0, 1, 2, 10
    run_constant_term_test(0, 100, 37, 37);
    run_constant_term_test(1, 100, 37, 137);
    run_constant_term_test(2, 100, 37, 237);
    run_constant_term_test(10, 100, 37, 1037);

    // Negative bounds N = -1, -10
    run_constant_term_test(-1, 100, 37, 37);
    run_constant_term_test(-10, 100, 37, 37);

    // 64-bit modular wrapping test
    // 0x7FFFFFFFFFFFFFFF + 0x10000 = 0x800000000000FFFF
    run_constant_term_test(1, 0x10000, 0x7FFFFFFFFFFFFFFFLL, 0x800000000000FFFFULL);

    std::cout << "--- Constant-Term Recurrence Reduction Passed ---" << std::endl;
}

void test_inliner_scev_integration() {
    std::cout << "--- Testing FunctionInliner + SCEV Integration ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("inliner_scev_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::IntegerType* i64Ty = ctx->getIntegerType(64);

    // 1. Create callee: sum_loop(%n : i32) -> i64
    ir::Function* callee = builder.createFunction("sum_loop", i64Ty, {i32Ty});
    ir::BasicBlock* c_entry = builder.createBasicBlock("entry", callee);
    ir::BasicBlock* c_header = builder.createBasicBlock("header", callee);
    ir::BasicBlock* c_body = builder.createBasicBlock("body", callee);
    ir::BasicBlock* c_exit = builder.createBasicBlock("exit", callee);

    ir::Value* argN = callee->getParameters().front().get();

    builder.setInsertPoint(c_entry);
    builder.createJmp(c_header);

    builder.setInsertPoint(c_header);
    auto phiI = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, c_header);
    ir::PhiNode* rawPhiI = phiI.get();
    c_header->getInstructions().push_back(std::move(phiI));

    auto phiSum = std::make_unique<ir::PhiNode>(i64Ty, 0, nullptr, c_header);
    ir::PhiNode* rawPhiSum = phiSum.get();
    c_header->getInstructions().push_back(std::move(phiSum));

    rawPhiI->addIncoming(ctx->getConstantInt(i32Ty, 0), c_entry);
    rawPhiSum->addIncoming(ctx->getConstantInt(i64Ty, 0), c_entry);

    ir::Instruction* cond = builder.createCslt(rawPhiI, argN);
    builder.createBr(cond, c_body, c_exit);

    builder.setInsertPoint(c_body);
    ir::Instruction* term32 = builder.createMul(rawPhiI, ctx->getConstantInt(i32Ty, 2));
    ir::Instruction* term64 = builder.createExtSW(term32, i64Ty);
    ir::Instruction* sumNext = builder.createAdd(rawPhiSum, term64);
    ir::Instruction* iNext = builder.createAdd(rawPhiI, ctx->getConstantInt(i32Ty, 1));

    rawPhiI->addIncoming(iNext, c_body);
    rawPhiSum->addIncoming(sumNext, c_body);
    builder.createJmp(c_header);

    builder.setInsertPoint(c_exit);
    builder.createRet(rawPhiSum);

    transforms::CFGBuilder::run(*callee);

    // 2. Create caller: test_caller() -> i64 with call sum_loop(2000000)
    ir::Function* caller = builder.createFunction("test_caller", i64Ty, {});
    ir::BasicBlock* m_entry = builder.createBasicBlock("entry", caller);
    ir::BasicBlock* m_loop = builder.createBasicBlock("loop", caller);
    ir::BasicBlock* m_body = builder.createBasicBlock("body", caller);
    ir::BasicBlock* m_exit = builder.createBasicBlock("exit", caller);

    builder.setInsertPoint(m_entry);
    builder.createJmp(m_loop);

    builder.setInsertPoint(m_loop);
    auto m_k = std::make_unique<ir::PhiNode>(i32Ty, 0, nullptr, m_loop);
    ir::PhiNode* rawK = m_k.get();
    m_loop->getInstructions().push_back(std::move(m_k));

    auto m_tot = std::make_unique<ir::PhiNode>(i64Ty, 0, nullptr, m_loop);
    ir::PhiNode* rawTot = m_tot.get();
    m_loop->getInstructions().push_back(std::move(m_tot));

    rawK->addIncoming(ctx->getConstantInt(i32Ty, 1), m_entry);
    rawTot->addIncoming(ctx->getConstantInt(i64Ty, 0), m_entry);

    ir::Instruction* m_cond = builder.createCsle(rawK, ctx->getConstantInt(i32Ty, 50));
    builder.createBr(m_cond, m_body, m_exit);

    builder.setInsertPoint(m_body);
    ir::Instruction* callInst = builder.createCall(callee, {ctx->getConstantInt(i32Ty, 2000000)});
    ir::Instruction* totNext = builder.createAdd(rawTot, callInst);
    ir::Instruction* kNext = builder.createAdd(rawK, ctx->getConstantInt(i32Ty, 1));

    rawK->addIncoming(kNext, m_body);
    rawTot->addIncoming(totNext, m_body);
    builder.createJmp(m_loop);

    builder.setInsertPoint(m_exit);
    builder.createRet(rawTot);

    transforms::CFGBuilder::run(*caller);

    // Run FunctionInliner
    transforms::FunctionInliner inliner;
    bool inlined = inliner.runOnModule(module);
    assert(inlined && "sum_loop(2000000) call MUST be inlined into caller!");

    // Run ScalarEvolution on caller
    transforms::ScalarEvolution scev;
    bool scevFired = scev.run(*caller);
    assert(scevFired && "ScalarEvolution MUST eliminate the inlined constant-bound loop!");

    // Run SCCP
    transforms::SCCP sccp;
    sccp.run(*caller);

    std::cout << "--- FunctionInliner + SCEV Integration Passed ---" << std::endl;
}

int main() {
    test_unconstrained_runtime_bound_rejection();
    test_constant_bound_transformations();
    test_safe_bound_legality();
    test_negative_and_zero_constant_bounds();
    test_constant_term_recurrence();
    test_inliner_scev_integration();
    std::cout << "=== All ClosedFormReductionTest suites passed successfully! ===" << std::endl;
    return 0;
}
