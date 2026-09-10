#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include "target/artifact/linker/LinkedImage.h"

class ElfGenerator {
public:
    explicit ElfGenerator(const std::string& inputFilename, bool is64Bit = true, uint64_t baseAddress = 0x400000);
    ~ElfGenerator();

    bool generate(const std::string& assemblyPath, const std::string& outputPath, bool generateRelocatable = false);

    struct Symbol {
        std::string name;
        uint64_t value = 0;
        uint64_t size = 0;
        uint8_t type = 0;
        uint8_t binding = 1;
        std::string sectionName;
    };

    struct Relocation {
        uint64_t offset;
        std::string type;
        int64_t addend;
        std::string symbolName;
        std::string sectionName;
    };

    bool generateFromCode(const std::map<std::string, std::vector<uint8_t>>& sections,
                          const std::vector<Symbol>& symbols,
                          const std::vector<Relocation>& relocations,
                          const std::string& outputPath);

    bool generateRelocatableFromCode(const std::map<std::string, std::vector<uint8_t>>& sections,
                                    const std::vector<Symbol>& symbols,
                                    const std::vector<Relocation>& relocations,
                                    const std::string& outputPath);

    bool generateExecutableFromLinkedImage(const target::artifact::linker::LinkedImage& image,
                                           const std::string& outputPath);

    void setBaseAddress(uint64_t address);
    void setPageSize(uint64_t size);
    void setEntryPointName(const std::string& name);
    void setMachine(uint16_t machine);
    void setStrip(bool strip);
    std::string getLastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl;

    ElfGenerator(const ElfGenerator&) = delete;
    ElfGenerator& operator=(const ElfGenerator&) = delete;
    ElfGenerator(ElfGenerator&&) = delete;
    ElfGenerator& operator=(ElfGenerator&&) = delete;
};
