#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/Constant.h"
#include "ir/PhiNode.h"
#include "ir/Use.h"
#include "transforms/AffineAnalysis.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <memory>

using namespace transforms;

void test_constant() {
    std::cout << "--- Testing Constant ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto* c42 = ctx->getConstantInt(i32Ty, 42);
    
    AffineAnalysis analysis;
    auto result = analysis.analyze(c42);
    
    assert(result.isValid);
    assert(result.isConstant());
    assert(result.constant == 42);
    assert(result.terms.size() == 0);
    std::cout << "--- Constant Test Passed ---" << std::endl;
}

void test_simple_value() {
    std::cout << "--- Testing Simple Value ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto* param = func->getParameters().front().get();
    
    AffineAnalysis analysis;
    auto result = analysis.analyze(param);
    
    assert(result.isValid);
    assert(result.terms.size() == 1);
    assert(result.terms[0].value == param);
    assert(result.terms[0].coefficient == 1);
    assert(result.constant == 0);
    std::cout << "--- Simple Value Test Passed ---" << std::endl;
}

void test_multiply_by_constant() {
    std::cout << "--- Testing Multiply By Constant ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto* param = func->getParameters().front().get();
    auto* c4 = ctx->getConstantInt(i32Ty, 4);
    auto* mul = builder.createMul(param, c4);
    
    AffineAnalysis analysis;
    auto result = analysis.analyze(mul);
    
    assert(result.isValid);
    assert(result.terms.size() == 1);
    assert(result.terms[0].value == param);
    assert(result.terms[0].coefficient == 4);
    assert(result.constant == 0);
    std::cout << "--- Multiply By Constant Test Passed ---" << std::endl;
}

void test_shift_left_by_constant() {
    std::cout << "--- Testing Shift Left By Constant ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto* param = func->getParameters().front().get();
    auto* c2 = ctx->getConstantInt(i32Ty, 2);
    auto* shl = builder.createShl(param, c2);
    
    AffineAnalysis analysis;
    auto result = analysis.analyze(shl);
    
    assert(result.isValid);
    assert(result.terms.size() == 1);
    assert(result.terms[0].value == param);
    assert(result.terms[0].coefficient == 4);  // 2^2 = 4
    assert(result.constant == 0);
    std::cout << "--- Shift Left By Constant Test Passed ---" << std::endl;
}

void test_multiply_and_shift_equivalent() {
    std::cout << "--- Testing Multiply and Shift Equivalent ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto* param = func->getParameters().front().get();
    auto* c4 = ctx->getConstantInt(i32Ty, 4);
    auto* mul = builder.createMul(param, c4);
    
    auto* c2 = ctx->getConstantInt(i32Ty, 2);
    auto* shl = builder.createShl(param, c2);
    
    AffineAnalysis analysis;
    auto mulResult = analysis.analyze(mul);
    auto shlResult = analysis.analyze(shl);
    
    assert(mulResult.isValid);
    assert(shlResult.isValid);
    assert(mulResult.terms.size() == shlResult.terms.size());
    assert(mulResult.terms[0].coefficient == shlResult.terms[0].coefficient);
    assert(mulResult.constant == shlResult.constant);
    std::cout << "--- Multiply and Shift Equivalent Test Passed ---" << std::endl;
}

void test_base_plus_scaled_index() {
    std::cout << "--- Testing Base Plus Scaled Index ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty, i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto it = func->getParameters().begin();
    auto* base = it->get(); ++it;
    auto* idx = it->get();
    auto* c4 = ctx->getConstantInt(i32Ty, 4);
    auto* mul = builder.createMul(idx, c4);
    auto* add = builder.createAdd(base, mul);
    
    AffineAnalysis analysis;
    auto result = analysis.analyze(add);
    
    assert(result.isValid);
    assert(result.terms.size() == 2);
    
    bool foundBase = false;
    bool foundIdx = false;
    for (const auto& term : result.terms) {
        if (term.value == base) {
            assert(term.coefficient == 1);
            foundBase = true;
        }
        if (term.value == idx) {
            assert(term.coefficient == 4);
            foundIdx = true;
        }
    }
    assert(foundBase);
    assert(foundIdx);
    assert(result.constant == 0);
    std::cout << "--- Base Plus Scaled Index Test Passed ---" << std::endl;
}

