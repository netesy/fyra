#include "target/artifact/object/MachObjectReader.h"
#include <fstream>
#include <cstring>

namespace target {
namespace artifact {
namespace object {

namespace {

constexpr uint32_t MH_MAGIC_64 = 0xfeedfacf;
constexpr uint32_t LC_SEGMENT_64 = 0x19;
constexpr uint32_t LC_SYMTAB = 0x2;

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

bool MachObjectReader::parse(const std::vector<uint8_t>& bytes, ObjectArtifact& outArtifact) {
    lastError_.clear();
    outArtifact = ObjectArtifact{};
    outArtifact.format = ObjectFormat::MachO;
    outArtifact.os = target::OS::MacOS;
    outArtifact.arch = target::Arch::X64;

    if (bytes.size() < sizeof(mach_header_64)) {
        lastError_ = "File too small for Mach-O header";
        return false;
    }

    const auto* hdr = reinterpret_cast<const mach_header_64*>(bytes.data());
    if (hdr->magic != MH_MAGIC_64) {
        lastError_ = "Invalid Mach-O 64-bit magic";
        return false;
    }

    if (hdr->filetype != 0x1) { // MH_OBJECT
        lastError_ = "Not a Mach-O relocatable object (filetype != MH_OBJECT)";
        return false;
    }

    uint64_t cmdOffset = sizeof(mach_header_64);
    std::vector<std::string> sectionNames;

    for (uint32_t c = 0; c < hdr->ncmds && cmdOffset + 8 <= bytes.size(); ++c) {
        uint32_t cmd = *reinterpret_cast<const uint32_t*>(bytes.data() + cmdOffset);
        uint32_t cmdsize = *reinterpret_cast<const uint32_t*>(bytes.data() + cmdOffset + 4);

        if (cmd == LC_SEGMENT_64 && cmdOffset + sizeof(segment_command_64) <= bytes.size()) {
            const auto* seg = reinterpret_cast<const segment_command_64*>(bytes.data() + cmdOffset);
            uint64_t sectOffset = cmdOffset + sizeof(segment_command_64);

            for (uint32_t s = 0; s < seg->nsects && sectOffset + sizeof(section_64) <= bytes.size(); ++s) {
                const auto* sect = reinterpret_cast<const section_64*>(bytes.data() + sectOffset);

                char sectBuf[17] = {0};
                std::memcpy(sectBuf, sect->sectname, 16);
                std::string sectName = sectBuf;

                if (sectName == "__text") sectName = ".text";
                else if (sectName == "__data") sectName = ".data";
                else if (sectName == "__bss") sectName = ".bss";

                sectionNames.push_back(sectName);

                ObjectSection osec;
                osec.name = sectName;
                osec.virtualSize = sect->size;
                osec.virtualAddress = sect->addr;
                osec.alignment = 1ULL << sect->align;
                osec.flags = sect->flags;

                if (sect->offset > 0 && sect->offset + sect->size <= bytes.size()) {
                    osec.data.assign(bytes.data() + sect->offset, bytes.data() + sect->offset + sect->size);
                }

                outArtifact.addSection(osec);
                sectOffset += sizeof(section_64);
            }
        } else if (cmd == LC_SYMTAB && cmdOffset + sizeof(symtab_command) <= bytes.size()) {
            const auto* st = reinterpret_cast<const symtab_command*>(bytes.data() + cmdOffset);
            if (st->symoff + st->nsyms * sizeof(nlist_64) <= bytes.size() && st->stroff + st->strsize <= bytes.size()) {
                const auto* nlists = reinterpret_cast<const nlist_64*>(bytes.data() + st->symoff);
                const char* strtab = reinterpret_cast<const char*>(bytes.data() + st->stroff);

                for (uint32_t i = 0; i < st->nsyms; ++i) {
                    const auto& nl = nlists[i];
                    std::string symName;
                    if (nl.n_strx < st->strsize) {
                        symName = &strtab[nl.n_strx];
                        if (symName.length() > 0 && symName[0] == '_') {
                            symName = symName.substr(1); // Strip Mach-O leading underscore
                        }
                    }

                    if (!symName.empty()) {
                        ObjectSymbol osym;
                        osym.name = symName;
                        osym.value = nl.n_value;
                        osym.binding = SymbolBinding::Global;
                        osym.type = SymbolType::NoType;

                        if (nl.n_sect > 0 && static_cast<size_t>(nl.n_sect - 1) < sectionNames.size()) {
                            osym.sectionName = sectionNames[nl.n_sect - 1];
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

        cmdOffset += cmdsize;
    }

    return true;
}

bool MachObjectReader::read(const std::string& inputPath, ObjectArtifact& outArtifact) {
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
