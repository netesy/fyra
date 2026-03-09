#pragma once

#include <vector>
#include <cstdint>

namespace codegen {
namespace execgen {

class Assembler {
public:
    Assembler();

    // Emit raw bytes
    void emitByte(uint8_t byte);
    void emitBytes(const std::vector<uint8_t>& bytes);

    // Emit integer types
    void emitWord(uint16_t word);   // 2 bytes
    void emitDWord(uint32_t dword); // 4 bytes
    void emitQWord(uint64_t qword); // 8 bytes

    // Get the generated code
    const std::vector<uint8_t>& getCode() const;
    size_t getCodeSize() const;
    void setByteAt(size_t index, uint8_t value);

private:
    std::vector<uint8_t> code;
};

} // namespace execgen
} // namespace codegen