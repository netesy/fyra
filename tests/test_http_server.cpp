#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "parser/Parser.h"
#include "codegen/CodeGen.h"
#include "codegen/target/SystemV_x64.h"

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

    auto target = std::make_unique<SystemV_x64>();
    CodeGen cg(*module, std::move(target), &std::cout);
    cg.emit(true);

    return 0;
}
