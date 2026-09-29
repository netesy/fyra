#include "parser/Parser.h"
#include "fyra/BackendBuilder.h"
#include "ir/IRContext.h"
#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <cstring>
#include <algorithm>

static std::string getFileExtension(const std::string& filename) {
    size_t pos = filename.find_last_of('.');
    if (pos == std::string::npos) return "";
    std::string ext = filename.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

static parser::FileFormat detectFileFormat(const std::string& filename) {
    std::string ext = getFileExtension(filename);
    if (ext == ".fyra") {
        return parser::FileFormat::FYRA;
    } else if (ext == ".fy") {
        return parser::FileFormat::FY;
    } else {
        throw std::runtime_error("Unknown file extension: " + ext);
    }
}

static std::string get_arg(int argc, char** argv, const std::string& arg) {
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
        std::string outputFile = get_arg(argc, argv, "-o");
        std::string targetTriple = get_arg(argc, argv, "--target");
        if (targetTriple.empty()) targetTriple = "x64-linux-bin";
        if (outputFile.empty()) outputFile = "a.out";

        auto ctx = std::make_shared<ir::IRContext>();
        ir::Module dummyModule("link_session", ctx);
        fyra::BackendBuilder builder(dummyModule);
        builder.target(targetTriple);

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--import" && i + 1 < argc) {
                std::string spec = argv[++i];
                size_t eq = spec.find('=');
                if (eq != std::string::npos) {
                    builder.importSymbol(spec.substr(0, eq), spec.substr(eq + 1));
                }
                continue;
            }
            if (arg == "--link" || arg == "--shared" || arg == "-o" || arg == "--target") {
                if ((arg == "-o" || arg == "--target") && i + 1 < argc) i++;
                continue;
            }

            if (!arg.empty() && arg[0] != '-') {
                std::string ext = getFileExtension(arg);
                if (ext == ".a" || ext == ".lib") {
                    builder.addStaticLibrary(arg);
                } else {
                    builder.addObject(arg);
                }
            }
        }

        fyra::BuildResult result = isSharedMode ? builder.emitSharedLibrary(outputFile)
                                                : builder.emitExecutable(outputFile);

        if (result.success) {
            std::cout << (isSharedMode ? "Shared library" : "Executable")
                      << " linked successfully: " << outputFile << std::endl;
            return 0;
        } else {
            std::cerr << "Linker error:" << std::endl;
            for (const auto& err : result.errors) {
                std::cerr << "  - " << err << std::endl;
            }
            return 1;
        }
    }

    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.fyra|input.fy> -o <output.s> [options]" << std::endl;
        std::cerr << "Options:" << std::endl;
        std::cerr << "  --target <triple>                                Target triple (e.g., x64-linux-bin, aarch64-macos-executable)" << std::endl;
        std::cerr << "  -O0                                              Disable optimizations" << std::endl;
        std::cerr << "  -O1                                              Enable conservative optimizations" << std::endl;
        std::cerr << "  -O2                                              Enable full optimization pipeline (default)" << std::endl;
        std::cerr << "  --disable-slp                                     Disable SLP vectorization" << std::endl;
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

    fyra::OptimizationLevel optLevel = fyra::OptimizationLevel::O2;
    bool enableValidation = true;
    bool generateObject = false;
    bool createStaticLib = false;
    bool generateExecutable = false;
    bool enableUnroll = true;
    bool enableSLP = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--no-unroll") {
            enableUnroll = false;
        } else if (arg == "--disable-slp") {
            enableSLP = false;
        } else if (arg == "--no-validate") {
            enableValidation = false;
        } else if (arg == "--validate") {
            enableValidation = true;
        } else if (arg == "--object") {
            generateObject = true;
        } else if (arg == "--static-lib") {
            createStaticLib = true;
            generateObject = true;
        } else if (arg == "--gen-exec") {
            generateExecutable = true;
        } else if (arg == "-O0") {
            optLevel = fyra::OptimizationLevel::O0;
        } else if (arg == "-O1") {
            optLevel = fyra::OptimizationLevel::O1;
        } else if (arg == "-O2") {
            optLevel = fyra::OptimizationLevel::O2;
        }
    }

    if (outputFile.empty()) {
        std::cerr << "Error: missing output file (-o <output>)" << std::endl;
        return 1;
    }

    parser::FileFormat format = detectFileFormat(inputFile);
    std::string formatName = (format == parser::FileFormat::FYRA) ? "Fyra (.fyra)" : "Fyra (.fy)";

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

    fyra::BackendBuilder builder(*module);
    builder.target(targetTriple)
           .optimize(optLevel)
           .validate(enableValidation)
           .enableSLP(enableSLP)
           .enableLoopUnroll(enableUnroll);

    fyra::BuildResult result;
    std::string ext = getFileExtension(outputFile);

    if (createStaticLib) {
        result = builder.emitStaticLibrary(outputFile);
    } else if (generateExecutable) {
        result = builder.emitExecutable(outputFile);
    } else if (generateObject) {
        result = builder.emitObject(outputFile);
    } else if (ext == ".wat") {
        result = builder.emitWAT(outputFile);
    } else if (ext == ".wasm") {
        result = builder.emitWasm(outputFile);
    } else {
        result = builder.emitAssembly(outputFile);
    }

    if (result.success) {
        std::cout << "Compilation successful: " << outputFile << std::endl;
        return 0;
    } else {
        std::cerr << "Compilation failed for " << outputFile << std::endl;
        for (const auto& err : result.errors) {
            std::cerr << "Error: " << err << std::endl;
        }
        return 1;
    }
}
