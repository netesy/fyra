#pragma once

#include <string>
#include <vector>
#include <cstdint>

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
};

} // namespace linker
} // namespace artifact
} // namespace target
