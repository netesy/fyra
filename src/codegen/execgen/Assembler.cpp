#include "codegen/execgen/Assembler.h"

namespace codegen {
namespace execgen {

Assembler::Assembler() {}

void Assembler::emitByte(uint8_t byte) {
    code.push_back(byte);
}

void Assembler::emitBytes(const std::vector<uint8_t>& bytes) {
    code.insert(code.end(), bytes.begin(), bytes.end());
}

void Assembler::emitWord(uint16_t word) {
    emitByte(word & 0xFF);
    emitByte((word >> 8) & 0xFF);
}

void Assembler::emitDWord(uint32_t dword) {
    emitByte(dword & 0xFF);
    emitByte((dword >> 8) & 0xFF);
    emitByte((dword >> 16) & 0xFF);
    emitByte((dword >> 24) & 0xFF);
}

void Assembler::emitQWord(uint64_t qword) {
    emitByte(qword & 0xFF);
    emitByte((qword >> 8) & 0xFF);
    emitByte((qword >> 16) & 0xFF);
    emitByte((qword >> 24) & 0xFF);
    emitByte((qword >> 32) & 0xFF);
    emitByte((qword >> 40) & 0xFF);
    emitByte((qword >> 48) & 0xFF);
    emitByte((qword >> 56) & 0xFF);
}

const std::vector<uint8_t>& Assembler::getCode() const {
    return code;
}

size_t Assembler::getCodeSize() const {
    return code.size();
}

void Assembler::setByteAt(size_t index, uint8_t value) {
    if (index < code.size()) {
        code[index] = value;
    }
}

} // namespace execgen
} // namespace codegen