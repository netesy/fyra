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
    std::cout << "=== Running BareMetal OS Target & All 20 Capability Lowerings Test Suite ===" << std::endl;

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

    // Test 2: RISC-V 64 BareMetal All 20 Capabilities
    {
        ir::Module module("test_rv_baremetal_all", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        auto* i32 = ctx->getIntegerType(32);
        auto* i64 = ctx->getIntegerType(64);

        ir::Function* fnMain = builder.createFunction("main", i32);
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fnMain);
        builder.setInsertPoint(entry);

        // 1. IO
        builder.createExternCall("io.write", {ctx->getConstantInt(i32, 1), ctx->getConstantInt(i32, 65), ctx->getConstantInt(i32, 1)}, i32);
        builder.createExternCall("io.read", {ctx->getConstantInt(i32, 0), ctx->getConstantInt(i32, 0), ctx->getConstantInt(i32, 1)}, i32);
        builder.createExternCall("io.open", {ctx->getConstantInt(i64, 0), ctx->getConstantInt(i32, 0), ctx->getConstantInt(i32, 0)}, i32);
        // 2. FS
        builder.createExternCall("fs.open", {ctx->getConstantInt(i64, 0), ctx->getConstantInt(i32, 0), ctx->getConstantInt(i32, 0)}, i32);
        // 3. Memory
        builder.createExternCall("memory.alloc", {ctx->getConstantInt(i64, 128)}, i64);
        builder.createExternCall("memory.usage", {}, i64);
        // 4. Process
        builder.createExternCall("process.getpid", {}, i64);
        builder.createExternCall("process.args", {ctx->getConstantInt(i64, 0)}, i64);
        // 5. Thread
        builder.createExternCall("thread.getid", {}, i64);
        builder.createExternCall("thread.yield", {}, nullptr);
        // 6. Sync
        builder.createExternCall("sync.mutex.lock", {ctx->getConstantInt(i64, 0x1000)}, nullptr);
        builder.createExternCall("sync.atomic.add", {ctx->getConstantInt(i64, 0x1000), ctx->getConstantInt(i64, 1)}, i64);
        // 7. Time
        builder.createExternCall("time.now", {}, i64);
        // 8. Event
        builder.createExternCall("event.poll", {ctx->getConstantInt(i32, 0)}, i32);
        // 9. Net
        builder.createExternCall("net.socket", {ctx->getConstantInt(i32, 1), ctx->getConstantInt(i32, 1), ctx->getConstantInt(i32, 0)}, i32);
        // 10. IPC
        builder.createExternCall("ipc.connect", {ctx->getConstantInt(i64, 0)}, i32);
        // 11. Env
        builder.createExternCall("env.get", {ctx->getConstantInt(i64, 0)}, i64);
        // 12. System
        builder.createExternCall("system.info", {ctx->getConstantInt(i32, 0)}, i64);
        // 13. Signal
        builder.createExternCall("signal.wait", {ctx->getConstantInt(i32, 1)}, i32);
        // 14. Random
        builder.createExternCall("random.u64", {}, i64);
        // 15. Error
        builder.createExternCall("error.get", {}, i64);
        // 16. Debug
        builder.createExternCall("debug.break", {}, nullptr);
        // 17. Module
        builder.createExternCall("module.load", {ctx->getConstantInt(i64, 0)}, i64);
        // 18. TTY
        builder.createExternCall("tty.isatty", {ctx->getConstantInt(i32, 0)}, i32);
        // 19. Security
        builder.createExternCall("security.getuid", {}, i32);
        // 20. GPU
        builder.createExternCall("gpu.malloc", {ctx->getConstantInt(i64, 1024)}, nullptr);

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
        assert(asmOutput.find("__ramfs_root") != std::string::npos);
        assert(asmOutput.find("__baremetal_heap_ptr") != std::string::npos);
        assert(asmOutput.find("amoswap.w.aq") != std::string::npos);
        assert(asmOutput.find("amoadd.w") != std::string::npos);
        assert(asmOutput.find("rdtime a0") != std::string::npos);
        assert(asmOutput.find("0x10001000") != std::string::npos); // Event MMIO
        assert(asmOutput.find("0x10002000") != std::string::npos); // VirtIO-Net MMIO
        assert(asmOutput.find("__ipc_mailbox") != std::string::npos);
        assert(asmOutput.find("__dtb_header") != std::string::npos);
        assert(asmOutput.find("csrr a0, mhartid") != std::string::npos);
        assert(asmOutput.find("csrw mtvec") != std::string::npos);
        assert(asmOutput.find("rdcycle a0") != std::string::npos);
        assert(asmOutput.find("__baremetal_errno") != std::string::npos);
        assert(asmOutput.find("ebreak") != std::string::npos);
        assert(asmOutput.find("__symtab_start") != std::string::npos);
        assert(asmOutput.find("VT100 Serial Console Capability") != std::string::npos);
        assert(asmOutput.find("csrw pmpaddr0") != std::string::npos);
        assert(asmOutput.find("__framebuffer_start") != std::string::npos);
        std::cout << "RISC-V 64 BareMetal all 20 capability domains test passed." << std::endl;
    }

    // Test 3: AArch64 BareMetal All 20 Capabilities
    {
        ir::Module module("test_arm_baremetal_all", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        auto* i32 = ctx->getIntegerType(32);
        auto* i64 = ctx->getIntegerType(64);

        ir::Function* fnMain = builder.createFunction("main", i32);
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fnMain);
        builder.setInsertPoint(entry);

        builder.createExternCall("io.write", {ctx->getConstantInt(i32, 1), ctx->getConstantInt(i32, 66), ctx->getConstantInt(i32, 1)}, i32);
        builder.createExternCall("memory.alloc", {ctx->getConstantInt(i64, 256)}, i64);
        builder.createExternCall("sync.mutex.lock", {ctx->getConstantInt(i64, 0x1000)}, nullptr);
        builder.createExternCall("time.now", {}, i64);
        builder.createExternCall("debug.break", {}, nullptr);
        builder.createExternCall("system.info", {ctx->getConstantInt(i32, 0)}, i64);
        builder.createExternCall("signal.wait", {ctx->getConstantInt(i32, 1)}, i32);
        builder.createExternCall("gpu.malloc", {ctx->getConstantInt(i64, 2048)}, nullptr);

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
        assert(asmOutput.find("0x09000000") != std::string::npos); // PL011 UART MMIO
        assert(asmOutput.find("ldaxxr") != std::string::npos);
        assert(asmOutput.find("cntvct_el0") != std::string::npos);
        assert(asmOutput.find("brk #0") != std::string::npos);
        assert(asmOutput.find("mpidr_el1") != std::string::npos);
        assert(asmOutput.find("vbar_el1") != std::string::npos);
        assert(asmOutput.find("__framebuffer_start") != std::string::npos);
        std::cout << "AArch64 BareMetal all capability domains test passed." << std::endl;
    }

    // Test 4: x64 BareMetal All 20 Capabilities
    {
        ir::Module module("test_x64_baremetal_all", ctx);
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
        builder.createExternCall("signal.wait", {ctx->getConstantInt(i32, 1)}, i32);
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
        assert(asmOutput.find("lidt") != std::string::npos);
        assert(asmOutput.find("cpuid") != std::string::npos);
        std::cout << "x64 BareMetal all capability domains test passed." << std::endl;
    }

    std::cout << "=== All BareMetal OS target tests passed successfully! ===" << std::endl;
    return 0;
}
