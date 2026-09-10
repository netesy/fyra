#include "target/artifact/archive/UnixArchiveWriter.hh"
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <iomanip>

namespace target {
namespace artifact {
namespace archive {

namespace {

std::string formatArchiveHeader(const std::string& name, size_t size) {
    std::string n = name;
    if (n != "/" && n != "//" && !n.empty() && n.back() != '/' && n.front() != '/') {
        n += "/";
    }
    std::ostringstream ss;
    ss << std::left << std::setw(16) << n
       << std::left << std::setw(12) << "0"
       << std::left << std::setw(6)  << "0"
       << std::left << std::setw(6)  << "0"
       << std::left << std::setw(8)  << "0644"
       << std::left << std::setw(10) << size
       << "`\n";
    return ss.str();
}

} // namespace

std::vector<std::string> UnixArchiveWriter::extractExportedElfSymbols(const std::vector<uint8_t>& data) {
    std::vector<std::string> symbols;
    if (data.size() < 64) return symbols;

    const uint8_t* bytes = data.data();
    if (bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F') {
        return symbols;
    }

    uint64_t shoff = *reinterpret_cast<const uint64_t*>(bytes + 40);
    uint16_t shentsize = *reinterpret_cast<const uint16_t*>(bytes + 58);
    uint16_t shnum = *reinterpret_cast<const uint16_t*>(bytes + 60);
    uint16_t shstrndx = *reinterpret_cast<const uint16_t*>(bytes + 62);

    if (shoff + static_cast<uint64_t>(shnum) * shentsize > data.size()) return symbols;

    const uint8_t* shstrtab_hdr = bytes + shoff + static_cast<uint64_t>(shstrndx) * shentsize;
    uint64_t shstrtab_off = *reinterpret_cast<const uint64_t*>(shstrtab_hdr + 24);
    uint64_t shstrtab_sz = *reinterpret_cast<const uint64_t*>(shstrtab_hdr + 32);

    uint64_t symtab_off = 0, symtab_sz = 0, symtab_entsz = 24, symtab_link = 0;

    for (uint16_t i = 0; i < shnum; ++i) {
        const uint8_t* shdr = bytes + shoff + static_cast<uint64_t>(i) * shentsize;
        uint32_t type = *reinterpret_cast<const uint32_t*>(shdr + 4);
        if (type == 2) { // SHT_SYMTAB
            symtab_off = *reinterpret_cast<const uint64_t*>(shdr + 24);
            symtab_sz = *reinterpret_cast<const uint64_t*>(shdr + 32);
            symtab_link = *reinterpret_cast<const uint32_t*>(shdr + 40);
            symtab_entsz = *reinterpret_cast<const uint64_t*>(shdr + 56);
            if (symtab_entsz == 0) symtab_entsz = 24;
            break;
        }
    }

    if (symtab_off == 0 || symtab_link >= shnum) return symbols;

    const uint8_t* strtab_hdr = bytes + shoff + static_cast<uint64_t>(symtab_link) * shentsize;
    uint64_t strtab_off = *reinterpret_cast<const uint64_t*>(strtab_hdr + 24);
    uint64_t strtab_sz = *reinterpret_cast<const uint64_t*>(strtab_hdr + 32);

    size_t num_syms = symtab_sz / symtab_entsz;
    for (size_t i = 1; i < num_syms; ++i) {
        const uint8_t* sym = bytes + symtab_off + i * symtab_entsz;
        uint32_t name_off = *reinterpret_cast<const uint32_t*>(sym);
        uint8_t info = sym[4];
        uint16_t shndx = *reinterpret_cast<const uint16_t*>(sym + 6);

        uint8_t bind = info >> 4;
        uint8_t type = info & 0xf;

        if (shndx != 0 && (bind == 1 || bind == 2)) { // STB_GLOBAL or STB_WEAK, defined
            if (strtab_off + name_off < data.size()) {
                const char* sym_name = reinterpret_cast<const char*>(bytes + strtab_off + name_off);
                if (sym_name[0] != '\0') {
                    symbols.push_back(sym_name);
                }
            }
        }
    }

    return symbols;
}

bool UnixArchiveWriter::writeArchive(const std::vector<ArchiveMember>& members,
                                      const std::string& outputPath,
                                      std::string& errorOutput) {
    // 1. Build string table '//' for filenames longer than 15 characters or containing spaces
    std::string stringTablePayload;
    std::vector<std::string> formattedMemberNames;
    formattedMemberNames.reserve(members.size());

    for (const auto& m : members) {
        if (m.name.size() > 15 || m.name.find(' ') != std::string::npos) {
            size_t offset = stringTablePayload.size();
            stringTablePayload += m.name + "/\n";
            formattedMemberNames.push_back("/" + std::to_string(offset));
        } else {
            formattedMemberNames.push_back(m.name);
        }
    }

    // 2. Extract symbols from members
    std::vector<std::pair<std::string, size_t>> allSymbolsWithMemberIndex;
    for (size_t mIdx = 0; mIdx < members.size(); ++mIdx) {
        std::vector<std::string> syms = members[mIdx].exportedSymbols;
        if (syms.empty()) {
            syms = extractExportedElfSymbols(members[mIdx].data);
        }
        for (const auto& sym : syms) {
            allSymbolsWithMemberIndex.push_back({sym, mIdx});
        }
    }

    // 3. Compute layout offsets
    struct MemberPosition {
        ArchiveMember member;
        std::string headerName;
        uint64_t fileOffset = 0;
    };
    std::vector<MemberPosition> memberPositions;

    const bool hasSymbols = !allSymbolsWithMemberIndex.empty();
    const bool hasStringTable = !stringTablePayload.empty();

    uint64_t memberOffsetCursor = 8; // "!<arch>\n"

    if (hasSymbols) {
        size_t indexPayloadSize = 4 + (allSymbolsWithMemberIndex.size() * 4);
        for (const auto& [sym, mIdx] : allSymbolsWithMemberIndex) {
            indexPayloadSize += sym.size() + 1;
        }

        uint64_t indexMemberSize = 60 + indexPayloadSize;
        if (indexMemberSize % 2 != 0) indexMemberSize++;
        memberOffsetCursor += indexMemberSize;
    }

    if (hasStringTable) {
        uint64_t strTableMemberSize = 60 + stringTablePayload.size();
        if (strTableMemberSize % 2 != 0) strTableMemberSize++;
        memberOffsetCursor += strTableMemberSize;
    }

    for (size_t i = 0; i < members.size(); ++i) {
        MemberPosition pos;
        pos.member = members[i];
        pos.headerName = formattedMemberNames[i];
        pos.fileOffset = memberOffsetCursor;
        memberPositions.push_back(pos);

        uint64_t size = members[i].data.size();
        uint64_t paddedSize = 60 + size + (size % 2 != 0 ? 1 : 0);
        memberOffsetCursor += paddedSize;
    }

    // 4. Open output file
    std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        errorOutput = "Cannot open output archive file: " + outputPath;
        return false;
    }

