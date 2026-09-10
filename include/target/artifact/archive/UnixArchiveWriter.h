#pragma once

#include "target/artifact/archive/ArchiveWriter.h"

namespace target {
namespace artifact {
namespace archive {

// GNU / System V archive writer (.a)
class UnixArchiveWriter : public ArchiveWriter {
public:
    UnixArchiveWriter() = default;
    ~UnixArchiveWriter() override = default;

    bool write(const std::vector<ArchiveMember>& members, const std::string& outputPath) override;

    // Direct binary serialization helper
    std::vector<uint8_t> serialize(const std::vector<ArchiveMember>& members);
};

} // namespace archive
} // namespace artifact
} // namespace target
