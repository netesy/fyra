#include "ir/IRBuilder.h"
#include "ir/Use.h"
#include "ir/IRContext.h"
#include "ir/Module.h"
#include "transforms/InstCombine.h"
#include <cstdlib>
#include <iostream>
#include <memory>

[[noreturn]] static void fail(const char* message) {
    std::cerr << "InstCombine width-semantics regression: " << message << '\n';
    std::exit(1);
}

int main() {
    auto context = std::make_shared<ir::IRContext>();
    ir::Module module("instcombine_width_semantics", context);
    ir::IRBuilder builder(context);
    builder.setModule(&module);
    auto* i8 = context->getIntegerType(8);
    auto* i64 = context->getIntegerType(64);
    auto* function = builder.createFunction("sign_extend_wrapping_add", i64, {i8});
    auto* entry = builder.createBasicBlock("entry", function);
    builder.setInsertPoint(entry);
    auto* input = function->getParameters().front().get();
    auto* narrowAdd = builder.createAdd(input, context->getConstantInt(i8, 1));
    auto* extension = builder.createExtSW(narrowAdd, i64);
    builder.createRet(extension);

    transforms::InstCombinePass pass;
    pass.run(*function);

    auto* returnValue = entry->getInstructions().back()->getOperands().front()->get();
    auto* resultingExtension = dynamic_cast<ir::Instruction*>(returnValue);
    if (!resultingExtension || resultingExtension->getOpcode() != ir::Instruction::ExtSW)
        fail("sign extension disappeared");
    auto* extendedValue = dynamic_cast<ir::Instruction*>(resultingExtension->getOperands().front()->get());
    if (!extendedValue || extendedValue->getOpcode() != ir::Instruction::Add ||
        extendedValue->getType() != i8)
        fail("narrow wrapping add was incorrectly reassociated after sign extension");

    // At input 127, the required i8 wrap produces sign_extend(-128), not 128.
    std::cout << "narrow-add/sign-extension ordering preserved\n";
    return 0;
}
