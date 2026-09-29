#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"
#include "codegen/CodeGen.h"
#include "ir/Type.h"
#include "ir/Instruction.h"
#include <iostream>
#include <cstring>

namespace target {

void TargetFeatureFlags::setFeature(const std::string& feature, bool enabled) {
    std::string feat = feature;
    if (!feat.empty() && (feat[0] == '+' || feat[0] == '-')) {
        enabled = (feat[0] == '+');
        feat = feat.substr(1);
    }
    if (enabled) {
        enabledFeatures_.insert(feat);
    } else {
        enabledFeatures_.erase(feat);
    }
}

bool TargetFeatureFlags::hasFeature(std::string_view feature) const {
    std::string feat(feature);
    if (!feat.empty() && (feat[0] == '+' || feat[0] == '-')) {
        feat = feat.substr(1);
    }
    return enabledFeatures_.count(feat) > 0;
}

void TargetFeatureFlags::parseFeatures(const std::string& featureString) {
    size_t start = 0;
    while (start < featureString.size()) {
        size_t end = featureString.find(',', start);
        if (end == std::string::npos) end = featureString.size();
        std::string token = featureString.substr(start, end - start);
        // Trim whitespace
        size_t first = token.find_first_not_of(" \t");
        size_t last = token.find_last_not_of(" \t");
        if (first != std::string::npos && last != std::string::npos) {
            token = token.substr(first, (last - first + 1));
            setFeature(token);
        }
        start = end + 1;
    }
}

VectorCapabilities TargetInfo::getVectorCapabilities() const {
    VectorCapabilities caps;
    if (hasFeature("avx512") || hasFeature("avx512f")) {
        caps.supportsAVX512 = true;
        caps.supportsAVX2 = true;
        caps.supportsAVX = true;
        caps.supportsSSE = true;
        caps.maxVectorWidth = 512;
        caps.supportedWidths = {128, 256, 512};
        caps.supportsFloatVectors = true;
        caps.supportsIntegerVectors = true;
        caps.supportsDoubleVectors = true;
        caps.supportsMaskedOps = true;
        caps.supportsGatherScatter = true;
        caps.supportsFMA = true;
        caps.supportsHorizontalOps = true;
        caps.simdExtension = "avx512";
    } else if (hasFeature("avx2")) {
        caps.supportsAVX2 = true;
        caps.supportsAVX = true;
        caps.supportsSSE = true;
        caps.maxVectorWidth = 256;
        caps.supportedWidths = {128, 256};
        caps.supportsFloatVectors = true;
        caps.supportsIntegerVectors = true;
        caps.supportsDoubleVectors = true;
        caps.supportsFMA = true;
        caps.supportsHorizontalOps = true;
        caps.simdExtension = "avx2";
    } else if (hasFeature("neon")) {
        caps.supportsNEON = true;
        caps.maxVectorWidth = 128;
        caps.supportedWidths = {64, 128};
        caps.supportsFloatVectors = true;
        caps.supportsIntegerVectors = true;
        caps.supportsDoubleVectors = true;
        caps.simdExtension = "neon";
    }
    return caps;
}

const CapabilitySpec* TargetInfo::findCapability(std::string_view name) const {
    return CapabilityRegistry::find(name);
}

bool TargetInfo::supportsCapability(const CapabilitySpec&) const {
    return false;
}

bool TargetInfo::validateCapability(ir::Instruction& instr, const CapabilitySpec& spec) const {
    const auto argc = static_cast<int>(instr.getOperands().size());
    if (argc < spec.minArgs || argc > spec.maxArgs) return false;
    const bool hasReturn = instr.getType() && instr.getType()->getTypeID() != ir::Type::VoidTyID;
    if (spec.returnsValue != hasReturn) return false;
    return supportsCapability(spec);
}

void TargetInfo::emitUnsupportedCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec* spec) const {
    uint64_t domain = spec ? static_cast<uint64_t>(spec->domain) : static_cast<uint64_t>(CapabilityDomain::SYSTEM);
    uint64_t category_unsupported = 0x01;
    uint64_t code = spec ? static_cast<uint64_t>(spec->id) : 0;
    uint64_t error = (domain << 24) | (category_unsupported << 16) | code;

    if (auto* os = cg.getTextStream()) {
        std::string rax = getRegisterName("rax", instr.getType());
        *os << "  movq $" << error << ", " << rax << "\n";
        if (instr.getType() && instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  movq " << rax << ", " << cg.getValueAsOperand(&instr) << "\n";
        }
    }
}

