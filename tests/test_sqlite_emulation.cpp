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
        "export function $main() : w {\n"
        "@start\n"
        "    # fs.open(path, flags, mode)\n"
        "    %fd = extern \"fs.open\"(l $db_file, l 66, l 420) : l\n"
        "    # io.write(fd, buffer, len)\n"
        "    %w = extern \"io.write\"(l %fd, l $msg, l 16) : l\n"
        "    # memory.alloc(size)\n"
        "    %buf = extern \"memory.alloc\"(l 4096) : l\n"
        "    # io.close(fd)\n"
        "    %c = extern \"io.close\"(l %fd) : l\n"
        "    ret 0 : w\n"
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
