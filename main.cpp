#include "parser/Parser.h"
#include "codegen/CodeGen.h"
#include "ir/Validator.h"
#include "target/artifact/executable/ElfImage.h"
#include "target/artifact/executable/MachOImage.h"
#include "target/artifact/object/ObjectReader.h"
#include "target/artifact/object/ObjectArtifact.h"
#include "target/artifact/linker/InternalLinker.h"
#include "target/artifact/linker/TargetDynamicImageBuilder.h"
#include "target/artifact/linker/DynamicLinkPlan.h"
#include "target/artifact/executable/PeImage.h"
#include "target/artifact/archive/ArchiveReader.h"
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
#include "transforms/FunctionInliner.h"
#include "transforms/DeadInstructionElimination.h"
#include "transforms/LoopInvariantCodeMotion.h"
#include "transforms/ScalarEvolution.h"
#include "transforms/LoopUnroll.h"
#include "transforms/LoopVectorizer.h"
#include "transforms/ControlFlowSimplification.h"
#include "transforms/DivisionStrengthReduction.h"
#include "transforms/ErrorReporter.h"
#include "codegen/regalloc/RegAllocRewriter.h"
#include "codegen/abi/ABIAnalysis.h"
#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <cstring>
#include <algorithm>

std::string getFileExtension(const std::string& filename) {
    size_t pos = filename.find_last_of('.');
    if (pos == std::string::npos) {
        return "";
    }
    std::string ext = filename.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

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

std::string get_arg(int argc, char** argv, const std::string& arg) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == arg && i + 1 < argc) {
            return std::string(argv[i + 1]);
        }
    }
    return "";
}

