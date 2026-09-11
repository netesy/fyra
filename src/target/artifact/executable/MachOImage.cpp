#include "target/artifact/executable/MachOImage.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

#include <sys/stat.h>

namespace target::artifact::linker {

namespace {

constexpr uint32_t MH_MAGIC_64 = 0xfeedfacf;
constexpr uint32_t MH_EXECUTE = 0x2;
constexpr uint32_t CPU_TYPE_X86_64 = 0x01000007;
constexpr uint32_t CPU_TYPE_ARM64 = 0x0100000c;
constexpr uint32_t CPU_SUBTYPE_LIB64 = 0x80000000;
constexpr uint32_t CPU_SUBTYPE_X86_64_ALL = CPU_SUBTYPE_LIB64 | 3;
constexpr uint32_t CPU_SUBTYPE_ARM64_ALL = 0;

constexpr uint32_t LC_SEGMENT_64 = 0x19;
constexpr uint32_t LC_SYMTAB = 0x2;
constexpr uint32_t LC_MAIN = 0x80000028;

constexpr uint32_t VM_PROT_READ = 0x1;
constexpr uint32_t VM_PROT_WRITE = 0x2;
constexpr uint32_t VM_PROT_EXECUTE = 0x4;

#pragma pack(push, 1)
struct mach_header_64 {
    uint32_t magic;
    int32_t  cputype;
    int32_t  cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
};

struct segment_command_64 {
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    int32_t  maxprot;
    int32_t  initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct section_64 {
    char     sectname[16];
    char     segname[16];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
};

struct entry_point_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint64_t entryoff;
    uint64_t stacksize;
};

struct symtab_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
};

struct nlist_64 {
    uint32_t n_strx;
    uint8_t  n_type;
    uint8_t  n_sect;
    uint16_t n_desc;
    uint64_t n_value;
};
#pragma pack(pop)

uint64_t alignUp(uint64_t val, uint64_t align) {
    if (align <= 1) return val;
    return (val + align - 1) & ~(align - 1);
}

} // namespace

