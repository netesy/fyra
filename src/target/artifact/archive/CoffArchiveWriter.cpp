#include "target/artifact/archive/CoffArchiveWriter.hh"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

namespace target {
namespace artifact {
namespace archive {

namespace {

std::string formatCoffHeader(const std::string& name, size_t size) {
    std::ostringstream ss;
    ss << std::left << std::setw(16) << name
       << std::left << std::setw(12) << "0"
       << std::left << std::setw(6)  << "0"
       << std::left << std::setw(6)  << "0"
       << std::left << std::setw(8)  << "0"
       << std::left << std::setw(10) << size
       << "`\n";
    return ss.str();
}

} // namespace

bool CoffArchiveWriter::writeArchive(const std::vector<ArchiveMember>& members,
                                      const std::string& outputPath,
                                      std::string& errorOutput) {
    // 1. Collect symbols and longnames
    std::string longNamesPayload;
    std::vector<std::string> formattedNames;
    formattedNames.reserve(members.size());

    for (const auto& m : members) {
        if (m.name.size() > 15 || m.name.find(' ') != std::string::npos) {
            size_t off = longNamesPayload.size();
            longNamesPayload += m.name;
            longNamesPayload.push_back('\0');
            formattedNames.push_back("/" + std::to_string(off));
        } else {
            std::string n = m.name;
            if (!n.empty() && n.back() != '/') n += "/";
            formattedNames.push_back(n);
        }
    }

    std::vector<std::pair<std::string, uint16_t>> allSymbols; // symbol name -> 1-based member index
    for (size_t i = 0; i < members.size(); ++i) {
        for (const auto& sym : members[i].exportedSymbols) {
            allSymbols.push_back({sym, static_cast<uint16_t>(i + 1)});
        }
    }

    // 2. Compute Layout
    // Header 8 bytes ("!<arch>\n")
    // First Linker Member '/'
    // Second Linker Member '/'
    // Longnames Member '//'
    // Object Members

    uint64_t cursor = 8;

    // First Linker Member payload (Big Endian)
    size_t firstPayloadSize = 4 + (allSymbols.size() * 4);
    for (const auto& [sym, idx] : allSymbols) {
        firstPayloadSize += sym.size() + 1;
    }
    uint64_t firstMemberTotalSize = 60 + firstPayloadSize + (firstPayloadSize % 2 != 0 ? 1 : 0);
    cursor += firstMemberTotalSize;

    // Sort symbols lexicographically for Second Linker Member
    std::vector<std::pair<std::string, uint16_t>> sortedSymbols = allSymbols;
    std::sort(sortedSymbols.begin(), sortedSymbols.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Second Linker Member payload (Little Endian)
    size_t secondPayloadSize = 4 + (members.size() * 4) + 4 + (sortedSymbols.size() * 2);
    for (const auto& [sym, idx] : sortedSymbols) {
        secondPayloadSize += sym.size() + 1;
    }
    uint64_t secondMemberTotalSize = 60 + secondPayloadSize + (secondPayloadSize % 2 != 0 ? 1 : 0);
    cursor += secondMemberTotalSize;

    // Longnames Member payload
    bool hasLongNames = !longNamesPayload.empty();
    if (hasLongNames) {
        uint64_t longNamesMemberTotalSize = 60 + longNamesPayload.size() + (longNamesPayload.size() % 2 != 0 ? 1 : 0);
        cursor += longNamesMemberTotalSize;
    }

    // Member Offsets
    std::vector<uint32_t> memberOffsets;
    memberOffsets.reserve(members.size());

    for (size_t i = 0; i < members.size(); ++i) {
        memberOffsets.push_back(static_cast<uint32_t>(cursor));
        uint64_t sz = members[i].data.size();
        cursor += 60 + sz + (sz % 2 != 0 ? 1 : 0);
    }

    // 3. Serialise
    std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        errorOutput = "Cannot open output COFF archive file: " + outputPath;
        return false;
    }

    file.write("!<arch>\n", 8);

    // Write First Linker Member (Big Endian)
    {
        std::vector<uint8_t> payload;
        uint32_t numSymsBE = __builtin_bswap32(static_cast<uint32_t>(allSymbols.size()));
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&numSymsBE);
        payload.insert(payload.end(), p, p + 4);

        for (const auto& [sym, idx] : allSymbols) {
            uint32_t offBE = __builtin_bswap32(memberOffsets[idx - 1]);
            const uint8_t* op = reinterpret_cast<const uint8_t*>(&offBE);
            payload.insert(payload.end(), op, op + 4);
        }

        for (const auto& [sym, idx] : allSymbols) {
            payload.insert(payload.end(), sym.begin(), sym.end());
            payload.push_back(0);
        }

        std::string hdr = formatCoffHeader("/", payload.size());
        file.write(hdr.data(), 60);
        file.write(reinterpret_cast<const char*>(payload.data()), payload.size());
        if (payload.size() % 2 != 0) file.put('\n');
    }

    // Write Second Linker Member (Little Endian)
    {
        std::vector<uint8_t> payload;
        uint32_t numMembersLE = static_cast<uint32_t>(members.size());
        const uint8_t* mp = reinterpret_cast<const uint8_t*>(&numMembersLE);
        payload.insert(payload.end(), mp, mp + 4);

        for (uint32_t off : memberOffsets) {
            const uint8_t* op = reinterpret_cast<const uint8_t*>(&off);
            payload.insert(payload.end(), op, op + 4);
        }

        uint32_t numSymsLE = static_cast<uint32_t>(sortedSymbols.size());
        const uint8_t* sp = reinterpret_cast<const uint8_t*>(&numSymsLE);
        payload.insert(payload.end(), sp, sp + 4);

        for (const auto& [sym, idx] : sortedSymbols) {
            uint16_t idxLE = idx;
            const uint8_t* ip = reinterpret_cast<const uint8_t*>(&idxLE);
            payload.insert(payload.end(), ip, ip + 2);
        }

        for (const auto& [sym, idx] : sortedSymbols) {
            payload.insert(payload.end(), sym.begin(), sym.end());
            payload.push_back(0);
        }

        std::string hdr = formatCoffHeader("/", payload.size());
        file.write(hdr.data(), 60);
        file.write(reinterpret_cast<const char*>(payload.data()), payload.size());
        if (payload.size() % 2 != 0) file.put('\n');
    }

    // Write Longnames Member
    if (hasLongNames) {
        std::string hdr = formatCoffHeader("//", longNamesPayload.size());
        file.write(hdr.data(), 60);
        file.write(longNamesPayload.data(), longNamesPayload.size());
        if (longNamesPayload.size() % 2 != 0) file.put('\n');
    }

    // Write Object Members
    for (size_t i = 0; i < members.size(); ++i) {
        std::string hdr = formatCoffHeader(formattedNames[i], members[i].data.size());
        file.write(hdr.data(), 60);
        file.write(reinterpret_cast<const char*>(members[i].data.data()), members[i].data.size());
        if (members[i].data.size() % 2 != 0) file.put('\n');
    }

    file.close();
    return !file.fail();
}

} // namespace archive
} // namespace artifact
} // namespace target
