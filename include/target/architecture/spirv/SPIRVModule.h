#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace target {
namespace spirv {

struct SPIRVInstruction {
    uint16_t opcode;
    std::vector<uint32_t> operands;

    uint32_t getWordCount() const {
        return static_cast<uint32_t>(1 + operands.size());
    }

    std::vector<uint32_t> encode() const {
        std::vector<uint32_t> words;
        words.reserve(getWordCount());
        uint32_t firstWord = (getWordCount() << 16) | (opcode & 0xFFFF);
        words.push_back(firstWord);
        words.insert(words.end(), operands.begin(), operands.end());
        return words;
    }
};

class SPIRVModule {
public:
    SPIRVModule();

    uint32_t allocateId() {
        return bound_++;
    }

    uint32_t getBound() const {
        return bound_;
    }

    void addCapability(uint32_t capability);
    void setMemoryModel(uint32_t addressingModel, uint32_t memoryModel);
    void addEntryPoint(uint32_t executionModel, uint32_t entryPointId, const std::string& name, const std::vector<uint32_t>& interfaces);
    void addInstruction(uint16_t opcode, const std::vector<uint32_t>& operands);

    const std::vector<SPIRVInstruction>& getInstructions() const {
        return instructions_;
    }

    std::vector<uint32_t> encodeBinary() const;

private:
    uint32_t bound_ = 1;
    uint32_t addressingModel_ = 0;
    uint32_t memoryModel_ = 1;
    std::vector<SPIRVInstruction> capabilities_;
    std::vector<SPIRVInstruction> entryPoints_;
    std::vector<SPIRVInstruction> memoryModelInsts_;
    std::vector<SPIRVInstruction> instructions_;
};

class SPIRVBinaryWriter {
public:
    static std::vector<uint8_t> write(const SPIRVModule& module);
};

} // namespace spirv
} // namespace target