void test_base_plus_shifted_index() {
    std::cout << "--- Testing Base Plus Shifted Index ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty, i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto it = func->getParameters().begin();
    auto* base = it->get(); ++it;
    auto* idx = it->get();
    auto* c2 = ctx->getConstantInt(i32Ty, 2);
    auto* shl = builder.createShl(idx, c2);
    auto* add = builder.createAdd(base, shl);
    
    AffineAnalysis analysis;
    auto result = analysis.analyze(add);
    
    assert(result.isValid);
    assert(result.terms.size() == 2);
    
    bool foundBase = false;
    bool foundIdx = false;
    for (const auto& term : result.terms) {
        if (term.value == base) {
            assert(term.coefficient == 1);
            foundBase = true;
        }
        if (term.value == idx) {
            assert(term.coefficient == 4);  // 2^2 = 4
            foundIdx = true;
        }
    }
    assert(foundBase);
    assert(foundIdx);
    assert(result.constant == 0);
    std::cout << "--- Base Plus Shifted Index Test Passed ---" << std::endl;
}

void test_complex_address() {
    std::cout << "--- Testing Complex Address ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty, i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto it = func->getParameters().begin();
    auto* base = it->get(); ++it;
    auto* idx = it->get();
    auto* c2 = ctx->getConstantInt(i32Ty, 2);
    auto* c8 = ctx->getConstantInt(i32Ty, 8);
    auto* shl = builder.createShl(idx, c2);
    auto* add1 = builder.createAdd(base, shl);
    auto* add2 = builder.createAdd(add1, c8);
    
    AffineAnalysis analysis;
    auto result = analysis.analyze(add2);
    
    assert(result.isValid);
    assert(result.terms.size() == 2);
    
    bool foundBase = false;
    bool foundIdx = false;
    for (const auto& term : result.terms) {
        if (term.value == base) {
            assert(term.coefficient == 1);
            foundBase = true;
        }
        if (term.value == idx) {
            assert(term.coefficient == 4);  // 2^2 = 4
            foundIdx = true;
        }
    }
    assert(foundBase);
    assert(foundIdx);
    assert(result.constant == 8);
    std::cout << "--- Complex Address Test Passed ---" << std::endl;
}

void test_multiple_symbolic_terms() {
    std::cout << "--- Testing Multiple Symbolic Terms ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty, i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto it = func->getParameters().begin();
    auto* x = it->get(); ++it;
    auto* y = it->get();
    auto* c2 = ctx->getConstantInt(i32Ty, 2);
    auto* c4 = ctx->getConstantInt(i32Ty, 4);
    auto* c8 = ctx->getConstantInt(i32Ty, 8);
    auto* mul1 = builder.createMul(x, c2);
    auto* mul2 = builder.createMul(y, c4);
    auto* add1 = builder.createAdd(mul1, mul2);
    auto* add2 = builder.createAdd(add1, c8);
    
    AffineAnalysis analysis;
    auto result = analysis.analyze(add2);
    
    assert(result.isValid);
    assert(result.terms.size() == 2);
    
    bool foundX = false;
    bool foundY = false;
    for (const auto& term : result.terms) {
        if (term.value == x) {
            assert(term.coefficient == 2);
            foundX = true;
        }
        if (term.value == y) {
            assert(term.coefficient == 4);
            foundY = true;
        }
    }
    assert(foundX);
    assert(foundY);
    assert(result.constant == 8);
    std::cout << "--- Multiple Symbolic Terms Test Passed ---" << std::endl;
}

