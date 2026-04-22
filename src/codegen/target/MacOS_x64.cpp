#include "codegen/target/MacOS_x64.h"
#include "target/architecture/x64/X64Architecture.h"
#include "target/os/macos/MacOSOS.h"

namespace codegen {
namespace target {

MacOS_x64::MacOS_x64() : CompositeTargetInfo(
    std::make_unique<X64Architecture>(X64ABI::SystemV),
    std::make_unique<MacOSOS>()
) {}

}
}
