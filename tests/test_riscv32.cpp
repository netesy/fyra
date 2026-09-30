#include "target/core/TargetDescriptor.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/ArchitectureInfo.h"
#include <iostream>
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

    std::cout << "=== All RISC-V 32-bit tests passed! ===" << std::endl;
    return 0;
}
