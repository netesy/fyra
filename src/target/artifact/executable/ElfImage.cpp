#include "target/artifact/executable/ElfImage.h"
#include "target/os/linux/LinuxOS.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

#include <sys/stat.h>

namespace target::artifact::linker {

namespace {

constexpr uint16_t ET_EXEC = 2;
constexpr uint16_t ET_DYN = 3;
constexpr uint16_t EM_X86_64 = 62;
constexpr uint32_t SHT_NULL = 0;
constexpr uint32_t SHT_PROGBITS = 1;
constexpr uint32_t SHT_SYMTAB = 2;
constexpr uint32_t SHT_STRTAB = 3;
constexpr uint32_t SHT_RELA = 4;
constexpr uint32_t SHT_HASH = 5;
constexpr uint32_t SHT_DYNAMIC = 6;
constexpr uint32_t SHT_NOBITS = 8;
constexpr uint32_t SHT_DYNSYM = 11;

constexpr uint64_t SHF_WRITE = 0x1;
constexpr uint64_t SHF_ALLOC = 0x2;
constexpr uint64_t SHF_EXECINSTR = 0x4;

constexpr uint32_t PT_LOAD = 1;
constexpr uint32_t PT_DYNAMIC = 2;
constexpr uint32_t PT_INTERP = 3;

constexpr uint32_t PF_X = 0x1;
constexpr uint32_t PF_W = 0x2;
constexpr uint32_t PF_R = 0x4;

constexpr uint8_t STB_LOCAL = 0;
constexpr uint8_t STB_GLOBAL = 1;
constexpr uint8_t STT_NOTYPE = 0;
constexpr uint8_t STT_OBJECT = 1;
constexpr uint8_t STT_FUNC = 2;

constexpr int64_t DT_NULL = 0;
constexpr int64_t DT_NEEDED = 1;
constexpr int64_t DT_PLTRELSZ = 2;
constexpr int64_t DT_PLTGOT = 3;
constexpr int64_t DT_HASH = 4;
constexpr int64_t DT_STRTAB = 5;
constexpr int64_t DT_SYMTAB = 6;
constexpr int64_t DT_RELA = 7;
constexpr int64_t DT_RELASZ = 8;
constexpr int64_t DT_RELAENT = 9;
constexpr int64_t DT_STRSZ = 10;
constexpr int64_t DT_SYMENT = 11;
constexpr int64_t DT_SONAME = 14;
constexpr int64_t DT_PLTREL = 20;
constexpr int64_t DT_JMPREL = 23;
constexpr int64_t DT_FLAGS = 30;
constexpr int64_t DT_FLAGS_1 = 0x6ffffffb;

constexpr uint64_t DF_BIND_NOW = 0x8;
constexpr uint64_t DF_1_NOW = 0x1;

constexpr uint32_t R_X86_64_64 = 1;
constexpr uint32_t R_X86_64_GLOB_DAT = 6;
constexpr uint32_t R_X86_64_JUMP_SLOT = 7;
constexpr uint32_t R_X86_64_RELATIVE = 8;

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

struct ProgramHeader64 {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
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

struct Dyn64 {
    int64_t d_tag;
    uint64_t d_val;
};

struct Elf64_Rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
};
#pragma pack(pop)

inline uint64_t ELF64_R_INFO(uint32_t sym, uint32_t type) {
    return (static_cast<uint64_t>(sym) << 32) + type;
}

uint32_t elfHash(const char* name) {
    uint32_t h = 0, g = 0;
    while (*name) {
        h = (h << 4) + static_cast<unsigned char>(*name++);
        g = h & 0xf0000000;
        if (g) {
            h ^= g >> 24;
        }
        h &= ~g;
    }
    return h;
}

uint64_t alignUp(uint64_t val, uint64_t align) {
    if (align <= 1) return val;
    return (val + align - 1) & ~(align - 1);
}

} // namespace

bool ElfImageWriter::writeSharedLibrary(const DynamicLinkPlan& plan, const std::string& outputPath) {
    lastError_.clear();

    if (plan.arch != target::Arch::X64 || plan.os != target::OS::Linux) {
        lastError_ = "shared-library output unsupported for target architecture/OS";
        return false;
    }

    // Determine soname from output path if not specified
    std::string soname = outputPath;
    size_t lastSlash = soname.find_last_of("/\\");
    if (lastSlash != std::string::npos) soname = soname.substr(lastSlash + 1);

    // Build .dynstr, .dynsym
    std::string dynstr;
    dynstr.push_back('\0'); // Index 0 is empty string

    std::vector<Symbol64> dynsyms;
    dynsyms.push_back({}); // NULL symbol at index 0

    auto add_dynstr = [&](const std::string& str) -> uint32_t {
        uint32_t off = dynstr.size();
        dynstr.append(str);
        dynstr.push_back('\0');
        return off;
    };

    uint32_t sonameStrOff = add_dynstr(soname);

    std::vector<std::pair<std::string, uint32_t>> exportedSymbols;

    for (const auto& exp : plan.exports) {
        Symbol64 s64 = {};
        s64.st_name = add_dynstr(exp.symbol);
        s64.st_value = exp.address;
        s64.st_size = exp.size;
        s64.st_info = (STB_GLOBAL << 4) | (exp.isFunction ? STT_FUNC : STT_OBJECT);
        s64.st_other = 0; // STV_DEFAULT
        s64.st_shndx = exp.sectionName == ".data" ? 4 : (exp.sectionName == ".rodata" ? 3 : 1);

        dynsyms.push_back(s64);
        exportedSymbols.push_back({exp.symbol, static_cast<uint32_t>(dynsyms.size() - 1)});
    }

    uint32_t firstGlobalIdx = 1;

    // Build SYSV ELF Hash table
    uint32_t nbucket = exportedSymbols.empty() ? 1 : static_cast<uint32_t>(exportedSymbols.size());
    uint32_t nchain = static_cast<uint32_t>(dynsyms.size());
    std::vector<uint32_t> bucket(nbucket, 0);
    std::vector<uint32_t> chain(nchain, 0);

    for (const auto& [name, symIdx] : exportedSymbols) {
        uint32_t h = elfHash(name.c_str()) % nbucket;
        chain[symIdx] = bucket[h];
        bucket[h] = symIdx;
    }

    std::vector<uint8_t> hashData;
    auto append32 = [&](uint32_t val) {
        uint8_t b[4];
        std::memcpy(b, &val, 4);
        hashData.insert(hashData.end(), b, b + 4);
    };

    append32(nbucket);
    append32(nchain);
    for (uint32_t b : bucket) append32(b);
    for (uint32_t c : chain) append32(c);

    // Build .shstrtab
    std::string shstrtab = "\0";
    auto add_shstr = [&](const std::string& str) -> uint32_t {
        uint32_t off = shstrtab.size();
        shstrtab.append(str).append(1, '\0');
        return off;
    };

    uint32_t shstr_null = 0;
    uint32_t shstr_text = add_shstr(".text");
    uint32_t shstr_dynstr = add_shstr(".dynstr");
    uint32_t shstr_dynsym = add_shstr(".dynsym");
    uint32_t shstr_hash = add_shstr(".hash");
    uint32_t shstr_rela_dyn = add_shstr(".rela.dyn");
    uint32_t shstr_dynamic = add_shstr(".dynamic");
    uint32_t shstr_rodata = add_shstr(".rodata");
    uint32_t shstr_data = add_shstr(".data");
    uint32_t shstr_bss = add_shstr(".bss");
    uint32_t shstr_shstrtab = add_shstr(".shstrtab");

    // Layout
    uint64_t baseVma = 0x0ULL;
    uint64_t pageAlign = 0x1000;

    const auto* textSec = plan.findSection(".text");
    const auto* rodataSec = plan.findSection(".rodata");
    const auto* dataSec = plan.findSection(".data");
    const auto* bssSec = plan.findSection(".bss");

    std::vector<uint8_t> textBytes = textSec ? textSec->data : std::vector<uint8_t>{};
    std::vector<uint8_t> rodataBytes = rodataSec ? rodataSec->data : std::vector<uint8_t>{};
    std::vector<uint8_t> dataBytes = dataSec ? dataSec->data : std::vector<uint8_t>{};
    uint64_t bssSize = bssSec ? bssSec->virtualSize : 0;

    // Check for relocation fixups in .data or .text if any
    std::vector<Elf64_Rela> relaDynList;
    for (uint64_t fixupVma : plan.relocationFixupVmas) {
        Elf64_Rela r = {};
        r.r_offset = fixupVma;
        r.r_info = ELF64_R_INFO(0, R_X86_64_RELATIVE);
        // Addend is original value at location
        r.r_addend = 0;
        relaDynList.push_back(r);
    }

    uint64_t numPhdrs = 3; // PT_LOAD (RX) + PT_LOAD (RW) + PT_DYNAMIC
    uint64_t headersSize = sizeof(ElfHeader64) + numPhdrs * sizeof(ProgramHeader64);

    uint64_t hashOffset = alignUp(headersSize, 8);
    uint64_t hashVma = baseVma + hashOffset;
    uint64_t hashSize = hashData.size();

    uint64_t dynsymOffset = alignUp(hashOffset + hashSize, 8);
    uint64_t dynsymVma = baseVma + dynsymOffset;
    uint64_t dynsymSize = dynsyms.size() * sizeof(Symbol64);

    uint64_t dynstrOffset = alignUp(dynsymOffset + dynsymSize, 8);
    uint64_t dynstrVma = baseVma + dynstrOffset;
    uint64_t dynstrSize = dynstr.size();

    uint64_t relaDynOffset = alignUp(dynstrOffset + dynstrSize, 8);
    uint64_t relaDynVma = baseVma + relaDynOffset;
    uint64_t relaDynSize = relaDynList.size() * sizeof(Elf64_Rela);

    uint64_t textOffset = alignUp(relaDynOffset + relaDynSize, 16);
    uint64_t textVma = baseVma + textOffset;

    uint64_t rxEndOffset = textOffset + textBytes.size();

    // RW Segment
    uint64_t rwOffset = alignUp(rxEndOffset, pageAlign);
    uint64_t rwVma = baseVma + rwOffset;

    std::vector<Dyn64> dynamics = {
        {DT_SONAME, sonameStrOff},
        {DT_HASH, hashVma},
        {DT_STRTAB, dynstrVma},
        {DT_SYMTAB, dynsymVma},
        {DT_STRSZ, dynstrSize},
        {DT_SYMENT, sizeof(Symbol64)},
    };

    if (!relaDynList.empty()) {
        dynamics.push_back({DT_RELA, relaDynVma});
        dynamics.push_back({DT_RELASZ, relaDynSize});
        dynamics.push_back({DT_RELAENT, sizeof(Elf64_Rela)});
    }

    dynamics.push_back({DT_NULL, 0});

    uint64_t dynamicOffset = rwOffset;
    uint64_t dynamicVma = rwVma;
    uint64_t dynamicSize = dynamics.size() * sizeof(Dyn64);

    uint64_t rodataOffset = alignUp(dynamicOffset + dynamicSize, 8);
    uint64_t rodataVma = baseVma + rodataOffset;

    uint64_t dataOffset = alignUp(rodataOffset + rodataBytes.size(), 16);
    uint64_t dataVma = baseVma + dataOffset;

    uint64_t bssOffset = dataOffset + dataBytes.size();
    uint64_t bssVma = dataVma + dataBytes.size();

    uint64_t shstrtabOffset = alignUp(bssOffset + bssSize, 8);
    uint64_t shstrtabSize = shstrtab.size();

    uint64_t shoff = alignUp(shstrtabOffset + shstrtabSize, 8);

    // Fixup symbol virtual addresses in .dynsym
    for (size_t i = 1; i < dynsyms.size(); ++i) {
        std::string symName = &dynstr[dynsyms[i].st_name];
        for (const auto& exp : plan.exports) {
            if (exp.symbol == symName) {
                if (exp.sectionName == ".data") {
                    dynsyms[i].st_value = dataVma + (exp.address - (dataSec ? dataSec->virtualAddress : baseVma));
                } else if (exp.sectionName == ".rodata") {
                    dynsyms[i].st_value = rodataVma + (exp.address - (rodataSec ? rodataSec->virtualAddress : baseVma));
                } else {
                    dynsyms[i].st_value = textVma + (exp.address - (textSec ? textSec->virtualAddress : baseVma));
                }
                break;
            }
        }
    }

    // Program Headers
    std::vector<ProgramHeader64> phdrs(3);
    // RX segment
    phdrs[0].p_type = PT_LOAD;
    phdrs[0].p_flags = PF_R | PF_X;
    phdrs[0].p_offset = 0;
    phdrs[0].p_vaddr = baseVma;
    phdrs[0].p_paddr = baseVma;
    phdrs[0].p_filesz = rxEndOffset;
    phdrs[0].p_memsz = rxEndOffset;
    phdrs[0].p_align = pageAlign;

    // RW segment
    uint64_t rwEndOffset = bssOffset + bssSize;
    phdrs[1].p_type = PT_LOAD;
    phdrs[1].p_flags = PF_R | PF_W;
    phdrs[1].p_offset = rwOffset;
    phdrs[1].p_vaddr = rwVma;
    phdrs[1].p_paddr = rwVma;
    phdrs[1].p_filesz = bssOffset - rwOffset;
    phdrs[1].p_memsz = rwEndOffset - rwOffset;
    phdrs[1].p_align = pageAlign;

    // PT_DYNAMIC segment
    phdrs[2].p_type = PT_DYNAMIC;
    phdrs[2].p_flags = PF_R | PF_W;
    phdrs[2].p_offset = dynamicOffset;
    phdrs[2].p_vaddr = dynamicVma;
    phdrs[2].p_paddr = dynamicVma;
    phdrs[2].p_filesz = dynamicSize;
    phdrs[2].p_memsz = dynamicSize;
    phdrs[2].p_align = 8;

    // Section Headers
    std::vector<SectionHeader64> shdrs;
    shdrs.push_back({}); // NULL

    auto add_shdr = [&](uint32_t name, uint32_t type, uint64_t flags, uint64_t addr, uint64_t off, uint64_t size, uint32_t link, uint32_t info, uint64_t align, uint64_t entsize) {
        SectionHeader64 h = {};
        h.sh_name = name; h.sh_type = type; h.sh_flags = flags;
        h.sh_addr = addr; h.sh_offset = off; h.sh_size = size;
        h.sh_link = link; h.sh_info = info; h.sh_addralign = align; h.sh_entsize = entsize;
        shdrs.push_back(h);
    };

    add_shdr(shstr_hash, SHT_HASH, SHF_ALLOC, hashVma, hashOffset, hashSize, 2, 0, 4, 0);
    add_shdr(shstr_dynsym, SHT_DYNSYM, SHF_ALLOC, dynsymVma, dynsymOffset, dynsymSize, 3, firstGlobalIdx, 8, sizeof(Symbol64));
    add_shdr(shstr_dynstr, SHT_STRTAB, SHF_ALLOC, dynstrVma, dynstrOffset, dynstrSize, 0, 0, 1, 0);
    if (!relaDynList.empty()) {
        add_shdr(shstr_rela_dyn, SHT_RELA, SHF_ALLOC, relaDynVma, relaDynOffset, relaDynSize, 2, 0, 8, sizeof(Elf64_Rela));
    }
    add_shdr(shstr_text, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, textVma, textOffset, textBytes.size(), 0, 0, 16, 0);
    add_shdr(shstr_dynamic, SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE, dynamicVma, dynamicOffset, dynamicSize, 3, 0, 8, sizeof(Dyn64));
    add_shdr(shstr_rodata, SHT_PROGBITS, SHF_ALLOC, rodataVma, rodataOffset, rodataBytes.size(), 0, 0, 8, 0);
    add_shdr(shstr_data, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, dataVma, dataOffset, dataBytes.size(), 0, 0, 8, 0);
    add_shdr(shstr_bss, SHT_NOBITS, SHF_ALLOC | SHF_WRITE, bssVma, bssOffset, bssSize, 0, 0, 8, 0);
    add_shdr(shstr_shstrtab, SHT_STRTAB, 0, 0, shstrtabOffset, shstrtabSize, 0, 0, 1, 0);

    // ELF Header
    ElfHeader64 ehdr = {};
    std::memcpy(ehdr.e_ident, "\x7f""ELF", 4);
    ehdr.e_ident[4] = 2; // 64-bit
    ehdr.e_ident[5] = 1; // Little endian
    ehdr.e_ident[6] = 1; // ELF version 1
    ehdr.e_ident[7] = 0; // SYSV
    ehdr.e_type = ET_DYN;
    ehdr.e_machine = EM_X86_64;
    ehdr.e_version = 1;
    ehdr.e_entry = 0;
    ehdr.e_phoff = sizeof(ElfHeader64);
    ehdr.e_shoff = shoff;
    ehdr.e_flags = 0;
    ehdr.e_ehsize = sizeof(ElfHeader64);
    ehdr.e_phentsize = sizeof(ProgramHeader64);
    ehdr.e_phnum = static_cast<uint16_t>(phdrs.size());
    ehdr.e_shentsize = sizeof(SectionHeader64);
    ehdr.e_shnum = static_cast<uint16_t>(shdrs.size());
    ehdr.e_shstrndx = static_cast<uint16_t>(shdrs.size() - 1);

    // Write file
    std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        lastError_ = "Cannot open output file: " + outputPath;
        return false;
    }

