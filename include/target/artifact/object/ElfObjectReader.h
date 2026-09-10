#pragma once

#include "target/artifact/object/ObjectReader.h"

namespace target {
namespace artifact {
namespace object {

class ElfObjectReader : public ObjectReader {
public:
    ElfObjectReader() = default;
    ~ElfObjectReader() override = default;

    bool parse(const std::vector<uint8_t>& bytes, ObjectArtifact& outArtifact) override;
    bool read(const std::string& inputPath, ObjectArtifact& outArtifact) override;
};

} // namespace object
} // namespace artifact
} // namespace target