void test_invalid_shift_negative() {
    std::cout << "--- Testing Invalid Shift Negative ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto* param = func->getParameters().front().get();
    auto* cNeg = ctx->getConstantInt(i32Ty, -1);
    auto* shl = builder.createShl(param, cNeg);
    
    AffineAnalysis analysis;
    auto result = analysis.analyze(shl);
    
    assert(!result.isValid);
    std::cout << "--- Invalid Shift Negative Test Passed ---" << std::endl;
}

void test_invalid_shift_too_large() {
    std::cout << "--- Testing Invalid Shift Too Large ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto* param = func->getParameters().front().get();
    auto* cLarge = ctx->getConstantInt(i32Ty, 64);
    auto* shl = builder.createShl(param, cLarge);
    
    AffineAnalysis analysis;
    auto result = analysis.analyze(shl);
    
    assert(!result.isValid);
    std::cout << "--- Invalid Shift Too Large Test Passed ---" << std::endl;
}

void test_value_value_multiplication_not_supported() {
    std::cout << "--- Testing Value Value Multiplication Not Supported ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty, i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto it = func->getParameters().begin();
    auto* x = it->get(); ++it;
    auto* y = it->get();
    auto* mul = builder.createMul(x, y);
    
    AffineAnalysis analysis;
    auto result = analysis.analyze(mul);
    
    assert(!result.isValid);
    std::cout << "--- Value Value Multiplication Not Supported Test Passed ---" << std::endl;
}

void test_get_coefficient() {
    std::cout << "--- Testing Get Coefficient ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto* param = func->getParameters().front().get();
    auto* c4 = ctx->getConstantInt(i32Ty, 4);
    auto* mul = builder.createMul(param, c4);
    
    AffineAnalysis analysis;
    auto result = analysis.analyze(mul);
    
    assert(result.isValid);
    auto coeff = result.getCoefficient(param);
    assert(coeff.has_value());
    assert(coeff.value() == 4);
    std::cout << "--- Get Coefficient Test Passed ---" << std::endl;
}

void test_unsupported_operations() {
    std::cout << "--- Testing Unsupported Operations ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty, i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto it = func->getParameters().begin();
    auto* x = it->get(); ++it;
    auto* y = it->get();
    
    // Bitwise AND is not supported in affine analysis
    auto* andInst = builder.createAnd(x, y);
    AffineAnalysis analysis;
    auto result = analysis.analyze(andInst);
    // AND should be treated as symbolic leaf, not fail
    assert(result.isValid);
    assert(result.terms.size() == 1);
    assert(result.terms[0].value == andInst);
    
    // Bitwise OR is not supported
    auto* orInst = builder.createOr(x, y);
    auto result2 = analysis.analyze(orInst);
    assert(result2.isValid);
    assert(result2.terms.size() == 1);
    assert(result2.terms[0].value == orInst);
    
    std::cout << "--- Unsupported Operations Test Passed ---" << std::endl;
}

void test_sub() {
    std::cout << "--- Testing Sub ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty, i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto it = func->getParameters().begin();
    auto* x = it->get(); ++it;
    auto* y = it->get();
    auto* sub = builder.createSub(x, y);

    AffineAnalysis analysis;
    auto result = analysis.analyze(sub);

    assert(result.isValid);
    assert(result.terms.size() == 2);
    auto coeffX = result.getCoefficient(x);
    auto coeffY = result.getCoefficient(y);
    assert(coeffX.has_value() && coeffX.value() == 1);
    assert(coeffY.has_value() && coeffY.value() == -1);
    assert(result.constant == 0);
    std::cout << "--- Sub Test Passed ---" << std::endl;
}

void test_constant_and_term_ordering() {
    std::cout << "--- Testing Constant + Term and Term + Constant ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto* param = func->getParameters().front().get();
    auto* c10 = ctx->getConstantInt(i32Ty, 10);

    // term + constant
    auto* add1 = builder.createAdd(param, c10);
    // constant + term
    auto* add2 = builder.createAdd(c10, param);

    AffineAnalysis analysis;
    auto res1 = analysis.analyze(add1);
    auto res2 = analysis.analyze(add2);

    assert(res1.isValid && res2.isValid);
    assert(res1.constant == 10 && res2.constant == 10);
    assert(res1.getCoefficient(param).value() == 1);
    assert(res2.getCoefficient(param).value() == 1);
    std::cout << "--- Constant + Term Ordering Test Passed ---" << std::endl;
}

