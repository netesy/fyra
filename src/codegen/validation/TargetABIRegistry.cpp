#include "codegen/validation/TargetABIRegistry.h"
#include <algorithm>
#include <sstream>
#include <regex>
#include <iostream>

namespace codegen {
namespace validation {

// ABISpecification implementation
bool ABISpecification::validateInstruction(const std::string& instruction) const {
    if (instruction.empty() || instruction[0] == '#') {
        return true;
    }
    
    std::regex instrPattern(R"(^\s*([a-zA-Z][a-zA-Z0-9]*)\s*(.*)$)");
    return std::regex_match(instruction, instrPattern);
}

bool ABISpecification::validateFunction(const std::string& functionAssembly) const {
    std::istringstream iss(functionAssembly);
    std::string line;
    bool hasReturn = false;
    
    while (std::getline(iss, line)) {
        if (line.find("ret") != std::string::npos) {
            hasReturn = true;
        }
        if (!validateInstruction(line)) {
            return false;
        }
    }
    
    return hasReturn;
}

bool ABISpecification::validateRegisterUsage(const std::string& reg, const std::string& context) const {
    return registerConstraints_.validateUsage(reg, context);
}

bool ABISpecification::validateStackFrame(const std::vector<std::string>& instructions) const {
    int totalStackAllocation = 0;
    
    for (const auto& instr : instructions) {
        if (instr.find("sub") != std::string::npos && instr.find("%rsp") != std::string::npos) {
            std::regex allocPattern(R"(sub.?\s+\$(\d+),\s*%rsp)");
            std::smatch match;
            if (std::regex_search(instr, match, allocPattern)) {
                totalStackAllocation += std::stoi(match[1].str());
            }
        }
    }
    
    return totalStackAllocation == 0 || (totalStackAllocation % stackAlignment_) == 0;
}

void ABISpecification::addValidationRule(const std::string& ruleName, 
                                       std::function<bool(const std::string&)> validator) {
    validationRules_[ruleName] = validator;
}

bool ABISpecification::applyValidationRule(const std::string& ruleName, const std::string& input) const {
    auto it = validationRules_.find(ruleName);
    return (it != validationRules_.end()) ? it->second(input) : true;
}

// TargetABIRegistry implementation
std::shared_ptr<TargetABIRegistry> TargetABIRegistry::instance = nullptr;

std::shared_ptr<TargetABIRegistry> TargetABIRegistry::getInstance() {
    if (!instance) {
        instance = std::make_shared<TargetABIRegistry>();
    }
    return instance;
}

TargetABIRegistry::TargetABIRegistry() = default;

void TargetABIRegistry::registerABI(const std::string& targetName, 
                                   std::unique_ptr<ABISpecification> abiSpec) {
    abiSpecs_[targetName] = std::move(abiSpec);
}

const ABISpecification* TargetABIRegistry::getABISpec(const std::string& targetName) const {
    auto it = abiSpecs_.find(targetName);
    return (it != abiSpecs_.end()) ? it->second.get() : nullptr;
}

ValidationResult TargetABIRegistry::validateABI(const std::string& targetName, 
                                               const std::string& assembly) const {
    ValidationResult result;
    result.targetName = targetName;
    
    const ABISpecification* spec = getABISpec(targetName);
    if (!spec) {
        result.addError(ErrorSeverity::Critical, "ABI", 
                       "No ABI specification found for target: " + targetName);
        return result;
    }
    
    std::istringstream iss(assembly);
    std::string line;
    size_t lineNumber = 1;
    
    while (std::getline(iss, line)) {
        if (!spec->validateInstruction(line)) {
            result.addError(ErrorSeverity::Error, "Instruction", 
                           "Invalid instruction at line " + std::to_string(lineNumber) + ": " + line);
        }
        lineNumber++;
    }
    
    return result;
}

std::vector<std::string> TargetABIRegistry::getSupportedTargets() const {
    std::vector<std::string> targets;
    for (const auto& [name, spec] : abiSpecs_) {
        targets.push_back(name);
    }
    return targets;
}

void TargetABIRegistry::registerFromTargetInfo(const std::string& targetName, 
                                              const codegen::target::TargetInfo* targetInfo) {
    if (!targetInfo) return;
    
    auto abiSpec = std::make_unique<ABISpecification>(targetName, "x86_64", "unknown");
    
    CallingConvention cc;
    cc.name = targetName;
    cc.stackAlignment = static_cast<uint32_t>(targetInfo->getStackAlignment());
    
    if (targetName.find("windows") != std::string::npos) {
        cc.shadowSpace = 32;
        cc.calleeSaved = {"%rbx", "%rbp", "%rdi", "%rsi", "%rsp", "%r12", "%r13", "%r14", "%r15"};
        cc.callerSaved = {"%rax", "%rcx", "%rdx", "%r8", "%r9", "%r10", "%r11"};
    } else {
        cc.shadowSpace = 0;
        cc.calleeSaved = {"%rbx", "%rbp", "%rsp", "%r12", "%r13", "%r14", "%r15"};
        cc.callerSaved = {"%rax", "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9", "%r10", "%r11"};
    }
    
    abiSpec->setCallingConvention(cc);
    registerABI(targetName, std::move(abiSpec));
}

void TargetABIRegistry::initializeDefaultSpecs() {
    registerABI("linux", createSystemVABISpec());
    registerABI("windows", createWindowsABISpec());
    registerABI("aarch64", createAArch64ABISpec());
    registerABI("wasm32", createWasm32ABISpec());
    registerABI("riscv64", createRiscV64ABISpec());
}

std::unique_ptr<ABISpecification> TargetABIRegistry::createSystemVABISpec() {
    auto spec = std::make_unique<ABISpecification>("System V", "x86_64", "linux");
    
    CallingConvention cc;
    cc.name = "System V AMD64";
    cc.integerArgRegs = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    cc.floatArgRegs = {"%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7"};
    cc.shadowSpace = 0;
    cc.stackAlignment = 16;
    
    spec->setCallingConvention(cc);
    return spec;
}

std::unique_ptr<ABISpecification> TargetABIRegistry::createWindowsABISpec() {
    auto spec = std::make_unique<ABISpecification>("Windows x64", "x86_64", "windows");
    
    CallingConvention cc;
    cc.name = "Microsoft x64";
    cc.integerArgRegs = {"%rcx", "%rdx", "%r8", "%r9"};
    cc.floatArgRegs = {"%xmm0", "%xmm1", "%xmm2", "%xmm3"};
    cc.shadowSpace = 32;
    cc.stackAlignment = 16;
    
    spec->setCallingConvention(cc);
    return spec;
}

std::unique_ptr<ABISpecification> TargetABIRegistry::createAArch64ABISpec() {
    auto spec = std::make_unique<ABISpecification>("AArch64", "aarch64", "linux");
    
    CallingConvention cc;
    cc.integerArgRegs = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"};
    cc.stackAlignment = 16;
    
    spec->setCallingConvention(cc);
    return spec;
}

std::unique_ptr<ABISpecification> TargetABIRegistry::createWasm32ABISpec() {
    auto spec = std::make_unique<ABISpecification>("WebAssembly", "wasm32", "unknown");
    
    CallingConvention cc;
    cc.stackAlignment = 4;
    
    spec->setCallingConvention(cc);
    return spec;
}

std::unique_ptr<ABISpecification> TargetABIRegistry::createRiscV64ABISpec() {
    auto spec = std::make_unique<ABISpecification>("RISC-V", "riscv64", "linux");
    
    CallingConvention cc;
    cc.integerArgRegs = {"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"};
    cc.stackAlignment = 16;
    
    spec->setCallingConvention(cc);
    return spec;
}

} // namespace validation
} // namespace codegen