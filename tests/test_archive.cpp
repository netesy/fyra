#include "target/artifact/archive/ArchiveWriter.h"
#include "target/artifact/archive/UnixArchiveWriter.h"
#include "target/artifact/archive/CoffArchiveWriter.h"
#include "target/artifact/archive/BsdArchiveWriter.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>
#include <string>

using namespace target::artifact::archive;

static uint16_t readLE16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

static uint32_t readLE32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static uint32_t readBE32(const uint8_t* p) {
    return static_cast<uint32_t>((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

void test_unix_archive() {
    std::cout << "[Test] Unix Archive Serialization..." << std::endl;

    UnixArchiveWriter writer;

    ArchiveMember m1;
    m1.name = "very_long_object_name_one.o";
    m1.bytes = {'H', 'e', 'l', 'l', 'o'}; // odd size = 5 bytes
    m1.exportedSymbols = {"func_one", "alpha_sym"};

    ArchiveMember m2;
    m2.name = "very_long_object_name_two.o";
    m2.bytes = {'W', 'o', 'r', 'l', 'd', '!'}; // even size = 6 bytes
    m2.exportedSymbols = {"func_two", "beta_sym"};

    ArchiveMember m3;
    m3.name = "short.o";
    m3.bytes = {0x01, 0x02, 0x03}; // odd size = 3 bytes
    m3.exportedSymbols = {"short_sym"};

    std::vector<ArchiveMember> members = {m1, m2, m3};
    std::vector<uint8_t> data = writer.serialize(members);

    // 1. Verify Magic
    assert(data.size() > 8);
    assert(std::memcmp(data.data(), "!<arch>\n", 8) == 0);

    std::string archiveStr(data.begin(), data.end());

    // 2. Verify symbol index `/` exists
    assert(archiveStr.find("/               ") != std::string::npos);

    // 3. Verify long-name string table `//` exists
    assert(archiveStr.find("//              ") != std::string::npos);

    // 4. Verify long member names remain distinguishable and intact
    assert(archiveStr.find("very_long_object_name_one.o/\n") != std::string::npos);
    assert(archiveStr.find("very_long_object_name_two.o/\n") != std::string::npos);

    // 5. Verify short member header formatting (append `/`)
    assert(archiveStr.find("short.o/        ") != std::string::npos);

    // 6. Deterministic test
    std::vector<uint8_t> data2 = writer.serialize(members);
    assert(data == data2);

    std::cout << "  -> Unix Archive Test PASSED." << std::endl;
}

void test_coff_archive() {
    std::cout << "[Test] Windows COFF Archive Serialization..." << std::endl;

    CoffArchiveWriter writer;

    ArchiveMember m1;
    m1.name = "very_long_coff_member_first.obj";
    m1.bytes = {0x86, 0x64, 0x01, 0x02, 0x03}; // 5 bytes
    m1.exportedSymbols = {"ZSymbolFirst", "ASymbolFirst"};

    ArchiveMember m2;
    m2.name = "very_long_coff_member_second.obj";
    m2.bytes = {0x86, 0x64, 0x0A, 0x0B}; // 4 bytes
    m2.exportedSymbols = {"BSymbolSecond"};

    std::vector<ArchiveMember> members = {m1, m2};
    std::vector<uint8_t> data = writer.serialize(members);

    // 1. Verify Magic
    assert(data.size() > 8);
    assert(std::memcmp(data.data(), "!<arch>\n", 8) == 0);

    // 2. First Linker Member starts at offset 8 (header 60 bytes)
    assert(std::memcmp(data.data() + 8, "/               ", 16) == 0);
    uint32_t firstLinkerNumSyms = readBE32(data.data() + 68);
    assert(firstLinkerNumSyms == 3);

    // 3. Find Second Linker Member header
    std::string archiveStr(data.begin(), data.end());
    size_t secondLinkerHdr = archiveStr.find("/               ", 68);
    assert(secondLinkerHdr != std::string::npos);

    uint32_t numMembers = readLE32(data.data() + secondLinkerHdr + 60);
    assert(numMembers == 2);

    uint32_t secondLinkerNumSyms = readLE32(data.data() + secondLinkerHdr + 60 + 4 + 2 * 4);
    assert(secondLinkerNumSyms == 3);

    // 4. Verify COFF Longnames Member `//`
    assert(archiveStr.find("//              ") != std::string::npos);
    assert(archiveStr.find("very_long_coff_member_first.obj") != std::string::npos);
    assert(archiveStr.find("very_long_coff_member_second.obj") != std::string::npos);

    // 5. Deterministic test
    std::vector<uint8_t> data2 = writer.serialize(members);
    assert(data == data2);

    std::cout << "  -> COFF Archive Test PASSED." << std::endl;
}

void test_bsd_archive() {
    std::cout << "[Test] BSD / Darwin Archive Serialization..." << std::endl;

    BsdArchiveWriter writer;

    ArchiveMember m1;
    m1.name = "very_long_macho_member_name_one.o";
    m1.bytes = {0xCF, 0xFA, 0xED, 0xFE, 0x01}; // 5 bytes
    m1.exportedSymbols = {"macho_sym_1"};

    ArchiveMember m2;
    m2.name = "short.o";
    m2.bytes = {0xCF, 0xFA, 0xED, 0xFE}; // 4 bytes
    m2.exportedSymbols = {"macho_sym_2"};

    std::vector<ArchiveMember> members = {m1, m2};
    std::vector<uint8_t> data = writer.serialize(members);

    // 1. Verify Magic
    assert(data.size() > 8);
    assert(std::memcmp(data.data(), "!<arch>\n", 8) == 0);

    std::string archiveStr(data.begin(), data.end());

    // 2. Verify BSD symbol table member `__.SYMDEF` (9 chars + 7 spaces = 16 bytes)
    assert(archiveStr.find("__.SYMDEF       ") != std::string::npos);

    // 3. Verify BSD `#1/<len>` long name header format
    std::string bsdHdr = "#1/" + std::to_string(m1.name.length());
    assert(archiveStr.find(bsdHdr) != std::string::npos);

    // 4. Verify short name header format
    assert(archiveStr.find("short.o         ") != std::string::npos);

    // 5. Deterministic test
    std::vector<uint8_t> data2 = writer.serialize(members);
    assert(data == data2);

    std::cout << "  -> BSD Archive Test PASSED." << std::endl;
}

int main() {
    std::cout << "==========================================" << std::endl;
    std::cout << " Running Archive Serialization Unit Tests " << std::endl;
    std::cout << "==========================================" << std::endl;

    test_unix_archive();
    test_coff_archive();
    test_bsd_archive();

    std::cout << "All Archive Serialization Tests Passed!" << std::endl;
    return 0;
}
