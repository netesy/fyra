#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>

namespace codegen {
namespace validation {

// Forward declarations
class TargetABIRegistry;
class ASMValidator;

// Error severity levels
enum class ErrorSeverity {
    Critical,    // Prevents object generation
    Error,       // ABI violation but might work
    Warning,     // Suboptimal but valid
    Info         // Informational message
};

// Source location for errors
struct SourceLocation {
    std::string filename;
    size_t lineNumber;
    size_t columnNumber;
    std::string lineContent;
    
    SourceLocation() : lineNumber(0), columnNumber(0) {}
    SourceLocation(const std::string& file, size_t line, size_t col, const std::string& content)
        : filename(file), lineNumber(line), columnNumber(col), lineContent(content) {}
};

// Validation error details
struct ValidationError {
    ErrorSeverity severity;
    std::string category;
    std::string message;
    SourceLocation location;
    std::vector<std::string> suggestions;
    std::string expectedValue;
    std::string actualValue;
    
    ValidationError(ErrorSeverity sev, const std::string& cat, const std::string& msg)
        : severity(sev), category(cat), message(msg) {}
};

// Validation warning
struct ValidationWarning {
    std::string category;
    std::string message;
    SourceLocation location;
    std::vector<std::string> suggestions;
    
    ValidationWarning(const std::string& cat, const std::string& msg)
        : category(cat), message(msg) {}
};

// Validation metrics for analysis
struct ValidationMetrics {
    size_t totalInstructions = 0;
    size_t totalFunctions = 0;
    size_t registerUsageViolations = 0;
    size_t stackAlignmentIssues = 0;
    size_t abiViolations = 0;
    size_t performanceWarnings = 0;
    double validationTimeMs = 0.0;
    
    void reset() {
        totalInstructions = 0;
        totalFunctions = 0;
        registerUsageViolations = 0;
        stackAlignmentIssues = 0;
        abiViolations = 0;
        performanceWarnings = 0;
        validationTimeMs = 0.0;
    }
};

// Complete validation result
struct ValidationResult {
    bool isValid = true;
    std::vector<ValidationError> errors;
    std::vector<ValidationWarning> warnings;
    ValidationMetrics metrics;
    std::string targetName;
    std::string assemblyPath;
    
    // Helper methods
    bool hasCriticalErrors() const {
        for (const auto& error : errors) {
            if (error.severity == ErrorSeverity::Critical) {
                return true;
            }
        }
        return false;
    }
    
    size_t getErrorCount(ErrorSeverity severity) const {
        size_t count = 0;
        for (const auto& error : errors) {
            if (error.severity == severity) {
                count++;
            }
        }
        return count;
    }
    
    void addError(ErrorSeverity severity, const std::string& category, const std::string& message) {
        errors.emplace_back(severity, category, message);
        if (severity == ErrorSeverity::Critical || severity == ErrorSeverity::Error) {
            isValid = false;
        }
    }
    
    void addWarning(const std::string& category, const std::string& message) {
        warnings.emplace_back(category, message);
    }
};

// Stack order for parameter passing
enum class StackOrder {
    LeftToRight,    // Parameters pushed left to right
    RightToLeft     // Parameters pushed right to left
};

// Register classes for validation
enum class RegisterClass {
    Integer,
    Float,
    Vector,
    Special        // Stack pointer, frame pointer, etc.
};

// Calling convention specification
struct CallingConvention {
    std::string name;
    std::vector<std::string> integerArgRegs;
    std::vector<std::string> floatArgRegs;
    std::map<std::string, std::string> returnRegs;  // type -> register
    std::set<std::string> callerSaved;
    std::set<std::string> calleeSaved;
    StackOrder stackParameterOrder;
    uint32_t shadowSpace;                           // Bytes of shadow space required
    uint32_t stackAlignment;                        // Required stack alignment
    bool hasRedZone;                               // Does this ABI have a red zone?
    uint32_t redZoneSize;                          // Size of red zone in bytes
    
    CallingConvention() : stackParameterOrder(StackOrder::RightToLeft), 
                         shadowSpace(0), stackAlignment(16), hasRedZone(false), redZoneSize(0) {}
};

// Register constraints and validation rules
struct RegisterConstraints {
    std::map<RegisterClass, std::vector<std::string>> availableRegisters;
    std::set<std::string> reservedRegisters;
    std::map<std::string, std::vector<std::string>> aliasMap;    // reg -> aliases
    std::map<std::string, RegisterClass> classMap;              // reg -> class
    
    bool validateUsage(const std::string& reg, const std::string& context) const {
        // Check if register is reserved
        if (reservedRegisters.count(reg)) {
            return false;
        }
        
        // Additional context-specific validation can be added here
        return true;
    }
    
    RegisterClass getRegisterClass(const std::string& reg) const {
        auto it = classMap.find(reg);
        return (it != classMap.end()) ? it->second : RegisterClass::Integer;
    }
};

} // namespace validation
} // namespace codegen