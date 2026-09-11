#pragma once

#include "target/artifact/object/ObjectArtifact.h"
#include "target/artifact/linker/LinkedImage.h"
#include "target/artifact/linker/DynamicLinkPlan.h"
#include "target/artifact/archive/ArchiveReader.h"
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

    bool extractLazyArchiveMembers(
        std::vector<target::artifact::object::ObjectArtifact>& inOutArtifacts,
        std::vector<std::vector<target::artifact::archive::ArchiveObjectMember>>& archives);

    bool link(const std::vector<target::artifact::object::ObjectArtifact>& artifacts,
              LinkedImage& outImage,
              LinkOutputKind outputKind = LinkOutputKind::Executable,
              const std::vector<DynamicImport>& dynamicImports = {});

    std::string getLastError() const { return lastError_; }

private:
    std::string lastError_;
};

} // namespace linker
} // namespace artifact
} // namespace target
