#include "target/artifact/archive/BsdArchiveWriter.hh"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace target {
namespace artifact {
namespace archive {

namespace {

std::string formatBsdHeader(const std::string& name, size_t size) {
    std::ostringstream ss;
    ss << std::left << std::setw(16) << name
       << std::left << std::setw(12) << "0"
       << std::left << std::setw(6)  << "0"
       << std::left << std::setw(6)  << "0"
       << std::left << std::setw(8)  << "0644"
       << std::left << std::setw(10) << size
       << "`\n";
    return ss.str();
}

} // namespace

bool BsdArchiveWriter::writeArchive(const std::vector<ArchiveMember>& members,
                                     const std::string& outputPath,
                                     std::string& errorOutput) {
    // 1. Collect exported symbols across members
    std::vector<std::pair<std::string, size_t>> allSymbols; // symbol name -> member index
    for (size_t i = 0; i < members.size(); ++i) {
        for (const auto& sym : members[i].exportedSymbols) {
            allSymbols.push_back({sym, i});
        }
    }

    // Prepare members with BSD extended headers `#1/<length>` if filename > 16 chars or contains space
    struct BsdMemberPrep {
        std::string hdrName;
        std::string fileNameData; // placed right before object bytes if extended header used
        const ArchiveMember* memberPtr;
    };

    std::vector<BsdMemberPrep> preppedMembers;
    preppedMembers.reserve(members.size());

    for (const auto& m : members) {
        BsdMemberPrep prep;
        prep.memberPtr = &m;
        if (m.name.size() > 16 || m.name.find(' ') != std::string::npos) {
            prep.fileNameData = m.name;
            // Pad filename data to 4-byte boundary for Darwin alignment
            while (prep.fileNameData.size() % 4 != 0) {
                prep.fileNameData.push_back('\0');
            }
            prep.hdrName = "#1/" + std::to_string(prep.fileNameData.size());
        } else {
            prep.hdrName = m.name;
        }
        preppedMembers.push_back(prep);
    }

    // 2. Layout calculation
    uint64_t cursor = 8; // "!<arch>\n"

    // Darwin/BSD `__.SYMDEF` symbol table
    // Payload:
    // uint32_t sym_struct_size = num_syms * 8
    // struct { uint32_t str_off; uint32_t member_off; } syms[num_syms]
    // uint32_t str_tab_size
    // char str_tab[...]

    size_t strTabSize = 0;
    for (const auto& [sym, idx] : allSymbols) {
        strTabSize += sym.size() + 1;
    }

    bool hasSymdef = !allSymbols.empty();
    size_t symdefPayloadSize = 0;
    if (hasSymdef) {
        symdefPayloadSize = 4 + (allSymbols.size() * 8) + 4 + strTabSize;
    } else {
        symdefPayloadSize = 4 + 4; // empty symdef
    }

    uint64_t symdefTotalSize = 60 + symdefPayloadSize + (symdefPayloadSize % 2 != 0 ? 1 : 0);
    cursor += symdefTotalSize;

    // Calculate member file offsets
    std::vector<uint32_t> memberOffsets;
    memberOffsets.reserve(members.size());

    for (const auto& prep : preppedMembers) {
        memberOffsets.push_back(static_cast<uint32_t>(cursor));
        uint64_t payloadSz = prep.fileNameData.size() + prep.memberPtr->data.size();
        cursor += 60 + payloadSz + (payloadSz % 2 != 0 ? 1 : 0);
    }

    // 3. Serialise
    std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        errorOutput = "Cannot open output BSD archive file: " + outputPath;
        return false;
    }

    file.write("!<arch>\n", 8);

    // Write `__.SYMDEF` member
    {
        std::vector<uint8_t> payload;
        if (hasSymdef) {
            uint32_t structSize = static_cast<uint32_t>(allSymbols.size() * 8);
            const uint8_t* p1 = reinterpret_cast<const uint8_t*>(&structSize);
            payload.insert(payload.end(), p1, p1 + 4);

            uint32_t currentStrOff = 0;
            for (const auto& [sym, idx] : allSymbols) {
                uint32_t sOff = currentStrOff;
                uint32_t mOff = memberOffsets[idx];

                const uint8_t* sp = reinterpret_cast<const uint8_t*>(&sOff);
                payload.insert(payload.end(), sp, sp + 4);

                const uint8_t* mp = reinterpret_cast<const uint8_t*>(&mOff);
                payload.insert(payload.end(), mp, mp + 4);

                currentStrOff += static_cast<uint32_t>(sym.size() + 1);
            }

            uint32_t strSz = static_cast<uint32_t>(strTabSize);
            const uint8_t* p2 = reinterpret_cast<const uint8_t*>(&strSz);
            payload.insert(payload.end(), p2, p2 + 4);

            for (const auto& [sym, idx] : allSymbols) {
                payload.insert(payload.end(), sym.begin(), sym.end());
                payload.push_back(0);
            }
        } else {
            uint32_t zero = 0;
            const uint8_t* zp = reinterpret_cast<const uint8_t*>(&zero);
            payload.insert(payload.end(), zp, zp + 4);
            payload.insert(payload.end(), zp, zp + 4);
        }

        std::string hdr = formatBsdHeader("__.SYMDEF", payload.size());
        file.write(hdr.data(), 60);
        file.write(reinterpret_cast<const char*>(payload.data()), payload.size());
        if (payload.size() % 2 != 0) file.put('\n');
    }

    // Write object members
    for (const auto& prep : preppedMembers) {
        size_t totalPayloadSz = prep.fileNameData.size() + prep.memberPtr->data.size();
        std::string hdr = formatBsdHeader(prep.hdrName, totalPayloadSz);
        file.write(hdr.data(), 60);
        if (!prep.fileNameData.empty()) {
            file.write(prep.fileNameData.data(), prep.fileNameData.size());
        }
        file.write(reinterpret_cast<const char*>(prep.memberPtr->data.data()), prep.memberPtr->data.size());
        if (totalPayloadSz % 2 != 0) file.put('\n');
    }

    file.close();
    return !file.fail();
}

} // namespace archive
} // namespace artifact
} // namespace target
