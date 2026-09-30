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
    std::cout << "=== Running FreeBSD OS Target & Capability Lowering Test Suite ===" << std::endl;

    auto ctx = std::make_shared<ir::IRContext>();

    // Test 1: Target Descriptor & Resolver Resolution
    {
        auto descX64 = target::TargetDescriptor::fromString("x64-freebsd-bin");
        assert(descX64.has_value());
        assert(descX64->arch == target::Arch::X64);
        assert(descX64->os == target::OS::FreeBSD);

        auto descArm = target::TargetDescriptor::fromString("aarch64-freebsd-bin");
        assert(descArm.has_value());
        assert(descArm->arch == target::Arch::AArch64);
        assert(descArm->os == target::OS::FreeBSD);

        auto descRv = target::TargetDescriptor::fromString("riscv64-freebsd-bin");
        assert(descRv.has_value());
        assert(descRv->arch == target::Arch::RISCV64);
        assert(descRv->os == target::OS::FreeBSD);

        auto targetX64 = target::TargetResolver::resolve(*descX64);
        assert(targetX64 != nullptr);
        assert(targetX64->getName().find("freebsd") != std::string::npos);

        auto targetArm = target::TargetResolver::resolve(*descArm);
        assert(targetArm != nullptr);
        assert(targetArm->getName().find("freebsd") != std::string::npos);

        auto targetRv = target::TargetResolver::resolve(*descRv);
        assert(targetRv != nullptr);
        assert(targetRv->getName().find("freebsd") != std::string::npos);
        std::cout << "FreeBSD target resolution verified successfully." << std::endl;
    }

    // Test 2: FreeBSD x64 Capability Lowering & BSD Syscall Emission
    {
        ir::Module module("test_freebsd_x64", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        auto* i32 = ctx->getIntegerType(32);
        auto* i64 = ctx->getIntegerType(64);

        ir::Function* fnMain = builder.createFunction("main", i32);
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fnMain);
        builder.setInsertPoint(entry);

        builder.createExternCall("io.write", {ctx->getConstantInt(i32, 1), ctx->getConstantInt(i32, 65), ctx->getConstantInt(i32, 1)}, i32);
        builder.createExternCall("process.getpid", {}, i64);
        builder.createExternCall("time.now", {}, i64);
        builder.createRet(ctx->getConstantInt(i32, 0));

        auto descX64 = target::TargetDescriptor::fromString("x64-freebsd-bin");
        auto targetX64 = target::TargetResolver::resolve(*descX64);

        std::stringstream ss;
        codegen::CodeGen codeGen(module, std::move(targetX64), &ss);
        codeGen.emit(true);

        std::string asmOutput = ss.str();
        std::cout << "FreeBSD x64 Assembly Output:\n" << asmOutput << std::endl;

        assert(asmOutput.find("movq $4, %rax") != std::string::npos || asmOutput.find("$4") != std::string::npos); // sys_write = 4
        assert(asmOutput.find("movq $20, %rax") != std::string::npos || asmOutput.find("$20") != std::string::npos); // sys_getpid = 20
        assert(asmOutput.find("movq $232, %rax") != std::string::npos || asmOutput.find("$232") != std::string::npos); // sys_clock_gettime = 232
        assert(asmOutput.find("syscall") != std::string::npos);
        std::cout << "FreeBSD x64 capability lowering test passed." << std::endl;
    }

    // Test 3: FreeBSD AArch64 Capability Lowering & BSD Syscall Emission
    {
        ir::Module module("test_freebsd_arm64", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        auto* i32 = ctx->getIntegerType(32);
        auto* i64 = ctx->getIntegerType(64);

        ir::Function* fnMain = builder.createFunction("main", i32);
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fnMain);
        builder.setInsertPoint(entry);

        builder.createExternCall("io.write", {ctx->getConstantInt(i32, 1), ctx->getConstantInt(i32, 66), ctx->getConstantInt(i32, 1)}, i32);
        builder.createExternCall("process.getpid", {}, i64);
        builder.createRet(ctx->getConstantInt(i32, 0));

        auto descArm = target::TargetDescriptor::fromString("aarch64-freebsd-bin");
        auto targetArm = target::TargetResolver::resolve(*descArm);

        std::stringstream ss;
        codegen::CodeGen codeGen(module, std::move(targetArm), &ss);
        codeGen.emit(true);

        std::string asmOutput = ss.str();
        std::cout << "FreeBSD AArch64 Assembly Output:\n" << asmOutput << std::endl;

        assert(asmOutput.find("#4") != std::string::npos); // sys_write = 4
        assert(asmOutput.find("#20") != std::string::npos); // sys_getpid = 20
        assert(asmOutput.find("svc #0") != std::string::npos);
        std::cout << "FreeBSD AArch64 capability lowering test passed." << std::endl;
    }

    std::cout << "=== All FreeBSD OS target tests passed successfully! ===" << std::endl;
    return 0;
}
