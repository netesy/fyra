#pragma once

#include "target/artifact/object/ObjectWriter.h"

namespace target {
namespace artifact {
namespace object {

class ElfObjectWriter : public ObjectWriter {
public:
    ElfObjectWriter() = default;
    ~ElfObjectWriter() override = default;

    std::vector<uint8_t> serialize(const ObjectArtifact& artifact) override;
    bool write(const ObjectArtifact& artifact, const std::string& outputPath) override;
};

} // namespace object
} // namespace artifact
} // namespace target
