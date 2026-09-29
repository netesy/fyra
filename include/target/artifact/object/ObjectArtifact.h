#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include "target/core/TargetDescriptor.h"

namespace target {
namespace artifact {
namespace object {

enum class ObjectFormat {
    ELF,
    COFF,
    MachO,
    Unknown
};

struct ObjectSection {
    std::string name;
    std::vector<uint8_t> data;
    uint64_t virtualSize = 0;
    uint64_t virtualAddress = 0;
    uint64_t alignment = 16;
    uint32_t flags = 0;
};

enum class SymbolBinding {
    Local,
    Global,
    Weak
};

enum class SymbolType {
    NoType,
    Function,
    Object,
    Section,
    File
};

struct ObjectSymbol {
    std::string name;
    uint64_t value = 0;
    uint64_t size = 0;
    SymbolBinding binding = SymbolBinding::Global;
    SymbolType type = SymbolType::NoType;
    std::string sectionName;
    bool isDefined = true;
};

struct ObjectRelocation {
    uint64_t offset = 0;
    std::string type;
    int64_t addend = 0;
    std::string symbolName;
    std::string sectionName;
};

struct ObjectArtifact {
    ObjectFormat format = ObjectFormat::ELF;
    target::Arch arch = target::Arch::X64;
    target::OS os = target::OS::Linux;

    std::map<std::string, ObjectSection> sections;
    std::vector<ObjectSymbol> symbols;
    std::vector<ObjectRelocation> relocations;

    void addSection(const ObjectSection& sec) {
        sections[sec.name] = sec;
    }

    void addSymbol(const ObjectSymbol& sym) {
        symbols.push_back(sym);
    }

    void addRelocation(const ObjectRelocation& reloc) {
        relocations.push_back(reloc);
    }

    const ObjectSection* findSection(const std::string& name) const {
        auto it = sections.find(name);
        return (it != sections.end()) ? &it->second : nullptr;
    }

    const ObjectSymbol* findSymbol(const std::string& name) const {
        for (const auto& sym : symbols) {
            if (sym.name == name) return &sym;
        }
        return nullptr;
    }
};

} // namespace object
} // namespace artifact
} // namespace target
