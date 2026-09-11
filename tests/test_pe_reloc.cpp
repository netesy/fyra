#include "target/artifact/object/ObjectArtifact.h"
#include "target/artifact/linker/InternalLinker.h"
#include "target/artifact/linker/DynamicLinkPlan.h"
#include "target/artifact/linker/TargetDynamicImageBuilder.h"
#include "target/artifact/executable/PeImage.h"
#include <iostream>
#include <cassert>
#include <fstream>
#include <vector>
#include <cstring>

using namespace target::artifact::object;
using namespace target::artifact::linker;
using namespace target::artifact::executable;

int main() {
    std::cout << "[Test] PE Base Relocations (.reloc) and ASLR Verification..." << std::endl;

    // Create a PE executable artifact with a 64-bit absolute relocation in .data
    ObjectArtifact art;
    art.format = ObjectFormat::COFF;
    art.arch = target::Arch::X64;
    art.os = target::OS::Windows;

    // .text section: ret
    ObjectSection textSec;
    textSec.name = ".text";
    textSec.data = {0xC3};
    textSec.alignment = 16;
    art.addSection(textSec);

    // .data section: 8-byte global pointer initialized to point to target_val (offset 8)
    ObjectSection dataSec;
    dataSec.name = ".data";
    dataSec.data = {0, 0, 0, 0, 0, 0, 0, 0,   0x2A, 0, 0, 0, 0, 0, 0, 0}; // [0..7]: ptr, [8..15]: int64 = 42
    dataSec.alignment = 16;
    art.addSection(dataSec);

    ObjectSymbol targetSym;
    targetSym.name = "target_val";
    targetSym.value = 8;
    targetSym.size = 8;
    targetSym.binding = SymbolBinding::Global;
    targetSym.type = SymbolType::NoType;
    targetSym.sectionName = ".data";
    targetSym.isDefined = true;
    art.addSymbol(targetSym);

    ObjectRelocation reloc;
    reloc.offset = 0; // At offset 0 of .data
    reloc.type = "IMAGE_REL_AMD64_ADDR64";
    reloc.addend = 0;
    reloc.symbolName = "target_val";
    reloc.sectionName = ".data";
    art.addRelocation(reloc);

    InternalLinker linker;
    LinkedImage linkedImage;
    bool linked = linker.link({art}, linkedImage, LinkOutputKind::Executable);
    assert(linked);
    assert(!linkedImage.relocationFixupVmas.empty());

    PeExecutableImageBuilder builder;
    const std::string exePath = "test_reloc_app.exe";
    bool built = builder.build(linkedImage, exePath);
    assert(built);

    std::ifstream file(exePath, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    file.close();
    std::remove(exePath.c_str());

    assert(bytes.size() > 0x200 && bytes[0] == 'M' && bytes[1] == 'Z');

    // Parse PE optional header DllCharacteristics
    uint32_t peOff = 0;
    std::memcpy(&peOff, bytes.data() + 0x3c, 4);
    uint16_t dllCharacteristics = 0;
    std::memcpy(&dllCharacteristics, bytes.data() + peOff + 4 + 20 + 70, 2);
    // DYNAMIC_BASE bit 0x0040 must be set
    assert((dllCharacteristics & 0x0040) != 0);

    // Parse Directory 5 (Base Relocation Directory)
    uint32_t baseRelocRva = 0, baseRelocSize = 0;
    std::memcpy(&baseRelocRva, bytes.data() + peOff + 4 + 20 + 112 + 5 * 8, 4);
    std::memcpy(&baseRelocSize, bytes.data() + peOff + 4 + 20 + 112 + 5 * 8 + 4, 4);

    assert(baseRelocRva != 0 && baseRelocSize >= 12);

    // Parse section headers to find raw file offset of .reloc
    uint16_t numSections = 0, optHeaderSize = 0;
    std::memcpy(&numSections, bytes.data() + peOff + 4 + 2, 2);
    std::memcpy(&optHeaderSize, bytes.data() + peOff + 4 + 16, 2);

    uint32_t sectionHeadersOff = peOff + 4 + 20 + optHeaderSize;
    uint32_t relocRawOff = 0;
    for (uint16_t i = 0; i < numSections; ++i) {
        uint32_t sHeader = sectionHeadersOff + i * 40;
        char name[9] = {0};
        std::memcpy(name, bytes.data() + sHeader, 8);
        uint32_t sRva = 0, sRawOff = 0;
        std::memcpy(&sRva, bytes.data() + sHeader + 12, 4);
        std::memcpy(&sRawOff, bytes.data() + sHeader + 20, 4);

        if (std::string(name) == ".reloc") {
            relocRawOff = sRawOff;
            assert(sRva == baseRelocRva);
        }
    }
    assert(relocRawOff != 0);

    // Decode relocation block
    uint32_t pageRva = 0, blockSize = 0;
    std::memcpy(&pageRva, bytes.data() + relocRawOff, 4);
    std::memcpy(&blockSize, bytes.data() + relocRawOff + 4, 4);
    assert(pageRva == 0x2000); // .data section RVA is 0x2000
    assert(blockSize == 12);   // 8 bytes header + 2 bytes DIR64 entry + 2 bytes ABSOLUTE pad

    uint16_t entry0 = 0, entry1 = 0;
    std::memcpy(&entry0, bytes.data() + relocRawOff + 8, 2);
    std::memcpy(&entry1, bytes.data() + relocRawOff + 10, 2);

    uint16_t type0 = (entry0 >> 12);
    uint16_t off0 = (entry0 & 0x0FFF);
    assert(type0 == 10); // IMAGE_REL_BASED_DIR64
    assert(off0 == 0);   // Offset 0 within page 0x2000

    uint16_t type1 = (entry1 >> 12);
    assert(type1 == 0);  // IMAGE_REL_BASED_ABSOLUTE (padding)

    // Verify explicit error handling on invalid fixups
    LinkedImage invalidImage = linkedImage;
    invalidImage.relocationFixupVmas = {0x100}; // VMA below imageBase (0x140000000)
    PeExecutableImageBuilder failBuilder;
    assert(!failBuilder.build(invalidImage, "invalid_app.exe"));
    assert(failBuilder.getLastError().find("Invalid base relocation VMA") != std::string::npos);

    std::cout << "PE Base Relocation (.reloc) Block Decoding & DYNAMIC_BASE Verification PASSED." << std::endl;
    return 0;
}
