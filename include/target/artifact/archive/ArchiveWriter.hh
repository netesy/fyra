#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace target {
namespace artifact {
namespace archive {

struct ArchiveMember {
    std::string name;
    std::vector<uint8_t> data;
    std::vector<std::string> exportedSymbols;
};

class ArchiveWriter {
public:
    virtual ~ArchiveWriter() = default;
    virtual bool writeArchive(const std::vector<ArchiveMember>& members,
                              const std::string& outputPath,
                              std::string& errorOutput) = 0;
};

} // namespace archive
} // namespace artifact
} // namespace target
