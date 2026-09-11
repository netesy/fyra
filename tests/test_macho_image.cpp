#include <cassert>
#include <cstdio>
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

    // Test Dynamic Link Plan explicit unsupported status for macOS/Mach-O
    DynamicLinkPlan dlp;
    dlp.os = target::OS::MacOS;
    dlp.arch = target::Arch::X64;
    dlp.outputKind = LinkOutputKind::SharedLibrary;
    auto machoDynBuilder = TargetDynamicImageBuilder::createForTarget(target::Arch::X64, target::OS::MacOS);
    assert(machoDynBuilder != nullptr);
    bool dynOk = machoDynBuilder->buildSharedLibrary(dlp, "test.dylib");
    assert(!dynOk);
    assert(machoDynBuilder->getLastError().find("not implemented for target: macOS/Mach-O") != std::string::npos);

    std::cout << "Canonical linked-image Mach-O executable tests passed.\n";
    return 0;
}
