#include "target/artifact/object/ObjectReader.h"
#include "target/artifact/object/ElfObjectReader.h"
#include "target/artifact/object/CoffObjectReader.h"
#include "target/artifact/object/MachObjectReader.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace target {
namespace artifact {
namespace object {

std::unique_ptr<ObjectReader> ObjectReader::createForTarget(const target::TargetDescriptor& desc) {
    if (desc.os == target::OS::Windows) {
        return std::make_unique<CoffObjectReader>();
    } else if (desc.os == target::OS::MacOS) {
        return std::make_unique<MachObjectReader>();
    } else {
        return std::make_unique<ElfObjectReader>();
    }
}

std::unique_ptr<ObjectReader> ObjectReader::createForTargetTriple(const std::string& targetTriple) {
    auto desc = target::TargetDescriptor::fromString(targetTriple);
    if (desc) {
        return createForTarget(*desc);
    }
    std::string t = targetTriple;
    for (auto& c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (t.find("win") != std::string::npos || t.find("pe") != std::string::npos) {
        return std::make_unique<CoffObjectReader>();
    } else if (t.find("mac") != std::string::npos || t.find("darwin") != std::string::npos) {
        return std::make_unique<MachObjectReader>();
    } else {
        return std::make_unique<ElfObjectReader>();
    }
}

std::unique_ptr<ObjectReader> ObjectReader::detectAndCreate(const std::vector<uint8_t>& bytes) {
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "\x7f""ELF", 4) == 0) {
        return std::make_unique<ElfObjectReader>();
    }
    if (bytes.size() >= 4 && *reinterpret_cast<const uint32_t*>(bytes.data()) == 0xfeedfacf) {
        return std::make_unique<MachObjectReader>();
    }
    if (bytes.size() >= 2 && (*reinterpret_cast<const uint16_t*>(bytes.data()) == 0x8664 || *reinterpret_cast<const uint16_t*>(bytes.data()) == 0xAA64)) {
        return std::make_unique<CoffObjectReader>();
    }
    return std::make_unique<ElfObjectReader>(); // Fallback
}

} // namespace object
} // namespace artifact
} // namespace target
