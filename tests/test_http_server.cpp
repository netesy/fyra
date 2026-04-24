#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "parser/Parser.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"

using namespace ir;
using namespace codegen;
using namespace codegen::target;

int main() {
    std::string inputFile = "tests/http_server.fyra";
    std::ifstream inFile(inputFile);
    if (!inFile.is_open()) {
        inputFile = "../tests/http_server.fyra";
        inFile.open(inputFile);
    }
    if (!inFile.is_open()) {
        std::cerr << "Error: could not open input file tests/http_server.fyra" << std::endl;
        return 1;
    }

    parser::Parser p(inFile, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = p.parseModule();
    if (!module) {
        std::cerr << "Error: failed to parse module." << std::endl;
        return 1;
    }

    auto target = codegen::target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux});
    CodeGen cg(*module, std::move(target), &std::cout);
    cg.emit(true);

    return 0;
}
