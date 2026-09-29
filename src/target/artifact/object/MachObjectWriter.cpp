#include "target/artifact/object/MachObjectWriter.h"
#include <fstream>
#include <cstring>
#include <algorithm>

namespace target {
namespace artifact {
namespace object {

namespace {

constexpr uint32_t MH_MAGIC_64 = 0xfeedfacf;
constexpr uint32_t CPU_TYPE_X86_64 = 0x01000007;
constexpr uint32_t CPU_TYPE_ARM64 = 0x0100000c;
constexpr uint32_t CPU_SUBTYPE_LIB64 = 0x80000000;
constexpr uint32_t CPU_SUBTYPE_X86_64_ALL = CPU_SUBTYPE_LIB64 | 3;
constexpr uint32_t CPU_SUBTYPE_ARM64_ALL = 0;

constexpr uint32_t LC_SEGMENT_64 = 0x19;
constexpr uint32_t LC_SYMTAB = 0x2;

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

} // namespace

std::vector<uint8_t> MachObjectWriter::serialize(const ObjectArtifact& artifact) {
    uint64_t bss_size = artifact.sections.count(".bss") ? artifact.sections.at(".bss").data.size() : 0;
    for (const auto& sym : artifact.symbols) {
        if (sym.sectionName == ".bss" || sym.sectionName == "__bss") {
            bss_size = std::max(bss_size, sym.value + sym.size);
        }
    }

    uint32_t data_nsects = (artifact.sections.count(".data") ? 1 : 0) + (bss_size > 0 ? 1 : 0);
    uint32_t total_nsects = (artifact.sections.count(".text") ? 1 : 0) + data_nsects;
    if (total_nsects == 0) total_nsects = 1;

    uint32_t ncmds = 2; // LC_SEGMENT_64, LC_SYMTAB
    uint32_t sizeofcmds = (sizeof(segment_command_64) + sizeof(section_64) * total_nsects) + sizeof(symtab_command);

    uint32_t cpuType = (artifact.arch == target::Arch::AArch64) ? CPU_TYPE_ARM64 : CPU_TYPE_X86_64;
    uint32_t cpuSubtype = (cpuType == CPU_TYPE_ARM64) ? CPU_SUBTYPE_ARM64_ALL : CPU_SUBTYPE_X86_64_ALL;

    uint64_t currentFileOff = sizeof(mach_header_64) + sizeofcmds;

    std::vector<section_64> sects;
    if (artifact.sections.count(".text")) {
        section_64 text_sect = {};
        std::strcpy(text_sect.sectname, "__text");
        std::strcpy(text_sect.segname, "__TEXT");
        text_sect.addr = 0;
        text_sect.size = artifact.sections.at(".text").data.size();
        text_sect.offset = static_cast<uint32_t>(currentFileOff);
        text_sect.align = 4;
        text_sect.flags = 0x80000400; // S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS
        sects.push_back(text_sect);
        currentFileOff += (text_sect.size + 7) & ~7;
    }

    if (artifact.sections.count(".data")) {
        section_64 data_sect = {};
        std::strcpy(data_sect.sectname, "__data");
        std::strcpy(data_sect.segname, "__DATA");
        data_sect.addr = 0;
        data_sect.size = artifact.sections.at(".data").data.size();
        data_sect.offset = static_cast<uint32_t>(currentFileOff);
        data_sect.align = 3;
        sects.push_back(data_sect);
        currentFileOff += (data_sect.size + 7) & ~7;
    }

    if (bss_size > 0) {
        section_64 bss_sect = {};
        std::strcpy(bss_sect.sectname, "__bss");
        std::strcpy(bss_sect.segname, "__DATA");
        bss_sect.addr = 0;
        bss_sect.size = bss_size;
        bss_sect.offset = 0;
        bss_sect.align = 3;
        bss_sect.flags = 0x1; // S_ZEROFILL
        sects.push_back(bss_sect);
    }

    std::vector<nlist_64> nlists;
    std::string stringTable = "\0";

    for (const auto& sym : artifact.symbols) {
        nlist_64 nl = {};
        nl.n_strx = static_cast<uint32_t>(stringTable.size());
        stringTable += "_" + sym.name;
        stringTable.push_back('\0');

        nl.n_type = 0x0F; // N_SECT | N_EXT
        nl.n_sect = 1; // 1-based section index
        nl.n_desc = 0;
        nl.n_value = sym.value;
        nlists.push_back(nl);
    }

    uint64_t symOff = currentFileOff;
    uint64_t strOff = symOff + nlists.size() * sizeof(nlist_64);
    uint64_t totalFileSize = strOff + stringTable.size();

    std::vector<uint8_t> buffer(totalFileSize, 0);

    mach_header_64 header = {};
    header.magic = MH_MAGIC_64;
    header.cputype = cpuType;
    header.cpusubtype = cpuSubtype;
    header.filetype = 0x1; // MH_OBJECT
    header.ncmds = ncmds;
    header.sizeofcmds = sizeofcmds;
    header.flags = 0;

    std::memcpy(buffer.data(), &header, sizeof(header));

    uint64_t writeOffset = sizeof(header);

    segment_command_64 seg = {};
    seg.cmd = LC_SEGMENT_64;
    seg.cmdsize = sizeof(segment_command_64) + sizeof(section_64) * total_nsects;
    seg.segname[0] = '\0';
    seg.vmaddr = 0;
    seg.vmsize = 0;
    seg.fileoff = sizeof(header) + sizeofcmds;
    seg.filesize = 0;
    seg.maxprot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;
    seg.initprot = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;
    seg.nsects = total_nsects;

    std::memcpy(buffer.data() + writeOffset, &seg, sizeof(seg));
    writeOffset += sizeof(seg);

    for (const auto& s : sects) {
        std::memcpy(buffer.data() + writeOffset, &s, sizeof(s));
        writeOffset += sizeof(s);
    }

    symtab_command sym_cmd = {};
    sym_cmd.cmd = LC_SYMTAB;
    sym_cmd.cmdsize = sizeof(symtab_command);
    sym_cmd.symoff = static_cast<uint32_t>(symOff);
    sym_cmd.nsyms = static_cast<uint32_t>(nlists.size());
    sym_cmd.stroff = static_cast<uint32_t>(strOff);
    sym_cmd.strsize = static_cast<uint32_t>(stringTable.size());

    std::memcpy(buffer.data() + writeOffset, &sym_cmd, sizeof(sym_cmd));

    for (const auto& s : sects) {
        if (s.offset > 0 && s.size > 0) {
            if (std::strcmp(s.sectname, "__text") == 0 && artifact.sections.count(".text")) {
                std::memcpy(buffer.data() + s.offset, artifact.sections.at(".text").data.data(), artifact.sections.at(".text").data.size());
            } else if (std::strcmp(s.sectname, "__data") == 0 && artifact.sections.count(".data")) {
                std::memcpy(buffer.data() + s.offset, artifact.sections.at(".data").data.data(), artifact.sections.at(".data").data.size());
            }
        }
    }

    if (!nlists.empty()) {
        std::memcpy(buffer.data() + symOff, nlists.data(), nlists.size() * sizeof(nlist_64));
    }
    std::memcpy(buffer.data() + strOff, stringTable.c_str(), stringTable.size());

    return buffer;
}

bool MachObjectWriter::write(const ObjectArtifact& artifact, const std::string& outputPath) {
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
