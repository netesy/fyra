#ifndef PLATFORM_UTILS_HH
#define PLATFORM_UTILS_HH

#include <string>
#include <cstdint>

namespace PlatformUtils {
    // Platform detection
    enum class Platform {
        WINDOWS,
        LINUX,
        MACOS,
        UNKNOWN
    };

    // Endianness detection
    enum class Endianness {
        IS_LITTLE_ENDIAN,
        IS_BIG_ENDIAN
    };

    // Platform detection functions
    Platform getCurrentPlatform();
    std::string getPlatformName();
    bool isWindows();
    bool isLinux();
    bool isMacOS();

    // File handling functions
    std::string normalizePath(const std::string& path);
    std::string getPathSeparator();
    std::string getExecutableExtension();
    bool setExecutablePermissions(const std::string& filename);
    bool createDirectoryRecursive(const std::string& path);

    // Endianness functions
    Endianness getSystemEndianness();
    bool isLittleEndian();
    bool isBigEndian();

    // Byte order conversion functions
    uint16_t hostToLittleEndian16(uint16_t value);
    uint32_t hostToLittleEndian32(uint32_t value);
    uint64_t hostToLittleEndian64(uint64_t value);
    uint16_t littleEndianToHost16(uint16_t value);
    uint32_t littleEndianToHost32(uint32_t value);
    uint64_t littleEndianToHost64(uint64_t value);

    uint16_t hostToBigEndian16(uint16_t value);
    uint32_t hostToBigEndian32(uint32_t value);
    uint64_t hostToBigEndian64(uint64_t value);
    uint16_t bigEndianToHost16(uint16_t value);
    uint32_t bigEndianToHost32(uint32_t value);
    uint64_t bigEndianToHost64(uint64_t value);

    // Structure packing validation
    bool validateStructurePacking();

    // Default format selection based on platform
    std::string getDefaultOutputFormat();

    // System command execution
    int runCommand(const std::string& command);
    std::string runCommandWithOutput(const std::string& command);
    bool deleteFile(const std::string& path);
}

#endif // PLATFORM_UTILS_HH