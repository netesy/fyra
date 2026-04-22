#ifndef FYRA_SYSTEMV_X64_H
#define FYRA_SYSTEMV_X64_H
#include "target/core/CompositeTargetInfo.h"
#include "target/architecture/x64/X64Architecture.h"
#include "target/os/linux/LinuxOS.h"

namespace codegen {
namespace target {

class SystemV_x64 : public CompositeTargetInfo {
public:
    SystemV_x64() : CompositeTargetInfo(
        std::make_unique<X64Architecture>(X64ABI::SystemV),
        std::make_unique<LinuxOS>()
    ) {}
};

}
}
#endif