    file.write(reinterpret_cast<const char*>(&ehdr), sizeof(ehdr));
    file.write(reinterpret_cast<const char*>(phdrs.data()), phdrs.size() * sizeof(ProgramHeader64));

    file.seekp(hashOffset);
    file.write(reinterpret_cast<const char*>(hashData.data()), hashData.size());

    file.seekp(dynsymOffset);
    file.write(reinterpret_cast<const char*>(dynsyms.data()), dynsyms.size() * sizeof(Symbol64));

    file.seekp(dynstrOffset);
    file.write(dynstr.c_str(), dynstr.size());

    if (!relaDynList.empty()) {
        file.seekp(relaDynOffset);
        file.write(reinterpret_cast<const char*>(relaDynList.data()), relaDynSize);
    }

    if (!textBytes.empty()) {
        file.seekp(textOffset);
        file.write(reinterpret_cast<const char*>(textBytes.data()), textBytes.size());
    }

    file.seekp(dynamicOffset);
    file.write(reinterpret_cast<const char*>(dynamics.data()), dynamics.size() * sizeof(Dyn64));

    if (!rodataBytes.empty()) {
        file.seekp(rodataOffset);
        file.write(reinterpret_cast<const char*>(rodataBytes.data()), rodataBytes.size());
    }

    if (!dataBytes.empty()) {
        file.seekp(dataOffset);
        file.write(reinterpret_cast<const char*>(dataBytes.data()), dataBytes.size());
    }

