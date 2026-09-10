#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "target/core/TargetDescriptor.h"
#include "target/artifact/object/ObjectArtifact.h"

namespace target {
namespace artifact {
namespace archive {

struct ArchiveMember {
    std::string name;
    std::vector<uint8_t> bytes;
    std::vector<std::string> exportedSymbols;
    target::artifact::object::ObjectArtifact artifact;
    uint32_t mtime = 0;
    uint32_t uid = 0;
    uint32_t gid = 0;
    uint32_t mode = 0100644;
};

class ArchiveWriter {
public:
    virtual ~ArchiveWriter() = default;

    virtual bool write(const std::vector<ArchiveMember>& members, const std::string& outputPath) = 0;
    virtual std::string getLastError() const { return lastError_; }

    static std::unique_ptr<ArchiveWriter> createForTarget(const target::TargetDescriptor& desc);
    static std::unique_ptr<ArchiveWriter> createForTargetTriple(const std::string& targetTriple);

protected:
    std::string lastError_;
};

} // namespace archive
} // namespace artifact
} // namespace target
