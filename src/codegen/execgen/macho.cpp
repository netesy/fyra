#include "macho.hh"
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>
#include <algorithm>

// Mach-O constants for 64-bit
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
constexpr uint32_t LC_DYSYMTAB = 0xb;
constexpr uint32_t LC_MAIN = 0x80000028;

constexpr uint32_t VM_PROT_NONE = 0x0;
constexpr uint32_t VM_PROT_READ = 0x1;
constexpr uint32_t VM_PROT_WRITE = 0x2;
constexpr uint32_t VM_PROT_EXECUTE = 0x4;
}

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

class MachOGenerator::Impl {
public:
    Impl(const std::string& inputFilename) : inputFilename_(inputFilename), cpuType_(CPU_TYPE_X86_64) {}

    void setCpuType(uint32_t type) { cpuType_ = type; }

    bool generateFromCode(const std::map<std::string, std::vector<uint8_t>>& sections_data,
                          const std::vector<MachOGenerator::Symbol>& symbols_in,
                          const std::vector<MachOGenerator::Relocation>& relocations_in,
                          const std::string& outputPath) {
        try {
            std::ofstream file(outputPath, std::ios::binary);
            if (!file) {
                lastError_ = "Cannot open output file: " + outputPath;
                return false;
            }

            // Mach-O segments: __PAGEZERO, __TEXT, __DATA, LC_MAIN, LC_SYMTAB
            uint32_t ncmds = 5;
            uint32_t sizeofcmds = sizeof(segment_command_64) + // __PAGEZERO
                                  (sizeof(segment_command_64) + sizeof(section_64) * 2) + // __TEXT (__text, __const)
                                  (sizeof(segment_command_64) + sizeof(section_64)) + // __DATA (__data)
                                  sizeof(entry_point_command) +
                                  sizeof(symtab_command);

            mach_header_64 header = {};
            header.magic = MH_MAGIC_64;
            header.cputype = cpuType_;
            header.cpusubtype = (cpuType_ == CPU_TYPE_ARM64) ? CPU_SUBTYPE_ARM64_ALL : CPU_SUBTYPE_X86_64_ALL;
            header.filetype = MH_EXECUTE;
            header.ncmds = ncmds;
            header.sizeofcmds = sizeofcmds;
            header.flags = 0x1; // MH_NOUNDEFS

            file.write(reinterpret_cast<const char*>(&header), sizeof(header));

            uint64_t currentFileOff = sizeof(header) + sizeofcmds;
            uint64_t currentVMAddr = 0x100000000ULL;

            // 1. __PAGEZERO
            segment_command_64 pagezero = {};
            pagezero.cmd = LC_SEGMENT_64;
            pagezero.cmdsize = sizeof(segment_command_64);
            strcpy(pagezero.segname, "__PAGEZERO");
            pagezero.vmsize = 0x100000000ULL;
            file.write(reinterpret_cast<const char*>(&pagezero), sizeof(pagezero));

            // 2. __TEXT
            uint64_t text_sects_size = 0;
            if (sections_data.count(".text")) text_sects_size += sections_data.at(".text").size();
            if (sections_data.count(".rodata") || sections_data.count(".rdata")) {
                 text_sects_size = (text_sects_size + 15) & ~15;
                 text_sects_size += (sections_data.count(".rodata") ? sections_data.at(".rodata").size() : sections_data.at(".rdata").size());
            }
            uint64_t text_vmsize = (text_sects_size + 0xFFF) & ~0xFFFULL;

            segment_command_64 text_seg = {};
            text_seg.cmd = LC_SEGMENT_64;
            text_seg.cmdsize = sizeof(segment_command_64) + sizeof(section_64) * 2;
            strcpy(text_seg.segname, "__TEXT");
            text_seg.vmaddr = currentVMAddr;
            text_seg.vmsize = text_vmsize;
            text_seg.fileoff = 0;
            text_seg.filesize = currentFileOff + text_sects_size;
            text_seg.maxprot = VM_PROT_READ | VM_PROT_EXECUTE;
            text_seg.initprot = VM_PROT_READ | VM_PROT_EXECUTE;
            text_seg.nsects = 2;
            file.write(reinterpret_cast<const char*>(&text_seg), sizeof(text_seg));

            section_64 text_sect = {};
            strcpy(text_sect.sectname, "__text");
            strcpy(text_sect.segname, "__TEXT");
            text_sect.addr = currentVMAddr + currentFileOff;
            text_sect.size = sections_data.count(".text") ? sections_data.at(".text").size() : 0;
            text_sect.offset = currentFileOff;
            text_sect.align = 4;
            text_sect.flags = 0x80000400; // S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS
            file.write(reinterpret_cast<const char*>(&text_sect), sizeof(text_sect));

            currentFileOff += (text_sect.size + 15) & ~15;

            section_64 const_sect = {};
            strcpy(const_sect.sectname, "__const");
            strcpy(const_sect.segname, "__TEXT");
            const_sect.addr = currentVMAddr + currentFileOff;
            const_sect.size = sections_data.count(".rodata") ? sections_data.at(".rodata").size() : (sections_data.count(".rdata") ? sections_data.at(".rdata").size() : 0);
            const_sect.offset = currentFileOff;
            const_sect.align = 3;
            file.write(reinterpret_cast<const char*>(&const_sect), sizeof(const_sect));

            currentFileOff += (const_sect.size + 0xFFF) & ~0xFFFULL;
            currentVMAddr += text_vmsize;

            // 3. __DATA
            uint64_t data_sects_size = sections_data.count(".data") ? sections_data.at(".data").size() : 0;
            uint64_t data_vmsize = (data_sects_size + 0xFFF) & ~0xFFFULL;

            segment_command_64 data_seg = {};
            data_seg.cmd = LC_SEGMENT_64;
            data_seg.cmdsize = sizeof(segment_command_64) + sizeof(section_64);
            strcpy(data_seg.segname, "__DATA");
            data_seg.vmaddr = currentVMAddr;
            data_seg.vmsize = data_vmsize;
            data_seg.fileoff = currentFileOff;
            data_seg.filesize = data_sects_size;
            data_seg.maxprot = VM_PROT_READ | VM_PROT_WRITE;
            data_seg.initprot = VM_PROT_READ | VM_PROT_WRITE;
            data_seg.nsects = 1;
            file.write(reinterpret_cast<const char*>(&data_seg), sizeof(data_seg));

            section_64 data_sect = {};
            strcpy(data_sect.sectname, "__data");
            strcpy(data_sect.segname, "__DATA");
            data_sect.addr = currentVMAddr;
            data_sect.size = data_sects_size;
            data_sect.offset = currentFileOff;
            data_sect.align = 3;
            file.write(reinterpret_cast<const char*>(&data_sect), sizeof(data_sect));

            currentFileOff += (data_sects_size + 0xFFF) & ~0xFFFULL;
            currentVMAddr += data_vmsize;

            // 4. LC_MAIN
            entry_point_command main_cmd = {};
            main_cmd.cmd = LC_MAIN;
            main_cmd.cmdsize = sizeof(entry_point_command);
            main_cmd.entryoff = text_sect.offset;
            file.write(reinterpret_cast<const char*>(&main_cmd), sizeof(main_cmd));

            // 5. LC_SYMTAB
            symtab_command sym_cmd = {};
            sym_cmd.cmd = LC_SYMTAB;
            sym_cmd.cmdsize = sizeof(symtab_command);
            file.write(reinterpret_cast<const char*>(&sym_cmd), sizeof(sym_cmd));

            // Write Section Data
            file.seekp(text_sect.offset);
            if (sections_data.count(".text")) {
                file.write(reinterpret_cast<const char*>(sections_data.at(".text").data()), sections_data.at(".text").size());
            }
            file.seekp(const_sect.offset);
            if (const_sect.size > 0) {
                const auto& rodata = sections_data.count(".rodata") ? sections_data.at(".rodata") : sections_data.at(".rdata");
                file.write(reinterpret_cast<const char*>(rodata.data()), rodata.size());
            }
            file.seekp(data_sect.offset);
            if (sections_data.count(".data")) {
                file.write(reinterpret_cast<const char*>(sections_data.at(".data").data()), sections_data.at(".data").size());
            }

            file.close();
            return true;
        } catch (const std::exception& e) {
            lastError_ = std::string("Mach-O generation failed: ") + e.what();
            return false;
        }
    }

    std::string getLastError() const { return lastError_; }

private:
    std::string inputFilename_;
    uint32_t cpuType_;
    std::string lastError_;
};

MachOGenerator::MachOGenerator(const std::string& inputFilename)
    : pImpl_(std::make_unique<Impl>(inputFilename)) {}

MachOGenerator::~MachOGenerator() = default;

bool MachOGenerator::generateFromCode(const std::map<std::string, std::vector<uint8_t>>& sections,
                                    const std::vector<Symbol>& symbols,
                                    const std::vector<Relocation>& relocations,
                                    const std::string& outputPath) {
    return pImpl_->generateFromCode(sections, symbols, relocations, outputPath);
}

void MachOGenerator::setCpuType(uint32_t type) { pImpl_->setCpuType(type); }

std::string MachOGenerator::getLastError() const { return pImpl_->getLastError(); }
