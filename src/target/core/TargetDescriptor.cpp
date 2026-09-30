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
        case Arch::RISCV32: s += "riscv32"; break;
        case Arch::LoongArch64: s += "loongarch64"; break;
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
        case OS::UEFI: s += "uefi"; break;
    }
    if (artifact) {
        s += "-";
        switch(*artifact) {
            case Artifact::Executable: s += "bin"; break;
            case Artifact::SharedLibrary: s += "shared"; break;
            case Artifact::StaticLibrary: s += "static"; break;
            case Artifact::APK: s += "apk"; break;
            case Artifact::WasmModule: s += "wasm"; break;
            case Artifact::FlatBinary: s += "flat"; break;
        }
    }
    return s;
}

std::string TargetDescriptor::normalizeTriple(const std::string& triple) {
    if (triple.empty()) return "x64-linux-bin";
    if (triple == "linux") return "x64-linux-bin";
    if (triple == "windows" || triple == "windows-amd64" || triple == "win32" || triple == "win64") return "x64-windows-bin";
    if (triple == "windows-arm64") return "aarch64-windows-bin";
    if (triple == "aarch64") return "aarch64-linux-bin";
    if (triple == "wasm32" || triple == "wasm") return "wasm32-wasi-wasm";
    if (triple == "riscv64") return "riscv64-linux-bin";
    if (triple == "riscv32") return "riscv32-linux-bin";
    if (triple == "loongarch64") return "loongarch64-linux-bin";
    if (triple == "freebsd" || triple == "x64-freebsd") return "x64-freebsd-bin";
    if (triple == "aarch64-freebsd") return "aarch64-freebsd-bin";
    if (triple == "riscv64-freebsd") return "riscv64-freebsd-bin";
    if (triple == "riscv32-freebsd") return "riscv32-freebsd-bin";
    if (triple == "loongarch64-freebsd") return "loongarch64-freebsd-bin";
    if (triple == "baremetal" || triple == "riscv64-baremetal") return "riscv64-baremetal-bin";
    if (triple == "riscv32-baremetal") return "riscv32-baremetal-bin";
    if (triple == "aarch64-baremetal") return "aarch64-baremetal-bin";
    if (triple == "loongarch64-baremetal") return "loongarch64-baremetal-bin";
    if (triple == "uefi") return "x64-uefi-bin";
    if (triple == "x64-uefi") return "x64-uefi-bin";
    if (triple == "aarch64-uefi") return "aarch64-uefi-bin";
    if (triple == "loongarch64-uefi") return "loongarch64-uefi-bin";

    if (triple.find('-') == std::string::npos) {
        return triple + "-linux-bin";
    }
    if (triple.find_last_of('-') == triple.find('-')) {
        return triple + "-bin";
    }
    return triple;
}

std::optional<TargetDescriptor> TargetDescriptor::fromString(const std::string& tripleStr) {
    std::string triple = tripleStr;
    std::vector<std::string> parts;
    std::stringstream ss(triple);
    std::string item;
    while (std::getline(ss, item, '-')) parts.push_back(item);

    if (parts.size() < 2) {
        triple = normalizeTriple(tripleStr);
        parts.clear();
        std::stringstream ss2(triple);
        while (std::getline(ss2, item, '-')) parts.push_back(item);
        if (parts.size() < 2) return std::nullopt;
    }

    TargetDescriptor desc;
    if (parts[0] == "x64" || parts[0] == "x86_64") desc.arch = Arch::X64;
    else if (parts[0] == "aarch64" || parts[0] == "arm64") desc.arch = Arch::AArch64;
    else if (parts[0] == "riscv64") desc.arch = Arch::RISCV64;
    else if (parts[0] == "riscv32") desc.arch = Arch::RISCV32;
    else if (parts[0] == "loongarch64") desc.arch = Arch::LoongArch64;
    else if (parts[0] == "wasm32") desc.arch = Arch::WASM32;
    else return std::nullopt;

    if (parts[1] == "linux") desc.os = OS::Linux;
    else if (parts[1] == "windows" || parts[1] == "win32") desc.os = OS::Windows;
    else if (parts[1] == "macos" || parts[1] == "darwin") desc.os = OS::MacOS;
    else if (parts[1] == "android") desc.os = OS::Android;
    else if (parts[1] == "freebsd") desc.os = OS::FreeBSD;
    else if (parts[1] == "baremetal" || parts[1] == "none" || parts[1] == "elf") desc.os = OS::BareMetal;
    else if (parts[1] == "wasi" || parts[1] == "unknown") desc.os = OS::WASI;
    else if (parts[1] == "uefi") desc.os = OS::UEFI;
    else return std::nullopt;

    if (parts.size() > 2) {
        if (parts[2] == "bin" || parts[2] == "executable") desc.artifact = Artifact::Executable;
        else if (parts[2] == "apk") desc.artifact = Artifact::APK;
        else if (parts[2] == "wasm") desc.artifact = Artifact::WasmModule;
        else if (parts[2] == "flat" || parts[2] == "raw" || parts[2] == "img") desc.artifact = Artifact::FlatBinary;
        else if (parts[2] == "shared") desc.artifact = Artifact::SharedLibrary;
        else if (parts[2] == "static") desc.artifact = Artifact::StaticLibrary;
    }

    return desc;
}

}
