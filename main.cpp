#include "parser/Parser.h"
#include "codegen/CodeGen.h"
#include "codegen/execgen/elf.hh"
#include "codegen/execgen/pe.hh"
#include "codegen/execgen/macho.hh"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "transforms/CFGBuilder.h"
#include "transforms/DominatorTree.h"
#include "transforms/DominanceFrontier.h"
#include "transforms/PhiInsertion.h"
#include "transforms/SSARenamer.h"
#include "transforms/Mem2Reg.h"
#include "transforms/CopyElimination.h"
#include "transforms/GVN.h"
#include "transforms/SCCP.h"
#include "transforms/DeadInstructionElimination.h"
#include "transforms/LoopInvariantCodeMotion.h"
#include "transforms/ControlFlowSimplification.h"
#include "transforms/ErrorReporter.h"
#include "transforms/RegAllocRewriter.h"
#include "transforms/ABIAnalysis.h"
#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <algorithm>

// File format detection is handled by the parser

// Helper function to get file extension
std::string getFileExtension(const std::string& filename) {
    size_t pos = filename.find_last_of('.');
    if (pos == std::string::npos) {
        return "";
    }
    std::string ext = filename.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

// Detect file format based on extension
parser::FileFormat detectFileFormat(const std::string& filename) {
    std::string ext = getFileExtension(filename);
    if (ext == ".fyra") {
        return parser::FileFormat::FYRA;
    } else if (ext == ".fy") {
        return parser::FileFormat::FY;
    } else {
        throw std::runtime_error("Unknown file extension: " + ext);
    }
}

// A simple CLI parser for now
std::string get_arg(int argc, char** argv, const std::string& arg) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == arg && i + 1 < argc) {
            return std::string(argv[i + 1]);
        }
    }
    return "";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.fyra|input.fy> -o <output.s> [options]" << std::endl;
        std::cerr << "Options:" << std::endl;
        std::cerr << "  --target <triple>                                Target triple (e.g., x64-linux-bin, aarch64-macos-executable)" << std::endl;
        std::cerr << "  -O0                                              Disable optimizations" << std::endl;
        std::cerr << "  -O1                                              Enable conservative optimizations" << std::endl;
        std::cerr << "  -O2                                              Enable full optimization pipeline (default)" << std::endl;
        std::cerr << "  --validate                                       Enable ASM validation (default: enabled)" << std::endl;
        std::cerr << "  --no-validate                                    Disable ASM validation" << std::endl;
        std::cerr << "  --object                                         Generate object file" << std::endl;
        std::cerr << "  --verbose                                        Enable verbose output" << std::endl;
        std::cerr << "  --pipeline                                       Run full compilation pipeline for all targets" << std::endl;
        std::cerr << "  --gen-exec                                       Generate an executable file" << std::endl;
        std::cerr << "Supported input formats:" << std::endl;
        std::cerr << "  .fyra  - Fyra Intermediate Language format" << std::endl;
        std::cerr << "  .fy    - Fyra Intermediate Language format (alternative extension)" << std::endl;
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputFile = get_arg(argc, argv, "-o");
    std::string targetTriple = get_arg(argc, argv, "--target");
    if (targetTriple.empty()) targetTriple = "x64-linux-bin"; // Default

    int optimizationLevel = 2;

    // Parse command line options
    bool enableValidation = true;
    bool generateObject = false;
    bool verboseOutput = false;
    bool runPipeline = false;
    bool generateExecutable = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--no-validate") {
            enableValidation = false;
        } else if (arg == "--validate") {
            enableValidation = true;
        } else if (arg == "--object") {
            generateObject = true;
        } else if (arg == "--verbose") {
            verboseOutput = true;
        } else if (arg == "--pipeline") {
            runPipeline = true;
        } else if (arg == "--gen-exec") {
            generateExecutable = true;
        } else if (arg == "-O0") {
            optimizationLevel = 0;
        } else if (arg == "-O1") {
            optimizationLevel = 1;
        } else if (arg == "-O2") {
            optimizationLevel = 2;
        }
    }
    
    // Resolve target
    auto desc = target::TargetDescriptor::fromString(targetTriple);
    if (!desc) {
        // Compatibility with old target names
        if (targetTriple == "linux") targetTriple = "x64-linux-bin";
        else if (targetTriple == "windows" || targetTriple == "windows-amd64") targetTriple = "x64-windows-bin";
        else if (targetTriple == "windows-arm64") targetTriple = "aarch64-windows-bin";
        else if (targetTriple == "aarch64") targetTriple = "aarch64-linux-bin";
        else if (targetTriple == "wasm32") targetTriple = "wasm32-wasi-wasm";
        else if (targetTriple == "riscv64") targetTriple = "riscv64-linux-bin";

        desc = target::TargetDescriptor::fromString(targetTriple);
        if (!desc) {
            std::cerr << "Error: invalid target triple: " << targetTriple << std::endl;
            return 1;
        }
    }

    // Detect input file format
    parser::FileFormat format = detectFileFormat(inputFile);
    std::string formatName = (format == parser::FileFormat::FYRA) ? "Fyra (.fyra)" : "Fyra (.fy)";

    if (outputFile.empty()) {
        std::cerr << "Error: missing output file (-o <output.s>)" << std::endl;
        return 1;
    }

    std::ifstream inFile(inputFile);
    if (!inFile.is_open()) {
        std::cerr << "Error: could not open input file " << inputFile << std::endl;
        return 1;
    }

    // 1. Parse the input file
    std::cout << "--- Parsing " << formatName << " input file: " << inputFile << " ---\n" << std::flush;
    parser::Parser p(inFile, static_cast<parser::FileFormat>(format));
    std::unique_ptr<ir::Module> module = p.parseModule();
    if (!module) {
        std::cerr << "Error: failed to parse module." << std::endl;
        return 1;
    }
    std::cout << "--- Parsing complete. ---\n" << std::flush;

    // 2. Run Optimization Pipeline
    std::cout << "--- Running Optimization Pipeline (-O" << optimizationLevel << ") ---\n" << std::flush;
    auto error_reporter = std::make_shared<transforms::ErrorReporter>(std::cerr, false);
    
    for (auto& func : module->getFunctions()) {
        transforms::CFGBuilder::run(*func);
        transforms::DominatorTree domTree; domTree.run(*func);
        transforms::DominanceFrontier domFrontier; domFrontier.run(*func, domTree);
        transforms::PhiInsertion phiInserter; phiInserter.run(*func, domFrontier);
        transforms::SSARenamer ssaRenamer; ssaRenamer.run(*func, domTree);
        
        transforms::Mem2Reg mem2reg;
        mem2reg.run(*func);

        if (optimizationLevel == 0) continue;

        transforms::SCCP enhanced_sccp(error_reporter);
        transforms::ControlFlowSimplification cfg_simplifier(error_reporter);
        transforms::DeadInstructionElimination enhanced_dce(error_reporter);
        transforms::CopyElimination copy_elim;
        transforms::GVN gvn;
        transforms::LoopInvariantCodeMotion licm(error_reporter);
        
        bool optimization_changed = true;
        int iteration = 1;
        const int maxIterations = (optimizationLevel >= 2) ? 5 : 2;
        bool isWasm = (desc->arch == target::Arch::WASM32);

        if (!isWasm) {
            while (optimization_changed && iteration <= maxIterations) {
                optimization_changed = false;
                if (enhanced_sccp.run(*func)) optimization_changed = true;
                if (copy_elim.run(*func)) optimization_changed = true;
                if (gvn.run(*func)) optimization_changed = true;
                if (cfg_simplifier.run(*func)) optimization_changed = true;
                if (optimizationLevel >= 2 && licm.run(*func)) optimization_changed = true;
                if (enhanced_dce.run(*func)) optimization_changed = true;
                iteration++;
            }
        }
    }
    
    if (error_reporter->hasErrors()) {
        std::cout << "--- Optimization completed with errors ---\n" << std::flush;
        error_reporter->printSummary();
    } else {
        std::cout << "--- Optimization Pipeline complete ---\n" << std::flush;
    }
    
    if (!error_reporter->hasCriticalErrors()) {
        if (desc->arch != target::Arch::WASM32) {
            std::cout << "--- Running Register Allocation... ---\n" << std::flush;
            for (auto& func : module->getFunctions()) { transforms::RegAllocRewriter rewriter; rewriter.run(*func); }
            std::cout << "--- Register Allocation complete. ---\n" << std::flush;
        }
    } else {
        std::cerr << "Critical errors detected during optimization. Skipping register allocation." << std::endl;
        return 1;
    }

    // 3. Generate code
    std::cout << "--- Target: " << desc->toString() << " ---\n" << std::flush;
    auto targetInfo = codegen::target::TargetResolver::resolve(*desc);
    
    if (generateExecutable) {
        std::cout << "--- Generating Executable (In-Memory) ---\n" << std::flush;
        codegen::CodeGen codeGenerator(*module, std::move(targetInfo), nullptr);
        codeGenerator.emit(true);

        std::map<std::string, std::vector<uint8_t>> sections;
        sections[".text"] = codeGenerator.getAssembler().getCode();
        sections[".rodata"] = codeGenerator.getRodataAssembler().getCode();

        if (desc->os == target::OS::Windows) {
            PEGenerator peGen(true);
            if (desc->arch == target::Arch::AArch64) peGen.setMachine(IMAGE_FILE_MACHINE_ARM64);
            std::vector<PEGenerator::Symbol> symbols;
            for (const auto& sym : codeGenerator.getSymbols()) symbols.push_back({sym.name, sym.value, sym.size, sym.type, sym.binding, sym.sectionName});
            std::vector<PEGenerator::Relocation> relocs;
            for (const auto& reloc : codeGenerator.getRelocations()) relocs.push_back({reloc.offset, reloc.type, reloc.addend, reloc.symbolName, reloc.sectionName});
            if (peGen.generateFromCode(sections, symbols, relocs, outputFile + ".exe")) std::cout << "PE Executable generated successfully: " << outputFile << ".exe" << std::endl;
            else { std::cerr << "Error generating PE: " << peGen.getLastError() << std::endl; return 1; }
        } else if (desc->os == target::OS::MacOS) {
            MachOGenerator machoGen(inputFile);
            if (desc->arch == target::Arch::AArch64) machoGen.setCpuType(0x0100000c);
            std::vector<MachOGenerator::Symbol> symbols;
            for (const auto& sym : codeGenerator.getSymbols()) symbols.push_back({sym.name, sym.value, sym.size, sym.type, sym.binding, sym.sectionName});
            std::vector<MachOGenerator::Relocation> relocs;
            for (const auto& reloc : codeGenerator.getRelocations()) relocs.push_back({reloc.offset, reloc.type, reloc.addend, reloc.symbolName, reloc.sectionName});
            if (machoGen.generateFromCode(sections, symbols, relocs, outputFile)) std::cout << "Mach-O Executable generated successfully: " << outputFile << std::endl;
            else { std::cerr << "Error generating Mach-O: " << machoGen.getLastError() << std::endl; return 1; }
        } else {
            ElfGenerator elfGen(inputFile);
            if (desc->arch == target::Arch::X64) elfGen.setMachine(62);
            else if (desc->arch == target::Arch::RISCV64) elfGen.setMachine(243);
            else if (desc->arch == target::Arch::AArch64) elfGen.setMachine(183);
            std::vector<ElfGenerator::Symbol> symbols;
            for (const auto& sym : codeGenerator.getSymbols()) symbols.push_back({sym.name, sym.value, sym.size, sym.type, sym.binding, sym.sectionName});
            std::vector<ElfGenerator::Relocation> relocations;
            for (const auto& reloc : codeGenerator.getRelocations()) relocations.push_back({reloc.offset, reloc.type, reloc.addend, reloc.symbolName, reloc.sectionName});
            if (elfGen.generateFromCode(sections, symbols, relocations, outputFile)) std::cout << "Executable generated successfully: " << outputFile << std::endl;
            else { std::cerr << "Error generating executable: " << elfGen.getLastError() << std::endl; return 1; }
        }
    } else {
        std::string outputPrefix = outputFile.substr(0, outputFile.find_last_of('.'));
        codegen::CodeGen codeGen(*module, std::move(targetInfo), nullptr);
        codeGen.enableVerboseOutput(verboseOutput);

        auto result = codeGen.compileToObject(outputPrefix, enableValidation, generateObject, false);
        if (result.success) {
            std::cout << "Compilation successful in " << result.totalTimeMs << "ms" << std::endl;
            std::cout << "Assembly: " << result.assemblyPath << std::endl;
            if (generateObject && !result.objectPath.empty()) std::cout << "Object: " << result.objectPath << std::endl;
        } else {
            std::cerr << "Compilation failed" << std::endl;
            for (const auto& error : result.getAllErrors()) std::cerr << "Error: " << error << std::endl;
            return 1;
        }
    }

    return 0;
}
