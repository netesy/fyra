#include "binary_writer.hh"
#include <iostream>

// BinaryWriter implementation
void BinaryWriter::writeLittleEndian16(uint16_t value) {
    uint16_t le_value = PlatformUtils::hostToLittleEndian16(value);
    stream_.write(reinterpret_cast<const char*>(&le_value), sizeof(le_value));
}

void BinaryWriter::writeLittleEndian32(uint32_t value) {
    uint32_t le_value = PlatformUtils::hostToLittleEndian32(value);
    stream_.write(reinterpret_cast<const char*>(&le_value), sizeof(le_value));
}

void BinaryWriter::writeLittleEndian64(uint64_t value) {
    uint64_t le_value = PlatformUtils::hostToLittleEndian64(value);
    stream_.write(reinterpret_cast<const char*>(&le_value), sizeof(le_value));
}

void BinaryWriter::writeBigEndian16(uint16_t value) {
    uint16_t be_value = PlatformUtils::hostToBigEndian16(value);
    stream_.write(reinterpret_cast<const char*>(&be_value), sizeof(be_value));
}

void BinaryWriter::writeBigEndian32(uint32_t value) {
    uint32_t be_value = PlatformUtils::hostToBigEndian32(value);
    stream_.write(reinterpret_cast<const char*>(&be_value), sizeof(be_value));
}

void BinaryWriter::writeBigEndian64(uint64_t value) {
    uint64_t be_value = PlatformUtils::hostToBigEndian64(value);
    stream_.write(reinterpret_cast<const char*>(&be_value), sizeof(be_value));
}

void BinaryWriter::writeBytes(const void* data, size_t size) {
    stream_.write(reinterpret_cast<const char*>(data), size);
}

void BinaryWriter::writeBytes(const std::vector<uint8_t>& data) {
    if (!data.empty()) {
        stream_.write(reinterpret_cast<const char*>(data.data()), data.size());
    }
}

void BinaryWriter::writeByte(uint8_t value) {
    stream_.write(reinterpret_cast<const char*>(&value), 1);
}

void BinaryWriter::writeString(const std::string& str) {
    stream_.write(str.c_str(), str.length());
}

void BinaryWriter::writeCString(const std::string& str) {
    stream_.write(str.c_str(), str.length() + 1); // Include null terminator
}

void BinaryWriter::writePadding(size_t count, uint8_t value) {
    for (size_t i = 0; i < count; ++i) {
        writeByte(value);
    }
}

void BinaryWriter::alignTo(size_t alignment, uint8_t padding) {
    size_t current_pos = tell();
    size_t remainder = current_pos % alignment;
    if (remainder != 0) {
        size_t padding_needed = alignment - remainder;
        writePadding(padding_needed, padding);
    }
}

size_t BinaryWriter::tell() {
    return static_cast<size_t>(stream_.tellp());
}

void BinaryWriter::seek(size_t position) {
    stream_.seekp(position);
}

void BinaryWriter::skip(size_t bytes) {
    stream_.seekp(bytes, std::ios::cur);
}

bool BinaryWriter::isValid() const {
    return stream_.good();
}

// BinaryReader implementation
uint16_t BinaryReader::readLittleEndian16() {
    uint16_t value;
    stream_.read(reinterpret_cast<char*>(&value), sizeof(value));
    return PlatformUtils::littleEndianToHost16(value);
}

uint32_t BinaryReader::readLittleEndian32() {
    uint32_t value;
    stream_.read(reinterpret_cast<char*>(&value), sizeof(value));
    return PlatformUtils::littleEndianToHost32(value);
}

uint64_t BinaryReader::readLittleEndian64() {
    uint64_t value;
    stream_.read(reinterpret_cast<char*>(&value), sizeof(value));
    return PlatformUtils::littleEndianToHost64(value);
}

uint16_t BinaryReader::readBigEndian16() {
    uint16_t value;
    stream_.read(reinterpret_cast<char*>(&value), sizeof(value));
    return PlatformUtils::bigEndianToHost16(value);
}

uint32_t BinaryReader::readBigEndian32() {
    uint32_t value;
    stream_.read(reinterpret_cast<char*>(&value), sizeof(value));
    return PlatformUtils::bigEndianToHost32(value);
}

uint64_t BinaryReader::readBigEndian64() {
    uint64_t value;
    stream_.read(reinterpret_cast<char*>(&value), sizeof(value));
    return PlatformUtils::bigEndianToHost64(value);
}

void BinaryReader::readBytes(void* data, size_t size) {
    stream_.read(reinterpret_cast<char*>(data), size);
}

std::vector<uint8_t> BinaryReader::readBytes(size_t size) {
    std::vector<uint8_t> data(size);
    stream_.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

uint8_t BinaryReader::readByte() {
    uint8_t value;
    stream_.read(reinterpret_cast<char*>(&value), 1);
    return value;
}

std::string BinaryReader::readString(size_t length) {
    std::string str(length, '\0');
    stream_.read(&str[0], length);
    return str;
}

std::string BinaryReader::readCString() {
    std::string str;
    char c;
    while (stream_.read(&c, 1) && c != '\0') {
        str += c;
    }
    return str;
}

size_t BinaryReader::tell() {
    return static_cast<size_t>(stream_.tellg());
}

void BinaryReader::seek(size_t position) {
    stream_.seekg(position);
}

void BinaryReader::skip(size_t bytes) {
    stream_.seekg(bytes, std::ios::cur);
}

bool BinaryReader::isValid() const {
    return stream_.good();
}

bool BinaryReader::eof() const {
    return stream_.eof();
}