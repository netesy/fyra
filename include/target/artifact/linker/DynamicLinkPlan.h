#pragma once

#include "target/artifact/linker/LinkedImage.h"
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace target {
namespace artifact {
namespace linker {

struct DynamicExport {
    std::string symbol;
    bool isFunction = true;
    uint64_t address = 0;
    uint64_t size = 0;
    std::string sectionName;
};

enum class DynamicImportKind {
    Function,
    Data
};

struct DynamicImport {
    std::string symbol;
    std::string dependencyLibrary;
    bool isWeak = false;
    DynamicImportKind kind = DynamicImportKind::Function;
    bool isOrdinal = false;
    uint32_t ordinal = 0;
};

struct DynamicDependency {
    std::string libraryName;
};

enum class DynamicRelocationType {
    AbsolutePointer,
    RelativePointer,
    Copy
};

struct DynamicRelocation {
    uint64_t offset = 0;
    std::string symbol;
    DynamicRelocationType type = DynamicRelocationType::AbsolutePointer;
    int64_t addend = 0;
    std::string sectionName;
};

class DynamicLinkPlan {
public:
    DynamicLinkPlan() = default;
    ~DynamicLinkPlan() = default;

    static DynamicLinkPlan createFromLinkedImage(const LinkedImage& image,
                                                        const std::vector<DynamicImport>& dynamicImports = {});

    target::Arch arch = target::Arch::X64;
    target::OS os = target::OS::Linux;
    LinkOutputKind outputKind = LinkOutputKind::SharedLibrary;

    std::map<std::string, LinkedSection> sections;
    std::vector<DynamicExport> exports;
    std::vector<DynamicImport> imports;
    std::vector<DynamicDependency> dependencies;
    std::vector<DynamicRelocation> relocations;

    std::vector<uint64_t> relocationFixupVmas;
    std::map<std::string, uint64_t> importThunkVmas;
    std::vector<LinkedImage::DataImportFixup> dataImportFixups;

    uint64_t entryAddress = 0;
    std::string entrySymbolName;

    const LinkedSection* findSection(const std::string& name) const;
};

} // namespace linker
} // namespace artifact
} // namespace target