bool MachOImageWriter::writeExecutable(const LinkedImage& image, const std::string& outputPath) {
    lastError_.clear();
    if (image.os != target::OS::MacOS || image.outputKind != LinkOutputKind::Executable) {
        lastError_ = "Mach-O executable writer requires a macOS executable LinkedImage";
        return false;
    }
    if (image.entryAddress == 0) {
        lastError_ = "Mach-O executable image has no resolved entrypoint";
        return false;
    }

    uint32_t cpuType = (image.arch == target::Arch::AArch64) ? CPU_TYPE_ARM64 : CPU_TYPE_X86_64;
    uint32_t cpuSubtype = (cpuType == CPU_TYPE_ARM64) ? CPU_SUBTYPE_ARM64_ALL : CPU_SUBTYPE_X86_64_ALL;

    const auto* textSec = image.findSection(".text");
    const auto* rodataSec = image.findSection(".rodata");
    if (!rodataSec) rodataSec = image.findSection(".rdata");
    const auto* dataSec = image.findSection(".data");
    const auto* bssSec = image.findSection(".bss");

    uint64_t textDataSize = textSec ? textSec->data.size() : 0;
    uint64_t constDataSize = rodataSec ? rodataSec->data.size() : 0;
    uint64_t dataDataSize = dataSec ? dataSec->data.size() : 0;
    uint64_t bssVirtualSize = bssSec ? bssSec->virtualSize : 0;

    uint32_t dataNSects = (dataDataSize > 0 ? 1 : 0) + (bssVirtualSize > 0 ? 1 : 0);
    if (dataNSects == 0) dataNSects = 1;

    uint32_t ncmds = 5;
    uint32_t sizeofcmds = sizeof(segment_command_64) +
                          (sizeof(segment_command_64) + sizeof(section_64) * 2) +
                          (sizeof(segment_command_64) + sizeof(section_64) * dataNSects) +
                          sizeof(entry_point_command) +
                          sizeof(symtab_command);

    mach_header_64 header = {};
    header.magic = MH_MAGIC_64;
    header.cputype = cpuType;
    header.cpusubtype = cpuSubtype;
    header.filetype = MH_EXECUTE;
    header.ncmds = ncmds;
    header.sizeofcmds = sizeofcmds;
    header.flags = 0x1; // MH_NOUNDEFS

    uint64_t currentFileOff = sizeof(mach_header_64) + sizeofcmds;
    uint64_t currentVMAddr = 0x100000000ULL;

    segment_command_64 pagezero = {};
    pagezero.cmd = LC_SEGMENT_64;
    pagezero.cmdsize = sizeof(segment_command_64);
    std::strcpy(pagezero.segname, "__PAGEZERO");
    pagezero.vmsize = 0x100000000ULL;

    uint64_t textSectsSize = textDataSize + alignUp(constDataSize, 16);
    uint64_t textVMSize = alignUp(textSectsSize + currentFileOff, 0x1000);

    segment_command_64 textSeg = {};
    textSeg.cmd = LC_SEGMENT_64;
    textSeg.cmdsize = sizeof(segment_command_64) + sizeof(section_64) * 2;
    std::strcpy(textSeg.segname, "__TEXT");
    textSeg.vmaddr = currentVMAddr;
    textSeg.vmsize = textVMSize;
    textSeg.fileoff = 0;
    textSeg.filesize = currentFileOff + textSectsSize;
    textSeg.maxprot = VM_PROT_READ | VM_PROT_EXECUTE;
    textSeg.initprot = VM_PROT_READ | VM_PROT_EXECUTE;
    textSeg.nsects = 2;

    section_64 textSect = {};
    std::strcpy(textSect.sectname, "__text");
    std::strcpy(textSect.segname, "__TEXT");
    textSect.addr = currentVMAddr + currentFileOff;
    textSect.size = textDataSize;
    textSect.offset = static_cast<uint32_t>(currentFileOff);
    textSect.align = 4;
    textSect.flags = 0x80000400; // S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS

    uint64_t constFileOff = currentFileOff + textDataSize;
    constFileOff = alignUp(constFileOff, 16);

    section_64 constSect = {};
    std::strcpy(constSect.sectname, "__const");
    std::strcpy(constSect.segname, "__TEXT");
    constSect.addr = currentVMAddr + constFileOff;
    constSect.size = constDataSize;
    constSect.offset = static_cast<uint32_t>(constFileOff);
    constSect.align = 3;

    currentFileOff = alignUp(constFileOff + constDataSize, 0x1000);
    currentVMAddr += textVMSize;

    uint64_t dataVMSize = alignUp(dataDataSize + bssVirtualSize, 0x1000);

    segment_command_64 dataSeg = {};
    dataSeg.cmd = LC_SEGMENT_64;
    dataSeg.cmdsize = sizeof(segment_command_64) + sizeof(section_64) * dataNSects;
    std::strcpy(dataSeg.segname, "__DATA");
    dataSeg.vmaddr = currentVMAddr;
    dataSeg.vmsize = dataVMSize;
    dataSeg.fileoff = currentFileOff;
    dataSeg.filesize = dataDataSize;
    dataSeg.maxprot = VM_PROT_READ | VM_PROT_WRITE;
    dataSeg.initprot = VM_PROT_READ | VM_PROT_WRITE;
    dataSeg.nsects = dataNSects;

    section_64 dataSect = {};
    if (dataDataSize > 0) {
        std::strcpy(dataSect.sectname, "__data");
        std::strcpy(dataSect.segname, "__DATA");
        dataSect.addr = currentVMAddr;
        dataSect.size = dataDataSize;
        dataSect.offset = static_cast<uint32_t>(currentFileOff);
        dataSect.align = 3;
    }

    section_64 bssSect = {};
    if (bssVirtualSize > 0) {
        std::strcpy(bssSect.sectname, "__bss");
        std::strcpy(bssSect.segname, "__DATA");
        bssSect.addr = currentVMAddr + dataDataSize;
        bssSect.size = bssVirtualSize;
        bssSect.offset = 0;
        bssSect.align = 4;
        bssSect.flags = 0x1; // S_ZEROFILL
    }

    currentFileOff += alignUp(dataDataSize, 0x1000);
    currentVMAddr += dataVMSize;

    entry_point_command mainCmd = {};
    mainCmd.cmd = LC_MAIN;
    mainCmd.cmdsize = sizeof(entry_point_command);
    // LinkedImage entryAddress is mapped relative to text section start
    uint64_t textBaseVma = textSec ? textSec->virtualAddress : 0x400000ULL;
    uint64_t entryOffsetInText = (image.entryAddress >= textBaseVma) ? (image.entryAddress - textBaseVma) : 0;
    mainCmd.entryoff = textSect.offset + entryOffsetInText;

    std::vector<nlist_64> nlists;
    std::string stringTable = "\0";

    for (const auto& [name, sym] : image.symbols) {
        nlist_64 nl = {};
        nl.n_strx = static_cast<uint32_t>(stringTable.size());
        stringTable += "_" + sym.name;
        stringTable.push_back('\0');

        nl.n_type = 0x0F; // N_SECT | N_EXT
        nl.n_sect = (sym.sectionName == ".text") ? 1 : ((sym.sectionName == ".data") ? 2 : 1);
        nl.n_desc = 0;
        nl.n_value = sym.virtualAddress;
        nlists.push_back(nl);
    }

    uint64_t symOff = currentFileOff;
    uint64_t strOff = symOff + nlists.size() * sizeof(nlist_64);

    symtab_command symCmd = {};
    symCmd.cmd = LC_SYMTAB;
    symCmd.cmdsize = sizeof(symtab_command);
    symCmd.symoff = static_cast<uint32_t>(symOff);
    symCmd.nsyms = static_cast<uint32_t>(nlists.size());
    symCmd.stroff = static_cast<uint32_t>(strOff);
    symCmd.strsize = static_cast<uint32_t>(stringTable.size());

    const std::string temporary = outputPath + ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) {
        lastError_ = "failed to open temporary Mach-O output: " + temporary;
        return false;
    }

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&pagezero), sizeof(pagezero));
    file.write(reinterpret_cast<const char*>(&textSeg), sizeof(textSeg));
    file.write(reinterpret_cast<const char*>(&textSect), sizeof(textSect));
    file.write(reinterpret_cast<const char*>(&constSect), sizeof(constSect));
    file.write(reinterpret_cast<const char*>(&dataSeg), sizeof(dataSeg));
    if (dataDataSize > 0) {
        file.write(reinterpret_cast<const char*>(&dataSect), sizeof(dataSect));
    }
    if (bssVirtualSize > 0) {
        file.write(reinterpret_cast<const char*>(&bssSect), sizeof(bssSect));
    }
    if (dataDataSize == 0 && bssVirtualSize == 0) {
        // Fallback dummy section header if dataNSects is 1
        section_64 dummySect = {};
        std::strcpy(dummySect.sectname, "__data");
        std::strcpy(dummySect.segname, "__DATA");
        dummySect.addr = currentVMAddr;
        file.write(reinterpret_cast<const char*>(&dummySect), sizeof(dummySect));
    }
    file.write(reinterpret_cast<const char*>(&mainCmd), sizeof(mainCmd));
    file.write(reinterpret_cast<const char*>(&symCmd), sizeof(symCmd));

    if (textSec && !textSec->data.empty()) {
        file.seekp(textSect.offset);
        file.write(reinterpret_cast<const char*>(textSec->data.data()), textSec->data.size());
    }
    if (rodataSec && !rodataSec->data.empty()) {
        file.seekp(constSect.offset);
        file.write(reinterpret_cast<const char*>(rodataSec->data.data()), rodataSec->data.size());
    }
    if (dataSec && !dataSec->data.empty()) {
        file.seekp(dataSect.offset);
        file.write(reinterpret_cast<const char*>(dataSec->data.data()), dataSec->data.size());
    }

    file.seekp(symOff);
    if (!nlists.empty()) {
        file.write(reinterpret_cast<const char*>(nlists.data()), nlists.size() * sizeof(nlist_64));
    }
    file.seekp(strOff);
    file.write(stringTable.c_str(), stringTable.size());

    file.close();
    if (!file) {
        std::remove(temporary.c_str());
        lastError_ = "failed while writing Mach-O output";
        return false;
    }

    if (std::rename(temporary.c_str(), outputPath.c_str()) != 0) {
        std::remove(temporary.c_str());
        lastError_ = "failed to replace Mach-O output";
        return false;
    }

    chmod(outputPath.c_str(), 0755);
    return true;
}

bool MachOExecutableImageBuilder::build(const LinkedImage& image, const std::string& outputPath) {
    MachOImageWriter writer;
    if (!writer.writeExecutable(image, outputPath)) {
        lastError_ = writer.getLastError();
        return false;
    }
    lastError_.clear();
    return true;
}

} // namespace target::artifact::linker
