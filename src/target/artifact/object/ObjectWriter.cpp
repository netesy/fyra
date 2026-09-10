#include "target/artifact/object/ObjectWriter.h"
#include "target/artifact/object/ElfObjectWriter.h"
#include "target/artifact/object/CoffObjectWriter.h"
#include "target/artifact/object/MachObjectWriter.h"
#include <algorithm>
#include <cctype>

namespace target {
namespace artifact {
namespace object {

std::unique_ptr<ObjectWriter> ObjectWriter::createForTarget(const target::TargetDescriptor& desc) {
    if (desc.os == target::OS::Windows) {
        return std::make_unique<CoffObjectWriter>();
    } else if (desc.os == target::OS::MacOS) {
        return std::make_unique<MachObjectWriter>();
    } else {
        return std::make_unique<ElfObjectWriter>();
    }
}

std::unique_ptr<ObjectWriter> ObjectWriter::createForTargetTriple(const std::string& targetTriple) {
    auto desc = target::TargetDescriptor::fromString(targetTriple);
    if (desc) {
        return createForTarget(*desc);
    }
    std::string t = targetTriple;
    for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (t.find("win") != std::string::npos || t.find("pe") != std::string::npos) {
        return std::make_unique<CoffObjectWriter>();
    } else if (t.find("mac") != std::string::npos || t.find("darwin") != std::string::npos) {
        return std::make_unique<MachObjectWriter>();
    } else {
        return std::make_unique<ElfObjectWriter>();
    }
}

} // namespace object
} // namespace artifact
} // namespace target
