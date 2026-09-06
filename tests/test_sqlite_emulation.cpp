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

int main() {
    std::string source =
        "data $db_file = { b \"sqlite.db\", b 0 }\n"
        "data $msg = { b \"SQLITE EMULATION\", b 0 }\n"
        "export function $main() : i32 {\n"
        "@start\n"
        "    # fs.open(path, flags, mode)\n"
        "    %fd = extern \"fs.open\"(i64 $db_file, i64 66, i64 420) : i64\n"
        "    # io.write(fd, buffer, len)\n"
        "    %w_res = extern \"io.write\"(i64 %fd, i64 $msg, i64 16) : i64\n"
        "    # memory.alloc(size)\n"
        "    %buf = extern \"memory.alloc\"(i64 4096) : i64\n"
        "    # io.close(fd)\n"
        "    %c = extern \"io.close\"(i64 %fd) : i64\n"
        "    ret 0 : i32\n"
        "}";

    std::stringstream input(source);
    parser::Parser p(input, parser::FileFormat::FYRA);
    std::unique_ptr<ir::Module> module = p.parseModule();
    if (!module) {
        std::cerr << "Error: failed to parse module." << std::endl;
        return 1;
    }

    auto target = target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux});
    CodeGen cg(*module, std::move(target), &std::cout);
    cg.emit(true);

    return 0;
}
