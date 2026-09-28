#include "target/core/TargetResolver.h"
#include "target/core/TargetDescriptor.h"
#include "codegen/CodeGen.h"
#include "ir/IRContext.h"
#include "ir/IRBuilder.h"
#include "ir/Module.h"
#include <cassert>
#include <iostream>
#include <sstream>

int main() {
    std::cout << "=== Running BareMetal OS Target & Capability Lowering Test Suite ===" << std::endl;

    auto ctx = std::make_shared<ir::IRContext>();

    // Test 1: Target Descriptor & Resolver Resolution
    {
        auto descRv = target::TargetDescriptor::fromString("riscv64-baremetal-bin");
        assert(descRv.has_value());
        assert(descRv->arch == target::Arch::RISCV64);
        assert(descRv->os == target::OS::BareMetal);

        auto descArm = target::TargetDescriptor::fromString("aarch64-baremetal-bin");
        assert(descArm.has_value());
        assert(descArm->arch == target::Arch::AArch64);
        assert(descArm->os == target::OS::BareMetal);

        auto targetRv = target::TargetResolver::resolve(*descRv);
        assert(targetRv != nullptr);
        assert(targetRv->getName().find("baremetal") != std::string::npos);

        auto targetArm = target::TargetResolver::resolve(*descArm);
        assert(targetArm != nullptr);
        assert(targetArm->getName().find("baremetal") != std::string::npos);
        std::cout << "BareMetal target resolution verified successfully." << std::endl;
    }

    // Test 2: RISC-V 64 BareMetal Start Routine & MMIO Lowering
    {
        ir::Module module("test_rv_baremetal", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        auto* i32 = ctx->getIntegerType(32);
        ir::Function* fnMain = builder.createFunction("main", i32);
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fnMain);
        builder.setInsertPoint(entry);

        ir::Instruction* printOp = builder.createExternCall("io.write", {ctx->getConstantInt(i32, 1), ctx->getConstantInt(i32, 65), ctx->getConstantInt(i32, 1)}, i32);
        builder.createRet(ctx->getConstantInt(i32, 0));

        auto descRv = target::TargetDescriptor::fromString("riscv64-baremetal-bin");
        auto targetRv = target::TargetResolver::resolve(*descRv);

        std::stringstream ss;
        codegen::CodeGen codeGen(module, std::move(targetRv), &ss);
        codeGen.emit(true);

        std::string asmOutput = ss.str();
        std::cout << "RISC-V 64 BareMetal Assembly Output:\n" << asmOutput << std::endl;

        assert(asmOutput.find("__stack_top") != std::string::npos);
        assert(asmOutput.find("wfi") != std::string::npos);
        assert(asmOutput.find("0x10000000") != std::string::npos); // UART MMIO address
        assert(asmOutput.find("sb ") != std::string::npos);
        std::cout << "RISC-V 64 BareMetal lowering test passed." << std::endl;
    }

    // Test 3: AArch64 BareMetal Start Routine & MMIO Lowering
    {
        ir::Module module("test_arm_baremetal", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        auto* i32 = ctx->getIntegerType(32);
        ir::Function* fnMain = builder.createFunction("main", i32);
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fnMain);
        builder.setInsertPoint(entry);

        ir::Instruction* printOp = builder.createExternCall("io.write", {ctx->getConstantInt(i32, 1), ctx->getConstantInt(i32, 66), ctx->getConstantInt(i32, 1)}, i32);
        builder.createRet(ctx->getConstantInt(i32, 0));

        auto descArm = target::TargetDescriptor::fromString("aarch64-baremetal-bin");
        auto targetArm = target::TargetResolver::resolve(*descArm);

        std::stringstream ss;
        codegen::CodeGen codeGen(module, std::move(targetArm), &ss);
        codeGen.emit(true);

        std::string asmOutput = ss.str();
        std::cout << "AArch64 BareMetal Assembly Output:\n" << asmOutput << std::endl;

        assert(asmOutput.find("__stack_top") != std::string::npos);
        assert(asmOutput.find("wfe") != std::string::npos);
        assert(asmOutput.find("0x09000000") != std::string::npos); // PL011 UART MMIO address
        assert(asmOutput.find("strb") != std::string::npos);
        std::cout << "AArch64 BareMetal lowering test passed." << std::endl;
    }

    std::cout << "=== All BareMetal OS target tests passed successfully! ===" << std::endl;
    return 0;
}
