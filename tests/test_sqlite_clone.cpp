#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <cassert>
#include "parser/Parser.h"
#include "codegen/CodeGen.h"
#include "codegen/target/SystemV_x64.h"
#include "../src/codegen/execgen/elf.hh"

using namespace ir;
using namespace codegen;
using namespace codegen::target;

int main() {
    std::string inputFile = "tests/sqlite_clone.fyra";
    std::ifstream inFile(inputFile);
    if (!inFile.is_open()) {
        std::cerr << "Error: could not open input file " << inputFile << std::endl;
        return 1;
    }

    parser::Parser p(inFile, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = p.parseModule();
    if (!module) {
        std::cerr << "Error: failed to parse module." << std::endl;
        return 1;
    }

    auto target = std::make_unique<SystemV_x64>();
    CodeGen cg(*module, std::move(target), nullptr);
    cg.emit(true);

    std::map<std::string, std::vector<uint8_t>> sections;
    sections[".text"] = cg.getAssembler().getCode();
    sections[".data"] = cg.getRodataAssembler().getCode();

    ElfGenerator elfGen(inputFile);
    elfGen.setMachine(62); // EM_X86_64
    elfGen.setBaseAddress(0x400000);

    std::vector<ElfGenerator::Symbol> symbols;
    for (const auto& sym_info : cg.getSymbols()) {
        symbols.push_back({sym_info.name, sym_info.value, sym_info.size,
                           (uint8_t)sym_info.type, (uint8_t)sym_info.binding, sym_info.sectionName});
    }

    std::vector<ElfGenerator::Relocation> relocations;
    for (const auto& reloc_info : cg.getRelocations()) {
        relocations.push_back({reloc_info.offset, reloc_info.type, reloc_info.addend,
                               reloc_info.symbolName, reloc_info.sectionName});
    }

    std::string outputPath = "./sqlite_clone_exec";
    if (!elfGen.generateFromCode(sections, symbols, relocations, outputPath)) {
        std::cerr << "Error generating ELF: " << elfGen.getLastError() << std::endl;
        return 1;
    }

    // Set executable permissions
    system("chmod +x ./sqlite_clone_exec");

    // Run the executable and check the return code
    int result = system("./sqlite_clone_exec");
    int exitCode = WEXITSTATUS(result);

    std::cout << "Application exited with code: " << exitCode << std::endl;

    if (exitCode == 59) {
        std::cout << "Execution result verified! (10 + 20 + 30 - 1 = 59)" << std::endl;
        return 0;
    } else {
        std::cerr << "Error: execution result mismatch. Expected 59, got " << exitCode << std::endl;
        return 1;
    }
}
