#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <sys/wait.h>

#include "target/artifact/executable/ElfImage.h"
#include "target/artifact/linker/InternalLinker.h"
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
    text.data = {0xb8, 0x2a, 0, 0, 0, 0xc3};
    text.alignment = 16;
    artifact.addSection(text);
    ObjectSymbol main;
    main.name = "main"; main.size = text.data.size(); main.sectionName = ".text";
    main.binding = SymbolBinding::Global; main.type = SymbolType::Function;
    artifact.addSymbol(main);
    return artifact;
}

int main() {
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

    std::cout << "Canonical linked-image ELF executable tests passed.\n";
}
