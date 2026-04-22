#ifndef FYRA_MACOS_X64_H
#define FYRA_MACOS_X64_H
#include "target/core/CompositeTargetInfo.h"
#include "target/architecture/x64/X64Architecture.h"
#include "target/os/macos/MacOSOS.h"

namespace codegen {
namespace target {
class MacOS_x64 : public CompositeTargetInfo {
public:
    MacOS_x64();
};
}
}
#endif