    file.seekp(shstrtabOffset);
    file.write(shstrtab.c_str(), shstrtab.size());

    file.seekp(shoff);
    file.write(reinterpret_cast<const char*>(shdrs.data()), shdrs.size() * sizeof(SectionHeader64));

    file.close();
    return !file.fail();
}


bool ElfImageWriter::writeExecutable(const LinkedImage& image, const std::string& outputPath) {
    lastError_.clear();
    if (image.arch != target::Arch::X64 || image.os != target::OS::Linux ||
        image.outputKind != LinkOutputKind::Executable) {
        lastError_ = "ELF executable writer requires a Linux x86-64 executable LinkedImage";
        return false;
    }
    if (image.entryAddress == 0) {
        lastError_ = "ELF executable image has no resolved entrypoint";
        return false;
    }

    // Check if we have dynamic imports / fixups
    bool isDynamicExecutable = !image.importThunkVmas.empty() || !image.dataImportFixups.empty();

    if (!isDynamicExecutable) {
        // Static executable path
        struct OutputSection {
            const LinkedSection* input;
            uint32_t nameOffset;
            uint64_t fileOffset;
        };
        std::vector<const LinkedSection*> ordered;
        for (const auto& [name, section] : image.sections) ordered.push_back(&section);
        std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
            return a->virtualAddress < b->virtualAddress;
        });

        std::string shstr(1, '\0');
        auto addName = [&](const std::string& name) {
            uint32_t offset = static_cast<uint32_t>(shstr.size());
            shstr += name;
            shstr.push_back('\0');
            return offset;
        };
        std::vector<OutputSection> sections;
        for (const auto* section : ordered) {
            uint64_t offset = section->name == ".bss" ? 0 : section->virtualAddress - 0x400000ULL;
            sections.push_back({section, addName(section->name), offset});
        }
        uint32_t shstrName = addName(".shstrtab");

        const uint64_t phoff = sizeof(ElfHeader64);
        const uint16_t phnum = static_cast<uint16_t>(1 + ordered.size());
        uint64_t dataEnd = sizeof(ElfHeader64) + phnum * sizeof(ProgramHeader64);
        for (const auto& section : sections) {
            if (!section.input->data.empty()) dataEnd = std::max(dataEnd, section.fileOffset + section.input->data.size());
        }
        uint64_t shstrOffset = dataEnd;
        uint64_t shoff = alignUp(shstrOffset + shstr.size(), 8);

        ElfHeader64 header{};
        header.e_ident[0] = 0x7f; header.e_ident[1] = 'E'; header.e_ident[2] = 'L'; header.e_ident[3] = 'F';
        header.e_ident[4] = 2; header.e_ident[5] = 1; header.e_ident[6] = 1;
        header.e_type = ET_EXEC; header.e_machine = EM_X86_64; header.e_version = 1;
        header.e_entry = image.entryAddress; header.e_phoff = phoff; header.e_shoff = shoff;
        header.e_ehsize = sizeof(ElfHeader64); header.e_phentsize = sizeof(ProgramHeader64); header.e_phnum = phnum;
        header.e_shentsize = sizeof(SectionHeader64); header.e_shnum = static_cast<uint16_t>(sections.size() + 2);
        header.e_shstrndx = static_cast<uint16_t>(sections.size() + 1);

        std::vector<ProgramHeader64> phdrs;
        phdrs.push_back({PT_LOAD, PF_R, 0, 0x400000, 0x400000,
                         sizeof(ElfHeader64) + phnum * sizeof(ProgramHeader64),
                         sizeof(ElfHeader64) + phnum * sizeof(ProgramHeader64), 0x1000});
        for (const auto& section : sections) {
            uint32_t flags = PF_R | (section.input->isExecutable ? PF_X : 0) | (section.input->isWritable ? PF_W : 0);
            uint64_t fileSize = section.input->name == ".bss" ? 0 : section.input->data.size();
            phdrs.push_back({PT_LOAD, flags, section.fileOffset, section.input->virtualAddress,
                             section.input->virtualAddress, fileSize, section.input->virtualSize, 0x1000});
        }

        std::vector<SectionHeader64> shdrs(sections.size() + 2);
        for (size_t i = 0; i < sections.size(); ++i) {
            const auto& section = sections[i];
            auto& sh = shdrs[i + 1];
            sh.sh_name = section.nameOffset;
            sh.sh_type = section.input->name == ".bss" ? SHT_NOBITS : SHT_PROGBITS;
            sh.sh_flags = SHF_ALLOC | (section.input->isWritable ? SHF_WRITE : 0) |
                          (section.input->isExecutable ? SHF_EXECINSTR : 0);
            sh.sh_addr = section.input->virtualAddress;
            sh.sh_offset = section.fileOffset;
            sh.sh_size = section.input->virtualSize;
            sh.sh_addralign = section.input->alignment ? section.input->alignment : 1;
        }
        auto& strings = shdrs.back();
        strings.sh_name = shstrName; strings.sh_type = SHT_STRTAB; strings.sh_offset = shstrOffset;
        strings.sh_size = shstr.size(); strings.sh_addralign = 1;

        const std::string temporary = outputPath + ".tmp";
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) { lastError_ = "failed to open temporary ELF output"; return false; }
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(phdrs.data()), phdrs.size() * sizeof(ProgramHeader64));
        for (const auto& section : sections) {
            if (section.input->name == ".bss" || section.input->data.empty()) continue;
            file.seekp(section.fileOffset);
            file.write(reinterpret_cast<const char*>(section.input->data.data()), section.input->data.size());
        }
        file.seekp(shstrOffset); file.write(shstr.data(), shstr.size());
        file.seekp(shoff); file.write(reinterpret_cast<const char*>(shdrs.data()), shdrs.size() * sizeof(SectionHeader64));
        file.close();
        if (!file) { std::remove(temporary.c_str()); lastError_ = "failed while writing ELF output"; return false; }
        if (std::rename(temporary.c_str(), outputPath.c_str()) != 0) {
            std::remove(temporary.c_str()); lastError_ = "failed to replace ELF output"; return false;
        }
        chmod(outputPath.c_str(), 0755);
        return true;
    }

    // Dynamic Executable Path (PT_INTERP, .interp, .dynsym, .dynstr, .hash, .rela.dyn, .rela.plt, .got, .plt, .dynamic)
    std::string interpPath;
    if (image.os == target::OS::Linux) {
        LinuxOS linuxOS;
        interpPath = linuxOS.getDynamicInterpreterPath(image.arch);
    }
    if (interpPath.empty()) {
        interpPath = "/lib64/ld-linux-x86-64.so.2";
    }
    std::string interpStr = interpPath + '\0';

    // Collect all required imported symbols and library dependencies
    struct ImportInfo {
        std::string name;
        std::string libName;
        bool isFunction;
        uint64_t thunkVma;
        uint64_t gotSlotVma;
    };
    std::vector<ImportInfo> imports;
    std::vector<std::string> neededLibs;

    auto addNeeded = [&](const std::string& lib) {
        if (std::find(neededLibs.begin(), neededLibs.end(), lib) == neededLibs.end()) {
            neededLibs.push_back(lib);
        }
    };

    // Process function imports
    for (const auto& [symName, thunkVma] : image.importThunkVmas) {
        ImportInfo imp;
        imp.name = symName;
        imp.isFunction = true;
        imp.thunkVma = thunkVma;
        imp.gotSlotVma = 0;
        auto libIt = image.importLibraryNames.find(symName);
        imp.libName = (libIt != image.importLibraryNames.end()) ? libIt->second : "libfixture.so";
        addNeeded(imp.libName);
        imports.push_back(imp);
    }

    // Process data imports
    for (const auto& fixup : image.dataImportFixups) {
        ImportInfo imp;
        imp.name = fixup.symbolName;
        imp.isFunction = false;
        imp.thunkVma = 0;
        imp.gotSlotVma = 0;
        auto libIt = image.importLibraryNames.find(fixup.symbolName);
        imp.libName = (libIt != image.importLibraryNames.end()) ? libIt->second : "libfixture.so";
        addNeeded(imp.libName);
        imports.push_back(imp);
    }

    // Build .dynstr, .dynsym
    std::string dynstr;
    dynstr.push_back('\0'); // NULL at 0

    auto add_dynstr = [&](const std::string& s) -> uint32_t {
        uint32_t off = dynstr.size();
        dynstr.append(s);
        dynstr.push_back('\0');
        return off;
    };

    std::vector<uint32_t> neededStrOffs;
    for (const auto& lib : neededLibs) {
        neededStrOffs.push_back(add_dynstr(lib));
    }

    std::vector<Symbol64> dynsyms;
    dynsyms.push_back({}); // NULL symbol at index 0

    for (auto& imp : imports) {
        Symbol64 s64 = {};
        s64.st_name = add_dynstr(imp.name);
        s64.st_value = 0; // Undefined imported symbol
        s64.st_size = 0;
        s64.st_info = (STB_GLOBAL << 4) | (imp.isFunction ? STT_FUNC : STT_OBJECT);
        s64.st_other = 0;
        s64.st_shndx = 0; // SHN_UNDEF
        dynsyms.push_back(s64);
    }

    // SYSV Hash
    uint32_t nbucket = imports.empty() ? 1 : static_cast<uint32_t>(imports.size());
    uint32_t nchain = static_cast<uint32_t>(dynsyms.size());
    std::vector<uint32_t> bucket(nbucket, 0);
    std::vector<uint32_t> chain(nchain, 0);

    for (size_t i = 0; i < imports.size(); ++i) {
        uint32_t symIdx = static_cast<uint32_t>(i + 1);
        uint32_t h = elfHash(imports[i].name.c_str()) % nbucket;
        chain[symIdx] = bucket[h];
        bucket[h] = symIdx;
    }

    std::vector<uint8_t> hashData;
    auto append32 = [&](uint32_t val) {
        uint8_t b[4];
        std::memcpy(b, &val, 4);
        hashData.insert(hashData.end(), b, b + 4);
    };
    append32(nbucket);
    append32(nchain);
    for (uint32_t b : bucket) append32(b);
    for (uint32_t c : chain) append32(c);

    // Section headers & layout calculation
    uint64_t baseVma = 0x400000ULL;
    uint64_t pageAlign = 0x1000;

    // We will place GOT entries and relocations.
    // .got section will contain GOT entries for each import.
    // .rela.dyn for data imports (R_X86_64_GLOB_DAT)
    // .rela.plt for function imports (R_X86_64_JUMP_SLOT)
    std::vector<Elf64_Rela> relaDynList;
    std::vector<Elf64_Rela> relaPltList;

    // Base text/rodata/data copies
    auto textSec = image.findSection(".text");
    auto rodataSec = image.findSection(".rodata");
    auto dataSec = image.findSection(".data");
    auto bssSec = image.findSection(".bss");

    std::vector<uint8_t> textBytes = textSec ? textSec->data : std::vector<uint8_t>{};
    std::vector<uint8_t> rodataBytes = rodataSec ? rodataSec->data : std::vector<uint8_t>{};
    std::vector<uint8_t> dataBytes = dataSec ? dataSec->data : std::vector<uint8_t>{};
    uint64_t bssSize = bssSec ? bssSec->virtualSize : 0;

    // Headers size & layout
    // Program headers: PT_PHDR, PT_INTERP, PT_LOAD (RX), PT_LOAD (RW), PT_DYNAMIC
    uint64_t numPhdrs = 5;
    uint64_t headersSize = sizeof(ElfHeader64) + numPhdrs * sizeof(ProgramHeader64);

    uint64_t interpOffset = alignUp(headersSize, 8);
    uint64_t interpVma = baseVma + interpOffset;
    uint64_t interpSize = interpStr.size();

    uint64_t dynstrOffset = alignUp(interpOffset + interpSize, 8);
    uint64_t dynstrVma = baseVma + dynstrOffset;
    uint64_t dynstrSize = dynstr.size();

    uint64_t dynsymOffset = alignUp(dynstrOffset + dynstrSize, 8);
    uint64_t dynsymVma = baseVma + dynsymOffset;
    uint64_t dynsymSize = dynsyms.size() * sizeof(Symbol64);

    uint64_t hashOffset = alignUp(dynsymOffset + dynsymSize, 8);
    uint64_t hashVma = baseVma + hashOffset;
    uint64_t hashSize = hashData.size();

    uint64_t textOffset = alignUp(hashOffset + hashSize, 16);
    uint64_t textVma = baseVma + textOffset;

    // Address adjustment for text section if textVma shifted relative to LinkedImage virtualAddress
    uint64_t textAddrDiff = textVma - (textSec ? textSec->virtualAddress : baseVma);

    // Dynamic segment layout in RW segment:
    // RW segment starts at page boundary after RX segment.
    // In RW: .got, .rela.dyn, .rela.plt, .dynamic, .rodata, .data, .bss
    uint64_t rxEndOffset = textOffset + textBytes.size();

    uint64_t rwOffset = alignUp(rxEndOffset, pageAlign);
    uint64_t rwVma = baseVma + rwOffset;

    // GOT layout:
    // Reserved slots 0, 1, 2 for dynamic linker (PLTGOT), followed by entries for each import.
    std::vector<uint64_t> gotEntries(3 + imports.size(), 0);

    uint64_t gotOffset = rwOffset;
    uint64_t gotVma = rwVma;
    uint64_t gotSize = gotEntries.size() * sizeof(uint64_t);

    uint64_t relaDynOffset = alignUp(gotOffset + gotSize, 8);
    uint64_t relaDynVma = baseVma + relaDynOffset;

    size_t numDataImports = 0, numFuncImports = 0;
    for (size_t i = 0; i < imports.size(); ++i) {
        if (imports[i].isFunction) numFuncImports++; else numDataImports++;
    }

    uint64_t relaDynSize = numDataImports * sizeof(Elf64_Rela);

    uint64_t relaPltOffset = alignUp(relaDynOffset + relaDynSize, 8);
    uint64_t relaPltVma = baseVma + relaPltOffset;
    uint64_t relaPltSize = numFuncImports * sizeof(Elf64_Rela);

    // Assign GOT slot VMAs to imports and build relocations
    size_t funcRelocIdx = 0, dataRelocIdx = 0;
    for (size_t i = 0; i < imports.size(); ++i) {
        uint32_t symIdx = static_cast<uint32_t>(i + 1);
        // GOT slots start at index 3 after reserved 0, 1, 2
        imports[i].gotSlotVma = gotVma + (3 + i) * sizeof(uint64_t);

        if (imports[i].isFunction) {
            Elf64_Rela r = {};
            r.r_offset = imports[i].gotSlotVma;
            r.r_info = ELF64_R_INFO(symIdx, R_X86_64_JUMP_SLOT);
            r.r_addend = 0;
            relaPltList.push_back(r);

            // Fixup function thunk indirect jump displacement!
            // Thunk is at (imports[i].thunkVma + textAddrDiff) in textBytes
            // Instruction: FF 25 [disp32]
            // disp32 = GOT_VMA - (Thunk_VMA + 6)
            uint64_t realThunkVma = imports[i].thunkVma + textAddrDiff;
            uint64_t thunkTextOffset = realThunkVma - textVma;
            int64_t disp = static_cast<int64_t>(imports[i].gotSlotVma) - static_cast<int64_t>(realThunkVma + 6);
            int32_t disp32 = static_cast<int32_t>(disp);
            std::memcpy(textBytes.data() + thunkTextOffset + 2, &disp32, 4);
        } else {
            Elf64_Rela r = {};
            r.r_offset = imports[i].gotSlotVma;
            r.r_info = ELF64_R_INFO(symIdx, R_X86_64_GLOB_DAT);
            r.r_addend = 0;
            relaDynList.push_back(r);
        }
    }

    // Fixup data import fixups in textBytes/dataBytes
    LinkedImage mutImage = image; // Copy to fixup data references
    for (const auto& fixup : mutImage.dataImportFixups) {
        // Find matching import to get GOT slot VMA
        uint64_t targetGotVma = 0;
        for (const auto& imp : imports) {
            if (imp.name == fixup.symbolName) {
                targetGotVma = imp.gotSlotVma;
                break;
            }
        }
        if (targetGotVma != 0) {
            // Apply PC32 relocation targeting GOT slot
            uint64_t sectionDataOffset = fixup.sectionOffset;
            uint64_t placeAddress = fixup.placeAddress + textAddrDiff;
            int64_t delta = static_cast<int64_t>(targetGotVma) + fixup.addend - static_cast<int64_t>(placeAddress);
            int32_t val32 = static_cast<int32_t>(delta);
            if (fixup.sectionName == ".text") {
                std::memcpy(textBytes.data() + sectionDataOffset, &val32, 4);
            } else if (fixup.sectionName == ".data") {
                std::memcpy(dataBytes.data() + sectionDataOffset, &val32, 4);
            }
        }
    }

    // Build .dynamic section
    std::vector<Dyn64> dynamics;
    for (size_t i = 0; i < neededLibs.size(); ++i) {
        dynamics.push_back({DT_NEEDED, neededStrOffs[i]});
    }
    dynamics.push_back({DT_HASH, hashVma});
    dynamics.push_back({DT_STRTAB, dynstrVma});
    dynamics.push_back({DT_SYMTAB, dynsymVma});
    dynamics.push_back({DT_STRSZ, dynstrSize});
    dynamics.push_back({DT_SYMENT, sizeof(Symbol64)});
    dynamics.push_back({DT_PLTGOT, gotVma});
    if (!relaPltList.empty()) {
        dynamics.push_back({DT_PLTRELSZ, relaPltSize});
        dynamics.push_back({DT_PLTREL, 7}); // DT_RELA
        dynamics.push_back({DT_JMPREL, relaPltVma});
    }
    if (!relaDynList.empty()) {
        dynamics.push_back({DT_RELA, relaDynVma});
        dynamics.push_back({DT_RELASZ, relaDynSize});
        dynamics.push_back({DT_RELAENT, sizeof(Elf64_Rela)});
    }
    dynamics.push_back({DT_FLAGS, DF_BIND_NOW});
    dynamics.push_back({DT_FLAGS_1, DF_1_NOW});
    dynamics.push_back({DT_NULL, 0});

    uint64_t dynamicOffset = alignUp(relaPltOffset + relaPltSize, 8);
    uint64_t dynamicVma = baseVma + dynamicOffset;
    uint64_t dynamicSize = dynamics.size() * sizeof(Dyn64);

    // Reserved GOT slot 0 points to dynamic section
    gotEntries[0] = dynamicVma;

    uint64_t rodataOffset = alignUp(dynamicOffset + dynamicSize, 8);
    uint64_t rodataVma = baseVma + rodataOffset;

    uint64_t dataOffset = alignUp(rodataOffset + rodataBytes.size(), 16);
    uint64_t dataVma = baseVma + dataOffset;

    uint64_t bssOffset = dataOffset + dataBytes.size();
    uint64_t bssVma = dataVma + dataBytes.size();

    // Section string table
    std::string shstrtab = "\0";
    auto add_shstr = [&](const std::string& str) -> uint32_t {
        uint32_t off = shstrtab.size();
        shstrtab.append(str).append(1, '\0');
        return off;
    };

    uint32_t shstr_interp = add_shstr(".interp");
    uint32_t shstr_dynstr = add_shstr(".dynstr");
    uint32_t shstr_dynsym = add_shstr(".dynsym");
    uint32_t shstr_hash = add_shstr(".hash");
    uint32_t shstr_text = add_shstr(".text");
    uint32_t shstr_got = add_shstr(".got");
    uint32_t shstr_rela_dyn = add_shstr(".rela.dyn");
    uint32_t shstr_rela_plt = add_shstr(".rela.plt");
    uint32_t shstr_dynamic = add_shstr(".dynamic");
    uint32_t shstr_rodata = add_shstr(".rodata");
    uint32_t shstr_data = add_shstr(".data");
    uint32_t shstr_bss = add_shstr(".bss");
    uint32_t shstr_shstrtab = add_shstr(".shstrtab");

    uint64_t shstrtabOffset = alignUp(bssOffset + bssSize, 8);
    uint64_t shstrtabSize = shstrtab.size();

    uint64_t shoff = alignUp(shstrtabOffset + shstrtabSize, 8);

    // Entrypoint adjustment
    uint64_t realEntry = image.entryAddress + textAddrDiff;

    // Program headers
    std::vector<ProgramHeader64> phdrs(5);
    // PT_PHDR
    phdrs[0].p_type = 6; // PT_PHDR
    phdrs[0].p_flags = PF_R;
    phdrs[0].p_offset = sizeof(ElfHeader64);
    phdrs[0].p_vaddr = baseVma + sizeof(ElfHeader64);
    phdrs[0].p_paddr = phdrs[0].p_vaddr;
    phdrs[0].p_filesz = numPhdrs * sizeof(ProgramHeader64);
    phdrs[0].p_memsz = phdrs[0].p_filesz;
    phdrs[0].p_align = 8;

    // PT_INTERP
    phdrs[1].p_type = PT_INTERP;
    phdrs[1].p_flags = PF_R;
    phdrs[1].p_offset = interpOffset;
    phdrs[1].p_vaddr = interpVma;
    phdrs[1].p_paddr = interpVma;
    phdrs[1].p_filesz = interpSize;
    phdrs[1].p_memsz = interpSize;
    phdrs[1].p_align = 1;

    // PT_LOAD RX
    phdrs[2].p_type = PT_LOAD;
    phdrs[2].p_flags = PF_R | PF_X;
    phdrs[2].p_offset = 0;
    phdrs[2].p_vaddr = baseVma;
    phdrs[2].p_paddr = baseVma;
    phdrs[2].p_filesz = rxEndOffset;
    phdrs[2].p_memsz = rxEndOffset;
    phdrs[2].p_align = pageAlign;

    // PT_LOAD RW
    uint64_t rwEndOffset = bssOffset + bssSize;
    phdrs[3].p_type = PT_LOAD;
    phdrs[3].p_flags = PF_R | PF_W;
    phdrs[3].p_offset = rwOffset;
    phdrs[3].p_vaddr = rwVma;
    phdrs[3].p_paddr = rwVma;
    phdrs[3].p_filesz = bssOffset - rwOffset;
    phdrs[3].p_memsz = rwEndOffset - rwOffset;
    phdrs[3].p_align = pageAlign;

    // PT_DYNAMIC
    phdrs[4].p_type = PT_DYNAMIC;
    phdrs[4].p_flags = PF_R | PF_W;
    phdrs[4].p_offset = dynamicOffset;
    phdrs[4].p_vaddr = dynamicVma;
    phdrs[4].p_paddr = dynamicVma;
    phdrs[4].p_filesz = dynamicSize;
    phdrs[4].p_memsz = dynamicSize;
    phdrs[4].p_align = 8;

    // Set initial GOT reserved entries required by ld-linux
    // GOT[0] = Address of .dynamic
    // GOT[1] = link_map (filled by loader at runtime)
    // GOT[2] = _dl_runtime_resolve (filled by loader at runtime)
    gotEntries[0] = dynamicVma;
    gotEntries[1] = 0;
    gotEntries[2] = 0;

    // Section headers
    std::vector<SectionHeader64> shdrs;
    shdrs.push_back({}); // NULL section

    auto add_shdr = [&](uint32_t name, uint32_t type, uint64_t flags, uint64_t addr, uint64_t off, uint64_t size, uint32_t link, uint32_t info, uint64_t align, uint64_t entsize) {
        SectionHeader64 h = {};
        h.sh_name = name; h.sh_type = type; h.sh_flags = flags;
        h.sh_addr = addr; h.sh_offset = off; h.sh_size = size;
        h.sh_link = link; h.sh_info = info; h.sh_addralign = align; h.sh_entsize = entsize;
        shdrs.push_back(h);
    };

    add_shdr(shstr_interp, SHT_PROGBITS, SHF_ALLOC, interpVma, interpOffset, interpSize, 0, 0, 1, 0);
    add_shdr(shstr_dynstr, SHT_STRTAB, SHF_ALLOC, dynstrVma, dynstrOffset, dynstrSize, 0, 0, 1, 0);
    add_shdr(shstr_dynsym, SHT_DYNSYM, SHF_ALLOC, dynsymVma, dynsymOffset, dynsymSize, 2, 1, 8, sizeof(Symbol64));
    add_shdr(shstr_hash, SHT_HASH, SHF_ALLOC, hashVma, hashOffset, hashSize, 3, 0, 4, 0);
    add_shdr(shstr_text, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, textVma, textOffset, textBytes.size(), 0, 0, 16, 0);
    add_shdr(shstr_got, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, gotVma, gotOffset, gotSize, 0, 0, 8, 8);
    if (!relaDynList.empty()) add_shdr(shstr_rela_dyn, SHT_RELA, SHF_ALLOC, relaDynVma, relaDynOffset, relaDynSize, 3, 0, 8, sizeof(Elf64_Rela));
    if (!relaPltList.empty()) add_shdr(shstr_rela_plt, SHT_RELA, SHF_ALLOC, relaPltVma, relaPltOffset, relaPltSize, 3, 0, 8, sizeof(Elf64_Rela));
    add_shdr(shstr_dynamic, SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE, dynamicVma, dynamicOffset, dynamicSize, 2, 0, 8, sizeof(Dyn64));
    add_shdr(shstr_rodata, SHT_PROGBITS, SHF_ALLOC, rodataVma, rodataOffset, rodataBytes.size(), 0, 0, 8, 0);
    add_shdr(shstr_data, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, dataVma, dataOffset, dataBytes.size(), 0, 0, 8, 0);
    add_shdr(shstr_bss, SHT_NOBITS, SHF_ALLOC | SHF_WRITE, bssVma, bssOffset, bssSize, 0, 0, 8, 0);
    add_shdr(shstr_shstrtab, SHT_STRTAB, 0, 0, shstrtabOffset, shstrtabSize, 0, 0, 1, 0);

    // ELF Header
    ElfHeader64 header{};
    header.e_ident[0] = 0x7f; header.e_ident[1] = 'E'; header.e_ident[2] = 'L'; header.e_ident[3] = 'F';
    header.e_ident[4] = 2; header.e_ident[5] = 1; header.e_ident[6] = 1;
    header.e_type = ET_EXEC; header.e_machine = EM_X86_64; header.e_version = 1;
    header.e_entry = realEntry; header.e_phoff = sizeof(ElfHeader64); header.e_shoff = shoff;
    header.e_ehsize = sizeof(ElfHeader64); header.e_phentsize = sizeof(ProgramHeader64); header.e_phnum = static_cast<uint16_t>(phdrs.size());
    header.e_shentsize = sizeof(SectionHeader64); header.e_shnum = static_cast<uint16_t>(shdrs.size());
    header.e_shstrndx = static_cast<uint16_t>(shdrs.size() - 1);

    const std::string temporary = outputPath + ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) { lastError_ = "failed to open temporary ELF output"; return false; }

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(phdrs.data()), phdrs.size() * sizeof(ProgramHeader64));

    file.seekp(interpOffset); file.write(interpStr.c_str(), interpSize);
    file.seekp(dynstrOffset); file.write(dynstr.c_str(), dynstrSize);
    file.seekp(dynsymOffset); file.write(reinterpret_cast<const char*>(dynsyms.data()), dynsymSize);
    file.seekp(hashOffset); file.write(reinterpret_cast<const char*>(hashData.data()), hashSize);

    if (!textBytes.empty()) {
        file.seekp(textOffset);
        file.write(reinterpret_cast<const char*>(textBytes.data()), textBytes.size());
    }

    if (!gotEntries.empty()) {
        file.seekp(gotOffset);
        file.write(reinterpret_cast<const char*>(gotEntries.data()), gotSize);
    }

    if (!relaDynList.empty()) {
        file.seekp(relaDynOffset);
        file.write(reinterpret_cast<const char*>(relaDynList.data()), relaDynSize);
    }

    if (!relaPltList.empty()) {
        file.seekp(relaPltOffset);
        file.write(reinterpret_cast<const char*>(relaPltList.data()), relaPltSize);
    }

    file.seekp(dynamicOffset);
    file.write(reinterpret_cast<const char*>(dynamics.data()), dynamicSize);

    if (!rodataBytes.empty()) {
        file.seekp(rodataOffset);
        file.write(reinterpret_cast<const char*>(rodataBytes.data()), rodataBytes.size());
    }

    if (!dataBytes.empty()) {
        file.seekp(dataOffset);
        file.write(reinterpret_cast<const char*>(dataBytes.data()), dataBytes.size());
    }

    file.seekp(shstrtabOffset); file.write(shstrtab.data(), shstrtab.size());
    file.seekp(shoff); file.write(reinterpret_cast<const char*>(shdrs.data()), shdrs.size() * sizeof(SectionHeader64));
    file.close();

    if (!file) { std::remove(temporary.c_str()); lastError_ = "failed while writing dynamic ELF output"; return false; }
    if (std::rename(temporary.c_str(), outputPath.c_str()) != 0) {
        std::remove(temporary.c_str()); lastError_ = "failed to replace DYNAMIC ELF output"; return false;
    }
    chmod(outputPath.c_str(), 0755);
    return true;
}

bool ElfExecutableImageBuilder::build(const LinkedImage& image, const std::string& outputPath) {
    ElfImageWriter writer;
    if (!writer.writeExecutable(image, outputPath)) { lastError_ = writer.getLastError(); return false; }
    lastError_.clear();
    return true;
}

} // namespace target::artifact::linker
