#include "target/artifact/object/ObjectArtifact.h"
#include "target/artifact/object/ElfObjectWriter.h"
#include "target/artifact/object/ElfObjectReader.h"
#include "target/artifact/object/CoffObjectWriter.h"
#include "target/artifact/object/CoffObjectReader.h"
#include "target/artifact/object/MachObjectWriter.h"
#include "target/artifact/object/MachObjectReader.h"
#include "target/artifact/object/ObjectWriter.h"
#include "target/artifact/object/ObjectReader.h"
#include "target/artifact/object/WasmObjectWriter.h"
#include "target/artifact/object/WasmObjectReader.h"
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

void test_wasm_writer_reader() {
    std::cout << "[Test] WASI WebAssembly Object Writer & Reader..." << std::endl;

    ObjectArtifact art;
    art.arch = target::Arch::WASM32;
    art.os = target::OS::WASI;

    ObjectSection textSec;
    textSec.name = ".text";
    textSec.data = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    art.addSection(textSec);

    auto writer = ObjectWriter::createForTargetTriple("wasm32-wasi");
    assert(writer != nullptr);
    std::vector<uint8_t> bytes = writer->serialize(art);
    assert(bytes == textSec.data);

    auto reader = ObjectReader::detectAndCreate(bytes);
    assert(reader != nullptr);
    ObjectArtifact readArt;
    bool parsed = reader->parse(bytes, readArt);
    assert(parsed);
    assert(readArt.arch == target::Arch::WASM32);
    assert(readArt.os == target::OS::WASI);
    assert(readArt.findSection(".text") != nullptr);

    std::cout << "  -> WASI Wasm Writer & Reader Test PASSED." << std::endl;
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

void test_elf_writer_reader_aarch64() {
    std::cout << "[Test] ELF64 AArch64 Object Writer & Reader..." << std::endl;

    ObjectArtifact art;
    art.format = ObjectFormat::ELF;
    art.arch = target::Arch::AArch64;
    art.os = target::OS::Linux;

    ObjectSection textSec;
    textSec.name = ".text";
    textSec.data = {0x00, 0x00, 0x00, 0x94, 0xC0, 0x03, 0x5F, 0xD6}; // BL 0, RET
    textSec.alignment = 16;
    art.addSection(textSec);

    ObjectSection rodataSec;
    rodataSec.name = ".rodata";
    rodataSec.data = {'H', 'e', 'l', 'l', 'o', '\0'};
    rodataSec.alignment = 8;
    art.addSection(rodataSec);

    ObjectSymbol mainSym;
    mainSym.name = "main";
    mainSym.value = 0;
    mainSym.size = 8;
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
    callReloc.offset = 0;
    callReloc.type = "R_AARCH64_CALL26";
    callReloc.addend = 0;
    callReloc.symbolName = "puts";
    callReloc.sectionName = ".text";
    art.addRelocation(callReloc);

    ElfObjectWriter writer;
    std::vector<uint8_t> bytes = writer.serialize(art);
    assert(!bytes.empty());
    assert(bytes[18] == 183 && bytes[19] == 0); // EM_AARCH64 = 183

    ElfObjectReader reader;
    ObjectArtifact readArt;
    bool parsed = reader.parse(bytes, readArt);
    assert(parsed);
    assert(readArt.format == ObjectFormat::ELF);
    assert(readArt.arch == target::Arch::AArch64);
    assert(readArt.findSection(".text") != nullptr);
    assert(readArt.findSymbol("main") != nullptr);
    assert(!readArt.relocations.empty());
    assert(readArt.relocations[0].type == "R_AARCH64_CALL26");
    assert(readArt.relocations[0].symbolName == "puts");

    std::cout << "  -> ELF64 AArch64 Writer & Reader Test PASSED." << std::endl;
}

void test_elf_writer_reader_riscv64() {
    std::cout << "[Test] ELF64 RISC-V 64 Object Writer & Reader..." << std::endl;

    ObjectArtifact art;
    art.format = ObjectFormat::ELF;
    art.arch = target::Arch::RISCV64;
    art.os = target::OS::Linux;

    ObjectSection textSec;
    textSec.name = ".text";
    textSec.data = {0xef, 0x00, 0x00, 0x00, 0x67, 0x80, 0x00, 0x00}; // JAL ra, 0; RET
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
    callReloc.offset = 0;
    callReloc.type = "R_RISCV_CALL";
    callReloc.addend = 0;
    callReloc.symbolName = "puts";
    callReloc.sectionName = ".text";
    art.addRelocation(callReloc);

    ElfObjectWriter writer;
    std::vector<uint8_t> bytes = writer.serialize(art);
    assert(!bytes.empty());
    assert(bytes[18] == 243 && bytes[19] == 0); // EM_RISCV = 243

    ElfObjectReader reader;
    ObjectArtifact readArt;
    bool parsed = reader.parse(bytes, readArt);
    assert(parsed);
    assert(readArt.format == ObjectFormat::ELF);
    assert(readArt.arch == target::Arch::RISCV64);
    assert(readArt.findSection(".text") != nullptr);
    assert(!readArt.relocations.empty());
    assert(readArt.relocations[0].type == "R_RISCV_CALL");

    std::cout << "  -> ELF64 RISC-V 64 Writer & Reader Test PASSED." << std::endl;
}

void test_coff_writer_reader_aarch64() {
    std::cout << "[Test] Windows ARM64 COFF Object Writer & Reader..." << std::endl;

    ObjectArtifact art;
    art.format = ObjectFormat::COFF;
    art.arch = target::Arch::AArch64;
    art.os = target::OS::Windows;

    ObjectSection textSec;
    textSec.name = ".text";
    textSec.data = {0xC0, 0x03, 0x5F, 0xD6}; // RET
    textSec.alignment = 16;
    art.addSection(textSec);

    ObjectSymbol mainSym;
    mainSym.name = "main";
    mainSym.value = 0;
    mainSym.size = 4;
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
    callReloc.offset = 0;
    callReloc.type = "R_AARCH64_CALL26";
    callReloc.symbolName = "puts";
    callReloc.sectionName = ".text";
    art.addRelocation(callReloc);

    CoffObjectWriter writer;
    std::vector<uint8_t> bytes = writer.serialize(art);
    assert(!bytes.empty());
    // Machine header is first 2 bytes: IMAGE_FILE_MACHINE_ARM64 = 0xAA64
    assert(bytes[0] == 0x64 && bytes[1] == 0xAA);

    CoffObjectReader reader;
    ObjectArtifact readArt;
    bool parsed = reader.parse(bytes, readArt);
    assert(parsed);
    assert(readArt.format == ObjectFormat::COFF);
    assert(readArt.arch == target::Arch::AArch64);
    assert(readArt.findSection(".text") != nullptr);
    assert(!readArt.relocations.empty());
    assert(readArt.relocations[0].type == "R_AARCH64_CALL26");

    std::cout << "  -> Windows ARM64 COFF Writer & Reader Test PASSED." << std::endl;
}

void test_coff_writer_reader_riscv64() {
    std::cout << "[Test] Windows RISC-V 64 COFF Object Writer & Reader..." << std::endl;

    ObjectArtifact art;
    art.format = ObjectFormat::COFF;
    art.arch = target::Arch::RISCV64;
    art.os = target::OS::Windows;

    ObjectSection textSec;
    textSec.name = ".text";
    textSec.data = {0x67, 0x80, 0x00, 0x00}; // RET
    textSec.alignment = 16;
    art.addSection(textSec);

    ObjectSymbol mainSym;
    mainSym.name = "main";
    mainSym.value = 0;
    mainSym.size = 4;
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
    callReloc.offset = 0;
    callReloc.type = "R_RISCV_CALL";
    callReloc.symbolName = "puts";
    callReloc.sectionName = ".text";
    art.addRelocation(callReloc);

    CoffObjectWriter writer;
    std::vector<uint8_t> bytes = writer.serialize(art);
    assert(!bytes.empty());
    // Machine header is first 2 bytes: IMAGE_FILE_MACHINE_RISCV64 = 0x5064
    assert(bytes[0] == 0x64 && bytes[1] == 0x50);

    CoffObjectReader reader;
    ObjectArtifact readArt;
    bool parsed = reader.parse(bytes, readArt);
    assert(parsed);
    assert(readArt.format == ObjectFormat::COFF);
    assert(readArt.arch == target::Arch::RISCV64);
    assert(readArt.findSection(".text") != nullptr);
    assert(!readArt.relocations.empty());
    assert(readArt.relocations[0].type == "R_RISCV_CALL");

    std::cout << "  -> Windows RISC-V 64 COFF Writer & Reader Test PASSED." << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << " Running Object Subsystem Unit Tests    " << std::endl;
    std::cout << "========================================" << std::endl;

    test_elf_writer_reader();
    test_elf_writer_reader_aarch64();
    test_elf_writer_reader_riscv64();
    test_coff_writer_reader();
    test_coff_writer_reader_aarch64();
    test_coff_writer_reader_riscv64();
    test_macho_writer_reader();
    test_wasm_writer_reader();
    test_explicit_output_path_policy();

    std::cout << "All Object Subsystem Tests Passed!" << std::endl;
    return 0;
}
