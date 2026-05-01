#pragma once

#include "ValidationTypes.h"
#include "target/core/TargetInfo.h"
#include <string>
#include <memory>
#include <map>
#include <functional>

namespace codegen {
namespace validation {

// ABI specification for a target platform
class ABISpecification {
public:
    ABISpecification(const std::string& name, const std::string& arch, const std::string& os)
        : name_(name), architecture_(arch), operatingSystem_(os) {}

    // Basic properties
    const std::string& getName() const { return name_; }
    const std::string& getArchitecture() const { return architecture_; }
    const std::string& getOperatingSystem() const { return operatingSystem_; }
    
    // Calling convention
    void setCallingConvention(const CallingConvention& cc) { callingConvention_ = cc; }
    const CallingConvention& getCallingConvention() const { return callingConvention_; }
    
    // Register constraints
    void setRegisterConstraints(const RegisterConstraints& rc) { registerConstraints_ = rc; }
    const RegisterConstraints& getRegisterConstraints() const { return registerConstraints_; }
    
    // Stack alignment
    void setStackAlignment(uint32_t alignment) { stackAlignment_ = alignment; }
    uint32_t getStackAlignment() const { return stackAlignment_; }
    
    // Validation methods
    bool validateInstruction(const std::string& instruction) const;
    bool validateFunction(const std::string& functionAssembly) const;
    bool validateRegisterUsage(const std::string& reg, const std::string& context) const;
    bool validateStackFrame(const std::vector<std::string>& instructions) const;
    
    // ABI-specific rules
    void addValidationRule(const std::string& ruleName, 
                          std::function<bool(const std::string&)> validator);
    bool applyValidationRule(const std::string& ruleName, const std::string& input) const;

private:
    std::string name_;
    std::string architecture_;
    std::string operatingSystem_;
    CallingConvention callingConvention_;
    RegisterConstraints registerConstraints_;
    uint32_t stackAlignment_ = 16;
    
    // Custom validation rules
    std::map<std::string, std::function<bool(const std::string&)>> validationRules_;
};

// Registry for managing ABI specifications
class TargetABIRegistry {
public:
    static std::shared_ptr<TargetABIRegistry> getInstance();
    
    // ABI management
    void registerABI(const std::string& targetName, std::unique_ptr<ABISpecification> abiSpec);
    const ABISpecification* getABISpec(const std::string& targetName) const;
    bool hasABISpec(const std::string& targetName) const;
    
    // Validation
    ValidationResult validateABI(const std::string& targetName, const std::string& assembly) const;
    std::vector<std::string> getSupportedTargets() const;
    
    // Integration with existing TargetInfo
    void registerFromTargetInfo(const std::string& targetName, 
                               const target::TargetInfo* targetInfo);
    
    // Initialize default ABI specifications
    void initializeDefaultSpecs();

    TargetABIRegistry();
private:
    std::map<std::string, std::unique_ptr<ABISpecification>> abiSpecs_;
    static std::shared_ptr<TargetABIRegistry> instance;
    
    // Create ABI specs for supported targets
    std::unique_ptr<ABISpecification> createSystemVABISpec();
    std::unique_ptr<ABISpecification> createWindowsABISpec();
    std::unique_ptr<ABISpecification> createAArch64ABISpec();
    std::unique_ptr<ABISpecification> createWasm32ABISpec();
    std::unique_ptr<ABISpecification> createRiscV64ABISpec();
};

// Instruction validator for checking individual instructions
class InstructionValidator {
public:
    explicit InstructionValidator(const ABISpecification* abiSpec);
    
    // Instruction validation
    ValidationResult validateInstruction(const std::string& instruction) const;
    bool isValidOpcode(const std::string& opcode) const;
    bool areValidOperands(const std::string& opcode, const std::vector<std::string>& operands) const;
    bool isValidAddressingMode(const std::string& operand) const;
    
    // Architecture-specific validation
    bool isInstructionAvailable(const std::string& instruction, const std::string& architecture) const;
    bool checkInstructionEncoding(const std::string& instruction) const;

private:
    const ABISpecification* abiSpec_;
    
    // Parse instruction into components
    struct ParsedInstruction {
        std::string opcode;
        std::vector<std::string> operands;
        std::string suffix;  // For size suffixes like b, w, l, q
    };
    
    ParsedInstruction parseInstruction(const std::string& instruction) const;
    bool validateX86Instruction(const ParsedInstruction& instr) const;
    bool validateAArch64Instruction(const ParsedInstruction& instr) const;
    bool validateRiscVInstruction(const ParsedInstruction& instr) const;
};

// Register usage validator
class RegisterValidator {
public:
    explicit RegisterValidator(const ABISpecification* abiSpec);
    
    // Register validation
    ValidationResult validateRegisterUsage(const std::vector<std::string>& instructions) const;
    bool isCallerSavedPreserved(const std::vector<std::string>& instructions) const;
    bool isCalleeSavedRestored(const std::vector<std::string>& instructions) const;
    bool areParametersPassedCorrectly(const std::vector<std::string>& instructions) const;
    bool isReturnValueHandledCorrectly(const std::vector<std::string>& instructions) const;
    
    // Track register state
    struct RegisterState {
        std::map<std::string, bool> callerSavedUsed;
        std::map<std::string, bool> calleeSavedSaved;
        std::set<std::string> modifiedRegisters;
    };
    
    RegisterState analyzeRegisterUsage(const std::vector<std::string>& instructions) const;

private:
    const ABISpecification* abiSpec_;
    
    bool isRegisterModifyingInstruction(const std::string& instruction) const;
    std::vector<std::string> getModifiedRegisters(const std::string& instruction) const;
    bool isCallInstruction(const std::string& instruction) const;
    bool isSaveInstruction(const std::string& instruction, const std::string& reg) const;
    bool isRestoreInstruction(const std::string& instruction, const std::string& reg) const;
};

// Stack frame validator
class StackValidator {
public:
    explicit StackValidator(const ABISpecification* abiSpec);
    
    // Stack validation
    ValidationResult validateStackFrame(const std::vector<std::string>& instructions) const;
    bool isStackAlignmentCorrect(const std::vector<std::string>& instructions) const;
    bool isFrameStructureValid(const std::vector<std::string>& instructions) const;
    bool isShadowSpaceAllocated(const std::vector<std::string>& instructions) const;
    bool areLocalVariablesAllocatedCorrectly(const std::vector<std::string>& instructions) const;
    
    // Stack frame analysis
    struct StackFrame {
        int totalSize = 0;
        int shadowSpaceSize = 0;
        int localVariablesSize = 0;
        int savedRegistersSize = 0;
        bool isAligned = false;
        std::vector<std::string> allocationInstructions;
        std::vector<std::string> deallocationInstructions;
    };
    
    StackFrame analyzeStackFrame(const std::vector<std::string>& instructions) const;

private:
    const ABISpecification* abiSpec_;
    
    int extractStackAllocation(const std::string& instruction) const;
    bool isPrologueInstruction(const std::string& instruction) const;
    bool isEpilogueInstruction(const std::string& instruction) const;
    bool isStackPointerModification(const std::string& instruction) const;
};

} // namespace validation
} // namespace codegen