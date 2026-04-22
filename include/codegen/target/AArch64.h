#pragma once
#include "target/architecture/aarch64/AArch64Architecture.h"
#include "target/os/linux/LinuxOS.h"
#include "target/core/CompositeTargetInfo.h"

namespace codegen {
namespace target {
class AArch64 : public CompositeTargetInfo {
public:
    AArch64() : CompositeTargetInfo(
        std::make_unique<AArch64Architecture>(),
        std::make_unique<LinuxOS>()
    ) {}
};
}
}