void TargetInfo::emitDomainCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    switch (spec.domain) {
        case CapabilityDomain::IO: emitIOCapability(cg, instr, spec); break;
        case CapabilityDomain::FS: emitFSCapability(cg, instr, spec); break;
        case CapabilityDomain::MEMORY: emitMemoryCapability(cg, instr, spec); break;
        case CapabilityDomain::PROCESS: emitProcessCapability(cg, instr, spec); break;
        case CapabilityDomain::THREAD: emitThreadCapability(cg, instr, spec); break;
        case CapabilityDomain::SYNC: emitSyncCapability(cg, instr, spec); break;
        case CapabilityDomain::TIME: emitTimeCapability(cg, instr, spec); break;
        case CapabilityDomain::EVENT: emitEventCapability(cg, instr, spec); break;
        case CapabilityDomain::NET: emitNetCapability(cg, instr, spec); break;
        case CapabilityDomain::IPC: emitIPCCapability(cg, instr, spec); break;
        case CapabilityDomain::ENV: emitEnvCapability(cg, instr, spec); break;
        case CapabilityDomain::SYSTEM: emitSystemCapability(cg, instr, spec); break;
        case CapabilityDomain::SIGNAL: emitSignalCapability(cg, instr, spec); break;
        case CapabilityDomain::RANDOM: emitRandomCapability(cg, instr, spec); break;
        case CapabilityDomain::ERROR: emitErrorCapability(cg, instr, spec); break;
        case CapabilityDomain::DEBUG: emitDebugCapability(cg, instr, spec); break;
        case CapabilityDomain::MODULE: emitModuleCapability(cg, instr, spec); break;
        case CapabilityDomain::TTY: emitTTYCapability(cg, instr, spec); break;
        case CapabilityDomain::SECURITY: emitSecurityCapability(cg, instr, spec); break;
        case CapabilityDomain::GPU: emitGPUCapability(cg, instr, spec); break;
    }
}

void TargetInfo::emitExternCall(codegen::CodeGen& cg, ir::Instruction& instr) {
    auto* ei = dynamic_cast<ir::ExternCallInstruction*>(&instr);
    if (!ei) return;
    const auto* spec = findCapability(ei->getCapability());
    if (!spec || !validateCapability(instr, *spec)) {
        emitUnsupportedCapability(cg, instr, spec);
        return;
    }
    emitDomainCapability(cg, instr, *spec);
}

void TargetInfo::emitIOCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitFSCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitMemoryCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitProcessCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitThreadCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitSyncCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitTimeCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitEventCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitNetCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitIPCCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitEnvCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitSystemCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitSignalCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitRandomCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitErrorCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitDebugCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitModuleCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitTTYCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitSecurityCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitGPUCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }

SIMDContext TargetInfo::createSIMDContext(const ir::VectorType* type) const {
    SIMDContext ctx;
    ctx.vectorWidth = type->getBitWidth();
    ctx.vectorType = const_cast<ir::VectorType*>(type);
    return ctx;
}

std::string TargetInfo::getVectorRegister(const std::string& baseReg, unsigned) const {
    return baseReg;
}

std::string TargetInfo::getVectorInstruction(const std::string& baseInstr, const SIMDContext&) const {
    return baseInstr;
}

std::string TargetInfo::formatConstant(const ir::ConstantInt* C) const {
    return getImmediatePrefix() + std::to_string(C->getValue());
}

std::string TargetInfo::formatConstant(const ir::ConstantFP* C) const {
    uint64_t bits = 0;
    double val = C->getValue();
    std::memcpy(&bits, &val, sizeof(double));
    return getImmediatePrefix() + std::to_string(bits);
}


