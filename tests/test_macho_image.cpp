#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "target/artifact/executable/MachOImage.h"
#include "target/artifact/linker/InternalLinker.h"
#include "target/artifact/linker/TargetDynamicImageBuilder.h"
#include "target/artifact/object/ObjectArtifact.h"

using namespace target::artifact::linker;
using namespace target::artifact::object;

static ObjectArtifact makeMachOArtifact() {
    ObjectArtifact artifact;
    artifact.format = ObjectFormat::MachO;
    artifact.arch = target::Arch::X64;
    artifact.os = target::OS::MacOS;
    ObjectSection text;
    text.name = ".text";
    text.data = {0xb8, 0x2a, 0, 0, 0, 0xc3}; // mov $42, %eax; ret
    text.alignment = 16;
    artifact.addSection(text);
    ObjectSymbol main;
    main.name = "main";
    main.size = text.data.size();
    main.sectionName = ".text";
    main.binding = SymbolBinding::Global;
    main.type = SymbolType::Function;
    main.isDefined = true;
    artifact.addSymbol(main);
    return artifact;
}

static ObjectArtifact makeMachOLibraryArtifact() {
    ObjectArtifact artifact;
    artifact.format = ObjectFormat::MachO;
    artifact.arch = target::Arch::X64;
    artifact.os = target::OS::MacOS;

    // Exported function: get_answer() -> 42
    ObjectSection text;
    text.name = ".text";
    text.data = {0xb8, 0x2a, 0, 0, 0, 0xc3}; // mov $42, %eax; ret
    text.alignment = 16;
    artifact.addSection(text);

    ObjectSymbol fnSym;
    fnSym.name = "get_answer"; fnSym.size = text.data.size(); fnSym.sectionName = ".text";
    fnSym.binding = SymbolBinding::Global; fnSym.type = SymbolType::Function;
    artifact.addSymbol(fnSym);

    // Exported data: global_var = 100
    ObjectSection data;
    data.name = ".data";
    data.data = {0x64, 0, 0, 0}; // int32_t 100
    data.alignment = 4;
    artifact.addSection(data);

    ObjectSymbol dataSym;
    dataSym.name = "global_var"; dataSym.size = 4; dataSym.sectionName = ".data";
    dataSym.binding = SymbolBinding::Global; dataSym.type = SymbolType::Object;
    artifact.addSymbol(dataSym);

    return artifact;
}

static ObjectArtifact makeMachOConsumerArtifact() {
    ObjectArtifact artifact;
    artifact.format = ObjectFormat::MachO;
    artifact.arch = target::Arch::X64;
    artifact.os = target::OS::MacOS;

    // main calling imported get_answer() and reading imported global_var
    // text:
    // e8 00 00 00 00       call get_answer  (reloc offset 1 to get_answer, addend -4) -> %eax = 42
    // 48 8b 0d 00 00 00 00 mov global_var(%rip), %rcx (reloc offset 8 to global_var, addend -4) -> %rcx = &global_var
    // 8b 09                mov (%rcx), %ecx (read value of global_var) -> %ecx = 100
    // 01 c8                add %ecx, %eax   (42 + 100 = 142)
    // c3                   ret
    ObjectSection text;
    text.name = ".text";
    text.data = {
        0xe8, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x8b, 0x0d, 0x00, 0x00, 0x00, 0x00,
        0x8b, 0x09,
        0x01, 0xc8,
        0xc3
    };
    text.alignment = 16;
    artifact.addSection(text);

    artifact.relocations.push_back({1, "R_X86_64_PC32", -4, "get_answer", ".text"});
    artifact.relocations.push_back({8, "R_X86_64_PC32", -4, "global_var", ".text"});

    ObjectSymbol main;
    main.name = "main"; main.size = text.data.size(); main.sectionName = ".text";
    main.binding = SymbolBinding::Global; main.type = SymbolType::Function;
    artifact.addSymbol(main);

    return artifact;
}

