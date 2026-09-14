#pragma once

#include "target/artifact/object/ObjectWriter.h"

namespace target {
namespace artifact {
namespace object {

class WasmObjectWriter : public ObjectWriter {
public:
    WasmObjectWriter() = default;
    ~WasmObjectWriter() override = default;

    std::vector<uint8_t> serialize(const ObjectArtifact& artifact) override;
    bool write(const ObjectArtifact& artifact, const std::string& outputPath) override;
};

} // namespace object
} // namespace artifact
} // namespace target
