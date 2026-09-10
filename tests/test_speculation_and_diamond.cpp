#include "ir/IRBuilder.h"
#include "ir/Constant.h"
#include "ir/PhiNode.h"
#include "transforms/CFGBuilder.h"
#include "transforms/DiamondMatcher.h"
#include "transforms/InstructionSafety.h"
#include <iostream>
#include <stdexcept>

using namespace ir;

namespace {

void check(bool condition) {
    if (!condition) throw std::runtime_error("speculation/diamond check failed");
}

struct BuiltDiamond {
    std::shared_ptr<IRContext> context;
    std::unique_ptr<Module> module;
    Function* function;
    BasicBlock* condition;
    BasicBlock* trueBlock;
    BasicBlock* falseBlock;
    BasicBlock* merge;
    Instruction* trueValue;
};

BuiltDiamond buildDiamond(bool trappingTrueArm = false) {
    BuiltDiamond d;
    d.context = std::make_shared<IRContext>();
    d.module = std::make_unique<Module>("diamond", d.context);
    IRBuilder builder(d.context);
    builder.setModule(d.module.get());
    auto* i32 = d.context->getIntegerType(32);
    d.function = builder.createFunction("f", i32, {i32});
    auto* parameter = d.function->getParameters().front().get();
    d.condition = builder.createBasicBlock("condition", d.function);
    d.trueBlock = builder.createBasicBlock("true", d.function);
    d.falseBlock = builder.createBasicBlock("false", d.function);
    d.merge = builder.createBasicBlock("merge", d.function);

    builder.setInsertPoint(d.condition);
    auto* zero = ConstantInt::get(i32, 0);
    auto* condition = builder.createCgt(parameter, zero);
    builder.createJnz(condition, d.trueBlock, d.falseBlock);

    builder.setInsertPoint(d.trueBlock);
    d.trueValue = trappingTrueArm
        ? builder.createDiv(parameter, zero)
        : builder.createAdd(parameter, ConstantInt::get(i32, 1));
    builder.createJmp(d.merge);

    builder.setInsertPoint(d.falseBlock);
    auto* falseValue = builder.createMul(parameter, ConstantInt::get(i32, 2));
    builder.createJmp(d.merge);

    builder.setInsertPoint(d.merge);
    auto* phi = builder.createPhi(i32, 4, nullptr);
    phi->addIncoming(d.trueValue, d.trueBlock);
    phi->addIncoming(falseValue, d.falseBlock);
    builder.createRet(phi);
    transforms::CFGBuilder::run(*d.function);
    return d;
}

void testInstructionSafety() {
    auto d = buildDiamond();
    check(transforms::isSafeToSpeculativelyExecute(*d.trueValue));

    auto trapping = buildDiamond(true);
    check(!transforms::isSafeToSpeculativelyExecute(*trapping.trueValue));

    IRBuilder builder(d.context);
    builder.setModule(d.module.get());
    builder.setInsertPoint(d.trueBlock);
    auto* i32 = d.context->getIntegerType(32);
    auto* allocation = builder.createAlloc4(i32);
    auto* load = builder.createLoad(allocation);
    auto* store = builder.createStore(ConstantInt::get(i32, 1), allocation);
    auto* shift = builder.createShl(ConstantInt::get(i32, 1), ConstantInt::get(i32, 2));
    check(!transforms::isSafeToSpeculativelyExecute(*allocation));
    check(!transforms::isSafeToSpeculativelyExecute(*load));
    check(!transforms::isSafeToSpeculativelyExecute(*store));
    check(!transforms::isSafeToSpeculativelyExecute(*shift));
}

void testValidDiamond() {
    auto d = buildDiamond();
    auto match = transforms::matchSpeculatableSingleDiamond(*d.function, d.condition);
    check(match.has_value());
    check(match->trueBlock == d.trueBlock);
    check(match->falseBlock == d.falseBlock);
    check(match->mergeBlock == d.merge);
    check(match->mergePhis.size() == 1);
}

void testRejectsTrappingArm() {
    auto d = buildDiamond(true);
    check(!transforms::matchSpeculatableSingleDiamond(*d.function, d.condition));
}

void testRejectsEscapingArmValue() {
    auto d = buildDiamond();
    IRBuilder builder(d.context);
    builder.setModule(d.module.get());
    builder.setInsertPoint(d.merge);
    builder.createAdd(d.trueValue, ConstantInt::get(d.context->getIntegerType(32), 3));
    check(!transforms::matchSpeculatableSingleDiamond(*d.function, d.condition));
}

void testRejectsExtraEdgeAndMalformedPhi() {
    auto extraEdge = buildDiamond();
    extraEdge.trueBlock->addSuccessor(extraEdge.falseBlock);
    extraEdge.falseBlock->addPredecessor(extraEdge.trueBlock);
    check(!transforms::matchSpeculatableSingleDiamond(*extraEdge.function, extraEdge.condition));

    auto malformed = buildDiamond();
    auto* phi = dynamic_cast<PhiNode*>(malformed.merge->getInstructions().front().get());
    phi->removeIncomingValue(malformed.falseBlock);
    check(!transforms::matchSpeculatableSingleDiamond(*malformed.function, malformed.condition));
}

} // namespace

int main() {
    testInstructionSafety();
    testValidDiamond();
    testRejectsTrappingArm();
    testRejectsEscapingArmValue();
    testRejectsExtraEdgeAndMalformedPhi();
    std::cout << "Speculation safety and diamond matching tests passed\n";
}
