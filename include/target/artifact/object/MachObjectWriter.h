#pragma once

#include "target/artifact/object/ObjectWriter.h"

namespace target {
namespace artifact {
namespace object {

class MachObjectWriter : public ObjectWriter {
public:
    MachObjectWriter() = default;
    ~MachObjectWriter() override = default;

    std::vector<uint8_t> serialize(const ObjectArtifact& artifact) override;
    bool write(const ObjectArtifact& artifact, const std::string& outputPath) override;
};

} // namespace object
} // namespace artifact
} // namespace target