void test_negative_coefficients() {
    std::cout << "--- Testing Negative Coefficients ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto* param = func->getParameters().front().get();
    auto* cNeg3 = ctx->getConstantInt(i32Ty, -3);
    auto* mul = builder.createMul(param, cNeg3);

    auto* c0 = ctx->getConstantInt(i32Ty, 0);
    auto* sub = builder.createSub(c0, param);

    AffineAnalysis analysis;
    auto resMul = analysis.analyze(mul);
    auto resSub = analysis.analyze(sub);

    assert(resMul.isValid);
    assert(resMul.getCoefficient(param).value() == -3);
    assert(resMul.constant == 0);

    assert(resSub.isValid);
    assert(resSub.getCoefficient(param).value() == -1);
    assert(resSub.constant == 0);
    std::cout << "--- Negative Coefficients Test Passed ---" << std::endl;
}

void test_symbol_merging_and_cancellation() {
    std::cout << "--- Testing Symbol Merging and Cancellation ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto* x = func->getParameters().front().get();
    auto* c2 = ctx->getConstantInt(i32Ty, 2);
    auto* c3 = ctx->getConstantInt(i32Ty, 3);

    // x + x -> 2*x
    auto* add_xx = builder.createAdd(x, x);
    // 2*x + 3*x -> 5*x
    auto* mul2 = builder.createMul(x, c2);
    auto* mul3 = builder.createMul(x, c3);
    auto* add_2x3x = builder.createAdd(mul2, mul3);
    // x - x -> cancellation (0)
    auto* sub_xx = builder.createSub(x, x);

    AffineAnalysis analysis;
    auto res_xx = analysis.analyze(add_xx);
    auto res_2x3x = analysis.analyze(add_2x3x);
    auto res_cancel = analysis.analyze(sub_xx);

    assert(res_xx.isValid);
    assert(res_xx.terms.size() == 1);
    assert(res_xx.getCoefficient(x).value() == 2);

    assert(res_2x3x.isValid);
    assert(res_2x3x.terms.size() == 1);
    assert(res_2x3x.getCoefficient(x).value() == 5);

    assert(res_cancel.isValid);
    assert(res_cancel.terms.empty());
    assert(res_cancel.constant == 0);
    assert(!res_cancel.getCoefficient(x).has_value());
    std::cout << "--- Symbol Merging and Cancellation Test Passed ---" << std::endl;
}

void test_nested_equivalent_forms() {
    std::cout << "--- Testing Nested Equivalent Forms ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto* x = func->getParameters().front().get();
    auto* c1 = ctx->getConstantInt(i32Ty, 1);
    auto* c4 = ctx->getConstantInt(i32Ty, 4);

    // (x << 1) + (x << 1)
    auto* shl1 = builder.createShl(x, c1);
    auto* add_shls = builder.createAdd(shl1, shl1);
    // x * 4
    auto* mul4 = builder.createMul(x, c4);

    AffineAnalysis analysis;
    auto resShl = analysis.analyze(add_shls);
    auto resMul = analysis.analyze(mul4);

    assert(resShl.isValid && resMul.isValid);
    assert(resShl.getCoefficient(x).value() == 4);
    assert(resMul.getCoefficient(x).value() == 4);
    assert(resShl.constant == resMul.constant);
    std::cout << "--- Nested Equivalent Forms Test Passed ---" << std::endl;
}

