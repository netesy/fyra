#include "target/artifact/object/CoffObjectReader.h"
#include "target/core/TargetInfo.h"
#include <fstream>
#include <cstring>

namespace target {
namespace artifact {
namespace object {

namespace {

constexpr uint16_t IMAGE_FILE_MACHINE_AMD64 = 0x8664;
constexpr uint8_t IMAGE_SYM_CLASS_EXTERNAL = 2;
constexpr uint8_t IMAGE_SYM_CLASS_STATIC = 3;

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

bool CoffObjectReader::parse(const std::vector<uint8_t>& bytes, ObjectArtifact& outArtifact) {
    lastError_.clear();
    outArtifact = ObjectArtifact{};
    outArtifact.format = ObjectFormat::COFF;
    outArtifact.os = target::OS::Windows;
    outArtifact.arch = target::Arch::X64;

    if (bytes.size() < sizeof(CoffHeader)) {
        lastError_ = "File too small for COFF header";
        return false;
    }

    const auto* ch = reinterpret_cast<const CoffHeader*>(bytes.data());
    
    // Map COFF machine types to architecture enums using TargetInfo
    bool validMachine = false;
    if (ch->Machine == target::TargetInfo::getCoffMachine(target::Arch::X64)) {
        outArtifact.arch = target::Arch::X64;
        validMachine = true;
    } else if (ch->Machine == target::TargetInfo::getCoffMachine(target::Arch::AArch64)) {
        outArtifact.arch = target::Arch::AArch64;
        validMachine = true;
    } else if (ch->Machine == target::TargetInfo::getCoffMachine(target::Arch::RISCV64)) {
        outArtifact.arch = target::Arch::RISCV64;
        validMachine = true;
    } else if (ch->Machine == target::TargetInfo::getCoffMachine(target::Arch::LoongArch64)) {
        outArtifact.arch = target::Arch::LoongArch64;
        validMachine = true;
    }
    
    if (!validMachine) {
        lastError_ = "Invalid COFF machine type: 0x" + std::to_string(ch->Machine);
        return false;
    }

    uint64_t secHdrOffset = sizeof(CoffHeader) + ch->SizeOfOptionalHeader;
    if (secHdrOffset + static_cast<uint64_t>(ch->NumberOfSections) * sizeof(CoffSectionHeader) > bytes.size()) {
        lastError_ = "Section headers out of bounds";
        return false;
    }

    const auto* secHdrs = reinterpret_cast<const CoffSectionHeader*>(bytes.data() + secHdrOffset);

    std::vector<std::string> sectionNames(ch->NumberOfSections);
    for (uint16_t i = 0; i < ch->NumberOfSections; ++i) {
        const auto& sh = secHdrs[i];
        char nameBuf[9] = {0};
        std::memcpy(nameBuf, sh.Name, 8);
        sectionNames[i] = nameBuf;

        ObjectSection sec;
        sec.name = nameBuf;
        sec.virtualSize = sh.VirtualSize ? sh.VirtualSize : sh.SizeOfRawData;
        sec.virtualAddress = sh.VirtualAddress;
        sec.flags = sh.Characteristics;

        if (sh.PointerToRawData > 0 && sh.PointerToRawData + sh.SizeOfRawData <= bytes.size()) {
            sec.data.assign(bytes.data() + sh.PointerToRawData, bytes.data() + sh.PointerToRawData + sh.SizeOfRawData);
        }

        outArtifact.addSection(sec);
    }

    // String table starts after symbol table
    uint64_t strTableOffset = ch->PointerToSymbolTable + static_cast<uint64_t>(ch->NumberOfSymbols) * sizeof(CoffSymbol);

    std::vector<std::string> symbolNames(ch->NumberOfSymbols);

    if (ch->PointerToSymbolTable > 0 && strTableOffset <= bytes.size()) {
        const auto* syms = reinterpret_cast<const CoffSymbol*>(bytes.data() + ch->PointerToSymbolTable);

        for (uint32_t i = 0; i < ch->NumberOfSymbols; ++i) {
            const auto& s = syms[i];
            std::string symName;
            if (s.Name.LongName.Zeros == 0) {
                uint32_t strOff = s.Name.LongName.Offset;
                if (strTableOffset + strOff < bytes.size()) {
                    symName = reinterpret_cast<const char*>(bytes.data() + strTableOffset + strOff);
                }
            } else {
                char shortBuf[9] = {0};
                std::memcpy(shortBuf, s.Name.ShortName, 8);
                symName = shortBuf;
            }

            symbolNames[i] = symName;

            if (!symName.empty() && (s.StorageClass == IMAGE_SYM_CLASS_EXTERNAL ||
                                     s.StorageClass == IMAGE_SYM_CLASS_STATIC)) {
                ObjectSymbol osym;
                osym.name = symName;
                osym.value = s.Value;
                osym.binding = s.StorageClass == IMAGE_SYM_CLASS_EXTERNAL
                                   ? SymbolBinding::Global : SymbolBinding::Local;
                osym.type = (s.Type == 0x20) ? SymbolType::Function : SymbolType::NoType;

                if (s.SectionNumber > 0 && static_cast<size_t>(s.SectionNumber) <= sectionNames.size()) {
                    osym.sectionName = sectionNames[s.SectionNumber - 1];
                    osym.isDefined = true;
                } else {
                    osym.sectionName = "*UND*";
                    osym.isDefined = false;
                }

                outArtifact.addSymbol(osym);
            }

            i += s.NumberOfAuxSymbols;
        }
    }

    // Relocations
    for (uint16_t i = 0; i < ch->NumberOfSections; ++i) {
        const auto& sh = secHdrs[i];
        if (sh.PointerToRelocations > 0 && sh.PointerToRelocations + static_cast<uint64_t>(sh.NumberOfRelocations) * sizeof(CoffRelocation) <= bytes.size()) {
            const auto* relocs = reinterpret_cast<const CoffRelocation*>(bytes.data() + sh.PointerToRelocations);
            for (uint16_t r = 0; r < sh.NumberOfRelocations; ++r) {
                const auto& rel = relocs[r];
                ObjectRelocation orel;
                orel.offset = rel.VirtualAddress;
                orel.sectionName = sectionNames[i];
                if (rel.SymbolTableIndex < symbolNames.size()) {
                    orel.symbolName = symbolNames[rel.SymbolTableIndex];
                }
                if (outArtifact.arch == target::Arch::AArch64) {
                    if (rel.Type == 0x0003) orel.type = "R_AARCH64_CALL26";
                    else if (rel.Type == 0x0004) orel.type = "R_AARCH64_ADR_PREL_PG_HI21";
                    else if (rel.Type == 0x0006) orel.type = "R_AARCH64_ADD_ABS_LO12_NC";
                    else if (rel.Type == 0x000E) orel.type = "R_AARCH64_ABS64";
                    else orel.type = "R_TYPE_" + std::to_string(rel.Type);
                } else if (outArtifact.arch == target::Arch::RISCV64) {
                    if (rel.Type == 0x000A) orel.type = "R_RISCV_CALL";
                    else if (rel.Type == 0x0007) orel.type = "R_RISCV_BRANCH";
                    else if (rel.Type == 0x0002) orel.type = "R_RISCV_64";
                    else orel.type = "R_TYPE_" + std::to_string(rel.Type);
                } else if (outArtifact.arch == target::Arch::LoongArch64) {
                    if (rel.Type == 0x000A) orel.type = "R_LARCH_CALL";
                    else if (rel.Type == 0x0004) orel.type = "R_LARCH_B16";
                    else if (rel.Type == 0x0002) orel.type = "R_LARCH_64";
                    else orel.type = "R_TYPE_" + std::to_string(rel.Type);
                } else {
                    if (rel.Type == 0x0004) {
                        orel.type = "R_X86_64_PC32";
                        orel.addend = -4;
                    } else if (rel.Type == 0x0001) {
                        orel.type = "R_X86_64_64";
                    } else {
                        orel.type = "R_TYPE_" + std::to_string(rel.Type);
                    }
                }
                outArtifact.addRelocation(orel);
            }
        }
    }

    return true;
}

bool CoffObjectReader::read(const std::string& inputPath, ObjectArtifact& outArtifact) {
    std::ifstream file(inputPath, std::ios::binary);
    if (!file.is_open()) {
        lastError_ = "Failed to open input file: " + inputPath;
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return parse(bytes, outArtifact);
}

} // namespace object
} // namespace artifact
} // namespace target
