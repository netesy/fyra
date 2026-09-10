#pragma once

#include "target/artifact/archive/ArchiveWriter.hh"

namespace target {
namespace artifact {
namespace archive {

class UnixArchiveWriter : public ArchiveWriter {
public:
    bool writeArchive(const std::vector<ArchiveMember>& members,
                      const std::string& outputPath,
                      std::string& errorOutput) override;

    static std::vector<std::string> extractExportedElfSymbols(const std::vector<uint8_t>& data);
};

} // namespace archive
} // namespace artifact
} // namespace target
