#include "target/architecture/spirv/SPIRVModule.h"
#include <cstring>

namespace target {
namespace spirv {

// SPIR-V instruction opcodes used by the module builder itself.
namespace Op {
    constexpr uint16_t Capability        = 17;
    constexpr uint16_t Extension         = 10;
    constexpr uint16_t ExtInstImport     = 11;
    constexpr uint16_t MemoryModel       = 14;
    constexpr uint16_t EntryPoint        = 15;
    constexpr uint16_t ExecutionMode     = 16;
    constexpr uint16_t Name              = 5;
    constexpr uint16_t Decorate          = 71;
    constexpr uint16_t MemberDecorate    = 72;
} // namespace Op

// SPIR-V logical-layout section boundaries (first word opcode ranges are not
// used; sections are tracked explicitly by the vectors above).

std::vector<uint32_t> SPIRVModule::encodeStringWords(const std::string& str) {
    // Literal strings are packed as a NUL-terminated sequence of bytes into
    // 32-bit words, little-endian.
    std::vector<uint32_t> words;
    size_t len = str.size() + 1; // include NUL
    size_t wordCount = (len + 3) / 4;
    words.assign(wordCount, 0);
    std::memcpy(words.data(), str.data(), str.size());
    return words;
}

SPIRVModule::SPIRVModule() {
    bound_ = 1;
}

void SPIRVModule::addCapability(uint32_t capability) {
    capabilities_.push_back({Op::Capability, {capability}});
}

void SPIRVModule::addExtension(const std::string& name) {
    SPIRVInstruction inst;
    inst.opcode = Op::Extension;
    inst.operands = encodeStringWords(name);
    extensions_.push_back(std::move(inst));
}

uint32_t SPIRVModule::importExtendedInstructionSet(const std::string& name) {
    uint32_t id = allocateId();
    SPIRVInstruction inst;
    inst.opcode = Op::ExtInstImport;
    inst.operands.push_back(id);
    auto words = encodeStringWords(name);
    inst.operands.insert(inst.operands.end(), words.begin(), words.end());
    extInstImports_.push_back(std::move(inst));
    return id;
}

void SPIRVModule::setMemoryModel(uint32_t addressingModel, uint32_t memoryModel) {
    addressingModel_ = addressingModel;
    memoryModel_ = memoryModel;
    memoryModelInsts_.clear();
    memoryModelInsts_.push_back({Op::MemoryModel, {addressingModel, memoryModel}});
}

void SPIRVModule::addEntryPoint(uint32_t executionModel, uint32_t entryPointId,
                                const std::string& name,
                                const std::vector<uint32_t>& interfaces) {
    SPIRVInstruction inst;
    inst.opcode = Op::EntryPoint;
    inst.operands.push_back(executionModel);
    inst.operands.push_back(entryPointId);
    auto nameWords = encodeStringWords(name);
    inst.operands.insert(inst.operands.end(), nameWords.begin(), nameWords.end());
    inst.operands.insert(inst.operands.end(), interfaces.begin(), interfaces.end());
    entryPoints_.push_back(std::move(inst));
}

void SPIRVModule::addExecutionMode(uint32_t entryPointId, uint32_t mode,
                                   const std::vector<uint32_t>& literals) {
    SPIRVInstruction inst;
    inst.opcode = Op::ExecutionMode;
    inst.operands.push_back(entryPointId);
    inst.operands.push_back(mode);
    inst.operands.insert(inst.operands.end(), literals.begin(), literals.end());
    executionModes_.push_back(std::move(inst));
}

void SPIRVModule::addDecoration(uint32_t target, uint32_t decoration,
                                const std::vector<uint32_t>& operands) {
    SPIRVInstruction inst;
    inst.opcode = Op::Decorate;
    inst.operands.push_back(target);
    inst.operands.push_back(decoration);
    inst.operands.insert(inst.operands.end(), operands.begin(), operands.end());
    annotations_.push_back(std::move(inst));
}

void SPIRVModule::addMemberDecoration(uint32_t structId, uint32_t member,
                                      uint32_t decoration,
                                      const std::vector<uint32_t>& operands) {
    SPIRVInstruction inst;
    inst.opcode = Op::MemberDecorate;
    inst.operands.push_back(structId);
    inst.operands.push_back(member);
    inst.operands.push_back(decoration);
    inst.operands.insert(inst.operands.end(), operands.begin(), operands.end());
    annotations_.push_back(std::move(inst));
}

void SPIRVModule::addDebug(uint16_t opcode, const std::vector<uint32_t>& operands) {
    debug_.push_back({opcode, operands});
}

void SPIRVModule::addGlobal(uint16_t opcode, const std::vector<uint32_t>& operands) {
    globals_.push_back({opcode, operands});
}

void SPIRVModule::addInstruction(uint16_t opcode, const std::vector<uint32_t>& operands) {
    functions_.push_back({opcode, operands});
}

static void appendEncoded(std::vector<uint32_t>& words, const SPIRVInstruction& inst) {
    auto enc = inst.encode();
    words.insert(words.end(), enc.begin(), enc.end());
}

std::vector<uint32_t> SPIRVModule::encodeBinary() const {
    std::vector<uint32_t> words;
    // Header: magic, version 1.0, generator, bound, schema 0.
    words.push_back(0x07230203);
    words.push_back(0x00010000);
    words.push_back(0);          // generator
    words.push_back(bound_);
    words.push_back(0);          // schema

    for (const auto& inst : capabilities_)    appendEncoded(words, inst);
    for (const auto& inst : extensions_)      appendEncoded(words, inst);
    for (const auto& inst : extInstImports_)  appendEncoded(words, inst);
    for (const auto& inst : memoryModelInsts_)appendEncoded(words, inst);
    for (const auto& inst : entryPoints_)     appendEncoded(words, inst);
    for (const auto& inst : executionModes_)  appendEncoded(words, inst);
    for (const auto& inst : debug_)           appendEncoded(words, inst);
    for (const auto& inst : annotations_)     appendEncoded(words, inst);
    for (const auto& inst : globals_)         appendEncoded(words, inst);
    for (const auto& inst : functions_)       appendEncoded(words, inst);

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
