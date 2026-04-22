#pragma once
#include "target/architecture/riscv64/RiscV64Architecture.h"
#include "target/os/linux/LinuxOS.h"
#include "target/core/CompositeTargetInfo.h"

namespace codegen {
namespace target {
class RiscV64 : public CompositeTargetInfo {
public:
    RiscV64() : CompositeTargetInfo(
        std::make_unique<RiscV64Architecture>(),
        std::make_unique<LinuxOS>()
    ) {}
};
}
}
