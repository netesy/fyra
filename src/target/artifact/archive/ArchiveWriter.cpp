#include "target/artifact/archive/ArchiveWriter.h"
#include "target/artifact/archive/UnixArchiveWriter.h"
#include "target/artifact/archive/CoffArchiveWriter.h"
#include "target/artifact/archive/BsdArchiveWriter.h"
#include <algorithm>
#include <cctype>

namespace target {
namespace artifact {
namespace archive {

std::unique_ptr<ArchiveWriter> ArchiveWriter::createForTarget(const target::TargetDescriptor& desc) {
    if (desc.os == target::OS::Windows) {
        return std::make_unique<CoffArchiveWriter>();
    } else if (desc.os == target::OS::MacOS) {
        return std::make_unique<BsdArchiveWriter>();
    } else {
        return std::make_unique<UnixArchiveWriter>();
    }
}

std::unique_ptr<ArchiveWriter> ArchiveWriter::createForTargetTriple(const std::string& targetTriple) {
    auto desc = target::TargetDescriptor::fromString(targetTriple);
    if (desc) {
        return createForTarget(*desc);
    }
    std::string t = targetTriple;
    for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (t.find("win") != std::string::npos || t.find("pe") != std::string::npos) {
        return std::make_unique<CoffArchiveWriter>();
    } else if (t.find("mac") != std::string::npos || t.find("darwin") != std::string::npos) {
        return std::make_unique<BsdArchiveWriter>();
    } else {
        return std::make_unique<UnixArchiveWriter>();
    }
}

} // namespace archive
} // namespace artifact
} // namespace target
