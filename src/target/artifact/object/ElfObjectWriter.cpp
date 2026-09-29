#include "target/artifact/object/ElfObjectWriter.h"
#include <fstream>
#include <cstring>
#include <algorithm>

namespace target {
namespace artifact {
namespace object {

namespace {

constexpr uint16_t ET_REL = 1;
constexpr uint16_t EM_X86_64 = 62;
constexpr uint16_t EM_AARCH64 = 183;
constexpr uint16_t EM_RISCV = 243;
constexpr uint32_t SHT_NULL = 0;
constexpr uint32_t SHT_PROGBITS = 1;
constexpr uint32_t SHT_SYMTAB = 2;
constexpr uint32_t SHT_STRTAB = 3;
constexpr uint32_t SHT_RELA = 4;
constexpr uint32_t SHT_NOBITS = 8;
constexpr uint64_t SHF_WRITE = 0x1;
constexpr uint64_t SHF_ALLOC = 0x2;
constexpr uint64_t SHF_EXECINSTR = 0x4;
constexpr uint8_t STB_LOCAL = 0;
constexpr uint8_t STB_GLOBAL = 1;
constexpr uint8_t STT_NOTYPE = 0;
constexpr uint8_t STT_FUNC = 2;
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

inline uint8_t ELF64_ST_INFO(uint8_t bind, uint8_t type) { return (bind << 4) + (type & 0xF); }
inline uint64_t ELF64_R_INFO(uint32_t sym, uint32_t type) { return (static_cast<uint64_t>(sym) << 32) + type; }

uint32_t addToStringTable(std::string& table, const std::string& str) {
    uint32_t offset = static_cast<uint32_t>(table.size());
    table.append(str).append(1, '\0');
    return offset;
}

} // namespace

std::vector<uint8_t> ElfObjectWriter::serialize(const ObjectArtifact& artifact) {
    std::vector<uint8_t> buffer;

    std::string shStringTable = "\0";
    std::string stringTable = "\0";

    std::vector<SectionHeader64> finalSectionHeaders;
    std::map<std::string, uint16_t> sectionIndexMap;

    // 0: NULL section
    finalSectionHeaders.push_back({});
    sectionIndexMap[""] = 0;

    // Sections order
    std::vector<std::string> ordered_sections = {".text", ".rodata", ".data", ".bss", ".note.GNU-stack"};

    // Ensure .note.GNU-stack section exists to avoid executable stack warnings
    ObjectArtifact artCopy = artifact;
    if (!artCopy.findSection(".note.GNU-stack")) {
        ObjectSection gnuStack;
        gnuStack.name = ".note.GNU-stack";
        gnuStack.data = {};
        gnuStack.alignment = 1;
        gnuStack.flags = 0;
        artCopy.addSection(gnuStack);
    }

    for (const auto& name : ordered_sections) {
        const ObjectSection* s = artCopy.findSection(name);
        if (!s) continue;

        SectionHeader64 h = {};
        h.sh_name = addToStringTable(shStringTable, s->name);
        if (s->name == ".bss") {
            h.sh_type = SHT_NOBITS;
        } else {
            h.sh_type = SHT_PROGBITS;
        }

        h.sh_flags = 0;
        if (s->name == ".text") h.sh_flags = SHF_ALLOC | SHF_EXECINSTR;
        else if (s->name == ".rodata") h.sh_flags = SHF_ALLOC;
        else if (s->name == ".data" || s->name == ".bss") h.sh_flags = SHF_ALLOC | SHF_WRITE;

        h.sh_size = (s->name == ".bss") ? s->virtualSize : s->data.size();
        h.sh_addralign = s->alignment ? s->alignment : 16;

        finalSectionHeaders.push_back(h);
        sectionIndexMap[s->name] = static_cast<uint16_t>(finalSectionHeaders.size() - 1);
    }

    // Add .rela.text if relocations exist
    if (!artCopy.relocations.empty()) {
        SectionHeader64 relaHdr = {};
        relaHdr.sh_name = addToStringTable(shStringTable, ".rela.text");
        relaHdr.sh_type = SHT_RELA;
        relaHdr.sh_flags = 0;
        relaHdr.sh_addralign = 8;
        relaHdr.sh_entsize = sizeof(Elf64_Rela);
        relaHdr.sh_size = artCopy.relocations.size() * sizeof(Elf64_Rela);
        finalSectionHeaders.push_back(relaHdr);
        sectionIndexMap[".rela.text"] = static_cast<uint16_t>(finalSectionHeaders.size() - 1);
    }

    // Metadata section headers
    auto add_meta = [&](const std::string& name, uint32_t type, uint64_t entsize) {
        SectionHeader64 h = {};
        h.sh_name = addToStringTable(shStringTable, name);
        h.sh_type = type;
        h.sh_addralign = (name == ".symtab") ? 8 : 1;
        h.sh_entsize = entsize;
        finalSectionHeaders.push_back(h);
        sectionIndexMap[name] = static_cast<uint16_t>(finalSectionHeaders.size() - 1);
    };

    add_meta(".shstrtab", SHT_STRTAB, 0);
    add_meta(".symtab", SHT_SYMTAB, sizeof(Symbol64));
    add_meta(".strtab", SHT_STRTAB, 0);

    // Build symbol table
    std::vector<Symbol64> finalSymbols;
    std::map<std::string, uint32_t> symbolIndexMap;

    finalSymbols.push_back({}); // NULL symbol
    symbolIndexMap[""] = 0;

    std::vector<Symbol64> locals, globals;
    for (const auto& sym : artCopy.symbols) {
        Symbol64 s64 = {};
        s64.st_name = addToStringTable(stringTable, sym.name);
        s64.st_size = sym.size;
        uint8_t bindCode = (sym.binding == SymbolBinding::Local) ? STB_LOCAL : STB_GLOBAL;
        uint8_t typeCode = (sym.type == SymbolType::Function) ? STT_FUNC : STT_NOTYPE;
        s64.st_info = ELF64_ST_INFO(bindCode, typeCode);

        if (sym.isDefined && sectionIndexMap.count(sym.sectionName)) {
            s64.st_shndx = sectionIndexMap[sym.sectionName];
            s64.st_value = sym.value;
        } else {
            s64.st_shndx = SHN_UNDEF;
            s64.st_value = 0;
        }

        if (bindCode == STB_LOCAL) locals.push_back(s64);
        else globals.push_back(s64);
    }

    uint32_t first_global_idx = 1 + static_cast<uint32_t>(locals.size());
    for (const auto& s : locals) finalSymbols.push_back(s);
    for (const auto& s : globals) finalSymbols.push_back(s);

    for (size_t i = 1; i < finalSymbols.size(); ++i) {
        std::string symName = &stringTable[finalSymbols[i].st_name];
        symbolIndexMap[symName] = static_cast<uint32_t>(i);
    }

    // Build .rela.text payload
    std::vector<Elf64_Rela> relaTable;
    for (const auto& reloc : artCopy.relocations) {
        Elf64_Rela r = {};
        r.r_offset = reloc.offset;
        uint32_t symIdx = symbolIndexMap.count(reloc.symbolName) ? symbolIndexMap[reloc.symbolName] : 0;
        uint32_t typeCode = R_X86_64_PC32;
        if (reloc.type == "R_X86_64_64") typeCode = R_X86_64_64;
        else if (reloc.type == "R_X86_64_PLT32") typeCode = R_X86_64_PLT32;
        r.r_info = ELF64_R_INFO(symIdx, typeCode);
        r.r_addend = reloc.addend;
        relaTable.push_back(r);
    }

    // Link headers
    if (!artCopy.relocations.empty()) {
        uint16_t relaIdx = sectionIndexMap[".rela.text"];
        finalSectionHeaders[relaIdx].sh_link = sectionIndexMap[".symtab"];
        finalSectionHeaders[relaIdx].sh_info = sectionIndexMap[".text"];
    }

    finalSectionHeaders[sectionIndexMap[".shstrtab"]].sh_size = shStringTable.size();
    finalSectionHeaders[sectionIndexMap[".strtab"]].sh_size = stringTable.size();
    finalSectionHeaders[sectionIndexMap[".symtab"]].sh_size = finalSymbols.size() * sizeof(Symbol64);
    finalSectionHeaders[sectionIndexMap[".symtab"]].sh_link = sectionIndexMap[".strtab"];
    finalSectionHeaders[sectionIndexMap[".symtab"]].sh_info = first_global_idx;

    // Layout section offsets
    uint64_t fileOffset = sizeof(ElfHeader64);
    for (const auto& name : ordered_sections) {
        const ObjectSection* s = artCopy.findSection(name);
        if (!s || !sectionIndexMap.count(name)) continue;
        uint16_t idx = sectionIndexMap[name];
        uint64_t align = finalSectionHeaders[idx].sh_addralign ? finalSectionHeaders[idx].sh_addralign : 1;
        fileOffset = (fileOffset + align - 1) & ~(align - 1);
        finalSectionHeaders[idx].sh_offset = fileOffset;
        if (s->name != ".bss") fileOffset += s->data.size();
    }

    if (!artCopy.relocations.empty()) {
        fileOffset = (fileOffset + 7) & ~7;
        uint16_t relaIdx = sectionIndexMap[".rela.text"];
        finalSectionHeaders[relaIdx].sh_offset = fileOffset;
        fileOffset += relaTable.size() * sizeof(Elf64_Rela);
    }

    auto align_offset = [&](uint64_t off, uint64_t align) { return (off + align - 1) & ~(align - 1); };
    fileOffset = align_offset(fileOffset, 8);
    finalSectionHeaders[sectionIndexMap[".shstrtab"]].sh_offset = fileOffset;
    fileOffset += shStringTable.size();

    fileOffset = align_offset(fileOffset, 8);
    finalSectionHeaders[sectionIndexMap[".symtab"]].sh_offset = fileOffset;
    fileOffset += finalSymbols.size() * sizeof(Symbol64);

    fileOffset = align_offset(fileOffset, 8);
    finalSectionHeaders[sectionIndexMap[".strtab"]].sh_offset = fileOffset;
    fileOffset += stringTable.size();

    fileOffset = align_offset(fileOffset, 8);
    uint64_t sectionHeadersOffset = fileOffset;

    uint64_t totalFileSize = sectionHeadersOffset + finalSectionHeaders.size() * sizeof(SectionHeader64);
    buffer.resize(totalFileSize, 0);

    // Write ElfHeader64
    ElfHeader64 h = {};
    std::memcpy(h.e_ident, "\x7f""ELF", 4);
    h.e_ident[4] = 2; h.e_ident[5] = 1; h.e_ident[6] = 1; // 64-bit, LSB, v1
    h.e_type = ET_REL;
    h.e_machine = (artCopy.arch == target::Arch::AArch64) ? EM_AARCH64 : ((artCopy.arch == target::Arch::RISCV64) ? EM_RISCV : EM_X86_64);
    h.e_version = 1;
    h.e_entry = 0;
    h.e_phoff = 0;
    h.e_shoff = sectionHeadersOffset;
    h.e_flags = 0;
    h.e_ehsize = sizeof(ElfHeader64);
    h.e_phentsize = 0;
    h.e_phnum = 0;
    h.e_shentsize = sizeof(SectionHeader64);
    h.e_shnum = static_cast<uint16_t>(finalSectionHeaders.size());
    h.e_shstrndx = sectionIndexMap[".shstrtab"];

    std::memcpy(buffer.data(), &h, sizeof(h));

    // Write section payloads
    for (const auto& name : ordered_sections) {
        const ObjectSection* s = artCopy.findSection(name);
        if (!s || !sectionIndexMap.count(name)) continue;
        uint16_t idx = sectionIndexMap[name];
        if (s->name != ".bss" && !s->data.empty()) {
            std::memcpy(buffer.data() + finalSectionHeaders[idx].sh_offset, s->data.data(), s->data.size());
        }
    }

    if (!artCopy.relocations.empty()) {
        uint16_t relaIdx = sectionIndexMap[".rela.text"];
        std::memcpy(buffer.data() + finalSectionHeaders[relaIdx].sh_offset, relaTable.data(), relaTable.size() * sizeof(Elf64_Rela));
    }

    std::memcpy(buffer.data() + finalSectionHeaders[sectionIndexMap[".shstrtab"]].sh_offset, shStringTable.c_str(), shStringTable.size());
    std::memcpy(buffer.data() + finalSectionHeaders[sectionIndexMap[".symtab"]].sh_offset, finalSymbols.data(), finalSymbols.size() * sizeof(Symbol64));
    std::memcpy(buffer.data() + finalSectionHeaders[sectionIndexMap[".strtab"]].sh_offset, stringTable.c_str(), stringTable.size());
    std::memcpy(buffer.data() + sectionHeadersOffset, finalSectionHeaders.data(), finalSectionHeaders.size() * sizeof(SectionHeader64));

    return buffer;
}

bool ElfObjectWriter::write(const ObjectArtifact& artifact, const std::string& outputPath) {
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
