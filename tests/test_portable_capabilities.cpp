#include <iostream>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include "parser/Parser.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"

using namespace ir;
using namespace codegen;
using namespace target;

void run_test(const std::string& arch_name, ::target::Arch arch, ::target::OS os) {
    std::string source =
        "export function $main() : i32 {\n"
        "@start\n"
        "    # io.write(stdout, \"Hello\", 5)\n"
        "    %w = extern \"io.write\"(i64 1, i64 0, i64 5) : i64\n"
        "    # process.exit(0)\n"
        "    extern \"process.exit\"(i64 0)\n"
        "    ret 0 : i32\n"
        "}";

    std::stringstream input(source);
    parser::Parser p(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = p.parseModule();

    std::cout << "--- Target: " << arch_name << " ---\n";
    auto target = target::TargetResolver::resolve({arch, os});
    CodeGen cg(*module, std::move(target), &std::cout);
    cg.emit(true);
    std::cout << "\n";
}

int main() {
    run_test("x64-linux", ::target::Arch::X64, ::target::OS::Linux);
    run_test("aarch64-linux", ::target::Arch::AArch64, ::target::OS::Linux);
    run_test("riscv64-linux", ::target::Arch::RISCV64, ::target::OS::Linux);
    run_test("x64-windows", ::target::Arch::X64, ::target::OS::Windows);
    return 0;
}
