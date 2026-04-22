#ifndef FYRA_WINDOWS_AARCH64_H
#define FYRA_WINDOWS_AARCH64_H
#include "target/core/CompositeTargetInfo.h"
#include "target/architecture/aarch64/AArch64Architecture.h"
#include "target/os/windows/WindowsOS.h"

namespace codegen {
namespace target {
class Windows_AArch64 : public CompositeTargetInfo {
public:
    Windows_AArch64() : CompositeTargetInfo(
        std::make_unique<AArch64Architecture>(),
        std::make_unique<WindowsOS>()
    ) {}
};
}
}
#endif
