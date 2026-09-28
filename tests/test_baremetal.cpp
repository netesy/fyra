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
    std::cout << "=== Running BareMetal OS Target & Complete Capability Lowering Test Suite ===" << std::endl;

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

        auto descX64 = target::TargetDescriptor::fromString("x64-baremetal-bin");
        assert(descX64.has_value());
        assert(descX64->arch == target::Arch::X64);
        assert(descX64->os == target::OS::BareMetal);

        auto targetRv = target::TargetResolver::resolve(*descRv);
        assert(targetRv != nullptr);
        assert(targetRv->getName().find("baremetal") != std::string::npos);

        auto targetArm = target::TargetResolver::resolve(*descArm);
        assert(targetArm != nullptr);
        assert(targetArm->getName().find("baremetal") != std::string::npos);

        auto targetX64 = target::TargetResolver::resolve(*descX64);
        assert(targetX64 != nullptr);
        assert(targetX64->getName().find("baremetal") != std::string::npos);
        std::cout << "BareMetal target resolution verified successfully." << std::endl;
    }

    // Test 2: RISC-V 64 BareMetal Extended Capabilities (UART, Timer, Heap, Atomics, Debug)
    {
        ir::Module module("test_rv_baremetal", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        auto* i32 = ctx->getIntegerType(32);
        auto* i64 = ctx->getIntegerType(64);

        ir::Function* fnMain = builder.createFunction("main", i32);
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fnMain);
        builder.setInsertPoint(entry);

        builder.createExternCall("io.write", {ctx->getConstantInt(i32, 1), ctx->getConstantInt(i32, 65), ctx->getConstantInt(i32, 1)}, i32);
        builder.createExternCall("io.read", {ctx->getConstantInt(i32, 0), ctx->getConstantInt(i32, 0), ctx->getConstantInt(i32, 1)}, i32);
        builder.createExternCall("time.now", {}, i64);
        builder.createExternCall("memory.alloc", {ctx->getConstantInt(i64, 128)}, i64);
        builder.createExternCall("sync.mutex.lock", {ctx->getConstantInt(i64, 0x1000)}, nullptr);
        builder.createExternCall("sync.atomic.add", {ctx->getConstantInt(i64, 0x1000), ctx->getConstantInt(i64, 1)}, i64);
        builder.createExternCall("random.u64", {}, i64);
        builder.createExternCall("debug.break", {}, nullptr);
        builder.createExternCall("system.info", {ctx->getConstantInt(i32, 0)}, i64);
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
        assert(asmOutput.find("sb t1, 0(t0)") != std::string::npos);
        assert(asmOutput.find("lb a0, 0(t0)") != std::string::npos);
        assert(asmOutput.find("rdtime a0") != std::string::npos);
        assert(asmOutput.find("amoswap.w.aq") != std::string::npos);
        assert(asmOutput.find("amoadd.w") != std::string::npos);
        assert(asmOutput.find("rdcycle a0") != std::string::npos);
        assert(asmOutput.find("ebreak") != std::string::npos);
        assert(asmOutput.find("csrr a0, mhartid") != std::string::npos);
        std::cout << "RISC-V 64 BareMetal extended capabilities test passed." << std::endl;
    }

    // Test 3: AArch64 BareMetal Extended Capabilities (UART, Timer, Spinlock, Breakpoint)
    {
        ir::Module module("test_arm_baremetal", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        auto* i32 = ctx->getIntegerType(32);
        auto* i64 = ctx->getIntegerType(64);

        ir::Function* fnMain = builder.createFunction("main", i32);
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fnMain);
        builder.setInsertPoint(entry);

        builder.createExternCall("io.write", {ctx->getConstantInt(i32, 1), ctx->getConstantInt(i32, 66), ctx->getConstantInt(i32, 1)}, i32);
        builder.createExternCall("io.read", {ctx->getConstantInt(i32, 0), ctx->getConstantInt(i32, 0), ctx->getConstantInt(i32, 1)}, i32);
        builder.createExternCall("time.now", {}, i64);
        builder.createExternCall("memory.alloc", {ctx->getConstantInt(i64, 256)}, i64);
        builder.createExternCall("sync.mutex.lock", {ctx->getConstantInt(i64, 0x1000)}, nullptr);
        builder.createExternCall("debug.break", {}, nullptr);
        builder.createExternCall("system.info", {ctx->getConstantInt(i32, 0)}, i64);
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
        assert(asmOutput.find("strb w10, [x9]") != std::string::npos);
        assert(asmOutput.find("ldaxxr") != std::string::npos);
        assert(asmOutput.find("cntvct_el0") != std::string::npos);
        assert(asmOutput.find("brk #0") != std::string::npos);
        assert(asmOutput.find("mpidr_el1") != std::string::npos);
        std::cout << "AArch64 BareMetal extended capabilities test passed." << std::endl;
    }

    // Test 4: x64 BareMetal Extended Capabilities (COM1 UART, RDTSC, Lock Atomics, INT3)
    {
        ir::Module module("test_x64_baremetal", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        auto* i32 = ctx->getIntegerType(32);
        auto* i64 = ctx->getIntegerType(64);

        ir::Function* fnMain = builder.createFunction("main", i32);
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fnMain);
        builder.setInsertPoint(entry);

        builder.createExternCall("io.write", {ctx->getConstantInt(i32, 1), ctx->getConstantInt(i32, 67), ctx->getConstantInt(i32, 1)}, i32);
        builder.createExternCall("time.now", {}, i64);
        builder.createExternCall("sync.mutex.lock", {ctx->getConstantInt(i64, 0x1000)}, nullptr);
        builder.createExternCall("debug.break", {}, nullptr);
        builder.createExternCall("system.info", {ctx->getConstantInt(i32, 0)}, i64);
        builder.createRet(ctx->getConstantInt(i32, 0));

        auto descX64 = target::TargetDescriptor::fromString("x64-baremetal-bin");
        auto targetX64 = target::TargetResolver::resolve(*descX64);

        std::stringstream ss;
        codegen::CodeGen codeGen(module, std::move(targetX64), &ss);
        codeGen.emit(true);

        std::string asmOutput = ss.str();
        std::cout << "x64 BareMetal Assembly Output:\n" << asmOutput << std::endl;

        assert(asmOutput.find("__stack_top") != std::string::npos);
        assert(asmOutput.find("hlt") != std::string::npos);
        assert(asmOutput.find("0x3F8") != std::string::npos); // COM1 UART port
        assert(asmOutput.find("outb %al, %dx") != std::string::npos);
        assert(asmOutput.find("rdtsc") != std::string::npos);
        assert(asmOutput.find("xchg %ecx, (%rax)") != std::string::npos);
        assert(asmOutput.find("int3") != std::string::npos);
        assert(asmOutput.find("cpuid") != std::string::npos);
        std::cout << "x64 BareMetal extended capabilities test passed." << std::endl;
    }

    std::cout << "=== All BareMetal OS target tests passed successfully! ===" << std::endl;
    return 0;
}
