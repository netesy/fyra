#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace target {
namespace spirv {

// A single SPIR-V instruction: 16-bit opcode word count + opcode, followed by
// operand words. Strings are pre-packed into 32-bit words by the caller.
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

// SPIR-V modules have a strict logical layout. This class keeps each section
// in its own list so instructions can be registered lazily during lowering
// (e.g. a type first needed inside a function body) while still serializing in
// the order the specification requires:
//
//   1. OpCapability
//   2. OpExtension / OpExtInstImport
//   3. OpMemoryModel
//   4. OpEntryPoint
//   5. OpExecutionMode
//   6. debug instructions          (unused)
//   7. annotations                 (OpDecorate, OpMemberDecorate, ...)
//   8. types, constants, globals   (OpType*, OpConstant*, OpVariable, ...)
//   9. functions                   (OpFunction ... OpFunctionEnd)
class SPIRVModule {
public:
    SPIRVModule();

    // Ids are allocated from a monotonically increasing bound. Id 0 and 1 are
    // reserved by convention; the bound starts at 1 and pre-increments.
    uint32_t allocateId() { return bound_++; }
    uint32_t getBound() const { return bound_; }

    // --- Section 1: capabilities -------------------------------------------
    void addCapability(uint32_t capability);

    // --- Section 2: extensions / extended instruction sets -----------------
    void addExtension(const std::string& name);
    // Imports an extended instruction set (e.g. "GLSL.std.450") and returns
    // the id of the OpExtInstImport result for use in OpExtInst.
    uint32_t importExtendedInstructionSet(const std::string& name);

    // --- Section 3: memory model -------------------------------------------
    void setMemoryModel(uint32_t addressingModel, uint32_t memoryModel);

    // --- Section 4/5: entry points and execution modes ---------------------
    void addEntryPoint(uint32_t executionModel, uint32_t entryPointId,
                       const std::string& name,
                       const std::vector<uint32_t>& interfaces);
    void addExecutionMode(uint32_t entryPointId, uint32_t mode,
                          const std::vector<uint32_t>& literals);

    // --- Section 7: annotations --------------------------------------------
    void addDecoration(uint32_t target, uint32_t decoration,
                       const std::vector<uint32_t>& operands = {});
    void addMemberDecoration(uint32_t structId, uint32_t member,
                             uint32_t decoration,
                             const std::vector<uint32_t>& operands = {});

    // --- Section 6: debug instructions (OpName, OpMemberName, OpString, ...) -
    void addDebug(uint16_t opcode, const std::vector<uint32_t>& operands);

    // --- Section 8: types / constants / module-scope variables -------------
    // Everything that must appear before the first OpFunction. This includes
    // OpType*, OpConstant*, OpSpecConstant*, OpVariable with non-Function
    // storage, and OpUndef at module scope.
    void addGlobal(uint16_t opcode, const std::vector<uint32_t>& operands);

    // --- Section 9: function bodies ----------------------------------------
    // Instructions inside OpFunction ... OpFunctionEnd, emitted in call order.
    void addInstruction(uint16_t opcode, const std::vector<uint32_t>& operands);

    const std::vector<SPIRVInstruction>& getInstructions() const {
        return functions_;
    }

    std::vector<uint32_t> encodeBinary() const;

    // Diagnostic collection for lowering-time problems that should surface in
    // the emitted module consumers (e.g. unsupported operations).
    std::vector<std::string>& diagnostics() { return diagnostics_; }
    const std::vector<std::string>& diagnostics() const { return diagnostics_; }

private:
    uint32_t bound_ = 1;
    uint32_t addressingModel_ = 0;   // Logical
    uint32_t memoryModel_ = 1;       // GLSL450

    std::vector<SPIRVInstruction> capabilities_;
    std::vector<SPIRVInstruction> extensions_;
    std::vector<SPIRVInstruction> extInstImports_;
    std::vector<SPIRVInstruction> memoryModelInsts_;
    std::vector<SPIRVInstruction> entryPoints_;
    std::vector<SPIRVInstruction> executionModes_;
    std::vector<SPIRVInstruction> debug_;
    std::vector<SPIRVInstruction> annotations_;
    std::vector<SPIRVInstruction> globals_;
    std::vector<SPIRVInstruction> functions_;
    std::vector<std::string> diagnostics_;

    static std::vector<uint32_t> encodeStringWords(const std::string& str);
};

class SPIRVBinaryWriter {
public:
    static std::vector<uint8_t> write(const SPIRVModule& module);
};

} // namespace spirv
} // namespace target
