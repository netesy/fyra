#include "target/core/TargetDescriptor.h"
#include <sstream>
#include <vector>
#include <algorithm>

namespace target {

std::string TargetDescriptor::toString() const {
    std::string s;
    switch(arch) {
        case Arch::X64: s += "x64"; break;
        case Arch::AArch64: s += "aarch64"; break;
        case Arch::RISCV64: s += "riscv64"; break;
        case Arch::WASM32: s += "wasm32"; break;
    }
    s += "-";
    switch(os) {
        case OS::Linux: s += "linux"; break;
        case OS::Windows: s += "windows"; break;
        case OS::MacOS: s += "macos"; break;
        case OS::Android: s += "android"; break;
        case OS::FreeBSD: s += "freebsd"; break;
        case OS::WASI: s += "wasi"; break;
        case OS::BareMetal: s += "baremetal"; break;
    }
    if (artifact) {
        s += "-";
        switch(*artifact) {
            case Artifact::Executable: s += "bin"; break;
            case Artifact::SharedLibrary: s += "shared"; break;
            case Artifact::StaticLibrary: s += "static"; break;
            case Artifact::APK: s += "apk"; break;
            case Artifact::WasmModule: s += "wasm"; break;
        }
    }
    return s;
}

std::optional<TargetDescriptor> TargetDescriptor::fromString(const std::string& triple) {
    std::vector<std::string> parts;
    std::stringstream ss(triple);
    std::string item;
    while (std::getline(ss, item, '-')) parts.push_back(item);

    if (parts.size() < 2) return std::nullopt;

    TargetDescriptor desc;
    if (parts[0] == "x64" || parts[0] == "x86_64") desc.arch = Arch::X64;
    else if (parts[0] == "aarch64" || parts[0] == "arm64") desc.arch = Arch::AArch64;
    else if (parts[0] == "riscv64") desc.arch = Arch::RISCV64;
    else if (parts[0] == "wasm32") desc.arch = Arch::WASM32;
    else return std::nullopt;

    if (parts[1] == "linux") desc.os = OS::Linux;
    else if (parts[1] == "windows" || parts[1] == "win32") desc.os = OS::Windows;
    else if (parts[1] == "macos" || parts[1] == "darwin") desc.os = OS::MacOS;
    else if (parts[1] == "android") desc.os = OS::Android;
    else if (parts[1] == "wasi") desc.os = OS::WASI;
    else return std::nullopt;

    if (parts.size() > 2) {
        if (parts[2] == "bin" || parts[2] == "executable") desc.artifact = Artifact::Executable;
        else if (parts[2] == "apk") desc.artifact = Artifact::APK;
        else if (parts[2] == "wasm") desc.artifact = Artifact::WasmModule;
        else if (parts[2] == "shared") desc.artifact = Artifact::SharedLibrary;
        else if (parts[2] == "static") desc.artifact = Artifact::StaticLibrary;
    }

    return desc;
}

}
