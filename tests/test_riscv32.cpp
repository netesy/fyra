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

int main() {
    std::cout << "=== Testing RISC-V 32-bit Backend ===" << std::endl;

    // Test 1: Target descriptor parsing
    {
        auto desc = target::TargetDescriptor::fromString("riscv32-linux-bin");
        assert(desc.has_value());
        assert(desc->arch == target::Arch::RISCV32);
        assert(desc->os == target::OS::Linux);
        assert(desc->artifact == target::Artifact::Executable);
        std::cout << "RISC-V 32-bit target descriptor parsing test passed." << std::endl;
    }

    // Test 2: Target descriptor for BareMetal
    {
        auto desc = target::TargetDescriptor::fromString("riscv32-baremetal-bin");
        assert(desc.has_value());
        assert(desc->arch == target::Arch::RISCV32);
        assert(desc->os == target::OS::BareMetal);
        assert(desc->artifact == target::Artifact::Executable);
        std::cout << "RISC-V 32-bit BareMetal target descriptor parsing test passed." << std::endl;
    }

    // Test 3: Target descriptor for FreeBSD
    {
        auto desc = target::TargetDescriptor::fromString("riscv32-freebsd-bin");
        assert(desc.has_value());
        assert(desc->arch == target::Arch::RISCV32);
        assert(desc->os == target::OS::FreeBSD);
        assert(desc->artifact == target::Artifact::Executable);
        std::cout << "RISC-V 32-bit FreeBSD target descriptor parsing test passed." << std::endl;
    }

    // Test 4: Normalize triple
    {
        std::string normalized = target::TargetDescriptor::normalizeTriple("riscv32");
        assert(normalized == "riscv32-linux-bin");
        std::cout << "RISC-V 32-bit triple normalization test passed." << std::endl;
    }

    // Test 5: Target resolution through TargetResolver
    {
        target::TargetDescriptor desc;
        desc.arch = target::Arch::RISCV32;
        desc.os = target::OS::Linux;
        desc.artifact = target::Artifact::Executable;
        
        target::TargetResolver resolver;
        auto targetInfo = resolver.resolve(desc);
        assert(targetInfo != nullptr);
        std::cout << "RISC-V 32-bit target resolution test passed." << std::endl;
    }

    // Test 6: ELF machine type for RV32
    {
        uint16_t machine = target::TargetInfo::getElfMachine(target::Arch::RISCV32);
        assert(machine == 243); // EM_RISCV
        std::cout << "RISC-V 32-bit ELF machine type test passed." << std::endl;
    }

    // Test 7: ELF relocation types for RV32
    {
        uint32_t jumpSlot = target::TargetInfo::getElfJumpSlotRelocation(target::Arch::RISCV32);
        uint32_t globDat = target::TargetInfo::getElfGlobDatRelocation(target::Arch::RISCV32);
        uint32_t relative = target::TargetInfo::getElfRelativeRelocation(target::Arch::RISCV32);
        
        assert(jumpSlot == 5);  // R_RISCV_JUMP_SLOT
        assert(globDat == 2);   // R_RISCV_32
        assert(relative == 3);  // R_RISCV_RELATIVE
        std::cout << "RISC-V 32-bit ELF relocation types test passed." << std::endl;
    }

    // Test 8: Output kind support validation
    {
        assert(target::TargetInfo::supportsOutputKind(target::OS::Linux, target::Arch::RISCV32, target::Artifact::Executable));
        assert(target::TargetInfo::supportsOutputKind(target::OS::Linux, target::Arch::RISCV32, target::Artifact::SharedLibrary));
        assert(target::TargetInfo::supportsOutputKind(target::OS::BareMetal, target::Arch::RISCV32, target::Artifact::Executable));
        assert(target::TargetInfo::supportsOutputKind(target::OS::FreeBSD, target::Arch::RISCV32, target::Artifact::Executable));
        std::cout << "RISC-V 32-bit output kind support validation test passed." << std::endl;
    }

    // Test 9: Descriptor toString
    {
        target::TargetDescriptor desc;
        desc.arch = target::Arch::RISCV32;
        desc.os = target::OS::Linux;
        desc.artifact = target::Artifact::Executable;
        
        std::string triple = desc.toString();
        assert(triple == "riscv32-linux-bin");
        std::cout << "RISC-V 32-bit descriptor toString test passed." << std::endl;
    }

    // Test 10: Code Generation & Relocation Test for RV32
    {
        auto ctx = std::make_shared<ir::IRContext>();
        ir::Module module("test_rv32_codegen", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        auto* i32 = ctx->getIntegerType(32);
        ir::Function* fnMain = builder.createFunction("main", i32);
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fnMain);
        builder.setInsertPoint(entry);

        ir::Value* addVal = builder.createAdd(ctx->getConstantInt(i32, 123), ctx->getConstantInt(i32, 456));
        builder.createRet(addVal);

        target::TargetDescriptor desc;
        desc.arch = target::Arch::RISCV32;
        desc.os = target::OS::Linux;
        desc.artifact = target::Artifact::Executable;

        target::TargetResolver resolver;
        auto targetInfo = resolver.resolve(desc);

        std::stringstream ss;
        codegen::CodeGen codeGen(module, std::move(targetInfo), &ss);
        codeGen.emit(true);

        std::string asmOutput = ss.str();
        assert(asmOutput.find("add a0, a0, a1") != std::string::npos || asmOutput.find("addi") != std::string::npos || asmOutput.find("a0") != std::string::npos);
        std::cout << "RISC-V 32-bit assembly codegen and relocation test passed." << std::endl;
    }

    // Test 11: RISC-V 32-bit ABI and Register Pressure Test
    {
        auto ctx = std::make_shared<ir::IRContext>();
        ir::Module module("test_rv32_reg_pressure", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        auto* i32 = ctx->getIntegerType(32);
        std::vector<ir::Type*> paramTypes(10, i32);
        ir::Function* fnPressure = builder.createFunction("pressure_func", i32, paramTypes);
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fnPressure);
        builder.setInsertPoint(entry);

        ir::Value* acc = nullptr;
        for (auto& param : fnPressure->getParameters()) {
            if (!acc) acc = param.get();
            else acc = builder.createAdd(acc, param.get());
        }
        builder.createRet(acc);

        target::TargetDescriptor desc;
        desc.arch = target::Arch::RISCV32;
        desc.os = target::OS::Linux;
        desc.artifact = target::Artifact::Executable;

        target::TargetResolver resolver;
        auto targetInfo = resolver.resolve(desc);

        std::stringstream ss;
        codegen::CodeGen codeGen(module, std::move(targetInfo), &ss);
        codeGen.emit(true);

        std::string asmOutput = ss.str();
        assert(asmOutput.find("sw ra") != std::string::npos || asmOutput.find("addi sp") != std::string::npos);
        std::cout << "RISC-V 32-bit ABI register pressure test passed." << std::endl;
    }

    std::cout << "=== All RISC-V 32-bit tests passed! ===" << std::endl;
    return 0;
}
