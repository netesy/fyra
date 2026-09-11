#pragma once

#include "target/artifact/linker/DynamicLinkPlan.h"
#include "target/artifact/linker/LinkedImage.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace target::artifact::executable {

enum class PeImageKind { Executable, Dll };

struct PeSection {
    std::string name;
    std::vector<uint8_t> data;
    uint32_t virtualSize = 0;
    uint32_t characteristics = 0;
};

struct PeExport {
    std::string name;
    std::string sectionName;
    uint32_t sectionOffset = 0;
};

struct PeImportSymbol {
    std::string name;
    uint16_t hint = 0;
    bool isOrdinal = false;
    uint32_t ordinal = 0;
    bool isData = false;
};

struct PeImportDirectory {
    std::string dllName;
    std::vector<PeImportSymbol> symbols;
};

struct PeDataDirectory {
    uint32_t virtualAddress = 0;
    uint32_t size = 0;
};

// Target-specific semantic description below the neutral linker boundary.
struct PeImage {
    PeImageKind kind = PeImageKind::Executable;
    uint16_t machine = 0x8664;
    uint64_t imageBase = 0x140000000ULL;
    uint32_t sectionAlignment = 0x1000;
    uint32_t fileAlignment = 0x200;
    uint32_t entryRva = 0;
    uint16_t subsystem = 3;
    uint16_t dllCharacteristics = 0;
    std::string imageName;
    std::vector<PeSection> sections;
    std::vector<PeExport> exports;
    std::vector<PeImportDirectory> imports;
    std::vector<uint64_t> relocationFixupVmas;
    std::map<std::string, uint64_t> importThunkVmas;
    std::array<PeDataDirectory, 16> dataDirectories{};
};

class PeImageWriter {
public:
    bool write(PeImage image, const std::string& outputPath);
    const std::string& getLastError() const { return lastError_; }

private:
    std::string lastError_;
};

class PeExecutableImageBuilder {
public:
    bool build(const linker::LinkedImage& image, const std::string& outputPath);
    bool buildWithPlan(const linker::DynamicLinkPlan& plan, const std::string& outputPath);
    const std::string& getLastError() const { return lastError_; }

private:
    std::string lastError_;
};

PeImage createPeImageFromDynamicPlan(const linker::DynamicLinkPlan& plan,
                                     const std::string& imageName);

} // namespace target::artifact::executable
