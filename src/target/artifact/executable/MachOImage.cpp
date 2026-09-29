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
constexpr uint32_t MH_OBJECT = 0x1;
constexpr uint32_t MH_EXECUTE = 0x2;
constexpr uint32_t MH_DYLIB = 0x6;

constexpr uint32_t MH_NOUNDEFS = 0x1;
constexpr uint32_t MH_DYLDLINK = 0x4;
constexpr uint32_t MH_TWOLEVEL = 0x80;

constexpr uint32_t CPU_TYPE_X86_64 = 0x01000007;
constexpr uint32_t CPU_TYPE_ARM64 = 0x0100000c;
constexpr uint32_t CPU_SUBTYPE_LIB64 = 0x80000000;
constexpr uint32_t CPU_SUBTYPE_X86_64_ALL = CPU_SUBTYPE_LIB64 | 3;
constexpr uint32_t CPU_SUBTYPE_ARM64_ALL = 0;

constexpr uint32_t LC_SEGMENT_64 = 0x19;
constexpr uint32_t LC_SYMTAB = 0x2;
constexpr uint32_t LC_DYSYMTAB = 0x0b;
constexpr uint32_t LC_LOAD_DYLINKER = 0x0e;
constexpr uint32_t LC_ID_DYLIB = 0x0d;
constexpr uint32_t LC_LOAD_DYLIB = 0x0c;
constexpr uint32_t LC_MAIN = 0x80000028;
constexpr uint32_t LC_DYLD_INFO_ONLY = 0x80000022;

constexpr uint32_t VM_PROT_READ = 0x1;
constexpr uint32_t VM_PROT_WRITE = 0x2;
constexpr uint32_t VM_PROT_EXECUTE = 0x4;

constexpr uint8_t BIND_OPCODE_DONE = 0x00;
constexpr uint8_t BIND_OPCODE_SET_DYLIB_ORDINAL_IMM = 0x10;
constexpr uint8_t BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM = 0x40;
constexpr uint8_t BIND_OPCODE_SET_TYPE_IMM = 0x50;
constexpr uint8_t BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB = 0x70;
constexpr uint8_t BIND_OPCODE_DO_BIND = 0x90;
constexpr uint8_t BIND_TYPE_POINTER = 1;

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

struct dysymtab_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t ilocalsym;
    uint32_t nlocalsym;
    uint32_t iextdefsym;
    uint32_t nextdefsym;
    uint32_t iundefsym;
    uint32_t nundefsym;
    uint32_t tocoff;
    uint32_t ntoc;
    uint32_t modtaboff;
    uint32_t nmodtab;
    uint32_t extrefsymoff;
    uint32_t nextrefsyms;
    uint32_t indirectsymoff;
    uint32_t nindirectsyms;
    uint32_t extreloff;
    uint32_t nextrel;
    uint32_t locreloff;
    uint32_t nlocrel;
};

struct dylib_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t name_offset;
    uint32_t timestamp;
    uint32_t current_version;
    uint32_t compatibility_version;
};

struct dylinker_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t name_offset;
};

struct dyld_info_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t rebase_off;
    uint32_t rebase_size;
    uint32_t bind_off;
    uint32_t bind_size;
    uint32_t weak_bind_off;
    uint32_t weak_bind_size;
    uint32_t lazy_bind_off;
    uint32_t lazy_bind_size;
    uint32_t export_off;
    uint32_t export_size;
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

void appendULEB128(std::vector<uint8_t>& buf, uint64_t val) {
    do {
        uint8_t byte = val & 0x7f;
        val >>= 7;
        if (val != 0) byte |= 0x80;
        buf.push_back(byte);
    } while (val != 0);
}

} // namespace

