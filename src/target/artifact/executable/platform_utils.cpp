#include "target/artifact/executable/platform_utils.hh"
#include <iostream>
#include <filesystem>
#include <cstring>
#include <algorithm>

// Platform-specific includes
#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
    #define PLATFORM_WINDOWS
#elif defined(__linux__)
    #include <sys/stat.h>
    #include <unistd.h>
    #define PLATFORM_LINUX
#elif defined(__APPLE__)
    #include <sys/stat.h>
    #include <unistd.h>
    #define PLATFORM_MACOS
#else
    #define PLATFORM_UNKNOWN
#endif

namespace PlatformUtils {

    Platform getCurrentPlatform() {
        #ifdef PLATFORM_WINDOWS
            return Platform::WINDOWS;
        #elif defined(PLATFORM_LINUX)
            return Platform::LINUX;
        #elif defined(PLATFORM_MACOS)
            return Platform::MACOS;
        #else
            return Platform::UNKNOWN;
        #endif
    }

    std::string getPlatformName() {
        switch (getCurrentPlatform()) {
            case Platform::WINDOWS: return "Windows";
            case Platform::LINUX: return "Linux";
            case Platform::MACOS: return "macOS";
            default: return "Unknown";
        }
    }

    bool isWindows() {
        return getCurrentPlatform() == Platform::WINDOWS;
    }

    bool isLinux() {
        return getCurrentPlatform() == Platform::LINUX;
    }

    bool isMacOS() {
        return getCurrentPlatform() == Platform::MACOS;
    }

    std::string normalizePath(const std::string& path) {
        std::string normalized = path;
        std::string separator = getPathSeparator();
        
        // Replace all path separators with the platform-appropriate one
        #ifdef PLATFORM_WINDOWS
            std::replace(normalized.begin(), normalized.end(), '/', '\\');
        #else
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
        #endif
        
        return normalized;
    }

    std::string getPathSeparator() {
        #ifdef PLATFORM_WINDOWS
            return "\\";
        #else
            return "/";
        #endif
    }

    std::string getExecutableExtension() {
        #ifdef PLATFORM_WINDOWS
            return ".exe";
        #else
            return "";
        #endif
    }

