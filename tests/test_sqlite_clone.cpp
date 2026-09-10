#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <cassert>
#include "parser/Parser.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"
#include "LinkedElfTestUtils.h"

using namespace ir;
using namespace codegen;
using namespace target;

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

    auto target = target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux});
    CodeGen cg(*module, std::move(target), nullptr);
    cg.emit(true);

    std::string outputPath = "./sqlite_clone_exec";
    std::string error;
    if (!writeLinkedX64Elf(cg, outputPath, error)) {
        std::cerr << "Error generating ELF: " << error << std::endl;
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
