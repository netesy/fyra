#include "target/core/TargetDescriptor.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/ArchitectureInfo.h"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "=== Testing UEFI Platform Support ===" << std::endl;

    // Test 1: UEFI OS enum exists
    {
        target::TargetDescriptor desc;
        desc.os = target::OS::UEFI;
        assert(desc.os == target::OS::UEFI);
        std::cout << "UEFI OS enum test passed." << std::endl;
    }

    // Test 2: Target descriptor parsing for x86-64 UEFI
    {
        auto desc = target::TargetDescriptor::fromString("x64-uefi-bin");
        assert(desc.has_value());
        assert(desc->arch == target::Arch::X64);
        assert(desc->os == target::OS::UEFI);
        assert(desc->artifact == target::Artifact::Executable);
        std::cout << "x86-64 UEFI target descriptor parsing test passed." << std::endl;
    }

    // Test 3: Target descriptor parsing for AArch64 UEFI
    {
        auto desc = target::TargetDescriptor::fromString("aarch64-uefi-bin");
        assert(desc.has_value());
        assert(desc->arch == target::Arch::AArch64);
        assert(desc->os == target::OS::UEFI);
        assert(desc->artifact == target::Artifact::Executable);
        std::cout << "AArch64 UEFI target descriptor parsing test passed." << std::endl;
    }

    // Test 4: Normalize triple for UEFI
    {
        std::string normalized = target::TargetDescriptor::normalizeTriple("uefi");
        assert(normalized == "x64-uefi-bin");
        std::cout << "UEFI triple normalization test passed." << std::endl;
    }

    // Test 5: Target descriptor toString for UEFI
    {
        target::TargetDescriptor desc;
        desc.arch = target::Arch::X64;
        desc.os = target::OS::UEFI;
        desc.artifact = target::Artifact::Executable;
        
        std::string triple = desc.toString();
        assert(triple == "x64-uefi-bin");
        std::cout << "UEFI descriptor toString test passed." << std::endl;
    }

    // Test 6: Target resolution for x86-64 UEFI
    {
        target::TargetDescriptor desc;
        desc.arch = target::Arch::X64;
        desc.os = target::OS::UEFI;
        desc.artifact = target::Artifact::Executable;
        
        target::TargetResolver resolver;
        auto targetInfo = resolver.resolve(desc);
        assert(targetInfo != nullptr);
        std::cout << "x86-64 UEFI target resolution test passed." << std::endl;
    }

    // Test 7: Target resolution for AArch64 UEFI
    {
        target::TargetDescriptor desc;
        desc.arch = target::Arch::AArch64;
        desc.os = target::OS::UEFI;
        desc.artifact = target::Artifact::Executable;
        
        target::TargetResolver resolver;
        auto targetInfo = resolver.resolve(desc);
        assert(targetInfo != nullptr);
        std::cout << "AArch64 UEFI target resolution test passed." << std::endl;
    }

    // Test 8: COFF machine type for UEFI targets
    {
        uint16_t x64Machine = target::TargetInfo::getCoffMachine(target::Arch::X64);
        uint16_t aarch64Machine = target::TargetInfo::getCoffMachine(target::Arch::AArch64);
        
        assert(x64Machine == 0x8664);
        assert(aarch64Machine == 0xAA64);
        std::cout << "UEFI COFF machine type test passed." << std::endl;
    }

    // Test 9: Output kind support validation for UEFI
    {
        assert(target::TargetInfo::supportsOutputKind(target::OS::UEFI, target::Arch::X64, target::Artifact::Executable));
        assert(target::TargetInfo::supportsOutputKind(target::OS::UEFI, target::Arch::AArch64, target::Artifact::Executable));
        assert(target::TargetInfo::supportsOutputKind(target::OS::UEFI, target::Arch::X64, target::Artifact::SharedLibrary));
        assert(target::TargetInfo::supportsOutputKind(target::OS::UEFI, target::Arch::AArch64, target::Artifact::SharedLibrary));
        std::cout << "UEFI output kind support validation test passed." << std::endl;
    }

    // Test 10: UEFI is distinct from Windows
    {
        auto windowsDesc = target::TargetDescriptor::fromString("x64-windows-bin");
        auto uefiDesc = target::TargetDescriptor::fromString("x64-uefi-bin");
        
        assert(windowsDesc.has_value());
        assert(uefiDesc.has_value());
        assert(windowsDesc->os == target::OS::Windows);
        assert(uefiDesc->os == target::OS::UEFI);
        assert(windowsDesc->os != uefiDesc->os);
        std::cout << "UEFI vs Windows OS distinction test passed." << std::endl;
    }

    // Test 11: UEFI is distinct from BareMetal
    {
        auto baremetalDesc = target::TargetDescriptor::fromString("x64-baremetal-bin");
        auto uefiDesc = target::TargetDescriptor::fromString("x64-uefi-bin");
        
        assert(baremetalDesc.has_value());
        assert(uefiDesc.has_value());
        assert(baremetalDesc->os == target::OS::BareMetal);
        assert(uefiDesc->os == target::OS::UEFI);
        assert(baremetalDesc->os != uefiDesc->os);
        std::cout << "UEFI vs BareMetal OS distinction test passed." << std::endl;
    }

    std::cout << "=== All UEFI tests passed! ===" << std::endl;
    return 0;
}
