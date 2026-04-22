#ifndef FYRA_WINDOWS_X64_H
#define FYRA_WINDOWS_X64_H
#include "target/core/CompositeTargetInfo.h"
#include "target/architecture/x64/X64Architecture.h"
#include "target/os/windows/WindowsOS.h"

namespace codegen {
namespace target {
class Windows_x64 : public CompositeTargetInfo {
public:
    Windows_x64() : CompositeTargetInfo(
        std::make_unique<X64Architecture>(X64ABI::Windows),
        std::make_unique<WindowsOS>()
    ) {}
};
}
}
#endif
