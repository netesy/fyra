#pragma once

#include "target/artifact/object/ObjectArtifact.h"
#include <string>
#include <vector>
#include <memory>

namespace target {
namespace artifact {
namespace object {

class ObjectWriter {
public:
    virtual ~ObjectWriter() = default;

    virtual std::vector<uint8_t> serialize(const ObjectArtifact& artifact) = 0;
    virtual bool write(const ObjectArtifact& artifact, const std::string& outputPath) = 0;

    virtual std::string getLastError() const { return lastError_; }

    static std::unique_ptr<ObjectWriter> createForTarget(const target::TargetDescriptor& desc);
    static std::unique_ptr<ObjectWriter> createForTargetTriple(const std::string& targetTriple);

protected:
    std::string lastError_;
};

} // namespace object
} // namespace artifact
} // namespace target
