#pragma once

#include "target/artifact/object/ObjectArtifact.h"
#include <string>
#include <vector>
#include <memory>

namespace target {
namespace artifact {
namespace object {

class ObjectReader {
public:
    virtual ~ObjectReader() = default;

    virtual bool parse(const std::vector<uint8_t>& bytes, ObjectArtifact& outArtifact) = 0;
    virtual bool read(const std::string& inputPath, ObjectArtifact& outArtifact) = 0;

    virtual std::string getLastError() const { return lastError_; }

    static std::unique_ptr<ObjectReader> createForTarget(const target::TargetDescriptor& desc);
    static std::unique_ptr<ObjectReader> createForTargetTriple(const std::string& targetTriple);
    static std::unique_ptr<ObjectReader> detectAndCreate(const std::vector<uint8_t>& bytes);

protected:
    std::string lastError_;
};

} // namespace object
} // namespace artifact
} // namespace target