int32_t TargetInfo::getStackOffset(const codegen::CodeGen& cg, ir::Value* val) const {
    auto it = cg.getStackOffsets().find(val);
    if (it != cg.getStackOffsets().end()) return it->second;
    if (auto* param = dynamic_cast<ir::Parameter*>(val)) {
        if (cg.getCurrentFunction()) {
            size_t idx = 0;
            for (auto& p : cg.getCurrentFunction()->getParameters()) {
                if (p.get() == param) break;
                idx++;
            }
            if (idx >= 6) {
                return 16 + (idx - 6) * 8;
            }
        }
    }
    return 0;
}

uint16_t TargetInfo::getElfMachine(Arch arch) {
    switch (arch) {
        case Arch::X64: return 62;        // EM_X86_64
        case Arch::AArch64: return 183;   // EM_AARCH64
        case Arch::RISCV64: return 243;   // EM_RISCV
        case Arch::WASM32: return 0;      // Not applicable for ELF
        default: return 0;
    }
}

uint16_t TargetInfo::getCoffMachine(Arch arch) {
    switch (arch) {
        case Arch::X64: return 0x8664;     // IMAGE_FILE_MACHINE_AMD64
        case Arch::AArch64: return 0xAA64; // IMAGE_FILE_MACHINE_ARM64
        case Arch::RISCV64: return 0x5064; // IMAGE_FILE_MACHINE_RISCV64
        case Arch::WASM32: return 0;       // Not applicable for COFF
        default: return 0;
    }
}

uint32_t TargetInfo::getElfJumpSlotRelocation(Arch arch) {
    switch (arch) {
        case Arch::X64: return 7;         // R_X86_64_JUMP_SLOT
        case Arch::AArch64: return 1026;  // R_AARCH64_JUMP_SLOT
        case Arch::RISCV64: return 5;      // R_RISCV_JUMP_SLOT
        default: return 0;
    }
}

uint32_t TargetInfo::getElfGlobDatRelocation(Arch arch) {
    switch (arch) {
        case Arch::X64: return 6;         // R_X86_64_GLOB_DAT
        case Arch::AArch64: return 1025;  // R_AARCH64_GLOB_DAT
        case Arch::RISCV64: return 2;      // R_RISCV_64 (used for GLOB_DAT on RISC-V)
        default: return 0;
    }
}

uint32_t TargetInfo::getElfRelativeRelocation(Arch arch) {
    switch (arch) {
        case Arch::X64: return 8;         // R_X86_64_RELATIVE
        case Arch::AArch64: return 1027;  // R_AARCH64_RELATIVE
        case Arch::RISCV64: return 3;      // R_RISCV_RELATIVE
        default: return 0;
    }
}

bool TargetInfo::supportsOutputKind(OS os, Arch arch, Artifact artifact) {
    // ELF formats support Linux, Android, FreeBSD, and BareMetal
    if (os == OS::Linux || os == OS::Android || os == OS::FreeBSD || os == OS::BareMetal) {
        if (artifact == Artifact::Executable || artifact == Artifact::SharedLibrary) {
            return arch == Arch::X64 || arch == Arch::AArch64 || arch == Arch::RISCV64;
        }
        if (artifact == Artifact::StaticLibrary) {
            return true;
        }
    }
    // COFF/PE formats support Windows
    if (os == OS::Windows) {
        if (artifact == Artifact::Executable || artifact == Artifact::SharedLibrary) {
            return arch == Arch::X64 || arch == Arch::AArch64 || arch == Arch::RISCV64;
        }
        if (artifact == Artifact::StaticLibrary) {
            return true;
        }
    }
    // Mach-O formats support MacOS
    if (os == OS::MacOS) {
        if (artifact == Artifact::Executable || artifact == Artifact::SharedLibrary) {
            return arch == Arch::X64 || arch == Arch::AArch64;
        }
        if (artifact == Artifact::StaticLibrary) {
            return true;
        }
    }
    // WASM formats support WASI
    if (os == OS::WASI) {
        if (artifact == Artifact::WasmModule) {
            return arch == Arch::WASM32;
        }
    }
    return false;
}

}
