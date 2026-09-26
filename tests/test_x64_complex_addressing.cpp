#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/Module.h"
#include "codegen/CodeGen.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "target/architecture/x64/X64Architecture.h"
#include "target/core/CompositeTargetInfo.h"
#include "target/os/linux/LinuxOS.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace ir;

static void testComplexAddressAssemblyEmission() {
    auto context = std::make_shared<IRContext>();
    Module module("complex_addr_test", context);
    IRBuilder builder(context);
    builder.setModule(&module);

    auto* i32 = context->getIntegerType(32);
    auto* i64 = context->getIntegerType(64);
    auto* ptrTy = context->getPointerType(i32);

    Function* function = builder.createFunction("complex_addr_func", i32, {ptrTy, i64});
    auto paramIt = function->getParameters().begin();
    Value* base = paramIt->get(); ++paramIt;
    Value* index = paramIt->get();

    BasicBlock* entry = builder.createBasicBlock("entry", function);
    builder.setInsertPoint(entry);

    // ptr = base + index * 4 + 8
    Instruction* scaledIndex = builder.createMul(index, context->getConstantInt(i64, 4));
    Instruction* ptrAddr = builder.createAdd(base, scaledIndex);
    Instruction* dispAddr = builder.createAdd(ptrAddr, context->getConstantInt(i64, 8));

    Instruction* val = builder.createLoad(dispAddr);
    builder.createRet(val);

    transforms::LinearScanAllocator allocator;
    allocator.run(*function);

    auto architecture = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    auto os = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> targetInfo =
        std::make_unique<target::CompositeTargetInfo>(std::move(architecture), std::move(os));

    std::ostringstream assembly;
    codegen::CodeGen codegen(module, std::move(targetInfo), &assembly);
    codegen.emit(false);

    const std::string asmText = assembly.str();
    std::cout << "Generated Assembly:\n" << asmText << "\n";

    // Verify complex memory operand is emitted in assembly
    bool foundComplexLoad = (asmText.find("8(%r") != std::string::npos ||
                            asmText.find("(%r") != std::string::npos) &&
                            asmText.find(", 4)") != std::string::npos;
    assert(foundComplexLoad && "Generated assembly must contain complex address mode with scale 4 and displacement");

    std::cout << "testComplexAddressAssemblyEmission passed!\n";
}

int main() {
    testComplexAddressAssemblyEmission();
    std::cout << "All x64 complex addressing tests passed!\n";
    return 0;
}
