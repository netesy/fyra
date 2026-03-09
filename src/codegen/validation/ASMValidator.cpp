#include "codegen/validation/ASMValidator.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <iostream>

namespace codegen {
namespace validation {

// ASMValidator implementation
ASMValidator::ASMValidator() 
    : abiRegistry_(TargetABIRegistry::getInstance()) {
    abiRegistry_->initializeDefaultSpecs();
}

ASMValidator::ASMValidator(std::shared_ptr<TargetABIRegistry> registry)
    : abiRegistry_(registry) {}

ValidationResult ASMValidator::validateAssembly(
    const std::string& assembly,
    const std::string& targetName,
    const codegen::target::TargetInfo* targetInfo) {
    
    ValidationTimer timer;
    ValidationResult result;
    result.targetName = targetName;
    
    // Generate cache key for performance optimization
    std::string cacheKey = generateCacheKey(assembly, targetName);
    if (validationCaching_ && validationCache_.count(cacheKey)) {
        return validationCache_[cacheKey];
    }
    
    // Get or create ABI specification
    const ABISpecification* abiSpec = abiRegistry_->getABISpec(targetName);
    if (!abiSpec && targetInfo) {
        abiRegistry_->registerFromTargetInfo(targetName, targetInfo);
        abiSpec = abiRegistry_->getABISpec(targetName);
    }
    
    if (!abiSpec) {
        result.addError(ErrorSeverity::Critical, "Configuration", 
                       "No ABI specification available for target: " + targetName);
        return result;
    }
    
    // Parse assembly for structured validation
    ParsedAssembly parsed = parseAssembly(assembly);
    result.metrics.totalInstructions = parsed.lines.size();
    result.metrics.totalFunctions = parsed.functions.size();
    
    // Perform comprehensive validation
    ValidationResult validationResult = performValidation(parsed, targetName, abiSpec);
    
    // Merge results
    result.isValid = validationResult.isValid;
    result.errors.insert(result.errors.end(), validationResult.errors.begin(), validationResult.errors.end());
    result.warnings.insert(result.warnings.end(), validationResult.warnings.begin(), validationResult.warnings.end());
    
    // Update metrics
    result.metrics.validationTimeMs = timer.getElapsedMs();
    lastMetrics_ = result.metrics;
    
    // Cache result if enabled
    if (validationCaching_) {
        validationCache_[cacheKey] = result;
    }
    
    return result;
}

ValidationResult ASMValidator::validateAssemblyFile(
    const std::string& assemblyPath,
    const std::string& targetName,
    const codegen::target::TargetInfo* targetInfo) {
    
    std::ifstream file(assemblyPath);
    if (!file.is_open()) {
        ValidationResult result;
        result.addError(ErrorSeverity::Critical, "IO", "Cannot open assembly file: " + assemblyPath);
        return result;
    }
    
    std::string assembly((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    ValidationResult result = validateAssembly(assembly, targetName, targetInfo);
    result.assemblyPath = assemblyPath;
    
    return result;
}

ValidationResult ASMValidator::validateABICompliance(
    const std::string& assembly,
    const std::string& targetName) {
    
    const ABISpecification* abiSpec = abiRegistry_->getABISpec(targetName);
    if (!abiSpec) {
        ValidationResult result;
        result.addError(ErrorSeverity::Critical, "ABI", "No ABI specification for target: " + targetName);
        return result;
    }
    
    ParsedAssembly parsed = parseAssembly(assembly);
    ValidationResult result;
    
    // Validate each function for ABI compliance
    for (const auto& funcName : parsed.functions) {
        auto instructions = parsed.functionInstructions.at(funcName);
        ValidationResult funcResult = validateCallingConvention(instructions, abiSpec);
        
        result.errors.insert(result.errors.end(), funcResult.errors.begin(), funcResult.errors.end());
        result.warnings.insert(result.warnings.end(), funcResult.warnings.begin(), funcResult.warnings.end());
        
        if (!funcResult.isValid) {
            result.isValid = false;
        }
    }
    
    return result;
}

ValidationResult ASMValidator::validateInstructionCorrectness(
    const std::string& assembly,
    const std::string& targetName) {
    
    const ABISpecification* abiSpec = abiRegistry_->getABISpec(targetName);
    if (!abiSpec) {
        ValidationResult result;
        result.addError(ErrorSeverity::Critical, "Configuration", "No ABI specification for target");
        return result;
    }
    
    InstructionValidator validator(abiSpec);
    ParsedAssembly parsed = parseAssembly(assembly);
    ValidationResult result;
    
    for (const auto& line : parsed.lines) {
        if (!shouldSkipValidation(line)) {
            ValidationResult instrResult = validator.validateInstruction(line);
            result.errors.insert(result.errors.end(), instrResult.errors.begin(), instrResult.errors.end());
            result.warnings.insert(result.warnings.end(), instrResult.warnings.begin(), instrResult.warnings.end());
            
            if (!instrResult.isValid) {
                result.isValid = false;
            }
        }
    }
    
    return result;
}

ValidationResult ASMValidator::performValidation(
    const ParsedAssembly& assembly,
    const std::string& targetName,
    const ABISpecification* abiSpec) {
    
    ValidationResult result;
    
    // Create specialized validators
    InstructionValidator instrValidator(abiSpec);
    RegisterValidator regValidator(abiSpec);
    StackValidator stackValidator(abiSpec);
    
    // Validate each function
    for (const auto& funcName : assembly.functions) {
        auto instructions = assembly.functionInstructions.at(funcName);
        
        ValidationResult funcResult = validateFunction(funcName, instructions, abiSpec);
        result.errors.insert(result.errors.end(), funcResult.errors.begin(), funcResult.errors.end());
        result.warnings.insert(result.warnings.end(), funcResult.warnings.begin(), funcResult.warnings.end());
        
        if (!funcResult.isValid) {
            result.isValid = false;
        }
    }
    
    return result;
}

ASMValidator::ParsedAssembly ASMValidator::parseAssembly(const std::string& assembly) const {
    ParsedAssembly parsed;
    std::istringstream iss(assembly);
    std::string line;
    std::string currentFunction;
    bool inTextSection = false;
    
    while (std::getline(iss, line)) {
        parsed.lines.push_back(line);
        
        // Track sections
        if (line.find(".text") != std::string::npos) {
            inTextSection = true;
            parsed.textSection.push_back(line);
        } else if (line.find(".data") != std::string::npos || line.find(".rodata") != std::string::npos) {
            inTextSection = false;
            parsed.dataSection.push_back(line);
        }
        
        // Track functions
        if (inTextSection && line.find(':') != std::string::npos && 
            line.find('#') == std::string::npos && !line.empty() && std::isalpha(line[0])) {
            std::string funcName = line.substr(0, line.find(':'));
            funcName.erase(std::remove_if(funcName.begin(), funcName.end(), ::isspace), funcName.end());
            
            if (!funcName.empty()) {
                currentFunction = funcName;
                parsed.functions.push_back(funcName);
                parsed.functionInstructions[funcName] = std::vector<std::string>();
            }
        } else if (!currentFunction.empty() && inTextSection) {
            // Add instruction to current function
            parsed.functionInstructions[currentFunction].push_back(line);
        }
    }
    
    return parsed;
}

ValidationResult ASMValidator::validateFunction(
    const std::string& functionName,
    const std::vector<std::string>& instructions,
    const ABISpecification* abiSpec) {
    
    ValidationResult result;
    
    // Validate calling convention
    ValidationResult ccResult = validateCallingConvention(instructions, abiSpec);
    result.errors.insert(result.errors.end(), ccResult.errors.begin(), ccResult.errors.end());
    result.warnings.insert(result.warnings.end(), ccResult.warnings.begin(), ccResult.warnings.end());
    
    // Validate stack alignment
    ValidationResult stackResult = validateStackAlignment(instructions, abiSpec);
    result.errors.insert(result.errors.end(), stackResult.errors.begin(), stackResult.errors.end());
    result.warnings.insert(result.warnings.end(), stackResult.warnings.begin(), stackResult.warnings.end());
    
    // Validate register preservation
    ValidationResult regResult = validateRegisterPreservation(instructions, abiSpec);
    result.errors.insert(result.errors.end(), regResult.errors.begin(), regResult.errors.end());
    result.warnings.insert(result.warnings.end(), regResult.warnings.begin(), regResult.warnings.end());
    
    // Validate instruction set
    ValidationResult instrResult = validateInstructionSet(instructions, abiSpec);
    result.errors.insert(result.errors.end(), instrResult.errors.begin(), instrResult.errors.end());
    result.warnings.insert(result.warnings.end(), instrResult.warnings.begin(), instrResult.warnings.end());
    
    // Combine validity
    result.isValid = ccResult.isValid && stackResult.isValid && regResult.isValid && instrResult.isValid;
    
    return result;
}

ValidationResult ASMValidator::validateCallingConvention(
    const std::vector<std::string>& instructions,
    const ABISpecification* abiSpec) {
    
    RegisterValidator validator(abiSpec);
    return validator.validateRegisterUsage(instructions);
}

ValidationResult ASMValidator::validateStackAlignment(
    const std::vector<std::string>& instructions,
    const ABISpecification* abiSpec) {
    
    StackValidator validator(abiSpec);
    return validator.validateStackFrame(instructions);
}

ValidationResult ASMValidator::validateRegisterPreservation(
    const std::vector<std::string>& instructions,
    const ABISpecification* abiSpec) {
    
    RegisterValidator validator(abiSpec);
    return validator.validateRegisterUsage(instructions);
}

ValidationResult ASMValidator::validateInstructionSet(
    const std::vector<std::string>& instructions,
    const ABISpecification* abiSpec) {
    
    InstructionValidator validator(abiSpec);
    ValidationResult result;
    
    for (const auto& instr : instructions) {
        if (!shouldSkipValidation(instr)) {
            ValidationResult instrResult = validator.validateInstruction(instr);
            result.errors.insert(result.errors.end(), instrResult.errors.begin(), instrResult.errors.end());
            result.warnings.insert(result.warnings.end(), instrResult.warnings.begin(), instrResult.warnings.end());
            
            if (!instrResult.isValid) {
                result.isValid = false;
            }
        }
    }
    
    return result;
}

std::string ASMValidator::generateCacheKey(const std::string& assembly, const std::string& targetName) const {
    // Simple hash-based cache key generation
    std::hash<std::string> hasher;
    return std::to_string(hasher(assembly + targetName));
}

bool ASMValidator::shouldSkipValidation(const std::string& instruction) const {
    // Skip empty lines, comments, and labels
    if (instruction.empty()) return true;
    
    std::string trimmed = instruction;
    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
    
    return trimmed.empty() || trimmed[0] == '#' || trimmed[0] == '.';
}

SourceLocation ASMValidator::createSourceLocation(const std::string& line, size_t lineNumber) const {
    return SourceLocation("", lineNumber, 0, line);
}

} // namespace validation
} // namespace codegen