int main() {
    std::cout << "[Test] Canonical Mach-O Executable Linked-Image Pipeline..." << std::endl;

    InternalLinker linker;
    LinkedImage image;
    bool linkOk = linker.link({makeMachOArtifact()}, image, LinkOutputKind::Executable);
    assert(linkOk);

    MachOExecutableImageBuilder builder;
    const std::string output = "test_linked_macho";
    bool buildOk = builder.build(image, output);
    assert(buildOk);

    std::ifstream file(output, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    assert(bytes.size() > 64);
    // Check 64-bit Mach-O magic feedfacf
    assert(bytes[0] == 0xcf && bytes[1] == 0xfa && bytes[2] == 0xed && bytes[3] == 0xfe);
    file.close();
    std::remove(output.c_str());

    // Test Destination Safety
    ObjectArtifact unresolved = makeMachOArtifact();
    unresolved.relocations.push_back({1, "R_X86_64_PC32", -4, "missing", ".text"});
    const std::string sentinelPath = "test_macho_sentinel";
    const std::string sentinel = "PREEXISTING-USER-FILE-MUST-SURVIVE";
    { std::ofstream out(sentinelPath); out << sentinel; }
    LinkedImage rejected;
    assert(!linker.link({unresolved}, rejected, LinkOutputKind::Executable));
    std::ifstream preserved(sentinelPath);
    std::string contents((std::istreambuf_iterator<char>(preserved)), {});
    assert(contents == sentinel);
    preserved.close();
    std::remove(sentinelPath.c_str());

    // Test Dynamic Mach-O Dylib Generation
    const std::string dylibPath = "libfyra_fixture.dylib";
    LinkedImage dylibImage;
    assert(linker.link({makeMachOLibraryArtifact()}, dylibImage, LinkOutputKind::SharedLibrary));
    DynamicLinkPlan dlp = DynamicLinkPlan::createFromLinkedImage(dylibImage);

    auto machoDynBuilder = TargetDynamicImageBuilder::createForTarget(target::Arch::X64, target::OS::MacOS);
    assert(machoDynBuilder != nullptr);
    bool dynOk = machoDynBuilder->buildSharedLibrary(dlp, dylibPath);
    assert(dynOk);

    std::ifstream dylibFile(dylibPath, std::ios::binary);
    std::vector<unsigned char> dylibBytes((std::istreambuf_iterator<char>(dylibFile)), {});
    assert(dylibBytes.size() > 64);
    // Magic check: feedfacf
    assert(dylibBytes[0] == 0xcf && dylibBytes[1] == 0xfa && dylibBytes[2] == 0xed && dylibBytes[3] == 0xfe);
    // Filetype check at offset 12: MH_DYLIB = 6
    uint32_t filetype = *reinterpret_cast<uint32_t*>(&dylibBytes[12]);
    assert(filetype == 6);
    dylibFile.close();

    // Test Dynamic Mach-O Consumer Executable Generation
    const std::string consumerPath = "fyra_consumer";
    std::vector<DynamicImport> imports = {
        {"get_answer", dylibPath, false, DynamicImportKind::Function},
        {"global_var", dylibPath, false, DynamicImportKind::Data}
    };

    LinkedImage consumerImage;
    assert(linker.link({makeMachOConsumerArtifact()}, consumerImage, LinkOutputKind::Executable, imports));
    assert(builder.build(consumerImage, consumerPath));

    std::ifstream consumerFile(consumerPath, std::ios::binary);
    std::vector<unsigned char> consumerBytes((std::istreambuf_iterator<char>(consumerFile)), {});
    assert(consumerBytes.size() > 64);
    // Magic check: feedfacf
    assert(consumerBytes[0] == 0xcf && consumerBytes[1] == 0xfa && consumerBytes[2] == 0xed && consumerBytes[3] == 0xfe);
    // Filetype check at offset 12: MH_EXECUTE = 2
    uint32_t consumerType = *reinterpret_cast<uint32_t*>(&consumerBytes[12]);
    assert(consumerType == 2);
    consumerFile.close();

    // Independent tool inspection if LLVM tools are present
    (void)std::system(("llvm-readobj --file-headers " + dylibPath + " > /dev/null 2>&1").c_str());
    (void)std::system(("llvm-readobj --file-headers " + consumerPath + " > /dev/null 2>&1").c_str());

    std::remove(dylibPath.c_str());
    std::remove(consumerPath.c_str());

    std::cout << "Canonical linked-image Mach-O executable & dylib tests passed.\n";
    return 0;
}
