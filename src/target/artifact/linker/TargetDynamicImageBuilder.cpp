#include "target/artifact/linker/TargetDynamicImageBuilder.h"
#include <fstream>
#include <cstring>
#include <vector>
#include <algorithm>
#include <map>

namespace target {
namespace artifact {
namespace linker {

namespace {

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
constexpr uint32_t PF_X = 0x1;
constexpr uint32_t PF_W = 0x2;
constexpr uint32_t PF_R = 0x4;
constexpr uint8_t STB_LOCAL = 0;
constexpr uint8_t STB_GLOBAL = 1;
constexpr uint8_t STT_NOTYPE = 0;
constexpr uint8_t STT_OBJECT = 1;
constexpr uint8_t STT_FUNC = 2;

constexpr int64_t DT_NULL = 0;
constexpr int64_t DT_HASH = 4;
constexpr int64_t DT_STRTAB = 5;
constexpr int64_t DT_SYMTAB = 6;
constexpr int64_t DT_STRSZ = 10;
constexpr int64_t DT_SYMENT = 11;

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
#pragma pack(pop)

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

std::unique_ptr<TargetDynamicImageBuilder> TargetDynamicImageBuilder::createForTarget(target::Arch arch, target::OS os) {
    if (arch == target::Arch::X64 && os == target::OS::Linux) {
        return std::make_unique<ElfDynamicImageBuilder>();
    } else if (os == target::OS::Windows) {
        return std::make_unique<PeDynamicImageBuilder>();
    } else if (os == target::OS::MacOS) {
        return std::make_unique<MachODynamicImageBuilder>();
    }
    return nullptr;
}

bool ElfDynamicImageBuilder::buildSharedLibrary(const DynamicLinkPlan& plan, const std::string& outputPath) {
    lastError_.clear();

    if (plan.arch != target::Arch::X64 || plan.os != target::OS::Linux) {
        lastError_ = "shared-library output unsupported for target architecture/OS";
        return false;
    }

    // Build .dynstr, .dynsym, .hash, and .dynamic tables from neutral plan
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

    std::vector<std::pair<std::string, uint32_t>> exportedSymbols;

    for (const auto& exp : plan.exports) {
        Symbol64 s64 = {};
        s64.st_name = add_dynstr(exp.symbol);
        s64.st_value = exp.address;
        s64.st_size = exp.size;
        s64.st_info = (STB_GLOBAL << 4) | (exp.isFunction ? STT_FUNC : STT_NOTYPE);
        s64.st_other = 0; // STV_DEFAULT
        s64.st_shndx = 1; // Section index (.text)

        dynsyms.push_back(s64);
        exportedSymbols.push_back({exp.symbol, static_cast<uint32_t>(dynsyms.size() - 1)});
    }

    uint32_t firstGlobalIdx = 1;

    // Build SYSV ELF Hash table
    uint32_t nbucket = exportedSymbols.empty() ? 1 : exportedSymbols.size();
    uint32_t nchain = dynsyms.size();
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
    uint32_t shstr_rodata = add_shstr(".rodata");
    uint32_t shstr_data = add_shstr(".data");
    uint32_t shstr_bss = add_shstr(".bss");
    uint32_t shstr_dynsym = add_shstr(".dynsym");
    uint32_t shstr_dynstr = add_shstr(".dynstr");
    uint32_t shstr_hash = add_shstr(".hash");
    uint32_t shstr_dynamic = add_shstr(".dynamic");
    uint32_t shstr_shstrtab = add_shstr(".shstrtab");

    // Layout
    uint64_t baseVma = 0x400000ULL;
    uint64_t pageAlign = 0x1000;

    const auto* textSec = plan.findSection(".text");
    const auto* rodataSec = plan.findSection(".rodata");
    const auto* dataSec = plan.findSection(".data");
    const auto* bssSec = plan.findSection(".bss");

    std::vector<uint8_t> textBytes = textSec ? textSec->data : std::vector<uint8_t>{};
    std::vector<uint8_t> rodataBytes = rodataSec ? rodataSec->data : std::vector<uint8_t>{};
    std::vector<uint8_t> dataBytes = dataSec ? dataSec->data : std::vector<uint8_t>{};
    uint64_t bssSize = bssSec ? bssSec->virtualSize : 0;

    uint64_t numPhdrs = 2; // PT_LOAD (RX) + PT_LOAD (RW)
    uint64_t headersSize = sizeof(ElfHeader64) + numPhdrs * sizeof(ProgramHeader64);

    uint64_t textOffset = alignUp(headersSize, 16);
    uint64_t textVma = baseVma + textOffset;

    uint64_t dynstrOffset = alignUp(textOffset + textBytes.size(), 8);
    uint64_t dynstrVma = baseVma + dynstrOffset;
    uint64_t dynstrSize = dynstr.size();

    uint64_t dynsymOffset = alignUp(dynstrOffset + dynstrSize, 8);
    uint64_t dynsymVma = baseVma + dynsymOffset;
    uint64_t dynsymSize = dynsyms.size() * sizeof(Symbol64);

    uint64_t hashOffset = alignUp(dynsymOffset + dynsymSize, 8);
    uint64_t hashVma = baseVma + hashOffset;
    uint64_t hashSize = hashData.size();

    std::vector<Dyn64> dynamics = {
        {DT_HASH, hashVma},
        {DT_STRTAB, dynstrVma},
        {DT_SYMTAB, dynsymVma},
        {DT_STRSZ, dynstrSize},
        {DT_SYMENT, sizeof(Symbol64)},
        {DT_NULL, 0}
    };

    uint64_t dynamicOffset = alignUp(hashOffset + hashSize, 8);
    uint64_t dynamicVma = baseVma + dynamicOffset;
    uint64_t dynamicSize = dynamics.size() * sizeof(Dyn64);

    uint64_t rxEndOffset = dynamicOffset + dynamicSize;

    // RW Segment
    uint64_t rwOffset = alignUp(rxEndOffset, pageAlign);
    uint64_t rwVma = baseVma + rwOffset;

    uint64_t rodataOffset = rwOffset;
    uint64_t rodataVma = rwVma;

    uint64_t dataOffset = alignUp(rodataOffset + rodataBytes.size(), 16);
    uint64_t dataVma = rwVma + (dataOffset - rwOffset);

    uint64_t bssOffset = dataOffset + dataBytes.size();
    uint64_t bssVma = dataVma + dataBytes.size();

    uint64_t shstrtabOffset = alignUp(bssOffset + dataBytes.size(), 8);
    uint64_t shstrtabSize = shstrtab.size();

    uint64_t shoff = alignUp(shstrtabOffset + shstrtabSize, 8);

    // Fixup symbol virtual addresses in .dynsym
    for (size_t i = 1; i < dynsyms.size(); ++i) {
        std::string symName = &dynstr[dynsyms[i].st_name];
        for (const auto& exp : plan.exports) {
            if (exp.symbol == symName) {
                dynsyms[i].st_value = textVma + (exp.address - (textSec ? textSec->virtualAddress : baseVma));
                break;
            }
        }
    }

    // Program Headers
    std::vector<ProgramHeader64> phdrs(2);
    // RX segment
    phdrs[0].p_type = PT_LOAD;
    phdrs[0].p_flags = PF_R | PF_X;
    phdrs[0].p_offset = 0;
    phdrs[0].p_vaddr = baseVma;
    phdrs[0].p_paddr = baseVma;
    phdrs[0].p_filesz = rxEndOffset;
    phdrs[0].p_memsz = rxEndOffset;
    phdrs[0].p_align = pageAlign;

    // PT_DYNAMIC segment
    phdrs[1].p_type = PT_DYNAMIC;
    phdrs[1].p_flags = PF_R;
    phdrs[1].p_offset = dynamicOffset;
    phdrs[1].p_vaddr = dynamicVma;
    phdrs[1].p_paddr = dynamicVma;
    phdrs[1].p_filesz = dynamicSize;
    phdrs[1].p_memsz = dynamicSize;
    phdrs[1].p_align = 8;

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

    add_shdr(shstr_text, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, textVma, textOffset, textBytes.size(), 0, 0, 16, 0);
    add_shdr(shstr_dynstr, SHT_STRTAB, SHF_ALLOC, dynstrVma, dynstrOffset, dynstrSize, 0, 0, 1, 0);
    add_shdr(shstr_dynsym, SHT_DYNSYM, SHF_ALLOC, dynsymVma, dynsymOffset, dynsymSize, 2, firstGlobalIdx, 8, sizeof(Symbol64));
    add_shdr(shstr_hash, SHT_HASH, SHF_ALLOC, hashVma, hashOffset, hashSize, 3, 0, 4, 0);
    add_shdr(shstr_dynamic, SHT_DYNAMIC, SHF_ALLOC, dynamicVma, dynamicOffset, dynamicSize, 2, 0, 8, sizeof(Dyn64));
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

    if (!textBytes.empty()) {
        file.seekp(textOffset);
        file.write(reinterpret_cast<const char*>(textBytes.data()), textBytes.size());
    }

    file.seekp(dynsymOffset);
    file.write(reinterpret_cast<const char*>(dynsyms.data()), dynsyms.size() * sizeof(Symbol64));

    file.seekp(dynstrOffset);
    file.write(dynstr.c_str(), dynstr.size());

    file.seekp(hashOffset);
    file.write(reinterpret_cast<const char*>(hashData.data()), hashData.size());

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

bool PeDynamicImageBuilder::buildSharedLibrary(const DynamicLinkPlan& plan, const std::string& outputPath) {
    (void)outputPath;
    if (plan.os != target::OS::Windows) {
        lastError_ = "shared-library output target OS mismatch for PE builder";
        return false;
    }
    lastError_ = "shared-library output not implemented for target: Windows/PE";
    return false;
}

bool MachODynamicImageBuilder::buildSharedLibrary(const DynamicLinkPlan& plan, const std::string& outputPath) {
    (void)outputPath;
    if (plan.os != target::OS::MacOS) {
        lastError_ = "shared-library output target OS mismatch for Mach-O builder";
        return false;
    }
    lastError_ = "shared-library output not implemented for target: macOS/Mach-O";
    return false;
}

} // namespace linker
} // namespace artifact
} // namespace target
