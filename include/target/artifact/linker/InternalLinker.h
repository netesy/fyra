#pragma once

#include "target/artifact/object/ObjectArtifact.h"
#include "target/artifact/linker/LinkedImage.h"
#include <vector>
#include <string>
#include <memory>

namespace target {
namespace artifact {
namespace linker {

class InternalLinker {
public:
    InternalLinker() = default;
    ~InternalLinker() = default;

    bool link(const std::vector<target::artifact::object::ObjectArtifact>& artifacts,
              LinkedImage& outImage);

    std::string getLastError() const { return lastError_; }

private:
    std::string lastError_;
};

} // namespace linker
} // namespace artifact
} // namespace target