int main(int argc, char** argv) {
    bool isLinkMode = false;
    bool isSharedMode = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--link") {
            isLinkMode = true;
        } else if (arg == "--shared") {
            isSharedMode = true;
            isLinkMode = true;
        }
    }

    if (isLinkMode) {
        target::artifact::linker::LinkOutputKind outputKind = isSharedMode ? target::artifact::linker::LinkOutputKind::SharedLibrary : target::artifact::linker::LinkOutputKind::Executable;
        std::string outputFile = get_arg(argc, argv, "-o");
        std::string targetTriple = get_arg(argc, argv, "--target");
        if (targetTriple.empty()) targetTriple = "x64-linux-bin";
        if (outputFile.empty()) outputFile = "a.out";

        std::vector<std::pair<std::string, std::string>> dynamicImports;
        std::vector<std::string> linkInputs;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--import" && i + 1 < argc) {
                std::string spec = argv[++i];
                size_t eq = spec.find('=');
                if (eq != std::string::npos) {
                    dynamicImports.push_back({spec.substr(0, eq), spec.substr(eq + 1)});
                }
                continue;
            }
            if (arg == "--link" || arg == "--shared" || arg == "-o" || arg == "--target") {
                if ((arg == "-o" || arg == "--target") && i + 1 < argc) i++;
                continue;
            }

            if (!arg.empty() && arg[0] != '-') {
                linkInputs.push_back(arg);
            }
        }

        std::cout << "--- Linking " << linkInputs.size() << " object/archive inputs ---" << std::endl;
        std::vector<target::artifact::object::ObjectArtifact> artifacts;
        std::vector<std::vector<target::artifact::archive::ArchiveObjectMember>> archives;

        for (const auto& path : linkInputs) {
            std::ifstream f(path, std::ios::binary);
            if (!f.is_open()) {
                std::cerr << "Error: cannot open input file " << path << std::endl;
                return 1;
            }
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            f.close();

            if (bytes.size() >= 8 && std::memcmp(bytes.data(), "!<arch>\n", 8) == 0) {
                target::artifact::archive::ArchiveReader arReader;
                std::vector<target::artifact::archive::ArchiveObjectMember> members;
                if (arReader.parse(bytes, members)) {
                    archives.push_back(std::move(members));
                } else {
                    std::cerr << "Error: failed to parse archive file " << path << ": " << arReader.getLastError() << std::endl;
                    return 1;
                }
            } else {
                auto objReader = target::artifact::object::ObjectReader::detectAndCreate(bytes);
                target::artifact::object::ObjectArtifact art;
                if (objReader && objReader->parse(bytes, art)) {
                    artifacts.push_back(art);
                } else {
                    std::cerr << "Error: failed to parse object file " << path << std::endl;
                    return 1;
                }
            }
        }

        target::artifact::linker::InternalLinker linker;
        linker.extractLazyArchiveMembers(artifacts, archives);

        target::artifact::linker::LinkedImage image;
        if (!linker.link(artifacts, image, outputKind, dynamicImports)) {
            std::cerr << "Linker error: " << linker.getLastError() << std::endl;
            return 1;
        }

        auto desc = target::TargetDescriptor::fromString(targetTriple);
        if (!desc) {
            target::TargetDescriptor d;
            d.arch = target::Arch::X64; d.os = target::OS::Linux;
            desc = d;
        }

        if (outputKind == target::artifact::linker::LinkOutputKind::SharedLibrary) {
            auto plan = target::artifact::linker::DynamicLinkPlan::createFromLinkedImage(image);
            auto builder = target::artifact::linker::TargetDynamicImageBuilder::createForTarget(desc->arch, desc->os);
            if (!builder || !builder->buildSharedLibrary(plan, outputFile)) {
                std::cerr << "Shared library generation failed: " << (builder ? builder->getLastError() : "Unsupported target") << std::endl;
                return 1;
            }
            std::cout << "Shared library linked successfully: " << outputFile << std::endl;
            return 0;
        }

        if (desc->os == target::OS::Windows) {
            target::artifact::executable::PeExecutableImageBuilder builder;
            if (!dynamicImports.empty()) {
                auto plan = target::artifact::linker::DynamicLinkPlan::createFromLinkedImage(image, dynamicImports);
                if (!builder.buildWithPlan(plan, outputFile)) {
                    std::cerr << "PE Executable generation failed: " << builder.getLastError() << std::endl;
                    return 1;
                }
            } else {
                if (!builder.build(image, outputFile)) {
                    std::cerr << "PE Executable generation failed: " << builder.getLastError() << std::endl;
                    return 1;
                }
            }
        } else if (desc->os == target::OS::MacOS) {
            target::artifact::linker::MachOExecutableImageBuilder builder;
            if (!builder.build(image, outputFile)) {
                std::cerr << "Mach-O Executable generation failed: " << builder.getLastError() << std::endl;
                return 1;
            }
        } else {
            target::artifact::linker::ElfExecutableImageBuilder builder;
            if (!builder.build(image, outputFile)) {
                std::cerr << "ELF Executable generation failed: " << builder.getLastError() << std::endl;
                return 1;
            }
        }

        std::cout << "Executable linked successfully: " << outputFile << std::endl;
        return 0;
    }

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
        std::cerr << "  --static-lib                                     Create static library" << std::endl;
        std::cerr << "  --link                                           Link multiple object files/archives" << std::endl;
        std::cerr << "  --verbose                                        Enable verbose output" << std::endl;
        std::cerr << "  --pipeline                                       Run full compilation pipeline for all targets" << std::endl;
        std::cerr << "  --gen-exec                                       Generate an executable file directly" << std::endl;
        std::cerr << "Supported input formats:" << std::endl;
        std::cerr << "  .fyra  - Fyra Intermediate Language format" << std::endl;
        std::cerr << "  .fy    - Fyra Intermediate Language format (alternative extension)" << std::endl;
        return 1;
    }

    std::string inputFile;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-o" || arg == "--target") && i + 1 < argc) {
            i++;
        } else if (!arg.empty() && arg[0] != '-') {
            inputFile = arg;
            break;
        }
    }
    std::string outputFile = get_arg(argc, argv, "-o");
    std::string targetTriple = get_arg(argc, argv, "--target");
    if (targetTriple.empty()) targetTriple = "x64-linux-bin";

    int optimizationLevel = 2;

    bool enableValidation = true;
    bool generateObject = false;
    bool createStaticLib = false;
    bool verboseOutput = false;
    bool runPipeline = false;
    bool generateExecutable = false;
    bool enableUnroll = true;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--no-unroll") {
            enableUnroll = false;
        } else if (arg == "--no-validate") {
            enableValidation = false;
        } else if (arg == "--validate") {
            enableValidation = true;
        } else if (arg == "--object") {
            generateObject = true;
        } else if (arg == "--static-lib") {
            createStaticLib = true;
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
    
    auto desc = target::TargetDescriptor::fromString(targetTriple);
    if (!desc) {
        if (targetTriple == "linux") targetTriple = "x64-linux-bin";
        else if (targetTriple == "windows" || targetTriple == "windows-amd64") targetTriple = "x64-windows-bin";
        else if (targetTriple == "windows-arm64") targetTriple = "aarch64-windows-bin";
        else if (targetTriple == "aarch64") targetTriple = "aarch64-linux-bin";
        else if (targetTriple == "wasm32") targetTriple = "wasm32-wasi-wasm";
        else if (targetTriple == "riscv64") targetTriple = "riscv64-linux-bin";
        else {
            targetTriple += "-bin";
        }

        desc = target::TargetDescriptor::fromString(targetTriple);
        if (!desc) {
            std::cerr << "Error: invalid target triple: " << targetTriple << std::endl;
            return 1;
        }
    }

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

    std::cout << "--- Parsing " << formatName << " input file: " << inputFile << " ---\n" << std::flush;
    parser::Parser p(inFile, static_cast<parser::FileFormat>(format));
    std::unique_ptr<ir::Module> module = p.parseModule();
    if (!module) {
        std::cerr << "Error: failed to parse module." << std::endl;
        return 1;
    }
    std::cout << "--- Parsing complete. ---\n" << std::flush;

    std::cout << "--- Validating IR correctness ---\n" << std::flush;
    std::vector<std::string> irErrors;
    if (!ir::Validator::validateModule(*module, irErrors)) {
        std::cerr << "IR Validation Failed! Detected " << irErrors.size() << " errors:\n";
        for (const auto& err : irErrors) {
            std::cerr << "  - " << err << "\n";
        }
        return 1;
    }
    std::cout << "--- IR Validation Successful! ---\n" << std::flush;

    std::cout << "--- Running Optimization Pipeline (-O" << optimizationLevel << ") ---\n" << std::flush;
    auto error_reporter = std::make_shared<transforms::ErrorReporter>(std::cerr, false);
    
    for (auto& func : module->getFunctions()) {
        if (func->getBasicBlocks().empty()) continue;
        transforms::CFGBuilder::run(*func);
        transforms::DominatorTree domTree; domTree.run(*func);
        transforms::DominanceFrontier domFrontier; domFrontier.run(*func, domTree);
        transforms::PhiInsertion phiInserter; phiInserter.run(*func, domFrontier);
        transforms::SSARenamer ssaRenamer; ssaRenamer.run(*func, domTree);
        
        transforms::Mem2Reg mem2reg;
        mem2reg.run(*func);
    }

    if (optimizationLevel > 0) {
        transforms::FunctionInliner inliner;
        inliner.runOnModule(*module);
    }

    for (auto& func : module->getFunctions()) {
        if (func->getBasicBlocks().empty()) continue;
        if (optimizationLevel == 0) continue;

        transforms::SCCP enhanced_sccp(error_reporter);
        transforms::ControlFlowSimplification cfg_simplifier(error_reporter);
        transforms::DeadInstructionElimination enhanced_dce(error_reporter);
        transforms::CopyElimination copy_elim;
        transforms::GVN gvn;
        transforms::LoopInvariantCodeMotion licm(error_reporter);
        transforms::ScalarEvolution scev;
        transforms::LoopUnroll loop_unroll(error_reporter);
        transforms::DivisionStrengthReduction div_sr(error_reporter);
        
        bool optimization_changed = true;
        int iteration = 1;
        const int maxIterations = (optimizationLevel >= 2) ? 5 : 2;
        bool isWasm = (desc->arch == target::Arch::WASM32);

        if (!isWasm) {
            while (optimization_changed && iteration <= maxIterations) {
                optimization_changed = false;
                if (div_sr.run(*func)) optimization_changed = true;
                if (enhanced_sccp.run(*func)) optimization_changed = true;
                if (copy_elim.run(*func)) optimization_changed = true;
                if (gvn.run(*func)) optimization_changed = true;
                if (cfg_simplifier.run(*func)) optimization_changed = true;
                if (optimizationLevel >= 2 && licm.run(*func)) optimization_changed = true;
                if (optimizationLevel >= 2 && scev.run(*func)) optimization_changed = true;
                if (optimizationLevel >= 2 && enableUnroll && loop_unroll.run(*func)) optimization_changed = true;
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
            auto targetInfoForAlloc = target::TargetResolver::resolve(*desc);
            for (auto& func : module->getFunctions()) {
                if (func->getBasicBlocks().empty()) continue;
                transforms::RegAllocRewriter rewriter;
                rewriter.run(*func, targetInfoForAlloc.get());
            }
            std::cout << "--- Register Allocation complete. ---\n" << std::flush;
        }
    } else {
        std::cerr << "Critical errors detected during optimization. Skipping register allocation." << std::endl;
        return 1;
    }

    std::cout << "--- Target: " << desc->toString() << " ---\n" << std::flush;
    auto targetInfo = target::TargetResolver::resolve(*desc);
    
    if (generateExecutable) {
        std::cout << "--- Generating Executable (In-Memory) ---\n" << std::flush;
        codegen::CodeGen codeGenerator(*module, std::move(targetInfo), nullptr);
        // The internal linker owns executable composition; code generation emits
        // a relocatable object payload rather than a target-format startup image.
        codeGenerator.emit(false);

        std::map<std::string, std::vector<uint8_t>> sections;
        sections[".text"] = codeGenerator.getAssembler().getCode();
        sections[".rodata"] = codeGenerator.getRodataAssembler().getCode();

        if (desc->os == target::OS::Windows) {
            if (desc->arch != target::Arch::X64) {
                std::cerr << "PE final-image generation currently supports x64 only" << std::endl;
                return 1;
            }
            target::artifact::object::ObjectArtifact artifact;
            artifact.format = target::artifact::object::ObjectFormat::COFF;
            artifact.arch = desc->arch;
            artifact.os = desc->os;
            target::artifact::object::ObjectSection text;
            text.name = ".text"; text.data = sections[".text"]; text.alignment = 16; text.flags = 0x6;
            artifact.addSection(text);
            if (!sections[".rodata"].empty()) {
                target::artifact::object::ObjectSection rodata;
                rodata.name = ".rodata"; rodata.data = sections[".rodata"]; rodata.alignment = 8; rodata.flags = 0x2;
                artifact.addSection(rodata);
            }
            if (sections.count(".data") && !sections[".data"].empty()) {
                target::artifact::object::ObjectSection data;
                data.name = ".data"; data.data = sections[".data"]; data.alignment = 8; data.flags = 0x3;
                artifact.addSection(data);
            }
            if (sections.count(".bss") && !sections[".bss"].empty()) {
                target::artifact::object::ObjectSection bss;
                bss.name = ".bss"; bss.virtualSize = sections[".bss"].size(); bss.alignment = 8; bss.flags = 0x3;
                artifact.addSection(bss);
            }
            for (const auto& sym : codeGenerator.getSymbols()) {
                target::artifact::object::ObjectSymbol out;
                out.name = sym.name; out.value = sym.value; out.size = sym.size;
                out.type = sym.type == 2 ? target::artifact::object::SymbolType::Function
                                         : target::artifact::object::SymbolType::NoType;
                out.binding = sym.binding == 1 ? target::artifact::object::SymbolBinding::Global
                                               : target::artifact::object::SymbolBinding::Local;
                out.sectionName = sym.sectionName; out.isDefined = true;
                artifact.addSymbol(out);
            }
            for (const auto& reloc : codeGenerator.getRelocations()) {
                artifact.addRelocation({reloc.offset, reloc.type, reloc.addend,
                                        reloc.symbolName, reloc.sectionName});
            }
            target::artifact::linker::InternalLinker linker;
            target::artifact::linker::LinkedImage linked;
            if (!linker.link({artifact}, linked, target::artifact::linker::LinkOutputKind::Executable)) {
                std::cerr << "Error linking PE: " << linker.getLastError() << std::endl;
                return 1;
            }
            target::artifact::executable::PeExecutableImageBuilder builder;
            if (!builder.build(linked, outputFile)) {
                std::cerr << "Error generating PE: " << builder.getLastError() << std::endl;
                return 1;
            }
            std::cout << "PE Executable generated successfully: " << outputFile << std::endl;
        } else if (desc->os == target::OS::MacOS) {
            target::artifact::object::ObjectArtifact artifact;
            artifact.format = target::artifact::object::ObjectFormat::MachO;
            artifact.arch = desc->arch;
            artifact.os = desc->os;
            target::artifact::object::ObjectSection text;
            text.name = ".text"; text.data = sections[".text"]; text.alignment = 16; text.flags = 0x6;
            artifact.addSection(text);
            if (!sections[".rodata"].empty()) {
                target::artifact::object::ObjectSection rodata;
                rodata.name = ".rodata"; rodata.data = sections[".rodata"]; rodata.alignment = 8; rodata.flags = 0x2;
                artifact.addSection(rodata);
            }
            for (const auto& sym : codeGenerator.getSymbols()) {
                target::artifact::object::ObjectSymbol out;
                out.name = sym.name; out.value = sym.value; out.size = sym.size;
                out.type = sym.type == 2 ? target::artifact::object::SymbolType::Function
                                         : target::artifact::object::SymbolType::NoType;
                out.binding = sym.binding == 1 ? target::artifact::object::SymbolBinding::Global
                                               : target::artifact::object::SymbolBinding::Local;
                out.sectionName = sym.sectionName; out.isDefined = true;
                artifact.addSymbol(out);
            }
            for (const auto& reloc : codeGenerator.getRelocations()) {
                artifact.addRelocation({reloc.offset, reloc.type, reloc.addend,
                                        reloc.symbolName, reloc.sectionName});
            }
            target::artifact::linker::InternalLinker linker;
            target::artifact::linker::LinkedImage linked;
            if (!linker.link({artifact}, linked, target::artifact::linker::LinkOutputKind::Executable)) {
                std::cerr << "Error linking Mach-O: " << linker.getLastError() << std::endl;
                return 1;
            }
            target::artifact::linker::MachOExecutableImageBuilder builder;
            if (!builder.build(linked, outputFile)) {
                std::cerr << "Error generating Mach-O: " << builder.getLastError() << std::endl;
                return 1;
            }
            std::cout << "Mach-O Executable generated successfully: " << outputFile << std::endl;
        } else {
            if (desc->arch != target::Arch::X64 || desc->os != target::OS::Linux) {
                std::cerr << "ELF final-image generation currently supports Linux x64 only" << std::endl;
                return 1;
            }
            target::artifact::object::ObjectArtifact artifact;
            artifact.format = target::artifact::object::ObjectFormat::ELF;
            artifact.arch = desc->arch; artifact.os = desc->os;
            target::artifact::object::ObjectSection text;
            text.name = ".text"; text.data = sections[".text"]; text.alignment = 16; text.flags = 0x6;
            artifact.addSection(text);
            if (!sections[".rodata"].empty()) {
                target::artifact::object::ObjectSection rodata;
                rodata.name = ".rodata"; rodata.data = sections[".rodata"]; rodata.alignment = 8; rodata.flags = 0x2;
                artifact.addSection(rodata);
            }
            for (const auto& sym : codeGenerator.getSymbols()) {
                target::artifact::object::ObjectSymbol out;
                out.name = sym.name; out.value = sym.value; out.size = sym.size;
                out.type = sym.type == 2 ? target::artifact::object::SymbolType::Function
                                         : target::artifact::object::SymbolType::NoType;
                out.binding = sym.binding == 1 ? target::artifact::object::SymbolBinding::Global
                                               : target::artifact::object::SymbolBinding::Local;
                out.sectionName = sym.sectionName; artifact.addSymbol(out);
            }
            for (const auto& reloc : codeGenerator.getRelocations())
                artifact.addRelocation({reloc.offset, reloc.type, reloc.addend, reloc.symbolName, reloc.sectionName});
            target::artifact::linker::InternalLinker linker;
            target::artifact::linker::LinkedImage linked;
            if (!linker.link({artifact}, linked, target::artifact::linker::LinkOutputKind::Executable)) {
                std::cerr << "Error linking ELF: " << linker.getLastError() << std::endl; return 1;
            }
            target::artifact::linker::ElfExecutableImageBuilder builder;
            if (!builder.build(linked, outputFile)) {
                std::cerr << "Error generating executable: " << builder.getLastError() << std::endl; return 1;
            }
            std::cout << "Executable generated successfully: " << outputFile << std::endl;
        }
    } else {
        codegen::CodeGen codeGen(*module, std::move(targetInfo), nullptr);
        codeGen.enableVerboseOutput(verboseOutput);
        codeGen.enableDebugInfo(true);
        codeGen.module.setSourceFilename(inputFile);

        auto result = codeGen.compileToObject(outputFile, enableValidation, generateObject, false);
        if (result.success) {
            std::cout << "Compilation successful in " << result.totalTimeMs << "ms" << std::endl;
            std::cout << "Assembly: " << result.assemblyPath << std::endl;
            if (generateObject && !result.objectPath.empty()) std::cout << "Object: " << result.objectPath << std::endl;

            if (createStaticLib) {
                std::cout << "--- Creating Static Library ---\n" << std::flush;
                std::string libPath = outputFile;
                auto libRes = codeGen.getObjectGenerator().createStaticLibrary({result.objectPath}, libPath, desc->toString());
                if (libRes.success) {
                    std::cout << "Static Library generated successfully: " << libPath << std::endl;
                } else {
                    std::cerr << "Error generating static library: " << libRes.errorOutput << std::endl;
                    return 1;
                }
            }
        } else {
            std::cerr << "Compilation failed" << std::endl;
            for (const auto& error : result.getAllErrors()) std::cerr << "Error: " << error << std::endl;
            return 1;
        }
    }

    return 0;
}
