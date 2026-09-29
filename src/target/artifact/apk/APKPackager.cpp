#include "target/artifact/apk/APKPackager.h"
#include <fstream>
#include <iostream>
#include <filesystem>

namespace target {
namespace artifact {

namespace {

static uint32_t calcCrc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int)(crc & 1)));
        }
    }
    return ~crc;
}

struct ZipEntry {
    std::string pathInZip;
    std::vector<uint8_t> data;
    uint32_t crc = 0;
    uint32_t localHeaderOffset = 0;
};

static void writeU16(std::ostream& os, uint16_t v) {
    os.write(reinterpret_cast<const char*>(&v), 2);
}

static void writeU32(std::ostream& os, uint32_t v) {
    os.write(reinterpret_cast<const char*>(&v), 4);
}

} // namespace

bool APKPackager::package(const std::string& outputPath, const std::string& assetsPath, const std::string& libPath, const std::string& manifestPath) {
    namespace fs = std::filesystem;
    std::cout << "[APKPackager] Packaging APK to " << outputPath << "\n";
    std::cout << "  - Manifest: " << manifestPath << "\n";
    std::cout << "  - Native Library: " << libPath << "\n";

    fs::path buildDir = fs::path(outputPath).parent_path() / "apk_build";
    fs::create_directories(buildDir / "lib" / "arm64-v8a");

    std::vector<ZipEntry> entries;

    std::error_code ec;
    // Collect manifest
    if (fs::exists(manifestPath)) {
        fs::copy_file(manifestPath, buildDir / "AndroidManifest.xml", fs::copy_options::overwrite_existing, ec);
        std::ifstream mf(manifestPath, std::ios::binary);
        ZipEntry e;
        e.pathInZip = "AndroidManifest.xml";
        e.data.assign((std::istreambuf_iterator<char>(mf)), std::istreambuf_iterator<char>());
        e.crc = calcCrc32(e.data.data(), e.data.size());
        entries.push_back(std::move(e));
    }

    // Collect native library
    if (fs::exists(libPath)) {
        fs::copy_file(libPath, buildDir / "lib" / "arm64-v8a" / "libfyra_app.so", fs::copy_options::overwrite_existing, ec);
        std::ifstream lf(libPath, std::ios::binary);
        ZipEntry e;
        e.pathInZip = "lib/arm64-v8a/libfyra_app.so";
        e.data.assign((std::istreambuf_iterator<char>(lf)), std::istreambuf_iterator<char>());
        e.crc = calcCrc32(e.data.data(), e.data.size());
        entries.push_back(std::move(e));
    }

    // Write standard valid uncompressed (stored) PKZIP container
    std::ofstream apk(outputPath, std::ios::binary | std::ios::trunc);
    if (!apk.is_open()) return false;

    // 1. Write Local File Headers + File Data
    for (auto& entry : entries) {
        entry.localHeaderOffset = static_cast<uint32_t>(apk.tellp());
        writeU32(apk, 0x04034b50); // Local file header signature
        writeU16(apk, 20);         // Version needed: 2.0
        writeU16(apk, 0);          // General purpose bit flag
        writeU16(apk, 0);          // Compression method: 0 (Stored)
        writeU16(apk, 0);          // Mod time
        writeU16(apk, 0);          // Mod date
        writeU32(apk, entry.crc);  // CRC32
        writeU32(apk, static_cast<uint32_t>(entry.data.size())); // Compressed size
        writeU32(apk, static_cast<uint32_t>(entry.data.size())); // Uncompressed size
        writeU16(apk, static_cast<uint16_t>(entry.pathInZip.size())); // Filename length
        writeU16(apk, 0);          // Extra field length
        apk.write(entry.pathInZip.data(), entry.pathInZip.size());
        apk.write(reinterpret_cast<const char*>(entry.data.data()), entry.data.size());
    }

    // 2. Write Central Directory
    uint32_t cdOffset = static_cast<uint32_t>(apk.tellp());
    for (const auto& entry : entries) {
        writeU32(apk, 0x02014b50); // Central file header signature
        writeU16(apk, 20);         // Version made by: 2.0
        writeU16(apk, 20);         // Version needed: 2.0
        writeU16(apk, 0);          // General purpose bit flag
        writeU16(apk, 0);          // Compression method: 0 (Stored)
        writeU16(apk, 0);          // Mod time
        writeU16(apk, 0);          // Mod date
        writeU32(apk, entry.crc);  // CRC32
        writeU32(apk, static_cast<uint32_t>(entry.data.size())); // Compressed size
        writeU32(apk, static_cast<uint32_t>(entry.data.size())); // Uncompressed size
        writeU16(apk, static_cast<uint16_t>(entry.pathInZip.size())); // Filename length
        writeU16(apk, 0);          // Extra field length
        writeU16(apk, 0);          // File comment length
        writeU16(apk, 0);          // Disk number start
        writeU16(apk, 0);          // Internal file attributes
        writeU32(apk, 0x81a40000); // External file attributes (-rw-r--r--)
        writeU32(apk, entry.localHeaderOffset); // Relative offset of local header
        apk.write(entry.pathInZip.data(), entry.pathInZip.size());
    }
    uint32_t cdSize = static_cast<uint32_t>(apk.tellp()) - cdOffset;

    // 3. Write End of Central Directory Record (EOCD)
    writeU32(apk, 0x06054b50); // EOCD signature
    writeU16(apk, 0);          // Number of this disk
    writeU16(apk, 0);          // Disk with start of CD
    writeU16(apk, static_cast<uint16_t>(entries.size())); // Total entries on this disk
    writeU16(apk, static_cast<uint16_t>(entries.size())); // Total entries
    writeU32(apk, cdSize);     // Size of Central Directory
    writeU32(apk, cdOffset);   // Offset of start of CD
    writeU16(apk, 0);          // Comment length
    apk.close();

    return true;
}

}
}
