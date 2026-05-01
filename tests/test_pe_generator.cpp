#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "parser/Parser.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"
#include "target/artifact/executable/pe.hh"

int main() {
    std::ifstream input("tests/windows.fyra");
    if (!input.good()) {
        input.open("../tests/windows.fyra");
    }
    assert(input.good());

    parser::Parser parser(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = parser.parseModule();
    assert(module != nullptr);

    auto target = target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Windows});
    codegen::CodeGen cg(*module, std::move(target), nullptr);
    cg.emit(true);

    std::map<std::string, std::vector<uint8_t>> sections;
    sections[".text"] = cg.getAssembler().getCode();
    sections[".data"] = cg.getRodataAssembler().getCode();

    std::vector<PEGenerator::Symbol> symbols;
    for (const auto& s : cg.getSymbols()) {
        symbols.push_back({s.name, s.value, s.size, s.type, s.binding, s.sectionName});
    }

    std::vector<PEGenerator::Relocation> relocs;
    for (const auto& r : cg.getRelocations()) {
        relocs.push_back({r.offset, r.type, r.addend, r.symbolName, r.sectionName});
    }

    PEGenerator pe(true);
    const std::string out = "./test_windows_out.exe";
    bool ok = pe.generateFromCode(sections, symbols, relocs, out);
    assert(ok && "PE generation failed");

    std::ifstream exe(out, std::ios::binary);
    assert(exe.good());
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(exe)), std::istreambuf_iterator<char>());
    assert(data.size() > 0x100);

    // MZ
    assert(data[0] == 'M' && data[1] == 'Z');

    uint32_t lfanew = *reinterpret_cast<uint32_t*>(&data[0x3C]);
    assert(lfanew + 0x80 < data.size());

    // PE\0\0
    assert(data[lfanew] == 'P' && data[lfanew + 1] == 'E' && data[lfanew + 2] == 0 && data[lfanew + 3] == 0);

    // OptionalHeader64 AddressOfEntryPoint at +0x10 from optional header start
    uint32_t entryRva = *reinterpret_cast<uint32_t*>(&data[lfanew + 4 + 20 + 0x10]);
    assert(entryRva != 0);

    std::remove(out.c_str());
    std::cout << "PE generator structural test passed.\n";
    return 0;
}
