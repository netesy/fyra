#pragma once
#include <string>
#include <optional>

namespace target {

enum class Arch {
    X64,
    AArch64,
    RISCV64,
    WASM32
};

enum class OS {
    Linux,
    Windows,
    MacOS,
    Android,
    FreeBSD,
    WASI,
    BareMetal
};

enum class Artifact {
    Executable,
    SharedLibrary,
    StaticLibrary,
    APK,
    WasmModule
};

struct TargetDescriptor {
    Arch arch;
    OS os;
    std::optional<Artifact> artifact;

    std::string toString() const;
    static std::optional<TargetDescriptor> fromString(const std::string& triple);
};

}
