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

    // 3. Test DynamicLinkPlan Neutral Extension with Data Import
    LinkedImage linkedImg;
    linkedImg.os = target::OS::Windows;
    linkedImg.arch = target::Arch::X64;

    DynamicImport impData;
    impData.symbol = "global_var";
    impData.dependencyLibrary = "answer.dll";
    impData.kind = DynamicImportKind::Data;

    std::vector<DynamicImport> imports = {impData};
    DynamicLinkPlan plan = DynamicLinkPlan::createFromLinkedImage(linkedImg, imports);
    assert(plan.imports.size() == 1);
    assert(plan.imports[0].symbol == "global_var");
    assert(plan.imports[0].dependencyLibrary == "answer.dll");
    assert(plan.imports[0].kind == DynamicImportKind::Data);

    std::cout << "Complete PE Dynamic Import Suite PASSED." << std::endl;
    return 0;
}
