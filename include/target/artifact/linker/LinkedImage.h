#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include "target/core/TargetDescriptor.h"

namespace target {
namespace artifact {
namespace linker {

struct LinkedSection {
    std::string name;
    std::vector<uint8_t> data;
    uint64_t virtualAddress = 0;
    uint64_t virtualSize = 0;
    uint64_t alignment = 16;
    bool isExecutable = false;
    bool isWritable = false;
    bool isReadable = true;
};

struct LinkedSymbol {
    std::string name;
    uint64_t virtualAddress = 0;
    uint64_t size = 0;
    bool isFunction = false;
    bool isGlobal = true;
    std::string sectionName;
};

struct LinkedImage {
    target::Arch arch = target::Arch::X64;
    target::OS os = target::OS::Linux;

    std::map<std::string, LinkedSection> sections;
    std::map<std::string, LinkedSymbol> symbols;
    uint64_t entryAddress = 0;
    std::string entrySymbolName = "main";

    LinkedSection* findSection(const std::string& name) {
        auto it = sections.find(name);
        return (it != sections.end()) ? &it->second : nullptr;
    }

    const LinkedSection* findSection(const std::string& name) const {
        auto it = sections.find(name);
        return (it != sections.end()) ? &it->second : nullptr;
    }

    const LinkedSymbol* findSymbol(const std::string& name) const {
        auto it = symbols.find(name);
        return (it != symbols.end()) ? &it->second : nullptr;
    }
};

} // namespace linker
} // namespace artifact
} // namespace target
