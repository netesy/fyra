#pragma once
#include "target/core/TargetDescriptor.h"
#include <memory>

namespace target {

class ArchitectureInfo;
class OperatingSystemInfo;

class TargetResolver {
public:
    static ::target::Artifact resolveDefaultArtifact(::target::OS os);
    static std::unique_ptr<class TargetInfo> resolve(const ::target::TargetDescriptor& desc);
};

}
}
