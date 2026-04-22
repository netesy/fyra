#pragma once
#include "target/core/CompositeTargetInfo.h"
#include "target/architecture/aarch64/AArch64Architecture.h"
#include "target/os/macos/MacOSOS.h"

namespace codegen {
namespace target {
class MacOS_AArch64 : public CompositeTargetInfo {
public:
    MacOS_AArch64() : CompositeTargetInfo(
        std::make_unique<AArch64Architecture>(),
        std::make_unique<MacOSOS>()
    ) {}
};
}
}
