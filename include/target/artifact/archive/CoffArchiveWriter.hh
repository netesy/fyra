#pragma once

#include "target/artifact/archive/ArchiveWriter.hh"

namespace target {
namespace artifact {
namespace archive {

class CoffArchiveWriter : public ArchiveWriter {
public:
    bool writeArchive(const std::vector<ArchiveMember>& members,
                      const std::string& outputPath,
                      std::string& errorOutput) override;
};

} // namespace archive
} // namespace artifact
} // namespace target
