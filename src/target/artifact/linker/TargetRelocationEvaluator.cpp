#include "target/artifact/linker/TargetRelocationEvaluator.h"
#include <cstring>
#include <limits>

namespace target {
namespace artifact {
namespace linker {

RelocationKind TargetRelocationEvaluator::normalizeType(const std::string& typeStr) {
    if (typeStr == "R_X86_64_PC32" || typeStr == "R_X86_64_PLT32" || typeStr == "IMAGE_REL_AMD64_REL32" ||
        typeStr == "X86_64_RELOC_BRANCH" || typeStr == "X86_64_RELOC_SIGNED") {
        return RelocationKind::PcRelative;
    }
    if (typeStr == "R_X86_64_64" || typeStr == "IMAGE_REL_AMD64_ADDR64" || typeStr == "R_AARCH64_ABS64" || typeStr == "R_RISCV_64") {
        return RelocationKind::Absolute;
    }
    if (typeStr == "R_AARCH64_CALL26" || typeStr == "R_AARCH64_JUMP26" || typeStr == "R_RISCV_JAL" || typeStr == "R_RISCV_CALL") {
        return RelocationKind::CallRelative;
    }
    return RelocationKind::Unknown;
}

bool TargetRelocationEvaluator::evaluate(RelocationKind kind,
                                          uint64_t symbolAddress,
                                          uint64_t placeAddress,
                                          int64_t addend,
                                          std::vector<uint8_t>& sectionData,
                                          uint64_t offset,
                                          std::string& errorOut) {
    if (kind == RelocationKind::PcRelative) {
        if (offset + 4 > sectionData.size()) {
            errorOut = "Relocation offset out of bounds for 32-bit PC-relative relocation";
            return false;
        }
        int64_t delta = static_cast<int64_t>(symbolAddress) + addend - static_cast<int64_t>(placeAddress);
        if (delta < -2147483648LL || delta > 2147483647LL) {
            errorOut = "Relocation overflow for 32-bit PC-relative relocation (delta = " + std::to_string(delta) + ")";
            return false;
        }
        int32_t val32 = static_cast<int32_t>(delta);
        std::memcpy(sectionData.data() + offset, &val32, 4);
        return true;
    }

    if (kind == RelocationKind::Absolute) {
        if (offset + 8 > sectionData.size()) {
            errorOut = "Relocation offset out of bounds for 64-bit absolute relocation";
            return false;
        }
        uint64_t val64 = symbolAddress + addend;
        std::memcpy(sectionData.data() + offset, &val64, 8);
        return true;
    }

    if (kind == RelocationKind::CallRelative) {
        if (offset + 4 > sectionData.size()) {
            errorOut = "Relocation offset out of bounds for call-relative relocation";
            return false;
        }
        int64_t delta = static_cast<int64_t>(symbolAddress) + addend - static_cast<int64_t>(placeAddress);
        int32_t val32 = static_cast<int32_t>(delta);
        std::memcpy(sectionData.data() + offset, &val32, 4);
        return true;
    }

    errorOut = "Unsupported relocation kind";
    return false;
}

} // namespace linker
} // namespace artifact
} // namespace target
