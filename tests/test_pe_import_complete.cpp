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
    std::cout << "[Test] Complete PE Dynamic Import Suite (Named, Ordinal, Function & Data)..." << std::endl;

    // 1. Test PE Ordinal Function Import Encoding
    PeImage peOrd;
    peOrd.machine = 0x8664;
    peOrd.sectionAlignment = 0x1000;
    peOrd.fileAlignment = 0x200;

    PeImportDirectory impDirOrd;
    impDirOrd.dllName = "answer.dll";

    PeImportSymbol ordSym;
    ordSym.name = "get_answer";
    ordSym.isOrdinal = true;
    ordSym.ordinal = 7;
    impDirOrd.symbols.push_back(ordSym);
    peOrd.imports.push_back(impDirOrd);

    PeImageWriter writer;
    const std::string ordPath = "test_ord_app.exe";
    bool builtOrd = writer.write(peOrd, ordPath);
    assert(builtOrd);

    std::ifstream fileOrd(ordPath, std::ios::binary);
    std::vector<uint8_t> bytesOrd((std::istreambuf_iterator<char>(fileOrd)), {});
    fileOrd.close();
    std::remove(ordPath.c_str());

    assert(bytesOrd.size() > 0x200);
    uint32_t peOff = 0;
    std::memcpy(&peOff, bytesOrd.data() + 0x3c, 4);

    uint32_t importRva = 0;
    std::memcpy(&importRva, bytesOrd.data() + peOff + 4 + 20 + 112 + 1 * 8, 4);
    assert(importRva != 0);

    // Verify section header to locate raw offset of .idata
    uint16_t numSections = 0, optHeaderSize = 0;
    std::memcpy(&numSections, bytesOrd.data() + peOff + 4 + 2, 2);
    std::memcpy(&optHeaderSize, bytesOrd.data() + peOff + 4 + 16, 2);

    uint32_t sectionHeadersOff = peOff + 4 + 20 + optHeaderSize;
    uint32_t idataRawOff = 0;
    for (uint16_t i = 0; i < numSections; ++i) {
        uint32_t sHeader = sectionHeadersOff + i * 40;
        char name[9] = {0};
        std::memcpy(name, bytesOrd.data() + sHeader, 8);
        uint32_t sRva = 0, sRawOff = 0;
        std::memcpy(&sRva, bytesOrd.data() + sHeader + 12, 4);
        std::memcpy(&sRawOff, bytesOrd.data() + sHeader + 20, 4);
        if (std::string(name) == ".idata") {
            idataRawOff = sRawOff;
            assert(sRva == importRva);
        }
    }
    assert(idataRawOff != 0);

    // Read ILT entry 0 for ordinal 7 (must have high bit 1ULL << 63 and low 16-bits = 7)
    uint32_t descSize = 2 * 20; // 1 DLL + 1 null terminator
    uint64_t ilt0 = 0;
    std::memcpy(&ilt0, bytesOrd.data() + idataRawOff + descSize, 8);
    assert((ilt0 & (1ULL << 63)) != 0);
    assert((ilt0 & 0xFFFF) == 7);

    // 2. Test Invalid Ordinal Range Rejection
    PeImage peInvalidOrd = peOrd;
    peInvalidOrd.imports[0].symbols[0].ordinal = 0x10000; // Out of range (> 65535)
    bool failOrd = writer.write(peInvalidOrd, "test_fail_ord.exe");
    assert(!failOrd);
    assert(writer.getLastError().find("ordinal out of range") != std::string::npos);

    // 3. Test End-to-End Imported Data Symbol Relocation Patching
    ObjectArtifact dataExeArt;
    dataExeArt.format = ObjectFormat::COFF;
    dataExeArt.arch = target::Arch::X64;
    dataExeArt.os = target::OS::Windows;

    // Code instruction referencing imported data:
    // mov rax, qword ptr [rip + disp32] (48 8b 05 00 00 00 00)
    // Relocation at offset 3 for symbol "imported_var" (IMAGE_REL_AMD64_REL32)
    ObjectSection textSec;
    textSec.name = ".text";
    textSec.data = {0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0xC3};
    textSec.alignment = 16;
    dataExeArt.addSection(textSec);

    ObjectSymbol mainSym;
    mainSym.name = "main";
    mainSym.value = 0;
    mainSym.size = 8;
    mainSym.binding = SymbolBinding::Global;
    mainSym.type = SymbolType::Function;
    mainSym.sectionName = ".text";
    mainSym.isDefined = true;
    dataExeArt.addSymbol(mainSym);

    ObjectRelocation dataReloc;
    dataReloc.offset = 3;
    dataReloc.type = "IMAGE_REL_AMD64_REL32";
    dataReloc.addend = -4;
    dataReloc.symbolName = "imported_var";
    dataReloc.sectionName = ".text";
    dataExeArt.addRelocation(dataReloc);

    InternalLinker linker;
    LinkedImage dataExeImage;
    DynamicImport dataImp;
    dataImp.symbol = "imported_var";
    dataImp.dependencyLibrary = "data_supplier.dll";
    dataImp.kind = DynamicImportKind::Data;

    std::vector<DynamicImport> dataImports = {dataImp};
    bool linked = linker.link({dataExeArt}, dataExeImage, LinkOutputKind::Executable, dataImports);
    assert(linked);

    // Verify no function thunk was synthesized for imported data in .text
    assert(dataExeImage.importThunkVmas.find("imported_var") == dataExeImage.importThunkVmas.end());
    assert(dataExeImage.dataImportFixups.size() == 1);

    PeExecutableImageBuilder peExeBuilder;
    DynamicLinkPlan dataPlan = DynamicLinkPlan::createFromLinkedImage(dataExeImage, dataImports);
    const std::string dataExePath = "test_data_import_app.exe";
    bool builtDataExe = peExeBuilder.buildWithPlan(dataPlan, dataExePath);
    assert(builtDataExe);

    std::ifstream fileDataExe(dataExePath, std::ios::binary);
    std::vector<uint8_t> bytesDataExe((std::istreambuf_iterator<char>(fileDataExe)), {});
    fileDataExe.close();
    std::remove(dataExePath.c_str());

    assert(bytesDataExe.size() > 0x200);

    // Read patched displacement in the instruction at offset 3 of .text (0x1000 RVA)
    // Section headers -> .text offset
    uint32_t peOffData = 0;
    std::memcpy(&peOffData, bytesDataExe.data() + 0x3c, 4);
    uint16_t numSecsData = 0, optHeaderSizeData = 0;
    std::memcpy(&numSecsData, bytesDataExe.data() + peOffData + 4 + 2, 2);
    std::memcpy(&optHeaderSizeData, bytesDataExe.data() + peOffData + 4 + 16, 2);

    uint32_t secHeaderOffData = peOffData + 4 + 20 + optHeaderSizeData;
    uint32_t textRawOffData = 0;
    for (uint16_t i = 0; i < numSecsData; ++i) {
        char name[9] = {0};
        std::memcpy(name, bytesDataExe.data() + secHeaderOffData + i * 40, 8);
        if (std::string(name) == ".text") {
            std::memcpy(&textRawOffData, bytesDataExe.data() + secHeaderOffData + i * 40 + 20, 4);
            break;
        }
    }
    assert(textRawOffData != 0);

    int32_t disp32 = 0;
    std::memcpy(&disp32, bytesDataExe.data() + textRawOffData + 3, 4);
    std::cout << "Debug disp32 = " << disp32 << " (hex: 0x" << std::hex << disp32 << std::dec << ")" << std::endl;
    assert(disp32 > 0);
    uint64_t computedIatVma = (0x140001007) + disp32;
    assert((computedIatVma & 0xFFF) >= 0x030); // Points into IAT in .idata section

    std::cout << "Complete PE Dynamic Import Suite PASSED." << std::endl;
    return 0;
}
