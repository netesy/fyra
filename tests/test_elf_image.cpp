#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <sys/wait.h>

#include "target/artifact/executable/ElfImage.h"
#include "target/artifact/linker/InternalLinker.h"
#include "target/artifact/linker/TargetDynamicImageBuilder.h"
#include "target/artifact/object/ObjectArtifact.h"

using namespace target::artifact::linker;
using namespace target::artifact::object;

static ObjectArtifact makeArtifact() {
    ObjectArtifact artifact;
    artifact.format = ObjectFormat::ELF;
    artifact.arch = target::Arch::X64;
    artifact.os = target::OS::Linux;
    ObjectSection text;
    text.name = ".text";
    text.data = {0xb8, 0x2a, 0, 0, 0, 0xc3}; // mov $42, %eax; ret
    text.alignment = 16;
    artifact.addSection(text);
    ObjectSymbol main;
    main.name = "main"; main.size = text.data.size(); main.sectionName = ".text";
    main.binding = SymbolBinding::Global; main.type = SymbolType::Function;
    artifact.addSymbol(main);
    return artifact;
}

static ObjectArtifact makeLibraryArtifact() {
    ObjectArtifact artifact;
    artifact.format = ObjectFormat::ELF;
    artifact.arch = target::Arch::X64;
    artifact.os = target::OS::Linux;

    // exported function: get_answer() -> returns 42
    ObjectSection text;
    text.name = ".text";
    text.data = {0xb8, 0x2a, 0, 0, 0, 0xc3}; // mov $42, %eax; ret
    text.alignment = 16;
    artifact.addSection(text);

    ObjectSymbol fnSym;
    fnSym.name = "get_answer"; fnSym.size = text.data.size(); fnSym.sectionName = ".text";
    fnSym.binding = SymbolBinding::Global; fnSym.type = SymbolType::Function;
    artifact.addSymbol(fnSym);

    // exported data: global_var = 100
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

static ObjectArtifact makeConsumerArtifact() {
    ObjectArtifact artifact;
    artifact.format = ObjectFormat::ELF;
    artifact.arch = target::Arch::X64;
    artifact.os = target::OS::Linux;

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
    // 1. Static executable generation test
    InternalLinker linker;
    LinkedImage image;
    assert(linker.link({makeArtifact()}, image, LinkOutputKind::Executable));
    assert(image.findSymbol("_start") != nullptr);

    ElfExecutableImageBuilder builder;
    const std::string output = "test_linked_elf";
    assert(builder.build(image, output));
    std::ifstream file(output, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    assert(bytes.size() > 64);
    assert(bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F');
    int status = std::system(("./" + output).c_str());
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 42);
    std::remove(output.c_str());

    // 2. Negative test - sentinel preservation on failure
    ObjectArtifact unresolved = makeArtifact();
    unresolved.relocations.push_back({1, "R_X86_64_PC32", -4, "missing", ".text"});
    const std::string sentinelPath = "test_elf_sentinel";
    const std::string sentinel = "PREEXISTING-USER-FILE-MUST-SURVIVE";
    { std::ofstream out(sentinelPath); out << sentinel; }
    LinkedImage rejected;
    assert(!linker.link({unresolved}, rejected, LinkOutputKind::Executable));
    std::ifstream preserved(sentinelPath);
    std::string contents((std::istreambuf_iterator<char>(preserved)), {});
    assert(contents == sentinel);
    std::remove(sentinelPath.c_str());

    // 3. Dynamic ELF shared library & dynamic executable test
    const std::string libPath = "libfixture.so";
    const std::string consumerPath = "test_elf_consumer";

    // Link shared library
    LinkedImage libImage;
    assert(linker.link({makeLibraryArtifact()}, libImage, LinkOutputKind::SharedLibrary));
    DynamicLinkPlan libPlan = DynamicLinkPlan::createFromLinkedImage(libImage);

    auto dynBuilder = TargetDynamicImageBuilder::createForTarget(target::Arch::X64, target::OS::Linux);
    assert(dynBuilder != nullptr);
    assert(dynBuilder->buildSharedLibrary(libPlan, libPath));

    // Verify libfixture.so with readelf / objdump independent tool checks
    assert(std::system(("readelf -h " + libPath + " > /dev/null").c_str()) == 0);
    assert(std::system(("readelf -d " + libPath + " > /dev/null").c_str()) == 0);
    assert(std::system(("readelf -s " + libPath + " > /dev/null").c_str()) == 0);

    // Link consumer executable against libfixture.so dynamic imports
    std::vector<DynamicImport> imports = {
        {"get_answer", libPath, false, DynamicImportKind::Function},
        {"global_var", libPath, false, DynamicImportKind::Data}
    };

    LinkedImage consumerImage;
    assert(linker.link({makeConsumerArtifact()}, consumerImage, LinkOutputKind::Executable, imports));

    assert(builder.build(consumerImage, consumerPath));

    // Verify consumer binary with readelf
    assert(std::system(("readelf -h " + consumerPath + " > /dev/null").c_str()) == 0);
    assert(std::system(("readelf -d " + consumerPath + " > /dev/null").c_str()) == 0);
    assert(std::system(("readelf -r " + consumerPath + " > /dev/null").c_str()) == 0);

    // Run runtime execution test with LD_LIBRARY_PATH=.
    int runStatus = std::system(("LD_LIBRARY_PATH=. ./" + consumerPath).c_str());
    assert(WIFEXITED(runStatus));
    assert(WEXITSTATUS(runStatus) == 142); // 42 + 100

    // Cleanup
    std::remove(libPath.c_str());
    std::remove(consumerPath.c_str());

    // 4. External ELF shared library consumption test
    const std::string extSrcPath = "ext_fixture.c";
    const std::string extLibPath = "libextfixture.so";
    const std::string extConsumerPath = "test_ext_consumer";

    {
        std::ofstream extSrc(extSrcPath);
        extSrc << "int get_answer() { return 42; }\n";
        extSrc << "int global_var = 100;\n";
    }

    int extCompileRc = std::system(("gcc -fPIC -shared -o " + extLibPath + " " + extSrcPath).c_str());
    assert(extCompileRc == 0);

    std::vector<DynamicImport> extImports = {
        {"get_answer", extLibPath, false, DynamicImportKind::Function},
        {"global_var", extLibPath, false, DynamicImportKind::Data}
    };

    LinkedImage extConsumerImage;
    assert(linker.link({makeConsumerArtifact()}, extConsumerImage, LinkOutputKind::Executable, extImports));
    assert(builder.build(extConsumerImage, extConsumerPath));

    int extRunStatus = std::system(("LD_LIBRARY_PATH=. ./" + extConsumerPath).c_str());
    assert(WIFEXITED(extRunStatus));
    assert(WEXITSTATUS(extRunStatus) == 142); // 42 + 100

    std::remove(extSrcPath.c_str());
    std::remove(extLibPath.c_str());
    std::remove(extConsumerPath.c_str());

    std::cout << "Canonical linked-image ELF executable & shared library tests passed.\n";
}
