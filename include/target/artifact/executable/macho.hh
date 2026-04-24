#ifndef MACHO_HH
#define MACHO_HH

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

class MachOGenerator {
public:
    MachOGenerator(const std::string& inputFilename);
    ~MachOGenerator();

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

    void setCpuType(uint32_t type);

    std::string getLastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> pImpl_;
};

#endif // MACHO_HH
