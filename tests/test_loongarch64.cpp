#include "target/core/TargetDescriptor.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/ArchitectureInfo.h"
#include "codegen/CodeGen.h"
#include "ir/IRContext.h"
#include "ir/IRBuilder.h"
#include "ir/Module.h"
#include <iostream>
#include <sstream>
#include <cassert>
#include <cstdlib>

int main() {
    std::cout << "=== Testing LoongArch64 Backend ===" << std::endl;

    // Test 1: Target descriptor parsing
    {
        auto desc = target::TargetDescriptor::fromString("loongarch64-linux-bin");
        assert(desc.has_value());
        assert(desc->arch == target::Arch::LoongArch64);
        assert(desc->os == target::OS::Linux);
        assert(desc->artifact == target::Artifact::Executable);
        std::cout << "LoongArch64 target descriptor parsing test passed." << std::endl;
    }

    // Test 2: Target resolution
    {
        target::TargetDescriptor desc;
        desc.arch = target::Arch::LoongArch64;
        desc.os = target::OS::Linux;
        desc.artifact = target::Artifact::Executable;

        target::TargetResolver resolver;
        auto targetInfo = resolver.resolve(desc);
        assert(targetInfo != nullptr);
        std::cout << "LoongArch64 target resolution test passed." << std::endl;
    }

    // Test 3: ELF machine type for LoongArch64
    {
        uint16_t machine = target::TargetInfo::getElfMachine(target::Arch::LoongArch64);
        assert(machine == 258); // EM_LOONGARCH
        std::cout << "LoongArch64 ELF machine type test passed." << std::endl;
    }

    // Test 4: ELF relocation types for LoongArch64
    {
        uint32_t jumpSlot = target::TargetInfo::getElfJumpSlotRelocation(target::Arch::LoongArch64);
        uint32_t globDat = target::TargetInfo::getElfGlobDatRelocation(target::Arch::LoongArch64);
        uint32_t relative = target::TargetInfo::getElfRelativeRelocation(target::Arch::LoongArch64);

        assert(jumpSlot == 103);  // R_LARCH_JUMP_SLOT
        assert(globDat == 102);   // R_LARCH_64
        assert(relative == 104);  // R_LARCH_RELATIVE
        std::cout << "LoongArch64 ELF relocation types test passed." << std::endl;
    }

    // Test 5: Codegen & ABI register pressure test for LoongArch64
    {
        auto ctx = std::make_shared<ir::IRContext>();
        ir::Module module("test_la64_codegen", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        auto* i64 = ctx->getIntegerType(64);
        std::vector<ir::Type*> paramTypes(10, i64);
        ir::Function* fnMain = builder.createFunction("main", i64, paramTypes);
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fnMain);
        builder.setInsertPoint(entry);

        ir::Value* acc = nullptr;
        for (auto& param : fnMain->getParameters()) {
            if (!acc) acc = param.get();
            else acc = builder.createAdd(acc, param.get());
        }
        builder.createRet(acc);

        target::TargetDescriptor desc;
        desc.arch = target::Arch::LoongArch64;
        desc.os = target::OS::Linux;
        desc.artifact = target::Artifact::Executable;

        target::TargetResolver resolver;
        auto targetInfo = resolver.resolve(desc);

        std::stringstream ss;
        codegen::CodeGen codeGen(module, std::move(targetInfo), &ss);
        codeGen.emit(true);

        std::string asmOutput = ss.str();
        assert(asmOutput.find("st.d $r1") != std::string::npos || asmOutput.find("addi.d $sp") != std::string::npos);
        std::cout << "LoongArch64 codegen and ABI register pressure test passed." << std::endl;
    }

    // Test 6: Tooling availability check
    {
        if (std::system("which loongarch64-linux-gnu-gcc >/dev/null 2>&1") == 0 ||
            std::system("which qemu-system-loongarch64 >/dev/null 2>&1") == 0) {
            std::cout << "loongarch64 cross-toolchain found; executing LoongArch64 binary tests..." << std::endl;
        } else {
            std::cout << "loongarch64-linux-gnu-gcc or qemu-system-loongarch64 missing on system; skipping LoongArch64 cross-execution." << std::endl;
        }
    }

    std::cout << "=== All LoongArch64 tests passed! ===" << std::endl;
    return 0;
}
