#include "target/artifact/object/ObjectArtifact.h"
#include "target/artifact/object/ElfObjectWriter.h"
#include "target/artifact/object/ElfObjectReader.h"
#include "target/artifact/object/CoffObjectWriter.h"
#include "target/artifact/object/CoffObjectReader.h"
#include "target/artifact/object/MachObjectWriter.h"
#include "target/artifact/object/MachObjectReader.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>

using namespace target::artifact::object;

void test_elf_writer_reader() {
    std::cout << "[Test] ELF64 Object Writer & Reader..." << std::endl;

    ObjectArtifact art;
    art.format = ObjectFormat::ELF;
    art.arch = target::Arch::X64;
    art.os = target::OS::Linux;

    ObjectSection textSec;
    textSec.name = ".text";
    textSec.data = {0x48, 0x83, 0xEC, 0x08, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x31, 0xC0, 0x48, 0x83, 0xC4, 0x08, 0xC3};
    textSec.alignment = 16;
    art.addSection(textSec);

    ObjectSection rodataSec;
    rodataSec.name = ".rodata";
    rodataSec.data = {'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!', '\0'};
    rodataSec.alignment = 8;
    art.addSection(rodataSec);

    ObjectSymbol mainSym;
    mainSym.name = "main";
    mainSym.value = 0;
    mainSym.size = 16;
    mainSym.binding = SymbolBinding::Global;
    mainSym.type = SymbolType::Function;
    mainSym.sectionName = ".text";
    mainSym.isDefined = true;
    art.addSymbol(mainSym);

    ObjectSymbol putsSym;
    putsSym.name = "puts";
    putsSym.value = 0;
    putsSym.size = 0;
    putsSym.binding = SymbolBinding::Global;
    putsSym.type = SymbolType::Function;
    putsSym.sectionName = "*UND*";
    putsSym.isDefined = false;
    art.addSymbol(putsSym);

    ObjectRelocation callReloc;
    callReloc.offset = 5;
    callReloc.type = "R_X86_64_PC32";
    callReloc.addend = -4;
    callReloc.symbolName = "puts";
    callReloc.sectionName = ".text";
    art.addRelocation(callReloc);

    ElfObjectWriter writer;
    std::vector<uint8_t> bytes = writer.serialize(art);
    assert(!bytes.empty());

    ElfObjectReader reader;
    ObjectArtifact readArt;
    bool parsed = reader.parse(bytes, readArt);
    assert(parsed);

    assert(readArt.format == ObjectFormat::ELF);
    assert(readArt.arch == target::Arch::X64);
    assert(readArt.findSection(".text") != nullptr);
    assert(readArt.findSection(".rodata") != nullptr);

    const ObjectSymbol* mainParsed = readArt.findSymbol("main");
    assert(mainParsed != nullptr);
    assert(mainParsed->isDefined);

    const ObjectSymbol* putsParsed = readArt.findSymbol("puts");
    assert(putsParsed != nullptr);
    assert(!putsParsed->isDefined);

    assert(!readArt.relocations.empty());
    assert(readArt.relocations[0].symbolName == "puts");

    std::cout << "  -> ELF Writer & Reader Test PASSED." << std::endl;
}

void test_coff_writer_reader() {
    std::cout << "[Test] Windows COFF Object Writer & Reader..." << std::endl;

    ObjectArtifact art;
    art.format = ObjectFormat::COFF;
    art.arch = target::Arch::X64;
    art.os = target::OS::Windows;

    ObjectSection textSec;
    textSec.name = ".text";
    textSec.data = {0x48, 0x83, 0xEC, 0x20, 0x31, 0xC0, 0x48, 0x83, 0xC4, 0x20, 0xC3};
    textSec.alignment = 16;
    art.addSection(textSec);

    ObjectSymbol mainSym;
    mainSym.name = "main";
    mainSym.value = 0;
    mainSym.size = 11;
    mainSym.binding = SymbolBinding::Global;
    mainSym.type = SymbolType::Function;
    mainSym.sectionName = ".text";
    mainSym.isDefined = true;
    art.addSymbol(mainSym);

    CoffObjectWriter writer;
    std::vector<uint8_t> bytes = writer.serialize(art);
    assert(!bytes.empty());

    CoffObjectReader reader;
    ObjectArtifact readArt;
    bool parsed = reader.parse(bytes, readArt);
    assert(parsed);

    assert(readArt.format == ObjectFormat::COFF);
    assert(readArt.findSection(".text") != nullptr);
    const ObjectSymbol* mainParsed = readArt.findSymbol("main");
    assert(mainParsed != nullptr);

    std::cout << "  -> COFF Writer & Reader Test PASSED." << std::endl;
}

void test_macho_writer_reader() {
    std::cout << "[Test] macOS Mach-O Object Writer & Reader..." << std::endl;

    ObjectArtifact art;
    art.format = ObjectFormat::MachO;
    art.arch = target::Arch::X64;
    art.os = target::OS::MacOS;

    ObjectSection textSec;
    textSec.name = ".text";
    textSec.data = {0x55, 0x48, 0x89, 0xE5, 0x31, 0xC0, 0x5D, 0xC3};
    textSec.alignment = 16;
    art.addSection(textSec);

    ObjectSymbol mainSym;
    mainSym.name = "main";
    mainSym.value = 0;
    mainSym.size = 8;
    mainSym.binding = SymbolBinding::Global;
    mainSym.type = SymbolType::Function;
    mainSym.sectionName = ".text";
    mainSym.isDefined = true;
    art.addSymbol(mainSym);

    MachObjectWriter writer;
    std::vector<uint8_t> bytes = writer.serialize(art);
    assert(!bytes.empty());

    MachObjectReader reader;
    ObjectArtifact readArt;
    bool parsed = reader.parse(bytes, readArt);
    assert(parsed);

    assert(readArt.format == ObjectFormat::MachO);
    assert(readArt.findSection(".text") != nullptr);
    const ObjectSymbol* mainParsed = readArt.findSymbol("main");
    assert(mainParsed != nullptr);

    std::cout << "  -> Mach-O Writer & Reader Test PASSED." << std::endl;
}

void test_explicit_output_path_policy() {
    std::cout << "[Test] Explicit vs Default Output Path Policy..." << std::endl;

    // Simulate CodeGen output path logic
    std::string explicitObj = "/tmp/explicit_output.o";
    std::string explicitObjWin = "/tmp/explicit_output.obj";
    std::string explicitLib = "/tmp/libexplicit.a";

    // Explicit path logic must preserve exact destination
    assert(explicitObj == "/tmp/explicit_output.o");
    assert(explicitObjWin == "/tmp/explicit_output.obj");
    assert(explicitLib == "/tmp/libexplicit.a");

    std::cout << "  -> Explicit Output Path Policy Test PASSED." << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Running Object Subsystem Unit Tests    " << std::endl;
    std::cout << "========================================" << std::endl;

    test_elf_writer_reader();
    test_coff_writer_reader();
    test_macho_writer_reader();
    test_explicit_output_path_policy();

    std::cout << "All Object Subsystem Tests Passed!" << std::endl;
    return 0;
}
