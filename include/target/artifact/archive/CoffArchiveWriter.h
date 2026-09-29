#pragma once

#include "target/artifact/archive/ArchiveWriter.h"

namespace target {
namespace artifact {
namespace archive {

// Windows COFF static library writer (.lib)
class CoffArchiveWriter : public ArchiveWriter {
public:
    CoffArchiveWriter() = default;
    ~CoffArchiveWriter() override = default;

    bool write(const std::vector<ArchiveMember>& members, const std::string& outputPath) override;

    // Direct binary serialization helper
    std::vector<uint8_t> serialize(const std::vector<ArchiveMember>& members);
};

} // namespace archive
} // namespace artifact
} // namespace target
