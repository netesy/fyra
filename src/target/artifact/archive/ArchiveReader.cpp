#include "target/artifact/archive/ArchiveReader.h"
#include "target/artifact/object/ObjectReader.h"
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <charconv>

namespace target {
namespace artifact {
namespace archive {

namespace {

uint64_t parseDecimal(const char* buf, size_t len) {
    // Trim spaces
    size_t start = 0;
    while (start < len && (buf[start] == ' ' || buf[start] == '\t')) {
        start++;
    }
    size_t end = len;
    while (end > start && (buf[end - 1] == ' ' || buf[end - 1] == '\t')) {
        end--;
    }
    if (start >= end) return 0;

    uint64_t val = 0;
    auto result = std::from_chars(buf + start, buf + end, val, 10);
    if (result.ec != std::errc()) {
        return 0;
    }
    return val;
}

} // namespace

bool ArchiveReader::parse(const std::vector<uint8_t>& bytes, std::vector<ArchiveObjectMember>& outMembers) {
    lastError_.clear();
    outMembers.clear();

    if (bytes.size() < 8 || std::memcmp(bytes.data(), "!<arch>\n", 8) != 0) {
        lastError_ = "Invalid archive magic";
        return false;
    }

    uint64_t currentOffset = 8;
    std::string longNamesTable;

    while (currentOffset + 60 <= bytes.size()) {
        const char* hdr = reinterpret_cast<const char*>(bytes.data() + currentOffset);
        if (hdr[58] != 0x60 || hdr[59] != 0x0A) {
            break; // Header magic end
        }

        std::string name(hdr, 16);
        uint64_t size = parseDecimal(hdr + 48, 10);
        uint64_t payloadOffset = currentOffset + 60;

        if (payloadOffset + size > bytes.size()) {
            lastError_ = "Archive member size exceeds file bounds";
            return false;
        }

        // Handle special GNU / COFF / BSD members
        if (name.rfind("//              ", 0) == 0 || name.rfind("// ", 0) == 0) {
            longNamesTable.assign(reinterpret_cast<const char*>(bytes.data() + payloadOffset), size);
        } else if (name.rfind("/               ", 0) == 0 || name.rfind("__.SYMDEF", 0) == 0) {
            // Symbol table member - skip
        } else {
            // Object member
            std::string memberName = name;
            size_t slashPos = memberName.find(' ');
            if (slashPos != std::string::npos) {
                memberName = memberName.substr(0, slashPos);
            }

            if (!memberName.empty() && memberName[0] == '/' && longNamesTable.size() > 0) {
                uint64_t tblOff = 0;
                auto res = std::from_chars(memberName.c_str() + 1, memberName.c_str() + memberName.size(), tblOff, 10);
                if (res.ec == std::errc() && tblOff < longNamesTable.size()) {
                    size_t endPos = longNamesTable.find_first_of("/\n\0", tblOff);
                    if (endPos != std::string::npos) {
                        memberName = longNamesTable.substr(tblOff, endPos - tblOff);
                    } else {
                        memberName = longNamesTable.substr(tblOff);
                    }
                }
            }

            if (!memberName.empty() && memberName.back() == '/') {
                memberName.pop_back();
            }

            ArchiveObjectMember mem;
            mem.name = memberName;
            mem.bytes.assign(bytes.data() + payloadOffset, bytes.data() + payloadOffset + size);

            auto reader = target::artifact::object::ObjectReader::detectAndCreate(mem.bytes);
            if (reader) {
                reader->parse(mem.bytes, mem.artifact);
            }

            outMembers.push_back(mem);
        }

        currentOffset = payloadOffset + size + (size % 2); // 2-byte alignment
    }

    return true;
}

bool ArchiveReader::read(const std::string& inputPath, std::vector<ArchiveObjectMember>& outMembers) {
    std::ifstream file(inputPath, std::ios::binary);
    if (!file.is_open()) {
        lastError_ = "Failed to open archive file: " + inputPath;
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return parse(bytes, outMembers);
}

} // namespace archive
} // namespace artifact
} // namespace target
