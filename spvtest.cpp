#include "fyra/BackendBuilder.h"
#include "parser/Parser.h"
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: spvtest <in.fyra> <out.spv>\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in.is_open()) { std::cerr << "cannot open input\n"; return 1; }
    parser::Parser p(in, parser::FileFormat::FYRA);
    auto mod = p.parseModule();
    if (!mod) { std::cerr << "parse failed\n"; return 1; }
    fyra::BackendBuilder b(*mod);
    b.target(argc > 3 ? argv[3] : "spirv");
    b.validate(false);
    auto res = b.emitObject(argv[2]);
    for (auto& w : res.warnings) std::cerr << "warn: " << w << "\n";
    for (auto& e : res.errors) std::cerr << "err: " << e << "\n";
    std::cout << (res.success ? "OK" : "FAIL") << " " << res.outputPath << "\n";
    return res.success ? 0 : 1;
}
