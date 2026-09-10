#include "target/artifact/object/CoffObjectWriter.h"
#include <fstream>
#include <cstring>
#include <ctime>
#include <map>

namespace target {
namespace artifact {
namespace object {

namespace {

constexpr uint16_t IMAGE_FILE_MACHINE_AMD64 = 0x8664;
constexpr uint32_t IMAGE_SCN_CNT_CODE = 0x00000020;
constexpr uint32_t IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040;
constexpr uint32_t IMAGE_SCN_MEM_EXECUTE = 0x20000000;
constexpr uint32_t IMAGE_SCN_MEM_READ = 0x40000000;
constexpr uint32_t IMAGE_SCN_MEM_WRITE = 0x80000000;
constexpr uint32_t IMAGE_SCN_ALIGN_16BYTES = 0x00500000;
constexpr uint32_t IMAGE_SCN_ALIGN_8BYTES = 0x00400000;
constexpr uint8_t IMAGE_SYM_CLASS_EXTERNAL = 2;
constexpr uint8_t IMAGE_SYM_CLASS_STATIC = 3;
constexpr uint16_t IMAGE_REL_AMD64_REL32 = 0x0004;

#pragma pack(push, 1)
struct CoffHeader {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
};

struct CoffSectionHeader {
    char Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
};

struct CoffSymbol {
    union {
        char ShortName[8];
        struct {
            uint32_t Zeros;
            uint32_t Offset;
        } LongName;
    } Name;
    uint32_t Value;
    int16_t SectionNumber;
    uint16_t Type;
    uint8_t StorageClass;
    uint8_t NumberOfAuxSymbols;
};

struct CoffRelocation {
    uint32_t VirtualAddress;
    uint32_t SymbolTableIndex;
    uint16_t Type;
};
#pragma pack(pop)

} // namespace

std::vector<uint8_t> CoffObjectWriter::serialize(const ObjectArtifact& artifact) {
    struct SecInfo {
        std::string name;
        std::vector<uint8_t> data;
        uint32_t characteristics;
        std::vector<CoffRelocation> relocs;
    };
    std::vector<SecInfo> secs;

    for (const auto& kv : artifact.sections) {
        if (kv.second.data.empty()) continue;
        SecInfo s;
        s.name = kv.first;
        s.data = kv.second.data;
        if (s.name == ".text" || s.name == "CODE") {
            s.characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES;
        } else {
            s.characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_ALIGN_8BYTES;
        }
        secs.push_back(s);
    }

    std::vector<CoffSymbol> coffSyms;
    std::vector<uint8_t> stringTable;
    stringTable.resize(4, 0);

    auto add_string = [&](const std::string& name) -> uint32_t {
        uint32_t offset = static_cast<uint32_t>(stringTable.size());
        stringTable.insert(stringTable.end(), name.begin(), name.end());
        stringTable.push_back('\0');
        return offset;
    };

    std::map<std::string, uint32_t> symIndexMap;
    for (const auto& sym : artifact.symbols) {
        CoffSymbol cs = {};
        if (sym.name.length() <= 8) {
            std::memcpy(cs.Name.ShortName, sym.name.c_str(), sym.name.length());
        } else {
            cs.Name.LongName.Zeros = 0;
            cs.Name.LongName.Offset = add_string(sym.name);
        }
        cs.Value = static_cast<uint32_t>(sym.value);

        int16_t secNum = 0;
        for (size_t i = 0; i < secs.size(); ++i) {
            if (secs[i].name == sym.sectionName) {
                secNum = static_cast<int16_t>(i + 1);
                break;
            }
        }
        cs.SectionNumber = secNum;
        cs.Type = (sym.type == SymbolType::Function) ? 0x20 : 0x00;
        cs.StorageClass = (sym.binding == SymbolBinding::Local) ? IMAGE_SYM_CLASS_STATIC : IMAGE_SYM_CLASS_EXTERNAL;
        cs.NumberOfAuxSymbols = 0;

        symIndexMap[sym.name] = static_cast<uint32_t>(coffSyms.size());
        coffSyms.push_back(cs);
    }

    for (const auto& r : artifact.relocations) {
        for (auto& s : secs) {
            if (s.name == r.sectionName) {
                CoffRelocation cr = {};
                cr.VirtualAddress = static_cast<uint32_t>(r.offset);
                cr.SymbolTableIndex = symIndexMap.count(r.symbolName) ? symIndexMap[r.symbolName] : 0;
                cr.Type = (r.type == "R_X86_64_PC32" || r.type == "R_X86_64_PLT32") ? IMAGE_REL_AMD64_REL32 : 0x0001;
                s.relocs.push_back(cr);
                break;
            }
        }
    }

    uint32_t strTableSize = static_cast<uint32_t>(stringTable.size());
    std::memcpy(stringTable.data(), &strTableSize, 4);

    uint32_t headerSize = sizeof(CoffHeader);
    uint32_t sectionHeadersSize = static_cast<uint32_t>(secs.size() * sizeof(CoffSectionHeader));
    uint32_t currentOffset = headerSize + sectionHeadersSize;

    std::vector<CoffSectionHeader> cHeaders;
    for (auto& s : secs) {
        CoffSectionHeader csh = {};
        std::strncpy(csh.Name, s.name.c_str(), 8);
        csh.VirtualSize = 0;
        csh.VirtualAddress = 0;
        csh.SizeOfRawData = static_cast<uint32_t>(s.data.size());
        csh.PointerToRawData = currentOffset;
        currentOffset += csh.SizeOfRawData;

        if (!s.relocs.empty()) {
            csh.PointerToRelocations = currentOffset;
            csh.NumberOfRelocations = static_cast<uint16_t>(s.relocs.size());
            currentOffset += static_cast<uint32_t>(s.relocs.size() * sizeof(CoffRelocation));
        } else {
            csh.PointerToRelocations = 0;
            csh.NumberOfRelocations = 0;
        }

        csh.PointerToLinenumbers = 0;
        csh.NumberOfLinenumbers = 0;
        csh.Characteristics = s.characteristics;
        cHeaders.push_back(csh);
    }

    uint32_t ptrToSymbolTable = currentOffset;
    uint32_t totalFileSize = ptrToSymbolTable + static_cast<uint32_t>(coffSyms.size() * sizeof(CoffSymbol) + stringTable.size());

    std::vector<uint8_t> buffer(totalFileSize, 0);

    CoffHeader ch = {};
    ch.Machine = IMAGE_FILE_MACHINE_AMD64;
    ch.NumberOfSections = static_cast<uint16_t>(secs.size());
    ch.TimeDateStamp = static_cast<uint32_t>(time(0));
    ch.PointerToSymbolTable = ptrToSymbolTable;
    ch.NumberOfSymbols = static_cast<uint32_t>(coffSyms.size());
    ch.SizeOfOptionalHeader = 0;
    ch.Characteristics = 0;

    std::memcpy(buffer.data(), &ch, sizeof(ch));
    std::memcpy(buffer.data() + sizeof(ch), cHeaders.data(), cHeaders.size() * sizeof(CoffSectionHeader));

    for (size_t i = 0; i < secs.size(); ++i) {
        if (!secs[i].data.empty()) {
            std::memcpy(buffer.data() + cHeaders[i].PointerToRawData, secs[i].data.data(), secs[i].data.size());
        }
        if (!secs[i].relocs.empty()) {
            std::memcpy(buffer.data() + cHeaders[i].PointerToRelocations, secs[i].relocs.data(), secs[i].relocs.size() * sizeof(CoffRelocation));
        }
    }

    std::memcpy(buffer.data() + ptrToSymbolTable, coffSyms.data(), coffSyms.size() * sizeof(CoffSymbol));
    std::memcpy(buffer.data() + ptrToSymbolTable + coffSyms.size() * sizeof(CoffSymbol), stringTable.data(), stringTable.size());

    return buffer;
}

bool CoffObjectWriter::write(const ObjectArtifact& artifact, const std::string& outputPath) {
    auto data = serialize(artifact);
    std::ofstream ofs(outputPath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        lastError_ = "Failed to open output file: " + outputPath;
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
    return ofs.good();
}

} // namespace object
} // namespace artifact
} // namespace target
