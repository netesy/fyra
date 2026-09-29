#include "target/artifact/archive/CoffArchiveWriter.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
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

void writeLE32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

void writeLE16(std::vector<uint8_t>& buf, uint16_t val) {
    buf.push_back(static_cast<uint8_t>(val & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
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

std::vector<uint8_t> CoffArchiveWriter::serialize(const std::vector<ArchiveMember>& members) {
    std::vector<uint8_t> buffer;

    // Magic
    const char* magic = "!<arch>\n";
    buffer.insert(buffer.end(), magic, magic + 8);

    // Build Longnames table (null-separated in COFF)
    std::vector<uint8_t> longNamesPayload;
    std::vector<size_t> memberNameOffsets(members.size(), 0);
    std::vector<bool> isLongName(members.size(), false);

    for (size_t i = 0; i < members.size(); ++i) {
        const auto& mem = members[i];
        if (mem.name.length() >= 16 || mem.name.find(' ') != std::string::npos) {
            isLongName[i] = true;
            memberNameOffsets[i] = longNamesPayload.size();
            longNamesPayload.insert(longNamesPayload.end(), mem.name.begin(), mem.name.end());
            longNamesPayload.push_back('\0');
        }
    }

    struct SymbolEntry {
        std::string name;
        uint16_t memberIndex; // 1-based index
    };
    std::vector<SymbolEntry> firstLinkerSymbols;
    for (size_t i = 0; i < members.size(); ++i) {
        for (const auto& sym : members[i].exportedSymbols) {
            firstLinkerSymbols.push_back({sym, static_cast<uint16_t>(i + 1)});
        }
    }

    // Sort symbols lexicographically for 2nd Linker Member
    std::vector<SymbolEntry> secondLinkerSymbols = firstLinkerSymbols;
    std::sort(secondLinkerSymbols.begin(), secondLinkerSymbols.end(),
              [](const SymbolEntry& a, const SymbolEntry& b) {
                  return a.name < b.name;
              });

    // 1st Linker Member payload size
    size_t firstLinkerPayloadSize = 4 + firstLinkerSymbols.size() * 4;
    for (const auto& sym : firstLinkerSymbols) {
        firstLinkerPayloadSize += sym.name.size() + 1;
    }

    // 2nd Linker Member payload size
    size_t secondLinkerPayloadSize = 4 + members.size() * 4 + 4 + secondLinkerSymbols.size() * 2;
    for (const auto& sym : secondLinkerSymbols) {
        secondLinkerPayloadSize += sym.name.size() + 1;
    }

    // Layout calculation
    uint64_t currentOffset = 8;

    uint64_t firstLinkerOffset = currentOffset;
    currentOffset += 60 + firstLinkerPayloadSize + (firstLinkerPayloadSize % 2);

    uint64_t secondLinkerOffset = currentOffset;
    currentOffset += 60 + secondLinkerPayloadSize + (secondLinkerPayloadSize % 2);

    uint64_t longNamesOffset = 0;
    if (!longNamesPayload.empty()) {
        longNamesOffset = currentOffset;
        currentOffset += 60 + longNamesPayload.size() + (longNamesPayload.size() % 2);
    }

    std::vector<uint64_t> memberHeaderOffsets(members.size(), 0);
    for (size_t i = 0; i < members.size(); ++i) {
        memberHeaderOffsets[i] = currentOffset;
        uint64_t memSize = members[i].bytes.size();
        currentOffset += 60 + memSize + (memSize % 2);
    }

    // 1. Emit First Linker Member
    {
        std::vector<uint8_t> payload;
        writeBE32(payload, static_cast<uint32_t>(firstLinkerSymbols.size()));
        for (const auto& sym : firstLinkerSymbols) {
            uint32_t off = static_cast<uint32_t>(memberHeaderOffsets[sym.memberIndex - 1]);
            writeBE32(payload, off);
        }
        for (const auto& sym : firstLinkerSymbols) {
            payload.insert(payload.end(), sym.name.begin(), sym.name.end());
            payload.push_back('\0');
        }

        auto hdr = create60ByteHeader("/", 0, 0, 0, 0, payload.size());
        buffer.insert(buffer.end(), hdr.begin(), hdr.end());
        buffer.insert(buffer.end(), payload.begin(), payload.end());
        if (payload.size() % 2 != 0) buffer.push_back('\n');
    }

    // 2. Emit Second Linker Member
    {
        std::vector<uint8_t> payload;
        writeLE32(payload, static_cast<uint32_t>(members.size()));
        for (size_t i = 0; i < members.size(); ++i) {
            writeLE32(payload, static_cast<uint32_t>(memberHeaderOffsets[i]));
        }
        writeLE32(payload, static_cast<uint32_t>(secondLinkerSymbols.size()));
        for (const auto& sym : secondLinkerSymbols) {
            writeLE16(payload, sym.memberIndex); // 1-based index
        }
        for (const auto& sym : secondLinkerSymbols) {
            payload.insert(payload.end(), sym.name.begin(), sym.name.end());
            payload.push_back('\0');
        }

        auto hdr = create60ByteHeader("/", 0, 0, 0, 0, payload.size());
        buffer.insert(buffer.end(), hdr.begin(), hdr.end());
        buffer.insert(buffer.end(), payload.begin(), payload.end());
        if (payload.size() % 2 != 0) buffer.push_back('\n');
    }

    // 3. Emit Longnames Member if present
    if (!longNamesPayload.empty()) {
        auto hdr = create60ByteHeader("//", 0, 0, 0, 0, longNamesPayload.size());
        buffer.insert(buffer.end(), hdr.begin(), hdr.end());
        buffer.insert(buffer.end(), longNamesPayload.begin(), longNamesPayload.end());
        if (longNamesPayload.size() % 2 != 0) buffer.push_back('\n');
    }

    // 4. Emit Object Members
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

bool CoffArchiveWriter::write(const std::vector<ArchiveMember>& members, const std::string& outputPath) {
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
