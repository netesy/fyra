#include "target/core/TargetResolver.h"
#include "target/core/CompositeTargetInfo.h"
#include "target/architecture/x64/X64Architecture.h"
#include "target/architecture/aarch64/AArch64Architecture.h"
#include "target/architecture/riscv64/RiscV64Architecture.h"
#include "target/architecture/wasm32/Wasm32Architecture.h"
#include "target/os/linux/LinuxOS.h"
#include "target/os/windows/WindowsOS.h"
#include "target/os/macos/MacOSOS.h"
#include "target/os/wasi/WASIOS.h"
#include "target/artifact/apk/APKArtifact.h"
#include <map>

namespace target {

::target::Artifact TargetResolver::resolveDefaultArtifact(::target::OS os) {
    switch (os) {
        case ::target::OS::Android: return ::target::Artifact::APK;
        case ::target::OS::WASI: return ::target::Artifact::WasmModule;
        default: return ::target::Artifact::Executable;
    }
}

std::unique_ptr<TargetInfo> TargetResolver::resolve(const ::target::TargetDescriptor& desc) {
    std::unique_ptr<ArchitectureInfo> arch;
    switch (desc.arch) {
        case ::target::Arch::X64: arch = std::make_unique<X64Architecture>(desc.os == ::target::OS::Windows ? X64ABI::Windows : X64ABI::SystemV); break;
        case ::target::Arch::AArch64: arch = std::make_unique<AArch64Architecture>(); break;
        case ::target::Arch::RISCV64: arch = std::make_unique<RiscV64Architecture>(); break;
        case ::target::Arch::WASM32: arch = std::make_unique<Wasm32Architecture>(); break;
    }

    std::unique_ptr<OperatingSystemInfo> os;
    switch (desc.os) {
        case ::target::OS::Linux: os = std::make_unique<LinuxOS>(); break;
        case ::target::OS::Windows: os = std::make_unique<WindowsOS>(); break;
        case ::target::OS::MacOS: os = std::make_unique<MacOSOS>(); break;
        case ::target::OS::Android: os = std::make_unique<LinuxOS>(); break; // Android uses Linux OS base
        case ::target::OS::WASI: os = std::make_unique<WASIOS>(); break;
        default: os = std::make_unique<LinuxOS>(); break;
    }

    auto target = std::make_unique<CompositeTargetInfo>(std::move(arch), std::move(os));

    ::target::Artifact art = desc.artifact.value_or(resolveDefaultArtifact(desc.os));
    if (art == ::target::Artifact::APK) {
        return std::make_unique<artifact::APKArtifact>(std::move(target));
    }

    return target;
}

}
}
