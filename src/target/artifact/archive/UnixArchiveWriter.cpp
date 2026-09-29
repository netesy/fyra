#include "target/artifact/archive/UnixArchiveWriter.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace target {
namespace artifact {
namespace archive {

namespace {

void writeBE32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
}

std::vector<uint8_t> create60ByteHeader(const std::string& name16,
                                        uint32_t mtime,
                                        uint32_t uid,
                                        uint32_t gid,
                                        uint32_t mode,
                                        uint64_t size) {
    std::vector<uint8_t> header(60, ' ');

    // Name (16 bytes)
    std::string n = name16.substr(0, 16);
    std::memcpy(header.data(), n.data(), n.size());

    // Mtime (12 bytes)
    std::string mtimeStr = std::to_string(mtime);
    if (mtimeStr.size() > 12) mtimeStr = mtimeStr.substr(0, 12);
    std::memcpy(header.data() + 16, mtimeStr.data(), mtimeStr.size());

    // UID (6 bytes)
    std::string uidStr = std::to_string(uid);
    if (uidStr.size() > 6) uidStr = uidStr.substr(0, 6);
    std::memcpy(header.data() + 28, uidStr.data(), uidStr.size());

    // GID (6 bytes)
    std::string gidStr = std::to_string(gid);
    if (gidStr.size() > 6) gidStr = gidStr.substr(0, 6);
    std::memcpy(header.data() + 34, gidStr.data(), gidStr.size());

    // Mode (8 bytes, octal)
    std::ostringstream modeOss;
    modeOss << std::oct << std::setw(6) << std::setfill('0') << mode << "  ";
    std::string modeStr = modeOss.str().substr(0, 8);
    std::memcpy(header.data() + 40, modeStr.data(), modeStr.size());

    // Size (10 bytes)
    std::string sizeStr = std::to_string(size);
    if (sizeStr.size() > 10) sizeStr = sizeStr.substr(0, 10);
    std::memcpy(header.data() + 48, sizeStr.data(), sizeStr.size());

    // Magic (2 bytes)
    header[58] = 0x60;
    header[59] = 0x0A;

    return header;
}

} // namespace

std::vector<uint8_t> UnixArchiveWriter::serialize(const std::vector<ArchiveMember>& members) {
    std::vector<uint8_t> buffer;

    // Magic
    const char* magic = "!<arch>\n";
    buffer.insert(buffer.end(), magic, magic + 8);

    // Collect long names and symbols
    std::vector<uint8_t> longNameTablePayload;
    std::vector<size_t> memberNameOffsets(members.size(), 0);
    std::vector<bool> isLongName(members.size(), false);

    for (size_t i = 0; i < members.size(); ++i) {
        const auto& mem = members[i];
        if (mem.name.length() >= 16 || mem.name.find(' ') != std::string::npos) {
            isLongName[i] = true;
            memberNameOffsets[i] = longNameTablePayload.size();
            longNameTablePayload.insert(longNameTablePayload.end(), mem.name.begin(), mem.name.end());
            longNameTablePayload.push_back('/');
            longNameTablePayload.push_back('\n');
        }
    }

    struct SymbolEntry {
        std::string name;
        size_t memberIndex;
    };
    std::vector<SymbolEntry> allSymbols;
    for (size_t i = 0; i < members.size(); ++i) {
        for (const auto& sym : members[i].exportedSymbols) {
            allSymbols.push_back({sym, i});
        }
    }

    // Build symbol index payload if symbols exist
    std::vector<uint8_t> symtabPayload;
    if (!allSymbols.empty()) {
        writeBE32(symtabPayload, static_cast<uint32_t>(allSymbols.size()));
        // Placeholder for offsets (4 bytes per symbol)
        size_t offsetTableStart = symtabPayload.size();
        symtabPayload.resize(offsetTableStart + allSymbols.size() * 4, 0);
        // String table
        for (const auto& sym : allSymbols) {
            symtabPayload.insert(symtabPayload.end(), sym.name.begin(), sym.name.end());
            symtabPayload.push_back('\0');
        }
    }

    // Determine layout & member header offsets
    uint64_t currentOffset = 8;

    uint64_t symtabHeaderOffset = 0;
    if (!allSymbols.empty()) {
        symtabHeaderOffset = currentOffset;
        uint64_t symtabSize = symtabPayload.size();
        currentOffset += 60 + symtabSize + (symtabSize % 2);
    }

    uint64_t longNameHeaderOffset = 0;
    if (!longNameTablePayload.empty()) {
        longNameHeaderOffset = currentOffset;
        uint64_t longNameSize = longNameTablePayload.size();
        currentOffset += 60 + longNameSize + (longNameSize % 2);
    }

    std::vector<uint64_t> memberHeaderOffsets(members.size(), 0);
    for (size_t i = 0; i < members.size(); ++i) {
        memberHeaderOffsets[i] = currentOffset;
        uint64_t memSize = members[i].bytes.size();
        currentOffset += 60 + memSize + (memSize % 2);
    }

    // Fill symbol index offsets now that memberHeaderOffsets are known
    if (!allSymbols.empty()) {
        size_t pos = 4; // after num_symbols
        for (const auto& sym : allSymbols) {
            uint32_t memOff = static_cast<uint32_t>(memberHeaderOffsets[sym.memberIndex]);
            symtabPayload[pos]     = static_cast<uint8_t>((memOff >> 24) & 0xFF);
            symtabPayload[pos + 1] = static_cast<uint8_t>((memOff >> 16) & 0xFF);
            symtabPayload[pos + 2] = static_cast<uint8_t>((memOff >> 8) & 0xFF);
            symtabPayload[pos + 3] = static_cast<uint8_t>(memOff & 0xFF);
            pos += 4;
        }

        // Emit symbol index member
        auto symHeader = create60ByteHeader("/", 0, 0, 0, 0, symtabPayload.size());
        buffer.insert(buffer.end(), symHeader.begin(), symHeader.end());
        buffer.insert(buffer.end(), symtabPayload.begin(), symtabPayload.end());
        if (symtabPayload.size() % 2 != 0) buffer.push_back('\n');
    }

    // Emit long name table member if needed
    if (!longNameTablePayload.empty()) {
        auto longNameHeader = create60ByteHeader("//", 0, 0, 0, 0, longNameTablePayload.size());
        buffer.insert(buffer.end(), longNameHeader.begin(), longNameHeader.end());
        buffer.insert(buffer.end(), longNameTablePayload.begin(), longNameTablePayload.end());
        if (longNameTablePayload.size() % 2 != 0) buffer.push_back('\n');
    }

    // Emit object members
    for (size_t i = 0; i < members.size(); ++i) {
        const auto& mem = members[i];
        std::string hdrName;
        if (isLongName[i]) {
            hdrName = "/" + std::to_string(memberNameOffsets[i]);
        } else {
            hdrName = mem.name;
            if (hdrName.empty() || hdrName.back() != '/') {
                hdrName += "/";
            }
        }

        auto hdr = create60ByteHeader(hdrName, mem.mtime, mem.uid, mem.gid, mem.mode, mem.bytes.size());
        buffer.insert(buffer.end(), hdr.begin(), hdr.end());
        buffer.insert(buffer.end(), mem.bytes.begin(), mem.bytes.end());
        if (mem.bytes.size() % 2 != 0) {
            buffer.push_back('\n');
        }
    }

    return buffer;
}

bool UnixArchiveWriter::write(const std::vector<ArchiveMember>& members, const std::string& outputPath) {
    auto data = serialize(members);
    std::ofstream ofs(outputPath, std::ios::binary);
    if (!ofs.is_open()) {
        lastError_ = "Failed to open output file: " + outputPath;
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
    return ofs.good();
}

} // namespace archive
} // namespace artifact
} // namespace target
