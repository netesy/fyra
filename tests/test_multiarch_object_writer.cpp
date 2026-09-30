#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"
#include "target/artifact/object/CoffObjectWriter.h"
#include "target/artifact/object/CoffObjectReader.h"
#include <cassert>
#include <iostream>

int main() {
    std::cout << "=== Testing Multi-Architecture Object Writer/Reader ===" << std::endl;

    // Test 1: TargetInfo ELF machine type mapping
    {
        assert(target::TargetInfo::getElfMachine(target::Arch::X64) == 62);        // EM_X86_64
        assert(target::TargetInfo::getElfMachine(target::Arch::AArch64) == 183);   // EM_AARCH64
        assert(target::TargetInfo::getElfMachine(target::Arch::RISCV64) == 243);   // EM_RISCV
        assert(target::TargetInfo::getElfMachine(target::Arch::LoongArch64) == 258); // EM_LOONGARCH
        std::cout << "TargetInfo ELF machine type mapping test passed." << std::endl;
    }

    // Test 2: TargetInfo COFF machine type mapping
    {
        assert(target::TargetInfo::getCoffMachine(target::Arch::X64) == 0x8664);     // IMAGE_FILE_MACHINE_AMD64
        assert(target::TargetInfo::getCoffMachine(target::Arch::AArch64) == 0xAA64); // IMAGE_FILE_MACHINE_ARM64
        assert(target::TargetInfo::getCoffMachine(target::Arch::RISCV64) == 0x5064); // IMAGE_FILE_MACHINE_RISCV64
        assert(target::TargetInfo::getCoffMachine(target::Arch::LoongArch64) == 0x6264); // IMAGE_FILE_MACHINE_LOONGARCH64
        std::cout << "TargetInfo COFF machine type mapping test passed." << std::endl;
    }

    // Test 3: TargetInfo ELF relocation type mapping
    {
        assert(target::TargetInfo::getElfJumpSlotRelocation(target::Arch::X64) == 7);         // R_X86_64_JUMP_SLOT
        assert(target::TargetInfo::getElfJumpSlotRelocation(target::Arch::AArch64) == 1026);  // R_AARCH64_JUMP_SLOT
        assert(target::TargetInfo::getElfJumpSlotRelocation(target::Arch::RISCV64) == 5);      // R_RISCV_JUMP_SLOT

        assert(target::TargetInfo::getElfGlobDatRelocation(target::Arch::X64) == 6);         // R_X86_64_GLOB_DAT
        assert(target::TargetInfo::getElfGlobDatRelocation(target::Arch::AArch64) == 1025);  // R_AARCH64_GLOB_DAT
        assert(target::TargetInfo::getElfGlobDatRelocation(target::Arch::RISCV64) == 2);      // R_RISCV_64

        assert(target::TargetInfo::getElfRelativeRelocation(target::Arch::X64) == 8);         // R_X86_64_RELATIVE
        assert(target::TargetInfo::getElfRelativeRelocation(target::Arch::AArch64) == 1027);  // R_AARCH64_RELATIVE
        assert(target::TargetInfo::getElfRelativeRelocation(target::Arch::RISCV64) == 3);      // R_RISCV_RELATIVE
        std::cout << "TargetInfo ELF relocation type mapping test passed." << std::endl;
    }

    // Test 4: TargetInfo supportsOutputKind validation
    {
        // Linux executable support
        assert(target::TargetInfo::supportsOutputKind(target::OS::Linux, target::Arch::X64, target::Artifact::Executable));
        assert(target::TargetInfo::supportsOutputKind(target::OS::Linux, target::Arch::AArch64, target::Artifact::Executable));
        assert(target::TargetInfo::supportsOutputKind(target::OS::Linux, target::Arch::RISCV64, target::Artifact::Executable));

        // Linux shared library support
        assert(target::TargetInfo::supportsOutputKind(target::OS::Linux, target::Arch::X64, target::Artifact::SharedLibrary));
        assert(target::TargetInfo::supportsOutputKind(target::OS::Linux, target::Arch::AArch64, target::Artifact::SharedLibrary));
        assert(target::TargetInfo::supportsOutputKind(target::OS::Linux, target::Arch::RISCV64, target::Artifact::SharedLibrary));

        // BareMetal support (NEW)
        assert(target::TargetInfo::supportsOutputKind(target::OS::BareMetal, target::Arch::X64, target::Artifact::Executable));
        assert(target::TargetInfo::supportsOutputKind(target::OS::BareMetal, target::Arch::AArch64, target::Artifact::Executable));
        assert(target::TargetInfo::supportsOutputKind(target::OS::BareMetal, target::Arch::RISCV64, target::Artifact::Executable));

        // Windows PE support
        assert(target::TargetInfo::supportsOutputKind(target::OS::Windows, target::Arch::X64, target::Artifact::Executable));
        assert(target::TargetInfo::supportsOutputKind(target::OS::Windows, target::Arch::AArch64, target::Artifact::Executable));
        assert(target::TargetInfo::supportsOutputKind(target::OS::Windows, target::Arch::RISCV64, target::Artifact::Executable));

        // MacOS support
        assert(target::TargetInfo::supportsOutputKind(target::OS::MacOS, target::Arch::X64, target::Artifact::Executable));
        assert(target::TargetInfo::supportsOutputKind(target::OS::MacOS, target::Arch::AArch64, target::Artifact::Executable));
        assert(!target::TargetInfo::supportsOutputKind(target::OS::MacOS, target::Arch::RISCV64, target::Artifact::Executable));

        std::cout << "TargetInfo supportsOutputKind validation test passed." << std::endl;
    }

    // Test 5: COFF Object Writer with multiple architectures
    {
        using namespace target::artifact::object;

        // Test x86-64 COFF writing
        {
            ObjectArtifact artifact;
            artifact.format = ObjectFormat::COFF;
            artifact.os = target::OS::Windows;
            artifact.arch = target::Arch::X64;
            
            ObjectSection textSec;
            textSec.name = ".text";
            textSec.data = {0x90, 0x90, 0x90}; // NOP instructions
            artifact.addSection(textSec);

            ObjectSymbol sym;
            sym.name = "main";
            sym.value = 0;
            sym.sectionName = ".text";
            sym.binding = SymbolBinding::Global;
            sym.type = SymbolType::Function;
            sym.isDefined = true;
            artifact.addSymbol(sym);

            CoffObjectWriter writer;
            auto data = writer.serialize(artifact);
            assert(!data.empty());
            assert(data.size() >= sizeof(uint16_t)); // At least the COFF header

            // Verify machine type in COFF header
            uint16_t machine = *reinterpret_cast<const uint16_t*>(data.data());
            assert(machine == target::TargetInfo::getCoffMachine(target::Arch::X64));
        }

        // Test AArch64 COFF writing
        {
            ObjectArtifact artifact;
            artifact.format = ObjectFormat::COFF;
            artifact.os = target::OS::Windows;
            artifact.arch = target::Arch::AArch64;
            
            ObjectSection textSec;
            textSec.name = ".text";
            textSec.data = {0x1F, 0x20, 0x03, 0xD5}; // NOP
            artifact.addSection(textSec);

            ObjectSymbol sym;
            sym.name = "main";
            sym.value = 0;
            sym.sectionName = ".text";
            sym.binding = SymbolBinding::Global;
            sym.type = SymbolType::Function;
            sym.isDefined = true;
            artifact.addSymbol(sym);

            CoffObjectWriter writer;
            auto data = writer.serialize(artifact);
            assert(!data.empty());

            uint16_t machine = *reinterpret_cast<const uint16_t*>(data.data());
            assert(machine == target::TargetInfo::getCoffMachine(target::Arch::AArch64));
        }

        // Test RISC-V 64 COFF writing
        {
            ObjectArtifact artifact;
            artifact.format = ObjectFormat::COFF;
            artifact.os = target::OS::Windows;
            artifact.arch = target::Arch::RISCV64;
            
            ObjectSection textSec;
            textSec.name = ".text";
            textSec.data = {0x13, 0x00, 0x00, 0x00}; // NOP
            artifact.addSection(textSec);

            ObjectSymbol sym;
            sym.name = "main";
            sym.value = 0;
            sym.sectionName = ".text";
            sym.binding = SymbolBinding::Global;
            sym.type = SymbolType::Function;
            sym.isDefined = true;
            artifact.addSymbol(sym);

            CoffObjectWriter writer;
            auto data = writer.serialize(artifact);
            assert(!data.empty());

            uint16_t machine = *reinterpret_cast<const uint16_t*>(data.data());
            assert(machine == target::TargetInfo::getCoffMachine(target::Arch::RISCV64));
        }

        std::cout << "COFF Object Writer multi-architecture test passed." << std::endl;
    }

    // Test 6: COFF Object Reader with multiple architectures
    {
        using namespace target::artifact::object;

        // Create minimal COFF object for x86-64
        {
            ObjectArtifact artifact;
            artifact.format = ObjectFormat::COFF;
            artifact.os = target::OS::Windows;
            artifact.arch = target::Arch::X64;
            
            ObjectSection textSec;
            textSec.name = ".text";
            textSec.data = {0x90, 0x90, 0x90};
            artifact.addSection(textSec);

            CoffObjectWriter writer;
            auto data = writer.serialize(artifact);

            CoffObjectReader reader;
            ObjectArtifact readArtifact;
            bool success = reader.parse(data, readArtifact);
            assert(success);
            assert(readArtifact.arch == target::Arch::X64);
        }

        // Create minimal COFF object for AArch64
        {
            ObjectArtifact artifact;
            artifact.format = ObjectFormat::COFF;
            artifact.os = target::OS::Windows;
            artifact.arch = target::Arch::AArch64;
            
            ObjectSection textSec;
            textSec.name = ".text";
            textSec.data = {0x1F, 0x20, 0x03, 0xD5};
            artifact.addSection(textSec);

            CoffObjectWriter writer;
            auto data = writer.serialize(artifact);

            CoffObjectReader reader;
            ObjectArtifact readArtifact;
            bool success = reader.parse(data, readArtifact);
            assert(success);
            assert(readArtifact.arch == target::Arch::AArch64);
        }

        // Create minimal COFF object for RISC-V 64
        {
            ObjectArtifact artifact;
            artifact.format = ObjectFormat::COFF;
            artifact.os = target::OS::Windows;
            artifact.arch = target::Arch::RISCV64;
            
            ObjectSection textSec;
            textSec.name = ".text";
            textSec.data = {0x13, 0x00, 0x00, 0x00};
            artifact.addSection(textSec);

            CoffObjectWriter writer;
            auto data = writer.serialize(artifact);

            CoffObjectReader reader;
            ObjectArtifact readArtifact;
            bool success = reader.parse(data, readArtifact);
            assert(success);
            assert(readArtifact.arch == target::Arch::RISCV64);
        }

        std::cout << "COFF Object Reader multi-architecture test passed." << std::endl;
    }

    std::cout << "=== All Multi-Architecture Object Writer/Reader tests passed! ===" << std::endl;
    return 0;
}