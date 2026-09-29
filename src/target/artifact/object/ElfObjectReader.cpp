#include "target/artifact/object/ElfObjectReader.h"
#include <fstream>
#include <cstring>
#include <iostream>

namespace target {
namespace artifact {
namespace object {

namespace {

constexpr uint16_t ET_REL = 1;
constexpr uint16_t EM_X86_64 = 62;
constexpr uint16_t EM_AARCH64 = 183;
constexpr uint16_t EM_RISCV = 243;
constexpr uint32_t SHT_SYMTAB = 2;
constexpr uint32_t SHT_STRTAB = 3;
constexpr uint32_t SHT_RELA = 4;
constexpr uint32_t SHT_NOBITS = 8;
constexpr uint8_t STB_LOCAL = 0;
constexpr uint8_t STB_GLOBAL = 1;
constexpr uint8_t STB_WEAK = 2;
constexpr uint8_t STT_FUNC = 2;
constexpr uint8_t STT_OBJECT = 1;
constexpr uint16_t SHN_UNDEF = 0;
constexpr uint32_t R_X86_64_64 = 1;
constexpr uint32_t R_X86_64_PC32 = 2;
constexpr uint32_t R_X86_64_PLT32 = 4;

#pragma pack(push, 1)
struct ElfHeader64 {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct SectionHeader64 {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

struct Symbol64 {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

struct Elf64_Rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
};
#pragma pack(pop)

} // namespace

bool ElfObjectReader::parse(const std::vector<uint8_t>& bytes, ObjectArtifact& outArtifact) {
    lastError_.clear();
    outArtifact = ObjectArtifact{};
    outArtifact.format = ObjectFormat::ELF;

    if (bytes.size() < sizeof(ElfHeader64)) {
        lastError_ = "File too small for ELF header";
        return false;
    }

    const auto* ehdr = reinterpret_cast<const ElfHeader64*>(bytes.data());
    if (std::memcmp(ehdr->e_ident, "\x7f""ELF", 4) != 0) {
        lastError_ = "Invalid ELF magic";
        return false;
    }

    if (ehdr->e_type != ET_REL) {
        lastError_ = "Not an ELF relocatable file (type != ET_REL)";
        return false;
    }

    if (ehdr->e_machine == EM_AARCH64) outArtifact.arch = target::Arch::AArch64;
    else if (ehdr->e_machine == EM_RISCV) outArtifact.arch = target::Arch::RISCV64;
    else outArtifact.arch = target::Arch::X64;

    outArtifact.os = target::OS::Linux;

    if (ehdr->e_shoff + static_cast<uint64_t>(ehdr->e_shnum) * sizeof(SectionHeader64) > bytes.size()) {
        lastError_ = "Section headers offset out of bounds";
        return false;
    }

    const auto* shdrs = reinterpret_cast<const SectionHeader64*>(bytes.data() + ehdr->e_shoff);

    if (ehdr->e_shstrndx >= ehdr->e_shnum) {
        lastError_ = "Invalid e_shstrndx";
        return false;
    }
    const auto& shstrHdr = shdrs[ehdr->e_shstrndx];
    if (shstrHdr.sh_offset + shstrHdr.sh_size > bytes.size()) {
        lastError_ = ".shstrtab offset out of bounds";
        return false;
    }
    const char* shstrtab = reinterpret_cast<const char*>(bytes.data() + shstrHdr.sh_offset);

    std::vector<std::string> sectionNames(ehdr->e_shnum);
    for (uint16_t i = 0; i < ehdr->e_shnum; ++i) {
        const auto& sh = shdrs[i];
        if (sh.sh_name < shstrHdr.sh_size) {
            sectionNames[i] = &shstrtab[sh.sh_name];
        }

        if (i == 0) continue;

        ObjectSection sec;
        sec.name = sectionNames[i];
        sec.virtualSize = sh.sh_size;
        sec.virtualAddress = sh.sh_addr;
        sec.alignment = sh.sh_addralign;
        sec.flags = static_cast<uint32_t>(sh.sh_flags);

        if (sh.sh_type != SHT_NOBITS && sh.sh_offset + sh.sh_size <= bytes.size()) {
            sec.data.assign(bytes.data() + sh.sh_offset, bytes.data() + sh.sh_offset + sh.sh_size);
        }

        outArtifact.addSection(sec);
    }

    int symtabIdx = -1;
    for (uint16_t i = 0; i < ehdr->e_shnum; ++i) {
        if (shdrs[i].sh_type == SHT_SYMTAB) {
            symtabIdx = i;
            break;
        }
    }

    std::vector<std::string> symbolNames;

    if (symtabIdx != -1) {
        const auto& symshdr = shdrs[symtabIdx];
        uint32_t strtabIdx = symshdr.sh_link;

        if (strtabIdx < ehdr->e_shnum && symshdr.sh_offset + symshdr.sh_size <= bytes.size()) {
            const auto& strshdr = shdrs[strtabIdx];
            if (strshdr.sh_offset + strshdr.sh_size <= bytes.size()) {
                const char* strtab = reinterpret_cast<const char*>(bytes.data() + strshdr.sh_offset);
                const auto* syms = reinterpret_cast<const Symbol64*>(bytes.data() + symshdr.sh_offset);
                size_t numSyms = symshdr.sh_size / sizeof(Symbol64);

                symbolNames.resize(numSyms);

                for (size_t i = 0; i < numSyms; ++i) {
                    const auto& s = syms[i];
                    std::string symName;
                    if (s.st_name < strshdr.sh_size) {
                        symName = &strtab[s.st_name];
                    }
                    if (symName.empty() && s.st_shndx < ehdr->e_shnum) {
                        symName = sectionNames[s.st_shndx];
                    }
                    symbolNames[i] = symName;

                    if (i == 0 || symName.empty()) continue;

                    ObjectSymbol osym;
                    osym.name = symName;
                    osym.value = s.st_value;
                    osym.size = s.st_size;

                    uint8_t bind = s.st_info >> 4;
                    uint8_t type = s.st_info & 0xF;

                    if (bind == STB_LOCAL) osym.binding = SymbolBinding::Local;
                    else if (bind == STB_WEAK) osym.binding = SymbolBinding::Weak;
                    else osym.binding = SymbolBinding::Global;

                    if (type == STT_FUNC) osym.type = SymbolType::Function;
                    else if (type == STT_OBJECT) osym.type = SymbolType::Object;
                    else osym.type = SymbolType::NoType;

                    if (s.st_shndx != SHN_UNDEF && s.st_shndx < ehdr->e_shnum) {
                        osym.sectionName = sectionNames[s.st_shndx];
                        osym.isDefined = true;
                    } else {
                        osym.sectionName = "*UND*";
                        osym.isDefined = false;
                    }

                    outArtifact.addSymbol(osym);
                }
            }
        }
    }

    for (uint16_t i = 0; i < ehdr->e_shnum; ++i) {
        if (shdrs[i].sh_type == SHT_RELA && shdrs[i].sh_offset + shdrs[i].sh_size <= bytes.size()) {
            std::string secName = sectionNames[i];
            std::string targetSecName;
            if (secName.rfind(".rela", 0) == 0) {
                targetSecName = secName.substr(5);
            } else {
                targetSecName = secName;
            }

            const auto* relas = reinterpret_cast<const Elf64_Rela*>(bytes.data() + shdrs[i].sh_offset);
            size_t numRelas = shdrs[i].sh_size / sizeof(Elf64_Rela);

            for (size_t r = 0; r < numRelas; ++r) {
                const auto& rela = relas[r];
                uint32_t symIdx = static_cast<uint32_t>(rela.r_info >> 32);
                uint32_t typeCode = static_cast<uint32_t>(rela.r_info & 0xFFFFFFFF);

                ObjectRelocation orel;
                orel.offset = rela.r_offset;
                orel.addend = rela.r_addend;
                orel.sectionName = targetSecName;

                if (symIdx < symbolNames.size()) {
                    orel.symbolName = symbolNames[symIdx];
                }

                if (typeCode == R_X86_64_64) orel.type = "R_X86_64_64";
                else if (typeCode == R_X86_64_PC32) orel.type = "R_X86_64_PC32";
                else if (typeCode == R_X86_64_PLT32) orel.type = "R_X86_64_PLT32";
                else orel.type = "R_TYPE_" + std::to_string(typeCode);

                outArtifact.addRelocation(orel);
            }
        }
    }

    return true;
}

bool ElfObjectReader::read(const std::string& inputPath, ObjectArtifact& outArtifact) {
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