bool MachOImageWriter::writeSharedLibrary(const DynamicLinkPlan& plan, const std::string& outputPath) {
    lastError_.clear();

    if (plan.os != target::OS::MacOS) {
        lastError_ = "Mach-O dylib writer requires a macOS DynamicLinkPlan";
        return false;
    }

    uint32_t cpuType = (plan.arch == target::Arch::AArch64) ? CPU_TYPE_ARM64 : CPU_TYPE_X86_64;
    uint32_t cpuSubtype = (cpuType == CPU_TYPE_ARM64) ? CPU_SUBTYPE_ARM64_ALL : CPU_SUBTYPE_X86_64_ALL;

    std::string dylibName = outputPath;
    size_t slash = dylibName.find_last_of("/\\");
    if (slash != std::string::npos) dylibName = dylibName.substr(slash + 1);

    const auto* textSec = plan.findSection(".text");
    const auto* rodataSec = plan.findSection(".rodata");
    if (!rodataSec) rodataSec = plan.findSection(".rdata");
    const auto* dataSec = plan.findSection(".data");
    const auto* bssSec = plan.findSection(".bss");

    std::vector<uint8_t> textBytes = textSec ? textSec->data : std::vector<uint8_t>{};
    std::vector<uint8_t> rodataBytes = rodataSec ? rodataSec->data : std::vector<uint8_t>{};
    std::vector<uint8_t> dataBytes = dataSec ? dataSec->data : std::vector<uint8_t>{};
    uint64_t bssSize = bssSec ? bssSec->virtualSize : 0;

    // Symbol table & string table
    std::vector<nlist_64> nlists;
    std::string stringTable = "\0";

    for (const auto& exp : plan.exports) {
        nlist_64 nl = {};
        nl.n_strx = static_cast<uint32_t>(stringTable.size());
        stringTable += "_" + exp.symbol;
        stringTable.push_back('\0');

        nl.n_type = 0x0F; // N_SECT | N_EXT
        nl.n_sect = (exp.sectionName == ".data") ? 3 : 1; // 1: __text, 2: __const, 3: __data
        nl.n_desc = 0;
        nl.n_value = exp.address;
        nlists.push_back(nl);
    }

    // Build dyld export trie
    std::vector<uint8_t> exportTrie;

    struct ExportTerminal {
        std::string name;
        std::vector<uint8_t> bytes;
    };
    std::vector<ExportTerminal> terminals;
    for (const auto& exp : plan.exports) {
        ExportTerminal t;
        t.name = "_" + exp.symbol;
        std::vector<uint8_t> payload;
        appendULEB128(payload, 0); // Regular symbol
        appendULEB128(payload, exp.address); // Symbol VM address
        appendULEB128(t.bytes, payload.size());
        t.bytes.insert(t.bytes.end(), payload.begin(), payload.end());
        terminals.push_back(t);
    }

    std::vector<uint8_t> rootHeader;
    rootHeader.push_back(0); // Terminal size = 0
    appendULEB128(rootHeader, terminals.size()); // Child count

    std::vector<uint64_t> childOffsets(terminals.size(), 0);
    bool changed = true;
    while (changed) {
        changed = false;
        uint64_t currentOffset = rootHeader.size();
        for (size_t i = 0; i < terminals.size(); ++i) {
            currentOffset += terminals[i].name.size() + 1;
            std::vector<uint8_t> dummyUleb;
            appendULEB128(dummyUleb, childOffsets[i]);
            currentOffset += dummyUleb.size();
        }
        uint64_t runningTerminalOffset = currentOffset;
        for (size_t i = 0; i < terminals.size(); ++i) {
            if (childOffsets[i] != runningTerminalOffset) {
                childOffsets[i] = runningTerminalOffset;
                changed = true;
            }
            runningTerminalOffset += terminals[i].bytes.size();
        }
    }

    exportTrie = rootHeader;
    for (size_t i = 0; i < terminals.size(); ++i) {
        for (char c : terminals[i].name) exportTrie.push_back(c);
        exportTrie.push_back('\0');
        appendULEB128(exportTrie, childOffsets[i]);
    }
    for (const auto& t : terminals) {
        exportTrie.insert(exportTrie.end(), t.bytes.begin(), t.bytes.end());
    }

    // Command sizes
    uint32_t idDylibCmdSize = alignUp(sizeof(dylib_command) + dylibName.size() + 1, 8);
    uint32_t ncmds = 5; // __TEXT, __DATA, LC_ID_DYLIB, LC_SYMTAB, LC_DYLD_INFO_ONLY
    uint32_t sizeofcmds = sizeof(segment_command_64) + sizeof(section_64) * 2 + // __TEXT (__text, __const)
                          sizeof(segment_command_64) + sizeof(section_64) * 2 + // __DATA (__data, __bss)
                          idDylibCmdSize +
                          sizeof(symtab_command) +
                          sizeof(dysymtab_command) +
                          sizeof(dyld_info_command);

    mach_header_64 header = {};
    header.magic = MH_MAGIC_64;
    header.cputype = cpuType;
    header.cpusubtype = cpuSubtype;
    header.filetype = MH_DYLIB;
    header.ncmds = ncmds + 1; // Including LC_DYSYMTAB
    header.sizeofcmds = sizeofcmds;
    header.flags = MH_NOUNDEFS | MH_DYLDLINK | MH_TWOLEVEL;

    uint64_t currentFileOff = sizeof(mach_header_64) + sizeofcmds;
    uint64_t currentVMAddr = 0x0ULL;

    // Segment & Section Layout
    uint64_t textSectsSize = textBytes.size() + alignUp(rodataBytes.size(), 16);
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
    textSect.size = textBytes.size();
    textSect.offset = static_cast<uint32_t>(currentFileOff);
    textSect.align = 4;
    textSect.flags = 0x80000400; // S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS

    uint64_t constFileOff = currentFileOff + textBytes.size();
    constFileOff = alignUp(constFileOff, 16);

    section_64 constSect = {};
    std::strcpy(constSect.sectname, "__const");
    std::strcpy(constSect.segname, "__TEXT");
    constSect.addr = currentVMAddr + constFileOff;
    constSect.size = rodataBytes.size();
    constSect.offset = static_cast<uint32_t>(constFileOff);
    constSect.align = 3;

    currentFileOff = alignUp(constFileOff + rodataBytes.size(), 0x1000);
    currentVMAddr += textVMSize;

    uint64_t dataVMSize = alignUp(dataBytes.size() + bssSize, 0x1000);

    segment_command_64 dataSeg = {};
    dataSeg.cmd = LC_SEGMENT_64;
    dataSeg.cmdsize = sizeof(segment_command_64) + sizeof(section_64) * 2;
    std::strcpy(dataSeg.segname, "__DATA");
    dataSeg.vmaddr = currentVMAddr;
    dataSeg.vmsize = dataVMSize;
    dataSeg.fileoff = currentFileOff;
    dataSeg.filesize = dataBytes.size();
    dataSeg.maxprot = VM_PROT_READ | VM_PROT_WRITE;
    dataSeg.initprot = VM_PROT_READ | VM_PROT_WRITE;
    dataSeg.nsects = 2;

    section_64 dataSect = {};
    std::strcpy(dataSect.sectname, "__data");
    std::strcpy(dataSect.segname, "__DATA");
    dataSect.addr = currentVMAddr;
    dataSect.size = dataBytes.size();
    dataSect.offset = static_cast<uint32_t>(currentFileOff);
    dataSect.align = 3;

    section_64 bssSect = {};
    std::strcpy(bssSect.sectname, "__bss");
    std::strcpy(bssSect.segname, "__DATA");
    bssSect.addr = currentVMAddr + dataBytes.size();
    bssSect.size = bssSize;
    bssSect.offset = 0;
    bssSect.align = 4;
    bssSect.flags = 0x1; // S_ZEROFILL

    currentFileOff += alignUp(dataBytes.size(), 0x1000);

    // Fixup export addresses relative to segment VMAs
    for (size_t i = 0; i < plan.exports.size(); ++i) {
        if (plan.exports[i].sectionName == ".data") {
            nlists[i].n_value = dataSect.addr + (plan.exports[i].address - (dataSec ? dataSec->virtualAddress : 0));
        } else {
            nlists[i].n_value = textSect.addr + (plan.exports[i].address - (textSec ? textSec->virtualAddress : 0));
        }
    }

    // Commands
    dylib_command idDylib = {};
    idDylib.cmd = LC_ID_DYLIB;
    idDylib.cmdsize = idDylibCmdSize;
    idDylib.name_offset = sizeof(dylib_command);
    idDylib.timestamp = 1;
    idDylib.current_version = 0x00010000;
    idDylib.compatibility_version = 0x00010000;

    uint64_t symOff = currentFileOff;
    uint64_t strOff = symOff + nlists.size() * sizeof(nlist_64);
    uint64_t exportTrieOff = alignUp(strOff + stringTable.size(), 8);

    symtab_command symCmd = {};
    symCmd.cmd = LC_SYMTAB;
    symCmd.cmdsize = sizeof(symtab_command);
    symCmd.symoff = static_cast<uint32_t>(symOff);
    symCmd.nsyms = static_cast<uint32_t>(nlists.size());
    symCmd.stroff = static_cast<uint32_t>(strOff);
    symCmd.strsize = static_cast<uint32_t>(stringTable.size());

    dysymtab_command dysymCmd = {};
    dysymCmd.cmd = LC_DYSYMTAB;
    dysymCmd.cmdsize = sizeof(dysymtab_command);
    dysymCmd.iextdefsym = 0;
    dysymCmd.nextdefsym = static_cast<uint32_t>(nlists.size());

    dyld_info_command dyldInfo = {};
    dyldInfo.cmd = LC_DYLD_INFO_ONLY;
    dyldInfo.cmdsize = sizeof(dyld_info_command);
    dyldInfo.export_off = static_cast<uint32_t>(exportTrieOff);
    dyldInfo.export_size = static_cast<uint32_t>(exportTrie.size());

    const std::string temporary = outputPath + ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) {
        lastError_ = "failed to open temporary Mach-O output: " + temporary;
        return false;
    }

    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&textSeg), sizeof(textSeg));
    file.write(reinterpret_cast<const char*>(&textSect), sizeof(textSect));
    file.write(reinterpret_cast<const char*>(&constSect), sizeof(constSect));
    file.write(reinterpret_cast<const char*>(&dataSeg), sizeof(dataSeg));
    file.write(reinterpret_cast<const char*>(&dataSect), sizeof(dataSect));
    file.write(reinterpret_cast<const char*>(&bssSect), sizeof(bssSect));

    file.write(reinterpret_cast<const char*>(&idDylib), sizeof(dylib_command));
    file.write(dylibName.c_str(), dylibName.size() + 1);
    uint32_t pad = idDylibCmdSize - (sizeof(dylib_command) + dylibName.size() + 1);
    if (pad > 0) {
        std::vector<char> zeroPad(pad, 0);
        file.write(zeroPad.data(), pad);
    }

    file.write(reinterpret_cast<const char*>(&symCmd), sizeof(symCmd));
    file.write(reinterpret_cast<const char*>(&dysymCmd), sizeof(dysymCmd));
    file.write(reinterpret_cast<const char*>(&dyldInfo), sizeof(dyldInfo));

    if (!textBytes.empty()) {
        file.seekp(textSect.offset);
        file.write(reinterpret_cast<const char*>(textBytes.data()), textBytes.size());
    }
    if (!rodataBytes.empty()) {
        file.seekp(constSect.offset);
        file.write(reinterpret_cast<const char*>(rodataBytes.data()), rodataBytes.size());
    }
    if (!dataBytes.empty()) {
        file.seekp(dataSect.offset);
        file.write(reinterpret_cast<const char*>(dataBytes.data()), dataBytes.size());
    }

    file.seekp(symOff);
    if (!nlists.empty()) {
        file.write(reinterpret_cast<const char*>(nlists.data()), nlists.size() * sizeof(nlist_64));
    }
    file.seekp(strOff);
    file.write(stringTable.c_str(), stringTable.size());

    file.seekp(exportTrieOff);
    if (!exportTrie.empty()) {
        file.write(reinterpret_cast<const char*>(exportTrie.data()), exportTrie.size());
    }

    file.close();
    if (!file) {
        std::remove(temporary.c_str());
        lastError_ = "failed while writing Mach-O dylib output";
        return false;
    }

    if (std::rename(temporary.c_str(), outputPath.c_str()) != 0) {
        std::remove(temporary.c_str());
        lastError_ = "failed to replace Mach-O dylib output";
        return false;
    }

    chmod(outputPath.c_str(), 0755);
    return true;
}


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

    bool isDynamicExecutable = !image.importThunkVmas.empty() || !image.dataImportFixups.empty();

    if (!isDynamicExecutable) {
        // Static executable path
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
        header.flags = MH_NOUNDEFS;

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

    // Dynamic Executable Path
    uint32_t cpuType = (image.arch == target::Arch::AArch64) ? CPU_TYPE_ARM64 : CPU_TYPE_X86_64;
    uint32_t cpuSubtype = (cpuType == CPU_TYPE_ARM64) ? CPU_SUBTYPE_ARM64_ALL : CPU_SUBTYPE_X86_64_ALL;

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

    for (const auto& [symName, thunkVma] : image.importThunkVmas) {
        ImportInfo imp;
        imp.name = symName;
        imp.isFunction = true;
        imp.thunkVma = thunkVma;
        imp.gotSlotVma = 0;
        auto libIt = image.importLibraryNames.find(symName);
        imp.libName = (libIt != image.importLibraryNames.end()) ? libIt->second : "libfyra_fixture.dylib";
        addNeeded(imp.libName);
        imports.push_back(imp);
    }

    for (const auto& fixup : image.dataImportFixups) {
        ImportInfo imp;
        imp.name = fixup.symbolName;
        imp.isFunction = false;
        imp.thunkVma = 0;
        imp.gotSlotVma = 0;
        auto libIt = image.importLibraryNames.find(fixup.symbolName);
        imp.libName = (libIt != image.importLibraryNames.end()) ? libIt->second : "libfyra_fixture.dylib";
        addNeeded(imp.libName);
        imports.push_back(imp);
    }

    // Base text & data section bytes
    auto textSec = image.findSection(".text");
    auto rodataSec = image.findSection(".rodata");
    auto dataSec = image.findSection(".data");
    auto bssSec = image.findSection(".bss");

    std::vector<uint8_t> textBytes = textSec ? textSec->data : std::vector<uint8_t>{};
    std::vector<uint8_t> rodataBytes = rodataSec ? rodataSec->data : std::vector<uint8_t>{};
    std::vector<uint8_t> dataBytes = dataSec ? dataSec->data : std::vector<uint8_t>{};
    uint64_t bssSize = bssSec ? bssSec->virtualSize : 0;

    std::string dylinkerPath = "/usr/lib/dyld";
    uint32_t dylinkerCmdSize = alignUp(sizeof(dylinker_command) + dylinkerPath.size() + 1, 8);

    std::vector<uint32_t> loadDylibCmdSizes;
    for (const auto& lib : neededLibs) {
        loadDylibCmdSizes.push_back(alignUp(sizeof(dylib_command) + lib.size() + 1, 8));
    }

    // Symbol table (undefined symbols for imports, defined symbols for main)
    std::vector<nlist_64> nlists;
    std::string stringTable = "\0";

    // Defined symbols first
    for (const auto& [name, sym] : image.symbols) {
        nlist_64 nl = {};
        nl.n_strx = static_cast<uint32_t>(stringTable.size());
        stringTable += "_" + sym.name;
        stringTable.push_back('\0');

        nl.n_type = 0x0F; // N_SECT | N_EXT
        nl.n_sect = 1; // __text
        nl.n_desc = 0;
        nl.n_value = sym.virtualAddress;
        nlists.push_back(nl);
    }

    uint32_t nextDefSyms = static_cast<uint32_t>(nlists.size());
    uint32_t iUndefSyms = nextDefSyms;

    // Undefined symbols
    for (const auto& imp : imports) {
        nlist_64 nl = {};
        nl.n_strx = static_cast<uint32_t>(stringTable.size());
        stringTable += "_" + imp.name;
        stringTable.push_back('\0');

        nl.n_type = 0x01; // N_EXT (undefined)
        nl.n_sect = 0; // NO_SECT
        nl.n_desc = 0;
        nl.n_value = 0;
        nlists.push_back(nl);
    }

    uint32_t nUndefSyms = static_cast<uint32_t>(imports.size());

    // Calculate layout & commands
    uint32_t totalLoadDylibCmdsSize = 0;
    for (uint32_t sz : loadDylibCmdSizes) totalLoadDylibCmdsSize += sz;

    uint32_t ncmds = 6 + static_cast<uint32_t>(neededLibs.size()); // PAGEZERO, __TEXT, __DATA, DYLINKER, MAIN, SYMTAB, DYSYMTAB, DYLD_INFO + LOAD_DYLIBs
    uint32_t sizeofcmds = sizeof(segment_command_64) + // PAGEZERO
                          sizeof(segment_command_64) + sizeof(section_64) * 2 + // __TEXT (__text, __const)
                          sizeof(segment_command_64) + sizeof(section_64) * 3 + // __DATA (__got, __data, __bss)
                          dylinkerCmdSize +
                          totalLoadDylibCmdsSize +
                          sizeof(entry_point_command) +
                          sizeof(symtab_command) +
                          sizeof(dysymtab_command) +
                          sizeof(dyld_info_command);

    mach_header_64 header = {};
    header.magic = MH_MAGIC_64;
    header.cputype = cpuType;
    header.cpusubtype = cpuSubtype;
    header.filetype = MH_EXECUTE;
    header.ncmds = ncmds + 1;
    header.sizeofcmds = sizeofcmds;
    header.flags = MH_NOUNDEFS | MH_DYLDLINK | MH_TWOLEVEL;

    uint64_t currentFileOff = sizeof(mach_header_64) + sizeofcmds;
    uint64_t currentVMAddr = 0x100000000ULL;

    segment_command_64 pagezero = {};
    pagezero.cmd = LC_SEGMENT_64;
    pagezero.cmdsize = sizeof(segment_command_64);
    std::strcpy(pagezero.segname, "__PAGEZERO");
    pagezero.vmsize = 0x100000000ULL;

    uint64_t textSectsSize = textBytes.size() + alignUp(rodataBytes.size(), 16);
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
    textSect.size = textBytes.size();
    textSect.offset = static_cast<uint32_t>(currentFileOff);
    textSect.align = 4;
    textSect.flags = 0x80000400;

    uint64_t textAddrDiff = textSect.addr - (textSec ? textSec->virtualAddress : 0x400000ULL);

    uint64_t constFileOff = currentFileOff + textBytes.size();
    constFileOff = alignUp(constFileOff, 16);

    section_64 constSect = {};
    std::strcpy(constSect.sectname, "__const");
    std::strcpy(constSect.segname, "__TEXT");
    constSect.addr = currentVMAddr + constFileOff;
    constSect.size = rodataBytes.size();
    constSect.offset = static_cast<uint32_t>(constFileOff);
    constSect.align = 3;

    currentFileOff = alignUp(constFileOff + rodataBytes.size(), 0x1000);
    currentVMAddr += textVMSize;

    // GOT layout in __DATA segment
    std::vector<uint64_t> gotEntries(imports.size(), 0);
    uint64_t gotSize = gotEntries.size() * sizeof(uint64_t);

    uint64_t dataVMSize = alignUp(gotSize + dataBytes.size() + bssSize, 0x1000);

    segment_command_64 dataSeg = {};
    dataSeg.cmd = LC_SEGMENT_64;
    dataSeg.cmdsize = sizeof(segment_command_64) + sizeof(section_64) * 3;
    std::strcpy(dataSeg.segname, "__DATA");
    dataSeg.vmaddr = currentVMAddr;
    dataSeg.vmsize = dataVMSize;
    dataSeg.fileoff = currentFileOff;
    dataSeg.filesize = gotSize + dataBytes.size();
    dataSeg.maxprot = VM_PROT_READ | VM_PROT_WRITE;
    dataSeg.initprot = VM_PROT_READ | VM_PROT_WRITE;
    dataSeg.nsects = 3;

    section_64 gotSect = {};
    std::strcpy(gotSect.sectname, "__got");
    std::strcpy(gotSect.segname, "__DATA");
    gotSect.addr = currentVMAddr;
    gotSect.size = gotSize;
    gotSect.offset = static_cast<uint32_t>(currentFileOff);
    gotSect.align = 3;
    gotSect.flags = 0x6; // S_NON_LAZY_SYMBOL_POINTERS

    section_64 dataSect = {};
    std::strcpy(dataSect.sectname, "__data");
    std::strcpy(dataSect.segname, "__DATA");
    dataSect.addr = currentVMAddr + gotSize;
    dataSect.size = dataBytes.size();
    dataSect.offset = static_cast<uint32_t>(currentFileOff + gotSize);
    dataSect.align = 3;

    section_64 bssSect = {};
    std::strcpy(bssSect.sectname, "__bss");
    std::strcpy(bssSect.segname, "__DATA");
    bssSect.addr = currentVMAddr + gotSize + dataBytes.size();
    bssSect.size = bssSize;
    bssSect.offset = 0;
    bssSect.align = 4;
    bssSect.flags = 0x1; // S_ZEROFILL

    currentFileOff += alignUp(gotSize + dataBytes.size(), 0x1000);

    // Assign GOT slot VMAs and build dyld bind stream
    std::vector<uint8_t> bindStream;

    for (size_t i = 0; i < imports.size(); ++i) {
        imports[i].gotSlotVma = gotSect.addr + i * sizeof(uint64_t);

        // Build bind opcode sequence for import i
        // Ordinal 1
        bindStream.push_back(BIND_OPCODE_SET_DYLIB_ORDINAL_IMM | 1);

        // Symbol name
        std::string symName = "_" + imports[i].name;
        bindStream.push_back(BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM);
        for (char c : symName) bindStream.push_back(c);
        bindStream.push_back('\0');

        // Type pointer
        bindStream.push_back(BIND_OPCODE_SET_TYPE_IMM | BIND_TYPE_POINTER);

        // Segment 2 (__DATA is 2nd segment after PAGEZERO and TEXT)
        bindStream.push_back(BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB | 2);
        appendULEB128(bindStream, i * sizeof(uint64_t));

        // Do bind
        bindStream.push_back(BIND_OPCODE_DO_BIND);

        if (imports[i].isFunction) {
            // Fixup thunk displacement
            uint64_t realThunkVma = imports[i].thunkVma + textAddrDiff;
            uint64_t thunkTextOffset = realThunkVma - textSect.addr;
            int64_t disp = static_cast<int64_t>(imports[i].gotSlotVma) - static_cast<int64_t>(realThunkVma + 6);
            int32_t disp32 = static_cast<int32_t>(disp);
            std::memcpy(textBytes.data() + thunkTextOffset + 2, &disp32, 4);
        }
    }
    bindStream.push_back(BIND_OPCODE_DONE);

    // Fixup data import fixups
    LinkedImage mutImage = image;
    for (const auto& fixup : mutImage.dataImportFixups) {
        uint64_t targetGotVma = 0;
        for (const auto& imp : imports) {
            if (imp.name == fixup.symbolName) {
                targetGotVma = imp.gotSlotVma;
                break;
            }
        }
        if (targetGotVma != 0) {
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

    // Correct defined symbol VMAs
    size_t symIdx = 0;
    for (const auto& [name, sym] : image.symbols) {
        if (symIdx < nlists.size()) {
            nlists[symIdx].n_value = textSect.addr + (sym.virtualAddress - (textSec ? textSec->virtualAddress : 0x400000ULL));
        }
        symIdx++;
    }

    // Commands
    dylinker_command dylinkerCmd = {};
    dylinkerCmd.cmd = LC_LOAD_DYLINKER;
    dylinkerCmd.cmdsize = dylinkerCmdSize;
    dylinkerCmd.name_offset = sizeof(dylinker_command);

    entry_point_command mainCmd = {};
    mainCmd.cmd = LC_MAIN;
    mainCmd.cmdsize = sizeof(entry_point_command);
    uint64_t textBaseVma = textSec ? textSec->virtualAddress : 0x400000ULL;
    uint64_t entryOffsetInText = (image.entryAddress >= textBaseVma) ? (image.entryAddress - textBaseVma) : 0;
    mainCmd.entryoff = textSect.offset + entryOffsetInText;

    uint64_t symOff = currentFileOff;
    uint64_t strOff = symOff + nlists.size() * sizeof(nlist_64);
    uint64_t bindOff = alignUp(strOff + stringTable.size(), 8);

    symtab_command symCmd = {};
    symCmd.cmd = LC_SYMTAB;
    symCmd.cmdsize = sizeof(symtab_command);
    symCmd.symoff = static_cast<uint32_t>(symOff);
    symCmd.nsyms = static_cast<uint32_t>(nlists.size());
    symCmd.stroff = static_cast<uint32_t>(strOff);
    symCmd.strsize = static_cast<uint32_t>(stringTable.size());

    dysymtab_command dysymCmd = {};
    dysymCmd.cmd = LC_DYSYMTAB;
    dysymCmd.cmdsize = sizeof(dysymtab_command);
    dysymCmd.iextdefsym = 0;
    dysymCmd.nextdefsym = nextDefSyms;
    dysymCmd.iundefsym = iUndefSyms;
    dysymCmd.nundefsym = nUndefSyms;

    dyld_info_command dyldInfo = {};
    dyldInfo.cmd = LC_DYLD_INFO_ONLY;
    dyldInfo.cmdsize = sizeof(dyld_info_command);
    dyldInfo.bind_off = static_cast<uint32_t>(bindOff);
    dyldInfo.bind_size = static_cast<uint32_t>(bindStream.size());

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
    file.write(reinterpret_cast<const char*>(&gotSect), sizeof(gotSect));
    file.write(reinterpret_cast<const char*>(&dataSect), sizeof(dataSect));
    file.write(reinterpret_cast<const char*>(&bssSect), sizeof(bssSect));

    file.write(reinterpret_cast<const char*>(&dylinkerCmd), sizeof(dylinker_command));
    file.write(dylinkerPath.c_str(), dylinkerPath.size() + 1);
    uint32_t dylinkerPad = dylinkerCmdSize - (sizeof(dylinker_command) + dylinkerPath.size() + 1);
    if (dylinkerPad > 0) {
        std::vector<char> pad(dylinkerPad, 0);
        file.write(pad.data(), dylinkerPad);
    }

    for (size_t i = 0; i < neededLibs.size(); ++i) {
        dylib_command loadDylib = {};
        loadDylib.cmd = LC_LOAD_DYLIB;
        loadDylib.cmdsize = loadDylibCmdSizes[i];
        loadDylib.name_offset = sizeof(dylib_command);
        loadDylib.timestamp = 2;
        loadDylib.current_version = 0x00010000;
        loadDylib.compatibility_version = 0x00010000;

        file.write(reinterpret_cast<const char*>(&loadDylib), sizeof(dylib_command));
        file.write(neededLibs[i].c_str(), neededLibs[i].size() + 1);
        uint32_t pad = loadDylibCmdSizes[i] - (sizeof(dylib_command) + neededLibs[i].size() + 1);
        if (pad > 0) {
            std::vector<char> zeroPad(pad, 0);
            file.write(zeroPad.data(), pad);
        }
    }

    file.write(reinterpret_cast<const char*>(&mainCmd), sizeof(mainCmd));
    file.write(reinterpret_cast<const char*>(&symCmd), sizeof(symCmd));
    file.write(reinterpret_cast<const char*>(&dysymCmd), sizeof(dysymCmd));
    file.write(reinterpret_cast<const char*>(&dyldInfo), sizeof(dyldInfo));

    if (!textBytes.empty()) {
        file.seekp(textSect.offset);
        file.write(reinterpret_cast<const char*>(textBytes.data()), textBytes.size());
    }
    if (!rodataBytes.empty()) {
        file.seekp(constSect.offset);
        file.write(reinterpret_cast<const char*>(rodataBytes.data()), rodataBytes.size());
    }
    if (!gotEntries.empty()) {
        file.seekp(gotSect.offset);
        file.write(reinterpret_cast<const char*>(gotEntries.data()), gotSize);
    }
    if (!dataBytes.empty()) {
        file.seekp(dataSect.offset);
        file.write(reinterpret_cast<const char*>(dataBytes.data()), dataBytes.size());
    }

    file.seekp(symOff);
    if (!nlists.empty()) {
        file.write(reinterpret_cast<const char*>(nlists.data()), nlists.size() * sizeof(nlist_64));
    }
    file.seekp(strOff);
    file.write(stringTable.c_str(), stringTable.size());

    file.seekp(bindOff);
    if (!bindStream.empty()) {
        file.write(reinterpret_cast<const char*>(bindStream.data()), bindStream.size());
    }

    file.close();
    if (!file) {
        std::remove(temporary.c_str());
        lastError_ = "failed while writing dynamic Mach-O output";
        return false;
    }

    if (std::rename(temporary.c_str(), outputPath.c_str()) != 0) {
        std::remove(temporary.c_str());
        lastError_ = "failed to replace dynamic Mach-O output";
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
