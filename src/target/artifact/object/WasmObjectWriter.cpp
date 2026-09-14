#include "target/artifact/object/WasmObjectWriter.h"
#include <fstream>

namespace target {
namespace artifact {
namespace object {

std::vector<uint8_t> WasmObjectWriter::serialize(const ObjectArtifact& artifact) {
    const ObjectSection* textSec = artifact.findSection(".text");
    if (textSec) {
        return textSec->data;
    }
    return {};
}

bool WasmObjectWriter::write(const ObjectArtifact& artifact, const std::string& outputPath) {
    auto data = serialize(artifact);
    std::ofstream ofs(outputPath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        lastError_ = "Failed to open output file: " + outputPath;
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
    return ofs.good();
}

} // namespace object
} // namespace artifact
} // namespace target
