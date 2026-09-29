#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "target/core/TargetDescriptor.h"

namespace target {
namespace artifact {
namespace linker {

enum class RelocationKind {
    Absolute,
    PcRelative,
    CallRelative,
    BranchRelative,
    Unknown
};

class TargetRelocationEvaluator {
public:
    static RelocationKind normalizeType(const std::string& typeStr);

    static bool evaluate(RelocationKind kind,
                         uint64_t symbolAddress,
                         uint64_t placeAddress,
                         int64_t addend,
                         std::vector<uint8_t>& sectionData,
                         uint64_t offset,
                         std::string& errorOut);

    static std::vector<uint8_t> getImportThunkBytes(target::Arch arch, target::OS os);
};

} // namespace linker
} // namespace artifact
} // namespace target
