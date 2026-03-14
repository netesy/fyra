#include "parser/Parser.h"
#include "codegen/CodeGen.h"
#include "codegen/EnhancedCodeGen.h"
#include "codegen/execgen/elf.hh"
#include "codegen/execgen/pe.hh"
#include "codegen/execgen/macho.hh"
#include "codegen/target/SystemV_x64.h"
#include "codegen/target/Windows_x64.h"
#include "codegen/target/Windows_AArch64.h"
#include "codegen/target/MacOS_x64.h"
#include "codegen/target/MacOS_AArch64.h"
#include "codegen/target/AArch64.h"
#include "codegen/target/Wasm32.h"
#include "codegen/target/RiscV64.h"
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
        std::cerr << "  --target <linux|windows|windows-amd64|windows-arm64|aarch64|wasm32|riscv64>  Target platform (default: linux)" << std::endl;
        std::cerr << "  --validate                                       Enable ASM validation (default: enabled)" << std::endl;
        std::cerr << "  --no-validate                                    Disable ASM validation" << std::endl;
        std::cerr << "  --object                                         Generate object file" << std::endl;
        std::cerr << "  --enhanced                                       Use enhanced CodeGen with validation and object generation" << std::endl;
        std::cerr << "  --verbose                                        Enable verbose output" << std::endl;
        std::cerr << "  --pipeline                                       Run full compilation pipeline for all targets" << std::endl;
        std::cerr << "  --gen-exec                                       Generate an executable file (requires --enhanced)" << std::endl;
        std::cerr << "Supported input formats:" << std::endl;
        std::cerr << "  .fyra  - Fyra Intermediate Language format" << std::endl;
        std::cerr << "  .fy    - Fyra Intermediate Language format (alternative extension)" << std::endl;
        return 1;
    }

    std::string inputFile = argv[1];
    std::string outputFile = get_arg(argc, argv, "-o");
    std::string target = get_arg(argc, argv, "--target");
    
    // Parse command line options
    bool enableValidation = true;
    bool generateObject = false;
    bool useEnhanced = false;
    bool verboseOutput = false;
    bool runPipeline = false;
    bool generateExecutable = false;
    bool disableOptimizations = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--no-validate") {
            enableValidation = false;
        } else if (arg == "--validate") {
            enableValidation = true;
        } else if (arg == "--object") {
            generateObject = true;
        } else if (arg == "--enhanced") {
            useEnhanced = true;
        } else if (arg == "--verbose") {
            verboseOutput = true;
        } else if (arg == "--pipeline") {
            runPipeline = true;
            useEnhanced = true; // Pipeline requires enhanced CodeGen
        } else if (arg == "--gen-exec") {
            generateExecutable = true;
            useEnhanced = true; // Executable generation requires enhanced CodeGen
        } else if (arg == "-O0") {
            disableOptimizations = true;
        }
    }
    
    // Detect input file format
    parser::FileFormat format = detectFileFormat(inputFile);
    std::string formatName;
    if (format == parser::FileFormat::FYRA) {
        formatName = "Fyra (.fyra)";
    } else {
        formatName = "Fyra (.fy)";
    }

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

    // 2. Run Enhanced SSA and Optimization Pipeline
    std::cout << "--- Running Enhanced Optimization Pipeline... ---\n" << std::flush;
    
    // Create shared error reporter for all passes
    auto error_reporter = std::make_shared<transforms::ErrorReporter>(std::cerr, false);
    
    for (auto& func : module->getFunctions()) {
        // Phase 1: SSA Construction (using legacy interface for compatibility)
        transforms::CFGBuilder::run(*func);
        transforms::DominatorTree domTree;
        domTree.run(*func);
        transforms::DominanceFrontier domFrontier;
        domFrontier.run(*func, domTree);
        transforms::PhiInsertion phiInserter;
        phiInserter.run(*func, domFrontier);
        transforms::SSARenamer ssaRenamer;
        ssaRenamer.run(*func, domTree);
        
        if (target != "wasm32") {
            transforms::Mem2Reg mem2reg;
            mem2reg.run(*func);
        } else {
            // Even for WASM, we should run Mem2Reg if we want SSA.
            // But we must ensure it doesn't break our assumptions.
            // Let's re-enable it and see.
            transforms::Mem2Reg mem2reg;
            mem2reg.run(*func);
        }

        if (disableOptimizations) continue;

        // Phase 2: Enhanced Optimization Passes
        // Create enhanced passes with shared error reporter
        transforms::SCCP enhanced_sccp(error_reporter);
        transforms::ControlFlowSimplification cfg_simplifier(error_reporter);
        transforms::DeadInstructionElimination enhanced_dce(error_reporter);
        
        // Run optimization passes in optimal order
        bool optimization_changed = true;
        int iteration = 1;
        
        while (optimization_changed && iteration <= 3) { // Limit iterations to prevent infinite loops
            optimization_changed = false;
            
            if (verboseOutput) {
                std::cout << "  Optimization iteration " << iteration << " on function: " << func->getName() << std::endl;
            }
            
            // Constant propagation and folding (should run first)
            if (verboseOutput) std::cout << "  Running SCCP..." << std::endl;
            if (enhanced_sccp.run(*func)) {
                optimization_changed = true;
            }

            // Control flow simplification (after constant propagation)
            if (verboseOutput) std::cout << "  Running CFG Simplifier..." << std::endl;
            if (cfg_simplifier.run(*func)) {
                optimization_changed = true;
            }

            // Dead code elimination (should run last to clean up)
            if (verboseOutput) std::cout << "  Running DCE..." << std::endl;
            if (enhanced_dce.run(*func)) {
                optimization_changed = true;
            }
            
            iteration++;
        }
    }
    
    // Print optimization summary
    if (error_reporter->hasErrors()) {
        std::cout << "--- Optimization completed with errors ---\n" << std::flush;
        error_reporter->printSummary();
    } else if (error_reporter->hasWarnings()) {
        std::cout << "--- Optimization completed with warnings ---\n" << std::flush;
    } else {
        std::cout << "--- Enhanced Optimization Pipeline complete ---\n" << std::flush;
    }
    
    // Continue with register allocation if no critical errors
    if (!error_reporter->hasCriticalErrors()) {
        // 3. Run Register Allocation (skip for WASM)
        if (target != "wasm32") {
            std::cout << "--- Running Register Allocation... ---\n" << std::flush;
            for (auto& func : module->getFunctions()) {
                transforms::RegAllocRewriter rewriter;
                rewriter.run(*func);
            }
            std::cout << "--- Register Allocation complete. ---\n" << std::flush;
        } else {
            std::cout << "--- Skipping Register Allocation for WASM target. ---\n" << std::flush;
        }
    } else {
        std::cerr << "Critical errors detected during optimization. Skipping register allocation." << std::endl;
        return 1;
    }

    // 4. Generate code
    std::string targetName = target.empty() ? "linux" : target;
    
    if (runPipeline) {
        // Use comprehensive pipeline for all targets
        std::cout << "--- Running Comprehensive Compilation Pipeline ---\n" << std::flush;
        
        codegen::CodeGenPipeline pipeline;
        codegen::CodeGenPipeline::PipelineConfig config;
        config.enableValidation = enableValidation;
        config.enableObjectGeneration = generateObject;
        config.enableVerboseOutput = verboseOutput;
        config.outputPrefix = outputFile.substr(0, outputFile.find_last_of('.'));
        
        if (!target.empty()) {
            config.targetPlatforms = {target};
        }
        
        auto pipelineResult = pipeline.execute(*module, config);
        
        if (pipelineResult.success) {
            std::cout << "Pipeline completed successfully for " << pipelineResult.getSuccessfulTargets() 
                     << " targets in " << pipelineResult.totalTimeMs << "ms" << std::endl;
        } else {
            std::cerr << "Pipeline failed. " << pipelineResult.getFailedTargets() << " targets failed." << std::endl;
            for (const auto& error : pipelineResult.errors) {
                std::cerr << "Pipeline Error: " << error << std::endl;
            }
            return 1;
        }
        
    } else if (useEnhanced) {
        // Use enhanced CodeGen with validation and object generation
        std::cout << "--- Using Enhanced CodeGen with Validation ---\n" << std::flush;
        
        auto enhancedCodeGen = codegen::EnhancedCodeGenFactory::create(*module, targetName);
        if (!enhancedCodeGen) {
            std::cerr << "Error: Failed to create enhanced code generator for target: " << targetName << std::endl;
            return 1;
        }
        
        enhancedCodeGen->enableVerboseOutput(verboseOutput);
        
        std::string outputPrefix = outputFile.substr(0, outputFile.find_last_of('.'));
        if (generateExecutable) {
            std::cout << "--- Generating Executable (In-Memory) ---\n" << std::flush;
            std::string executablePath = outputFile;
            if (executablePath.find_last_of('.') != std::string::npos) {
                 executablePath = executablePath.substr(0, executablePath.find_last_of('.'));
            }

            // Select target for in-memory generation
            std::unique_ptr<codegen::target::TargetInfo> targetInfo;
            bool isWindows = (targetName == "windows" || targetName == "windows-amd64" || targetName == "win64" || targetName == "windows-x64" || targetName == "windows-arm64");
            bool isMacOS = (targetName == "macos" || targetName == "darwin" || targetName == "macos-aarch64" || targetName == "macos-arm64");

            if (targetName == "linux") {
                targetInfo = std::make_unique<codegen::target::SystemV_x64>();
            } else if (targetName == "riscv64") {
                targetInfo = std::make_unique<codegen::target::RiscV64>();
            } else if (targetName == "aarch64") {
                targetInfo = std::make_unique<codegen::target::AArch64>();
            } else if (isWindows) {
                if (targetName == "windows-arm64") {
                    targetInfo = std::make_unique<codegen::target::Windows_AArch64>();
                } else {
                    targetInfo = std::make_unique<codegen::target::Windows_x64>();
                }
            } else if (isMacOS) {
                if (targetName == "macos-aarch64" || targetName == "macos-arm64") {
                    targetInfo = std::make_unique<codegen::target::MacOS_AArch64>();
                } else {
                    targetInfo = std::make_unique<codegen::target::MacOS_x64>();
                }
            } else {
                std::cerr << "Error: In-memory executable generation is currently not supported for target: " << targetName << std::endl;
                return 1;
            }

            // Create a CodeGen instance for binary-only output
            codegen::CodeGen codeGenerator(*module, std::move(targetInfo), nullptr);

            // Emit machine code into the in-memory assembler
            codeGenerator.emit(true);

            // Prepare section data
            std::map<std::string, std::vector<uint8_t>> sections;
            sections[".text"] = codeGenerator.getAssembler().getCode();
            sections[".rodata"] = codeGenerator.getRodataAssembler().getCode();

            if (isWindows) {
                PEGenerator peGen(true);
                if (targetName == "windows-arm64") {
                    peGen.setMachine(IMAGE_FILE_MACHINE_ARM64);
                }
                std::vector<PEGenerator::Symbol> symbols;
                for (const auto& sym : codeGenerator.getSymbols()) {
                    symbols.push_back({sym.name, sym.value, sym.size, sym.type, sym.binding, sym.sectionName});
                }
                std::vector<PEGenerator::Relocation> relocs;
                for (const auto& reloc : codeGenerator.getRelocations()) {
                    relocs.push_back({reloc.offset, reloc.type, reloc.addend, reloc.symbolName, reloc.sectionName});
                }
                if (peGen.generateFromCode(sections, symbols, relocs, executablePath + ".exe")) {
                    std::cout << "PE Executable generated successfully: " << executablePath << ".exe" << std::endl;
                } else {
                    std::cerr << "Error generating PE: " << peGen.getLastError() << std::endl;
                    return 1;
                }
            } else if (isMacOS) {
                MachOGenerator machoGen(inputFile);
                if (targetName == "macos-aarch64" || targetName == "macos-arm64") {
                    machoGen.setCpuType(0x0100000c); // CPU_TYPE_ARM64
                }
                std::vector<MachOGenerator::Symbol> symbols;
                for (const auto& sym : codeGenerator.getSymbols()) {
                    symbols.push_back({sym.name, sym.value, sym.size, sym.type, sym.binding, sym.sectionName});
                }
                std::vector<MachOGenerator::Relocation> relocs;
                for (const auto& reloc : codeGenerator.getRelocations()) {
                    relocs.push_back({reloc.offset, reloc.type, reloc.addend, reloc.symbolName, reloc.sectionName});
                }
                if (machoGen.generateFromCode(sections, symbols, relocs, executablePath)) {
                    std::cout << "Mach-O Executable generated successfully: " << executablePath << std::endl;
                } else {
                    std::cerr << "Error generating Mach-O: " << machoGen.getLastError() << std::endl;
                    return 1;
                }
            } else {
                ElfGenerator elfGen(inputFile);
                if (targetName == "linux") elfGen.setMachine(62); // EM_X86_64
                else if (targetName == "riscv64") elfGen.setMachine(243); // EM_RISCV
                else if (targetName == "aarch64") elfGen.setMachine(183); // EM_AARCH64

                std::vector<ElfGenerator::Symbol> symbols;
                for (const auto& sym_info : codeGenerator.getSymbols()) {
                    symbols.push_back({sym_info.name, sym_info.value, sym_info.size,
                                       sym_info.type, sym_info.binding, sym_info.sectionName});
                }
                std::vector<ElfGenerator::Relocation> relocations;
                for (const auto& reloc_info : codeGenerator.getRelocations()) {
                    relocations.push_back({reloc_info.offset, reloc_info.type, reloc_info.addend,
                                           reloc_info.symbolName, reloc_info.sectionName});
                }

                if (elfGen.generateFromCode(sections, symbols, relocations, executablePath)) {
                    std::cout << "Executable generated successfully: " << executablePath << std::endl;
                } else {
                    std::cerr << "Error generating executable from in-memory code: " << elfGen.getLastError() << std::endl;
                    return 1;
                }
            }
        } else {
            auto enhancedCodeGen = codegen::EnhancedCodeGenFactory::create(*module, targetName);
            if (!enhancedCodeGen) {
                std::cerr << "Error: Failed to create enhanced code generator for target: " << targetName << std::endl;
                return 1;
            }
            
            enhancedCodeGen->enableVerboseOutput(verboseOutput);

            std::string outputPrefix = outputFile.substr(0, outputFile.find_last_of('.'));
            auto result = enhancedCodeGen->compileToObject(outputPrefix, enableValidation, generateObject, false);

            if (result.success) {
                std::cout << "Enhanced compilation successful in " << result.totalTimeMs << "ms" << std::endl;
                std::cout << "Assembly: " << result.assemblyPath << std::endl;
                if (generateObject && !result.objectPath.empty()) {
                    std::cout << "Object: " << result.objectPath << std::endl;
                }
                
                // Print validation summary
                if (enableValidation) {
                    std::cout << "Validation: " << result.validation.errors.size() << " errors, "
                                << result.validation.warnings.size() << " warnings" << std::endl;

                    if (verboseOutput && !result.validation.errors.empty()) {
                        std::cout << "Validation Errors:" << std::endl;
                        for (const auto& error : result.validation.errors) {
                            std::cout << "  [" << error.category << "] " << error.message << std::endl;
                        }
                    }
                }
            } else {
                std::cerr << "Enhanced compilation failed" << std::endl;
                auto errors = result.getAllErrors();
                for (const auto& error : errors) {
                    std::cerr << "Error: " << error << std::endl;
                }
                return 1;
            }
        }
        
    } else {
        // Use legacy CodeGen
        std::cout << "--- Generating code for target: " << targetName << " (legacy mode) ---\n" << std::flush;
        
        // Select target
        std::unique_ptr<codegen::target::TargetInfo> targetInfo;
        if (target == "windows" || target == "windows-amd64" || target == "windows-x64") {
            targetInfo = std::make_unique<codegen::target::Windows_x64>();
        } else if (target == "windows-arm64") {
            targetInfo = std::make_unique<codegen::target::Windows_AArch64>();
        } else if (target == "aarch64") {
            targetInfo = std::make_unique<codegen::target::AArch64>();
        } else if (target == "wasm32") {
            targetInfo = std::make_unique<codegen::target::Wasm32>();
        } else if (target == "riscv64") {
            targetInfo = std::make_unique<codegen::target::RiscV64>();
        } else if (target == "macos" || target == "macos-x64") {
            targetInfo = std::make_unique<codegen::target::MacOS_x64>();
        } else if (target == "macos-aarch64" || target == "macos-arm64") {
            targetInfo = std::make_unique<codegen::target::MacOS_AArch64>();
        } else {
            // Default to Linux
            targetInfo = std::make_unique<codegen::target::SystemV_x64>();
        }
        
        std::ofstream outFile(outputFile);
        if (!outFile.is_open()) {
            std::cerr << "Error: could not open output file " << outputFile << std::endl;
            return 1;
        }

        codegen::CodeGen codeGenerator(*module, std::move(targetInfo), &outFile);
        codeGenerator.emit(false);
        std::cout << "Compilation successful. Assembly written to " << outputFile << std::endl;
    }

    return 0;
}
