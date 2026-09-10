#include "target/artifact/object/ObjectArtifact.h"
#include "target/artifact/object/ElfObjectWriter.h"
#include "target/artifact/object/ElfObjectReader.h"
#include "target/artifact/linker/InternalLinker.h"
#include "target/artifact/linker/LinkedImage.h"
#include "target/artifact/archive/ArchiveWriter.h"
#include "target/artifact/archive/UnixArchiveWriter.h"
#include "target/artifact/archive/ArchiveReader.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>

using namespace target::artifact::object;
using namespace target::artifact::linker;
using namespace target::artifact::archive;

void test_section_merging_and_rebasing() {
    std::cout << "[Test] Linker Section Merging and Symbol Rebasing..." << std::endl;

    ObjectArtifact art1;
    art1.format = ObjectFormat::ELF;
    art1.arch = target::Arch::X64;
    art1.os = target::OS::Linux;

    ObjectSection sec1;
    sec1.name = ".text";
    sec1.data = {0x90, 0x90, 0x90, 0x90}; // 4 bytes
    sec1.alignment = 16;
    art1.addSection(sec1);

    ObjectSymbol sym1;
    sym1.name = "fn1";
    sym1.value = 0;
    sym1.size = 4;
    sym1.binding = SymbolBinding::Global;
    sym1.type = SymbolType::Function;
    sym1.sectionName = ".text";
    sym1.isDefined = true;
    art1.addSymbol(sym1);

    ObjectArtifact art2;
    art2.format = ObjectFormat::ELF;
    art2.arch = target::Arch::X64;
    art2.os = target::OS::Linux;

    ObjectSection sec2;
    sec2.name = ".text";
    sec2.data = {0xC3}; // 1 byte
    sec2.alignment = 16;
    art2.addSection(sec2);

    ObjectSymbol sym2;
    sym2.name = "fn2";
    sym2.value = 0;
    sym2.size = 1;
    sym2.binding = SymbolBinding::Global;
    sym2.type = SymbolType::Function;
    sym2.sectionName = ".text";
    sym2.isDefined = true;
    art2.addSymbol(sym2);

    InternalLinker linker;
    LinkedImage image;
    bool ok = linker.link({art1, art2}, image);
    assert(ok);

    const LinkedSection* textMerged = image.findSection(".text");
    assert(textMerged != nullptr);
    assert(textMerged->data.size() >= 17); // 4 + 12 (pad to 16-byte align) + 1 = 17 bytes

    const LinkedSymbol* fn1L = image.findSymbol("fn1");
    const LinkedSymbol* fn2L = image.findSymbol("fn2");
    assert(fn1L != nullptr && fn2L != nullptr);
    assert(fn2L->virtualAddress > fn1L->virtualAddress);

    std::cout << "  -> Section Merging & Symbol Rebasing PASSED." << std::endl;
}

void test_duplicate_symbol_error() {
    std::cout << "[Test] Linker Duplicate Global Symbol Detection..." << std::endl;

    ObjectArtifact art1;
    art1.format = ObjectFormat::ELF;
    art1.arch = target::Arch::X64;
    art1.os = target::OS::Linux;

    ObjectSection sec;
    sec.name = ".text";
    sec.data = {0xC3};
    art1.addSection(sec);

    ObjectSymbol sym;
    sym.name = "duplicate_sym";
    sym.binding = SymbolBinding::Global;
    sym.sectionName = ".text";
    sym.isDefined = true;
    art1.addSymbol(sym);

    ObjectArtifact art2 = art1;

    InternalLinker linker;
    LinkedImage image;
    bool ok = linker.link({art1, art2}, image);
    assert(!ok);
    assert(linker.getLastError().find("Duplicate global symbol") != std::string::npos);

    std::cout << "  -> Duplicate Symbol Error Detection PASSED." << std::endl;
}

void test_undefined_symbol_error() {
    std::cout << "[Test] Linker Undefined Symbol Detection..." << std::endl;

    ObjectArtifact art;
    art.format = ObjectFormat::ELF;
    art.arch = target::Arch::X64;
    art.os = target::OS::Linux;

    ObjectSection sec;
    sec.name = ".text";
    sec.data = {0xE8, 0x00, 0x00, 0x00, 0x00};
    art.addSection(sec);

    ObjectRelocation reloc;
    reloc.offset = 1;
    reloc.type = "R_X86_64_PC32";
    reloc.symbolName = "nonexistent_fn";
    reloc.sectionName = ".text";
    art.addRelocation(reloc);

    InternalLinker linker;
    LinkedImage image;
    bool ok = linker.link({art}, image);
    assert(!ok);
    assert(linker.getLastError().find("Unresolved undefined symbol") != std::string::npos);

    std::cout << "  -> Undefined Symbol Error Detection PASSED." << std::endl;
}

void test_archive_linking() {
    std::cout << "[Test] Archive Reader and Member Linking..." << std::endl;

    ObjectArtifact memArt;
    memArt.format = ObjectFormat::ELF;
    memArt.arch = target::Arch::X64;
    memArt.os = target::OS::Linux;

    ObjectSection sec;
    sec.name = ".text";
    sec.data = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3}; // mov $42, %eax; ret
    memArt.addSection(sec);

    ObjectSymbol sym;
    sym.name = "answer";
    sym.value = 0;
    sym.size = 6;
    sym.binding = SymbolBinding::Global;
    sym.type = SymbolType::Function;
    sym.sectionName = ".text";
    sym.isDefined = true;
    memArt.addSymbol(sym);

    ElfObjectWriter objWriter;
    std::vector<uint8_t> objBytes = objWriter.serialize(memArt);

    ArchiveMember arMem;
    arMem.name = "answer.o";
    arMem.bytes = objBytes;
    arMem.exportedSymbols = {"answer"};

    UnixArchiveWriter arWriter;
    std::vector<uint8_t> arBytes = arWriter.serialize({arMem});

    ArchiveReader arReader;
    std::vector<ArchiveObjectMember> parsedMembers;
    bool parsed = arReader.parse(arBytes, parsedMembers);
    assert(parsed);
    assert(!parsedMembers.empty());
    assert(parsedMembers[0].artifact.findSymbol("answer") != nullptr);

    std::cout << "  -> Archive Reader and Member Linking PASSED." << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Running Internal Linker Unit Tests     " << std::endl;
    std::cout << "========================================" << std::endl;

    test_section_merging_and_rebasing();
    test_duplicate_symbol_error();
    test_undefined_symbol_error();
    test_archive_linking();

    std::cout << "All Internal Linker Tests Passed!" << std::endl;
    return 0;
}
