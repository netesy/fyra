#include "target/artifact/archive/BsdArchiveWriter.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace target {
namespace artifact {
namespace archive {

namespace {

void writeLE32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

std::vector<uint8_t> create60ByteHeader(const std::string& name16,
                                        uint32_t mtime,
                                        uint32_t uid,
                                        uint32_t gid,
                                        uint32_t mode,
                                        uint64_t size) {
    std::vector<uint8_t> header(60, ' ');

    std::string n = name16.substr(0, 16);
    std::memcpy(header.data(), n.data(), n.size());

    std::string mtimeStr = std::to_string(mtime);
    if (mtimeStr.size() > 12) mtimeStr = mtimeStr.substr(0, 12);
    std::memcpy(header.data() + 16, mtimeStr.data(), mtimeStr.size());

    std::string uidStr = std::to_string(uid);
    if (uidStr.size() > 6) uidStr = uidStr.substr(0, 6);
    std::memcpy(header.data() + 28, uidStr.data(), uidStr.size());

    std::string gidStr = std::to_string(gid);
    if (gidStr.size() > 6) gidStr = gidStr.substr(0, 6);
    std::memcpy(header.data() + 34, gidStr.data(), gidStr.size());

    std::ostringstream modeOss;
    modeOss << std::oct << std::setw(6) << std::setfill('0') << mode << "  ";
    std::string modeStr = modeOss.str().substr(0, 8);
    std::memcpy(header.data() + 40, modeStr.data(), modeStr.size());

    std::string sizeStr = std::to_string(size);
    if (sizeStr.size() > 10) sizeStr = sizeStr.substr(0, 10);
    std::memcpy(header.data() + 48, sizeStr.data(), sizeStr.size());

    header[58] = 0x60;
    header[59] = 0x0A;

    return header;
}

} // namespace

std::vector<uint8_t> BsdArchiveWriter::serialize(const std::vector<ArchiveMember>& members) {
    std::vector<uint8_t> buffer;

    // Magic
    const char* magic = "!<arch>\n";
    buffer.insert(buffer.end(), magic, magic + 8);

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

    // Determine payload sizes and BSD #1/longname formatting for members
    std::vector<bool> isBsdLongName(members.size(), false);
    std::vector<uint64_t> memberPayloadSizes(members.size(), 0);

    for (size_t i = 0; i < members.size(); ++i) {
        const auto& mem = members[i];
        if (mem.name.length() > 16 || mem.name.find(' ') != std::string::npos) {
            isBsdLongName[i] = true;
            memberPayloadSizes[i] = mem.name.length() + mem.bytes.size();
        } else {
            memberPayloadSizes[i] = mem.bytes.size();
        }
    }

    // Build __.SYMDEF payload if symbols exist
    std::vector<uint8_t> symdefPayload;
    size_t ranlibArraySize = allSymbols.size() * 8;
    size_t stringTableSize = 0;
    for (const auto& sym : allSymbols) {
        stringTableSize += sym.name.size() + 1;
    }

    if (!allSymbols.empty()) {
        writeLE32(symdefPayload, static_cast<uint32_t>(ranlibArraySize));
        // Reserve ranlib structs (8 bytes per symbol: uint32 ran_strx, uint32 ran_off)
        size_t ranlibStart = symdefPayload.size();
        symdefPayload.resize(ranlibStart + ranlibArraySize, 0);

        writeLE32(symdefPayload, static_cast<uint32_t>(stringTableSize));
        // Placeholder for string table
    }

    // Layout calculation
    uint64_t currentOffset = 8;

    uint64_t symdefHeaderOffset = 0;
    if (!allSymbols.empty()) {
        symdefHeaderOffset = currentOffset;
        uint64_t totalSymdefPayloadSize = 4 + ranlibArraySize + 4 + stringTableSize;
        currentOffset += 60 + totalSymdefPayloadSize + (totalSymdefPayloadSize % 2);
    }

    std::vector<uint64_t> memberHeaderOffsets(members.size(), 0);
    for (size_t i = 0; i < members.size(); ++i) {
        memberHeaderOffsets[i] = currentOffset;
        uint64_t pSize = memberPayloadSizes[i];
        currentOffset += 60 + pSize + (pSize % 2);
    }

    // Emit __.SYMDEF member
    if (!allSymbols.empty()) {
        size_t ranlibPos = 4; // after ranlib_size uint32
        std::vector<uint8_t> stringTable;
        for (const auto& sym : allSymbols) {
            uint32_t strx = static_cast<uint32_t>(stringTable.size());
            uint32_t off = static_cast<uint32_t>(memberHeaderOffsets[sym.memberIndex]);

            // Write ranlib struct
            symdefPayload[ranlibPos]     = static_cast<uint8_t>(strx & 0xFF);
            symdefPayload[ranlibPos + 1] = static_cast<uint8_t>((strx >> 8) & 0xFF);
            symdefPayload[ranlibPos + 2] = static_cast<uint8_t>((strx >> 16) & 0xFF);
            symdefPayload[ranlibPos + 3] = static_cast<uint8_t>((strx >> 24) & 0xFF);

            symdefPayload[ranlibPos + 4] = static_cast<uint8_t>(off & 0xFF);
            symdefPayload[ranlibPos + 5] = static_cast<uint8_t>((off >> 8) & 0xFF);
            symdefPayload[ranlibPos + 6] = static_cast<uint8_t>((off >> 16) & 0xFF);
            symdefPayload[ranlibPos + 7] = static_cast<uint8_t>((off >> 24) & 0xFF);

            ranlibPos += 8;

            stringTable.insert(stringTable.end(), sym.name.begin(), sym.name.end());
            stringTable.push_back('\0');
        }

        // Append string table to symdefPayload
        symdefPayload.insert(symdefPayload.end(), stringTable.begin(), stringTable.end());

        auto hdr = create60ByteHeader("__.SYMDEF", 0, 0, 0, 0, symdefPayload.size());
        buffer.insert(buffer.end(), hdr.begin(), hdr.end());
        buffer.insert(buffer.end(), symdefPayload.begin(), symdefPayload.end());
        if (symdefPayload.size() % 2 != 0) buffer.push_back('\n');
    }

    // Emit object members
    for (size_t i = 0; i < members.size(); ++i) {
        const auto& mem = members[i];
        std::string hdrName;
        if (isBsdLongName[i]) {
            hdrName = "#1/" + std::to_string(mem.name.length());
        } else {
            hdrName = mem.name;
        }

        uint64_t payloadSize = memberPayloadSizes[i];
        auto hdr = create60ByteHeader(hdrName, mem.mtime, mem.uid, mem.gid, mem.mode, payloadSize);
        buffer.insert(buffer.end(), hdr.begin(), hdr.end());

        if (isBsdLongName[i]) {
            buffer.insert(buffer.end(), mem.name.begin(), mem.name.end());
        }
        buffer.insert(buffer.end(), mem.bytes.begin(), mem.bytes.end());

        if (payloadSize % 2 != 0) {
            buffer.push_back('\n');
        }
    }

    return buffer;
}

bool BsdArchiveWriter::write(const std::vector<ArchiveMember>& members, const std::string& outputPath) {
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