    bool setExecutablePermissions(const std::string& filename) {
        #ifdef PLATFORM_WINDOWS
            // On Windows, executable permissions are handled by file extension
            // No additional action needed for .exe files
            (void)filename; // Suppress unused parameter warning
            return true;
        #else
            // On Unix-like systems, set executable permissions
            if (chmod(filename.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == 0) {
                return true;
            }
            std::cerr << "Warning: Failed to set executable permissions for " << filename << std::endl;
            return false;
        #endif
    }

    bool createDirectoryRecursive(const std::string& path) {
        try {
            std::filesystem::create_directories(path);
            return true;
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error creating directory " << path << ": " << e.what() << std::endl;
            return false;
        }
    }

    Endianness getSystemEndianness() {
        uint16_t test = 0x0001;
        uint8_t* bytes = reinterpret_cast<uint8_t*>(&test);
        return (bytes[0] == 0x01) ? Endianness::IS_LITTLE_ENDIAN : Endianness::IS_BIG_ENDIAN;
    }

    bool isLittleEndian() {
        return getSystemEndianness() == Endianness::IS_LITTLE_ENDIAN;
    }

    bool isBigEndian() {
        return getSystemEndianness() == Endianness::IS_BIG_ENDIAN;
    }

    // Byte swapping functions
    uint16_t swapBytes16(uint16_t value) {
        return ((value & 0xFF00) >> 8) | ((value & 0x00FF) << 8);
    }

    uint32_t swapBytes32(uint32_t value) {
        return ((value & 0xFF000000) >> 24) |
               ((value & 0x00FF0000) >> 8)  |
               ((value & 0x0000FF00) << 8)  |
               ((value & 0x000000FF) << 24);
    }

    uint64_t swapBytes64(uint64_t value) {
        return ((value & 0xFF00000000000000ULL) >> 56) |
               ((value & 0x00FF000000000000ULL) >> 40) |
               ((value & 0x0000FF0000000000ULL) >> 24) |
               ((value & 0x000000FF00000000ULL) >> 8)  |
               ((value & 0x00000000FF000000ULL) << 8)  |
               ((value & 0x0000000000FF0000ULL) << 24) |
               ((value & 0x000000000000FF00ULL) << 40) |
               ((value & 0x00000000000000FFULL) << 56);
    }

    // Host to little endian conversion
    uint16_t hostToLittleEndian16(uint16_t value) {
        return isLittleEndian() ? value : swapBytes16(value);
    }

    uint32_t hostToLittleEndian32(uint32_t value) {
        return isLittleEndian() ? value : swapBytes32(value);
    }

    uint64_t hostToLittleEndian64(uint64_t value) {
        return isLittleEndian() ? value : swapBytes64(value);
    }

    // Little endian to host conversion
    uint16_t littleEndianToHost16(uint16_t value) {
        return isLittleEndian() ? value : swapBytes16(value);
    }

    uint32_t littleEndianToHost32(uint32_t value) {
        return isLittleEndian() ? value : swapBytes32(value);
    }

    uint64_t littleEndianToHost64(uint64_t value) {
        return isLittleEndian() ? value : swapBytes64(value);
    }

    // Host to big endian conversion
    uint16_t hostToBigEndian16(uint16_t value) {
        return isBigEndian() ? value : swapBytes16(value);
    }

    uint32_t hostToBigEndian32(uint32_t value) {
        return isBigEndian() ? value : swapBytes32(value);
    }

    uint64_t hostToBigEndian64(uint64_t value) {
        return isBigEndian() ? value : swapBytes64(value);
    }

    // Big endian to host conversion
    uint16_t bigEndianToHost16(uint16_t value) {
        return isBigEndian() ? value : swapBytes16(value);
    }

    uint32_t bigEndianToHost32(uint32_t value) {
        return isBigEndian() ? value : swapBytes32(value);
    }

    uint64_t bigEndianToHost64(uint64_t value) {
        return isBigEndian() ? value : swapBytes64(value);
    }

    bool validateStructurePacking() {
        // Test structure packing by checking sizes of known structures
#ifdef _MSC_VER
        #pragma pack(push, 1)
        struct TestStruct {
            uint8_t a;
            uint16_t b;
            uint32_t c;
        };
        #pragma pack(pop)
#else
        struct TestStruct {
            uint8_t a;
            uint16_t b;
            uint32_t c;
        } __attribute__((packed));
#endif

        // On properly packed systems, this should be 7 bytes (1+2+4)
        // On unpacked systems, it might be 8 or 12 bytes due to alignment
        size_t expected_size = 7;
        size_t actual_size = sizeof(TestStruct);
        
        if (actual_size != expected_size) {
            std::cerr << "Warning: Structure packing may not be working correctly. "
                      << "Expected size: " << expected_size 
                      << ", Actual size: " << actual_size << std::endl;
            return false;
        }
        
        return true;
    }

    std::string getDefaultOutputFormat() {
        switch (getCurrentPlatform()) {
            case Platform::WINDOWS:
                return "pe";
            case Platform::LINUX:
            case Platform::MACOS:
                return "elf";
            default:
                return "elf"; // Default to ELF for unknown platforms
        }
    }

    int runCommand(const std::string& command) {
        return std::system(command.c_str());
    }

    std::string runCommandWithOutput(const std::string& command) {
        std::string result = "";
        char buffer[128];
        #ifdef _WIN32
            #define POPEN _popen
            #define PCLOSE _pclose
        #else
            #define POPEN popen
            #define PCLOSE pclose
        #endif

        FILE* pipe = POPEN(command.c_str(), "r");
        if (!pipe) {
            return "popen failed!";
        }
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
        PCLOSE(pipe);
        return result;
    }

    bool deleteFile(const std::string& path) {
        try {
            return std::filesystem::remove(path);
        } catch(const std::filesystem::filesystem_error& err) {
            // Can ignore if file doesn't exist, but report other errors
            if (err.code() != std::errc::no_such_file_or_directory) {
                 std::cerr << "Warning: Failed to delete temporary file " << path << ": " << err.what() << std::endl;
            }
            return false;
        }
    }
}