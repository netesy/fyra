#include "target/artifact/object/WasmObjectReader.h"
#include <fstream>
#include <cstring>

namespace target {
namespace artifact {
namespace object {

bool WasmObjectReader::parse(const std::vector<uint8_t>& bytes, ObjectArtifact& outArtifact) {
    if (bytes.size() < 8) {
        lastError_ = "Buffer too small to be a valid WebAssembly module";
        return false;
    }
    if (std::memcmp(bytes.data(), "\x00""asm", 4) != 0) {
        lastError_ = "Invalid WebAssembly magic header";
        return false;
    }

    outArtifact.arch = target::Arch::WASM32;
    outArtifact.os = target::OS::WASI;

    ObjectSection textSec;
    textSec.name = ".text";
    textSec.flags = 0x6; // SHF_ALLOC | SHF_EXECINSTR
    textSec.alignment = 1;
    textSec.data = bytes;
    outArtifact.sections[textSec.name] = textSec;

    return true;
}

bool WasmObjectReader::read(const std::string& inputPath, ObjectArtifact& outArtifact) {
    std::ifstream ifs(inputPath, std::ios::binary);
    if (!ifs.is_open()) {
        lastError_ = "Failed to open input file: " + inputPath;
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return parse(bytes, outArtifact);
}

} // namespace object
} // namespace artifact
} // namespace target
