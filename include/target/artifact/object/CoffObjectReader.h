#pragma once

#include "target/artifact/object/ObjectReader.h"

namespace target {
namespace artifact {
namespace object {

class CoffObjectReader : public ObjectReader {
public:
    CoffObjectReader() = default;
    ~CoffObjectReader() override = default;

    bool parse(const std::vector<uint8_t>& bytes, ObjectArtifact& outArtifact) override;
    bool read(const std::string& inputPath, ObjectArtifact& outArtifact) override;
};

} // namespace object
} // namespace artifact
} // namespace target
