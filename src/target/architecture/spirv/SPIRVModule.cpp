#include "target/architecture/spirv/SPIRVModule.h"
#include <cstring>

namespace target {
namespace spirv {

static std::vector<uint32_t> encodeString(const std::string& str) {
    std::vector<uint32_t> words;
    size_t len = str.size() + 1;
    size_t wordCount = (len + 3) / 4;
    words.resize(wordCount, 0);
    std::memcpy(words.data(), str.c_str(), str.size());
    return words;
}

SPIRVModule::SPIRVModule() {
    bound_ = 1;
}

void SPIRVModule::addCapability(uint32_t capability) {
    SPIRVInstruction inst;
    inst.opcode = 1;
    inst.operands = {capability};
    capabilities_.push_back(inst);
}

void SPIRVModule::setMemoryModel(uint32_t addressingModel, uint32_t memoryModel) {
    addressingModel_ = addressingModel;
    memoryModel_ = memoryModel;
    memoryModelInsts_.clear();
    SPIRVInstruction inst;
    inst.opcode = 14;
    inst.operands = {addressingModel, memoryModel};
    memoryModelInsts_.push_back(inst);
}

void SPIRVModule::addEntryPoint(uint32_t executionModel, uint32_t entryPointId, const std::string& name, const std::vector<uint32_t>& interfaces) {
    SPIRVInstruction inst;
    inst.opcode = 15;
    inst.operands.push_back(executionModel);
    inst.operands.push_back(entryPointId);
    auto nameWords = encodeString(name);
    inst.operands.insert(inst.operands.end(), nameWords.begin(), nameWords.end());
    inst.operands.insert(inst.operands.end(), interfaces.begin(), interfaces.end());
    entryPoints_.push_back(inst);
}

void SPIRVModule::addInstruction(uint16_t opcode, const std::vector<uint32_t>& operands) {
    SPIRVInstruction inst;
    inst.opcode = opcode;
    inst.operands = operands;
    instructions_.push_back(inst);
}

std::vector<uint32_t> SPIRVModule::encodeBinary() const {
    std::vector<uint32_t> words;
    words.push_back(0x07230203);
    words.push_back(0x00010500);
    words.push_back(0x002B0000);
    words.push_back(bound_);
    words.push_back(0);

    for (const auto& inst : capabilities_) {
        auto enc = inst.encode();
        words.insert(words.end(), enc.begin(), enc.end());
    }
    for (const auto& inst : entryPoints_) {
        auto enc = inst.encode();
        words.insert(words.end(), enc.begin(), enc.end());
    }
    for (const auto& inst : memoryModelInsts_) {
        auto enc = inst.encode();
        words.insert(words.end(), enc.begin(), enc.end());
    }
    for (const auto& inst : instructions_) {
        auto enc = inst.encode();
        words.insert(words.end(), enc.begin(), enc.end());
    }

    return words;
}

std::vector<uint8_t> SPIRVBinaryWriter::write(const SPIRVModule& module) {
    auto words = module.encodeBinary();
    std::vector<uint8_t> bytes(words.size() * 4);
    std::memcpy(bytes.data(), words.data(), bytes.size());
    return bytes;
}

} // namespace spirv
} // namespace target
