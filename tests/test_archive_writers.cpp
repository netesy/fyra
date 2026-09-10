#include "target/artifact/archive/UnixArchiveWriter.hh"
#include "target/artifact/archive/CoffArchiveWriter.hh"
#include "target/artifact/archive/BsdArchiveWriter.hh"
#include <cassert>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>

using namespace target::artifact::archive;

void testUnixArchiveWriter() {
    std::cout << "[Test] Testing UnixArchiveWriter..." << std::endl;

    ArchiveMember m1;
    m1.name = "very_long_object_name_one.o";
    m1.data = {'A', 'B', 'C', 'D'};
    m1.exportedSymbols = {"sym_one"};

    ArchiveMember m2;
    m2.name = "very_long_object_name_two.o";
    m2.data = {'E', 'F', 'G'};
    m2.exportedSymbols = {"sym_two"};

    std::vector<ArchiveMember> members = {m1, m2};
    std::string outputPath = "/tmp/test_unix_archive.a";
    std::string err;

    UnixArchiveWriter writer;
    bool ok = writer.writeArchive(members, outputPath, err);
    assert(ok && "UnixArchiveWriter failed");

    std::ifstream file(outputPath, std::ios::binary);
    assert(file.is_open() && "Output archive file missing");

    std::string magic(8, '\0');
    file.read(&magic[0], 8);
    assert(magic == "!<arch>\n" && "Invalid archive magic");

    std::filesystem::remove(outputPath);
    std::cout << "[Test] UnixArchiveWriter passed!" << std::endl;
}

void testCoffArchiveWriter() {
    std::cout << "[Test] Testing CoffArchiveWriter..." << std::endl;

    ArchiveMember m1;
    m1.name = "very_long_coff_member_name_one.obj";
    m1.data = {0x00, 0x01, 0x02, 0x03};
    m1.exportedSymbols = {"_coff_sym_one"};

    ArchiveMember m2;
    m2.name = "very_long_coff_member_name_two.obj";
    m2.data = {0x04, 0x05, 0x06};
    m2.exportedSymbols = {"_coff_sym_two"};

    std::vector<ArchiveMember> members = {m1, m2};
    std::string outputPath = "/tmp/test_coff_archive.lib";
    std::string err;

    CoffArchiveWriter writer;
    bool ok = writer.writeArchive(members, outputPath, err);
    assert(ok && "CoffArchiveWriter failed");

    std::ifstream file(outputPath, std::ios::binary);
    assert(file.is_open() && "Output COFF archive file missing");

    std::string magic(8, '\0');
    file.read(&magic[0], 8);
    assert(magic == "!<arch>\n" && "Invalid COFF archive magic");

    std::filesystem::remove(outputPath);
    std::cout << "[Test] CoffArchiveWriter passed!" << std::endl;
}

void testBsdArchiveWriter() {
    std::cout << "[Test] Testing BsdArchiveWriter..." << std::endl;

    ArchiveMember m1;
    m1.name = "very_long_darwin_member_name_one.o";
    m1.data = {0x10, 0x20, 0x30, 0x40};
    m1.exportedSymbols = {"_bsd_sym_one"};

    ArchiveMember m2;
    m2.name = "very_long_darwin_member_name_two.o";
    m2.data = {0x50, 0x60, 0x70};
    m2.exportedSymbols = {"_bsd_sym_two"};

    std::vector<ArchiveMember> members = {m1, m2};
    std::string outputPath = "/tmp/test_bsd_archive.a";
    std::string err;

    BsdArchiveWriter writer;
    bool ok = writer.writeArchive(members, outputPath, err);
    assert(ok && "BsdArchiveWriter failed");

    std::ifstream file(outputPath, std::ios::binary);
    assert(file.is_open() && "Output BSD archive file missing");

    std::string magic(8, '\0');
    file.read(&magic[0], 8);
    assert(magic == "!<arch>\n" && "Invalid BSD archive magic");

    std::filesystem::remove(outputPath);
    std::cout << "[Test] BsdArchiveWriter passed!" << std::endl;
}

int main() {
    testUnixArchiveWriter();
    testCoffArchiveWriter();
    testBsdArchiveWriter();
    std::cout << "All ArchiveWriter unit tests passed successfully!" << std::endl;
    return 0;
}
