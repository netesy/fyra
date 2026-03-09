#include "codegen/validation/TargetABIRegistry.h"

namespace codegen {
namespace validation {

// InstructionValidator
InstructionValidator::InstructionValidator(const ABISpecification* abiSpec) : abiSpec_(abiSpec) {}
ValidationResult InstructionValidator::validateInstruction(const std::string& instruction) const { return ValidationResult(); }
bool InstructionValidator::isValidOpcode(const std::string& opcode) const { return true; }
bool InstructionValidator::areValidOperands(const std::string& opcode, const std::vector<std::string>& operands) const { return true; }
bool InstructionValidator::isValidAddressingMode(const std::string& operand) const { return true; }
bool InstructionValidator::isInstructionAvailable(const std::string& instruction, const std::string& architecture) const { return true; }
bool InstructionValidator::checkInstructionEncoding(const std::string& instruction) const { return true; }
InstructionValidator::ParsedInstruction InstructionValidator::parseInstruction(const std::string& instruction) const { return ParsedInstruction(); }
bool InstructionValidator::validateX86Instruction(const ParsedInstruction& instr) const { return true; }
bool InstructionValidator::validateAArch64Instruction(const ParsedInstruction& instr) const { return true; }
bool InstructionValidator::validateRiscVInstruction(const ParsedInstruction& instr) const { return true; }

// RegisterValidator
RegisterValidator::RegisterValidator(const ABISpecification* abiSpec) : abiSpec_(abiSpec) {}
ValidationResult RegisterValidator::validateRegisterUsage(const std::vector<std::string>& instructions) const { return ValidationResult(); }
bool RegisterValidator::isCallerSavedPreserved(const std::vector<std::string>& instructions) const { return true; }
bool RegisterValidator::isCalleeSavedRestored(const std::vector<std::string>& instructions) const { return true; }
bool RegisterValidator::areParametersPassedCorrectly(const std::vector<std::string>& instructions) const { return true; }
bool RegisterValidator::isReturnValueHandledCorrectly(const std::vector<std::string>& instructions) const { return true; }
RegisterValidator::RegisterState RegisterValidator::analyzeRegisterUsage(const std::vector<std::string>& instructions) const { return RegisterState(); }
bool RegisterValidator::isRegisterModifyingInstruction(const std::string& instruction) const { return true; }
std::vector<std::string> RegisterValidator::getModifiedRegisters(const std::string& instruction) const { return {}; }
bool RegisterValidator::isCallInstruction(const std::string& instruction) const { return true; }
bool RegisterValidator::isSaveInstruction(const std::string& instruction, const std::string& reg) const { return true; }
bool RegisterValidator::isRestoreInstruction(const std::string& instruction, const std::string& reg) const { return true; }

// StackValidator
StackValidator::StackValidator(const ABISpecification* abiSpec) : abiSpec_(abiSpec) {}
ValidationResult StackValidator::validateStackFrame(const std::vector<std::string>& instructions) const { return ValidationResult(); }
bool StackValidator::isStackAlignmentCorrect(const std::vector<std::string>& instructions) const { return true; }
bool StackValidator::isFrameStructureValid(const std::vector<std::string>& instructions) const { return true; }
bool StackValidator::isShadowSpaceAllocated(const std::vector<std::string>& instructions) const { return true; }
bool StackValidator::areLocalVariablesAllocatedCorrectly(const std::vector<std::string>& instructions) const { return true; }
StackValidator::StackFrame StackValidator::analyzeStackFrame(const std::vector<std::string>& instructions) const { return StackFrame(); }
int StackValidator::extractStackAllocation(const std::string& instruction) const { return 0; }
bool StackValidator::isPrologueInstruction(const std::string& instruction) const { return true; }
bool StackValidator::isEpilogueInstruction(const std::string& instruction) const { return true; }
bool StackValidator::isStackPointerModification(const std::string& instruction) const { return true; }

} // namespace validation
} // namespace codegen