void test_overflow_rejections() {
    std::cout << "--- Testing Overflow Rejections ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i64Ty = ctx->getIntegerType(64);
    ir::Function* func = builder.createFunction("test_func", i64Ty, {i64Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto* x = func->getParameters().front().get();
    auto* cMax = ctx->getConstantInt(i64Ty, std::numeric_limits<int64_t>::max());
    auto* c1 = ctx->getConstantInt(i64Ty, 1);

    // Constant overflow: MAX + 1
    auto* addMax = builder.createAdd(cMax, c1);

    // Coefficient overflow: x * MAX + x * 2
    auto* mulMax = builder.createMul(x, cMax);
    auto* c2 = ctx->getConstantInt(i64Ty, 2);
    auto* mul2 = builder.createMul(x, c2);
    auto* addCoeffOverflow = builder.createAdd(mulMax, mul2);

    AffineAnalysis analysis;
    auto resConstOverflow = analysis.analyze(addMax);
    auto resCoeffOverflow = analysis.analyze(addCoeffOverflow);

    assert(!resConstOverflow.isValid);
    assert(!resCoeffOverflow.isValid);
    std::cout << "--- Overflow Rejections Test Passed ---" << std::endl;
}

void test_recursion_budget() {
    std::cout << "--- Testing Recursion Budget ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    ir::Value* current = func->getParameters().front().get();
    auto* c1 = ctx->getConstantInt(i32Ty, 1);

    // Create a deep chain of 40 additions
    for (int i = 0; i < 40; ++i) {
        current = builder.createAdd(current, c1);
    }

    AffineAnalysis analysis;
    // With small budget of 5 nodes, it must safely reject
    auto resLimited = analysis.analyze(current, /*nodeBudget=*/5);
    assert(!resLimited.isValid);

    // With sufficient budget, it must succeed
    analysis.clearCache();
    auto resFull = analysis.analyze(current, /*nodeBudget=*/100);
    assert(resFull.isValid);
    assert(resFull.constant == 40);
    std::cout << "--- Recursion Budget Test Passed ---" << std::endl;
}

void test_cyclic_input_graph_safety() {
    std::cout << "--- Testing Cyclic Input Graph Safety ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {});
    ir::BasicBlock* bEntry = builder.createBasicBlock("entry", func);
    ir::BasicBlock* bLoop = builder.createBasicBlock("loop", func);

    builder.setInsertPoint(bEntry);
    builder.createJmp(bLoop);

    builder.setInsertPoint(bLoop);
    auto* phi = builder.createPhi(i32Ty, 2, nullptr);
    auto* c1 = ctx->getConstantInt(i32Ty, 1);
    auto* add = builder.createAdd(phi, c1);
    phi->addIncoming(add, bLoop);

    // An SSA cycle: phi -> add -> phi
    AffineAnalysis analysis;
    auto res = analysis.analyze(phi);
    // Must safely terminate without infinite recursion/stack overflow
    assert(res.isValid);
    std::cout << "--- Cyclic Input Graph Safety Test Passed ---" << std::endl;
}

void test_null_malformed_operand_safety() {
    std::cout << "--- Testing Null/Malformed Operand Safety ---" << std::endl;
    AffineAnalysis analysis;
    // Null value
    auto resNull = analysis.analyze(nullptr);
    assert(!resNull.isValid);

    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    // Instruction with missing operands (empty vector)
    auto malformed = std::make_unique<ir::Instruction>(i32Ty, ir::Instruction::Add, std::vector<ir::Value*>{});
    auto resMalformed = analysis.analyze(malformed.get());
    assert(!resMalformed.isValid);
    std::cout << "--- Null/Malformed Operand Safety Test Passed ---" << std::endl;
}

void test_width_changing_rejection() {
    std::cout << "--- Testing Width Changing Rejection ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i8Ty = ctx->getIntegerType(8);
    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::IntegerType* i64Ty = ctx->getIntegerType(64);
    ir::Function* func = builder.createFunction("test_func", i32Ty, {i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto* param = func->getParameters().front().get();

    // 1. sext(add i8 127, 1) -> must NOT distribute as i64 addition
    auto* c127_8 = ctx->getConstantInt(i8Ty, 127);
    auto* c1_8 = ctx->getConstantInt(i8Ty, 1);
    auto* add8 = builder.createAdd(c127_8, c1_8);
    auto* extAdd8 = builder.createExtSB(add8, i64Ty);

    // 2. extsw(mul i32 param, 4) -> must NOT distribute as i64 multiplication
    auto* c4_32 = ctx->getConstantInt(i32Ty, 4);
    auto* mul32 = builder.createMul(param, c4_32);
    auto* extMul32 = builder.createExtSW(mul32, i64Ty);

    AffineAnalysis analysis;
    auto resExtAdd = analysis.analyze(extAdd8);
    auto resExtMul = analysis.analyze(extMul32);

    assert(!resExtAdd.isValid);
    assert(!resExtMul.isValid);
    std::cout << "--- Width Changing Rejection Test Passed ---" << std::endl;
}

void test_extension_leaf_and_boundary_values() {
    std::cout << "--- Testing Extension Leaf and Boundary Values ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i8Ty = ctx->getIntegerType(8);
    ir::IntegerType* i16Ty = ctx->getIntegerType(16);
    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::IntegerType* i64Ty = ctx->getIntegerType(64);
    ir::Function* func = builder.createFunction("test_func", i64Ty, {i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto* param = func->getParameters().front().get();
    auto* extLeaf = builder.createExtSW(param, i64Ty);
    auto* c2 = ctx->getConstantInt(i64Ty, 2);
    auto* shl = builder.createShl(extLeaf, c2);

    AffineAnalysis analysis;
    auto resExtLeaf = analysis.analyze(extLeaf);
    assert(resExtLeaf.isValid);
    assert(resExtLeaf.terms.size() == 1);
    assert(resExtLeaf.terms[0].value == param);
    assert(resExtLeaf.terms[0].rawValue == extLeaf);
    assert(resExtLeaf.terms[0].bitWidth == 32);
    assert(resExtLeaf.terms[0].coefficient == 1);

    auto resShl = analysis.analyze(shl);
    assert(resShl.isValid);
    assert(resShl.getCoefficient(param).value() == 4);
    assert(resShl.getCoefficient(extLeaf).value() == 4);

    // Boundary constant extensions for i8, i16, i32: 0, 1, -1, MIN, MAX, MIN+1, MAX-1
    auto testBoundarySB = [&](int64_t val, int64_t expected) {
        auto* c = ctx->getConstantInt(i8Ty, val);
        auto* ext = builder.createExtSB(c, i64Ty);
        auto res = analysis.analyze(ext);
        assert(res.isValid);
        assert(res.isConstant());
        assert(res.constant == expected);
    };

    // i8: MIN = -128, MAX = 127
    testBoundarySB(0, 0);
    testBoundarySB(1, 1);
    testBoundarySB(-1, -1);
    testBoundarySB(-128, -128);
    testBoundarySB(127, 127);
    testBoundarySB(-127, -127);
    testBoundarySB(126, 126);

    auto testBoundarySH = [&](int64_t val, int64_t expected) {
        auto* c = ctx->getConstantInt(i16Ty, val);
        auto* ext = builder.createExtSH(c, i64Ty);
        auto res = analysis.analyze(ext);
        assert(res.isValid);
        assert(res.isConstant());
        assert(res.constant == expected);
    };

    // i16: MIN = -32768, MAX = 32767
    testBoundarySH(0, 0);
    testBoundarySH(1, 1);
    testBoundarySH(-1, -1);
    testBoundarySH(-32768, -32768);
    testBoundarySH(32767, 32767);

    auto testBoundarySW = [&](int64_t val, int64_t expected) {
        auto* c = ctx->getConstantInt(i32Ty, val);
        auto* ext = builder.createExtSW(c, i64Ty);
        auto res = analysis.analyze(ext);
        assert(res.isValid);
        assert(res.isConstant());
        assert(res.constant == expected);
    };

    // i32: MIN = -2147483648, MAX = 2147483647
    testBoundarySW(0, 0);
    testBoundarySW(1, 1);
    testBoundarySW(-1, -1);
    testBoundarySW(-2147483648LL, -2147483648LL);
    testBoundarySW(2147483647LL, 2147483647LL);

    // Truncation: 0x100000005 truncated to i32 is 5
    auto* cHuge = ctx->getConstantInt(i64Ty, 0x100000005LL);
    auto* trunc = builder.createTruncD(cHuge, i32Ty);
    auto resTrunc = analysis.analyze(trunc);
    assert(resTrunc.isValid);
    assert(resTrunc.constant == 5);

    std::cout << "--- Extension Leaf and Boundary Values Test Passed ---" << std::endl;
}

void test_one_authoritative_result() {
    std::cout << "--- Testing One Authoritative Result across Consumers ---" << std::endl;
    auto ctx = std::make_shared<ir::IRContext>();
    ir::Module module("test_mod", ctx);
    ir::IRBuilder builder(ctx);
    builder.setModule(&module);

    ir::IntegerType* i32Ty = ctx->getIntegerType(32);
    ir::IntegerType* i64Ty = ctx->getIntegerType(64);
    ir::Function* func = builder.createFunction("test_func", i64Ty, {i64Ty, i32Ty});
    ir::BasicBlock* entry = builder.createBasicBlock("entry", func);
    builder.setInsertPoint(entry);

    auto it = func->getParameters().begin();
    auto* base = it->get(); ++it;
    auto* i = it->get();

    // IR: base + (extsw(i) << 2) + 8
    auto* extI = builder.createExtSW(i, i64Ty);
    auto* c2 = ctx->getConstantInt(i64Ty, 2);
    auto* shl = builder.createShl(extI, c2);
    auto* add1 = builder.createAdd(base, shl);
    auto* c8 = ctx->getConstantInt(i64Ty, 8);
    auto* addr = builder.createAdd(add1, c8);

    AffineAnalysis analysis;
    auto res = analysis.analyze(addr);

    assert(res.isValid);
    assert(res.constant == 8);

    // 1. Authoritative AffineAnalysis facts:
    auto baseCoeff = res.getCoefficient(base);
    auto iCoeff = res.getCoefficient(i);
    assert(baseCoeff.has_value() && baseCoeff.value() == 1);
    assert(iCoeff.has_value() && iCoeff.value() == 4);

    // 2. LoopVectorizer interprets:
    //    base term is invariant base (coeff 1)
    //    induction 'i' term has stride 4 (element size 4, stride factor 1)
    //    constant displacement is 8
    int64_t lvStride = iCoeff.value();
    assert(lvStride == 4);

    // 3. ScalarEvolution interprets:
    //    recurrence step is 4 * step(i)
    int64_t scevStep = iCoeff.value();
    assert(scevStep == 4);

    // 4. LoopStrengthReduction interprets:
    //    scale of induction 'i' is 4
    //    offset is 8
    int64_t lsrScale = iCoeff.value();
    int64_t lsrOffset = res.constant;
    assert(lsrScale == 4);
    assert(lsrOffset == 8);

    std::cout << "--- One Authoritative Result Test Passed ---" << std::endl;
}

int main() {
    std::cout << "=== Running AffineAnalysis Tests ===" << std::endl;

    test_constant();
    test_simple_value();
    test_multiply_by_constant();
    test_shift_left_by_constant();
    test_multiply_and_shift_equivalent();
    test_base_plus_scaled_index();
    test_base_plus_shifted_index();
    test_complex_address();
    test_multiple_symbolic_terms();
    test_invalid_shift_negative();
    test_invalid_shift_too_large();
    test_value_value_multiplication_not_supported();
    test_get_coefficient();
    test_unsupported_operations();
    test_sub();
    test_constant_and_term_ordering();
    test_negative_coefficients();
    test_symbol_merging_and_cancellation();
    test_nested_equivalent_forms();
    test_overflow_rejections();
    test_recursion_budget();
    test_cyclic_input_graph_safety();
    test_null_malformed_operand_safety();
    test_width_changing_rejection();
    test_extension_leaf_and_boundary_values();
    test_one_authoritative_result();

    std::cout << "=== All AffineAnalysis Tests Passed ===" << std::endl;
    return 0;
}