    // Write Magic
    file.write("!<arch>\n", 8);

    // Write Symbol Index '/' Member Header and Payload
    if (hasSymbols) {
        std::vector<uint8_t> indexPayload;
        uint32_t numSymsBE = __builtin_bswap32(static_cast<uint32_t>(allSymbolsWithMemberIndex.size()));
        const uint8_t* numBytes = reinterpret_cast<const uint8_t*>(&numSymsBE);
        indexPayload.insert(indexPayload.end(), numBytes, numBytes + 4);

        for (const auto& [sym, mIdx] : allSymbolsWithMemberIndex) {
            uint32_t offBE = __builtin_bswap32(static_cast<uint32_t>(memberPositions[mIdx].fileOffset));
            const uint8_t* offBytes = reinterpret_cast<const uint8_t*>(&offBE);
            indexPayload.insert(indexPayload.end(), offBytes, offBytes + 4);
        }

        for (const auto& [sym, mIdx] : allSymbolsWithMemberIndex) {
            indexPayload.insert(indexPayload.end(), sym.begin(), sym.end());
            indexPayload.push_back(0);
        }

        std::string indexHdr = formatArchiveHeader("/", indexPayload.size());
        file.write(indexHdr.data(), 60);
        file.write(reinterpret_cast<const char*>(indexPayload.data()), indexPayload.size());
        if (indexPayload.size() % 2 != 0) {
            file.put('\n');
        }
    } else {
        uint32_t zeroSyms = 0;
        std::string indexHdr = formatArchiveHeader("/", 4);
        file.write(indexHdr.data(), 60);
        file.write(reinterpret_cast<const char*>(&zeroSyms), 4);
        file.put('\n'); // 4-byte payload -> pad to 2-byte boundary
    }

    // Write GNU Long Name Table '//' Member Header and Payload
    if (hasStringTable) {
        std::string strHdr = formatArchiveHeader("//", stringTablePayload.size());
        file.write(strHdr.data(), 60);
        file.write(stringTablePayload.data(), stringTablePayload.size());
        if (stringTablePayload.size() % 2 != 0) {
            file.put('\n');
        }
    }

    // Write Member Headers and Data
    for (const auto& pos : memberPositions) {
        std::string memberHdr = formatArchiveHeader(pos.headerName, pos.member.data.size());
        file.write(memberHdr.data(), 60);
        file.write(reinterpret_cast<const char*>(pos.member.data.data()), pos.member.data.size());
        if (pos.member.data.size() % 2 != 0) {
            file.put('\n');
        }
    }

    file.close();
    return !file.fail();
}

} // namespace archive
} // namespace artifact
} // namespace target
