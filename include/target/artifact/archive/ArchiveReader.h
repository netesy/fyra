#pragma once

#include "target/artifact/object/ObjectArtifact.h"
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace target {
namespace artifact {
namespace archive {

struct ArchiveObjectMember {
    std::string name;
    std::vector<uint8_t> bytes;
    target::artifact::object::ObjectArtifact artifact;
};

class ArchiveReader {
public:
    ArchiveReader() = default;
    ~ArchiveReader() = default;

    bool parse(const std::vector<uint8_t>& bytes, std::vector<ArchiveObjectMember>& outMembers);
    bool read(const std::string& inputPath, std::vector<ArchiveObjectMember>& outMembers);

    std::string getLastError() const { return lastError_; }

private:
    std::string lastError_;
};

} // namespace archive
} // namespace artifact
} // namespace target
