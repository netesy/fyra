#ifndef BINARY_WRITER_HH
#define BINARY_WRITER_HH

#include "platform_utils.hh"
#include <fstream>
#include <vector>
#include <cstdint>

class BinaryWriter {
public:
    explicit BinaryWriter(std::ofstream& stream) : stream_(stream) {}

    // Write functions with explicit endianness
    void writeLittleEndian16(uint16_t value);
    void writeLittleEndian32(uint32_t value);
    void writeLittleEndian64(uint64_t value);
    
    void writeBigEndian16(uint16_t value);
    void writeBigEndian32(uint32_t value);
    void writeBigEndian64(uint64_t value);
    
    // Write raw bytes
    void writeBytes(const void* data, size_t size);
    void writeBytes(const std::vector<uint8_t>& data);
    void writeByte(uint8_t value);
    
    // Write strings
    void writeString(const std::string& str);
    void writeCString(const std::string& str); // Null-terminated
    
    // Padding and alignment
    void writePadding(size_t count, uint8_t value = 0);
    void alignTo(size_t alignment, uint8_t padding = 0);
    
    // Position management
    size_t tell();
    void seek(size_t position);
    void skip(size_t bytes);
    
    // Validation
    bool isValid() const;

private:
    std::ofstream& stream_;
};

class BinaryReader {
public:
    explicit BinaryReader(std::ifstream& stream) : stream_(stream) {}

    // Read functions with explicit endianness
    uint16_t readLittleEndian16();
    uint32_t readLittleEndian32();
    uint64_t readLittleEndian64();
    
    uint16_t readBigEndian16();
    uint32_t readBigEndian32();
    uint64_t readBigEndian64();
    
    // Read raw bytes
    void readBytes(void* data, size_t size);
    std::vector<uint8_t> readBytes(size_t size);
    uint8_t readByte();
    
    // Read strings
    std::string readString(size_t length);
    std::string readCString(); // Read until null terminator
    
    // Position management
    size_t tell();
    void seek(size_t position);
    void skip(size_t bytes);
    
    // Validation
    bool isValid() const;
    bool eof() const;

private:
    std::ifstream& stream_;
};

#endif // BINARY_WRITER_HH