#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/SIMDInstruction.h"
#include <iostream>
#include <cassert>
#include <sstream>

void test_simd_stubs() {
    ir::IRContext ctx;
    ir::Module mod("test_module");
    ir::IRBuilder builder;
    builder.setModule(&mod);

    ir::IntegerType* i32Ty = ir::IntegerType::get(32);
    ir::VectorType* v4i32Ty = ctx.getVectorType(i32Ty, 4);
    ir::Type* f32Ty = ir::FloatType::get();
    ir::VectorType* v4f32Ty = ctx.getVectorType(f32Ty, 4);

    // 1. Test SIMDBuilder dynamic width
    ir::Value* dummyPtr = ir::ConstantInt::get(i32Ty, 0);
    ir::VectorInstruction* vld = ir::SIMDBuilder::createVectorLoad(v4i32Ty, dummyPtr);
    assert(vld->getVectorWidth() == 128);
    assert(vld->isVectorMemory());

    ir::VectorInstruction* vadd = ir::SIMDBuilder::createVectorAdd(v4i32Ty, vld, vld);
    assert(vadd->getVectorWidth() == 128);
    assert(vadd->isVectorArithmetic());
    assert(vadd->getVectorType() == v4i32Ty);
    assert(vadd->getElementType() == i32Ty);
    assert(vadd->getNumElements() == 4);

    ir::VectorInstruction* vfadd = ir::SIMDBuilder::createVectorAdd(v4f32Ty, vld, vld);
    assert(vfadd->getOpcode() == ir::Instruction::VFAdd);

    // 2. Test Shuffle & Mask
    ir::ShuffleMask mask({3, 2, 1, 0}, 4);
    ir::VectorInstruction* vshuf = ir::SIMDBuilder::createShuffle(v4i32Ty, vld, vld, mask);
    assert(vshuf->isVectorShuffle());
    assert(vshuf->getShuffleMask() != nullptr);
    assert(vshuf->getShuffleMask()->toString() == "[3, 2, 1, 0]");

    // 3. Test Printing
    vadd->setName("res");
    std::stringstream ss;
    vadd->print(ss);
    std::string printStr = ss.str();
    assert(printStr.find("%res = vadd.128") != std::string::npos);

    // 4. Test SIMDPatternMatcher
    assert(ir::SIMDPatternMatcher::canVectorize(vadd));
    assert(ir::SIMDPatternMatcher::canFuseMultiplyAdd(vld, vadd) == false);

    std::cout << "All SIMD stub tests passed successfully!" << std::endl;
}

int main() {
    test_simd_stubs();
    return 0;
}
