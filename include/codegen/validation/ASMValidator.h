#pragma once

#include "TargetABIRegistry.h"
#include "ValidationTypes.h"
#include "target/core/TargetInfo.h"
#include <string>
#include <vector>
#include <memory>
#include <chrono>

namespace codegen {
namespace validation {

// Validation levels
enum class ValidationLevel {
    Basic,      // Essential ABI and instruction validation
    Standard,   // Standard + memory layout validation
    Strict,     // Standard + platform conventions
    Pedantic    // All validations + performance warnings
};

// Main ASM validator class
class ASMValidator {
public:
    ASMValidator();
    explicit ASMValidator(std::shared_ptr<TargetABIRegistry> registry);
    
    // Main validation interface
    ValidationResult validateAssembly(
        const std::string& assembly,
        const std::string& targetName,
        const codegen::target::TargetInfo* targetInfo = nullptr
    );
    
    ValidationResult validateAssemblyFile(
        const std::string& assemblyPath,
        const std::string& targetName,
        const codegen::target::TargetInfo* targetInfo = nullptr
    );
    
    // Validation categories
    ValidationResult validateABICompliance(
        const std::string& assembly,
        const std::string& targetName
    );
    
    ValidationResult validateInstructionCorrectness(
        const std::string& assembly,
        const std::string& targetName
    );
    
    ValidationResult validateMemoryLayout(
        const std::string& assembly,
        const std::string& targetName
    );
    
    ValidationResult validatePlatformConventions(
        const std::string& assembly,
        const std::string& targetName
    );
    
    ValidationResult validateArchitectureConstraints(
        const std::string& assembly,
        const std::string& targetName
    );
    
    // Configuration
    void setValidationLevel(ValidationLevel level) { validationLevel_ = level; }
    ValidationLevel getValidationLevel() const { return validationLevel_; }
    
    void enableParallelValidation(bool enable) { parallelValidation_ = enable; }
    bool isParallelValidationEnabled() const { return parallelValidation_; }
    
    void enableDetailedReporting(bool enable) { detailedReporting_ = enable; }
    bool isDetailedReportingEnabled() const { return detailedReporting_; }
    
    // Performance optimization
    void enableValidationCaching(bool enable) { validationCaching_ = enable; }
    bool isValidationCachingEnabled() const { return validationCaching_; }
    
    void clearValidationCache() { validationCache_.clear(); }
    
    // Statistics and metrics
    ValidationMetrics getLastValidationMetrics() const { return lastMetrics_; }
    void resetMetrics() { lastMetrics_.reset(); }

private:
    std::shared_ptr<TargetABIRegistry> abiRegistry_;
    std::unique_ptr<InstructionValidator> instructionValidator_;
    std::unique_ptr<RegisterValidator> registerValidator_;
    std::unique_ptr<StackValidator> stackValidator_;
    
    ValidationLevel validationLevel_ = ValidationLevel::Standard;
    bool parallelValidation_ = true;
    bool detailedReporting_ = true;
    bool validationCaching_ = true;
    
    ValidationMetrics lastMetrics_;
    
    // Validation cache for performance
    std::map<std::string, ValidationResult> validationCache_;
    
    // Assembly parsing and analysis
    struct ParsedAssembly {
        std::vector<std::string> lines;
        std::vector<std::string> functions;
        std::vector<std::string> dataSection;
        std::vector<std::string> textSection;
        std::map<std::string, std::vector<std::string>> functionInstructions;
    };
    
    ParsedAssembly parseAssembly(const std::string& assembly) const;
    std::vector<std::string> extractFunctions(const std::vector<std::string>& lines) const;
    std::vector<std::string> extractFunctionInstructions(
        const std::vector<std::string>& lines, 
        const std::string& functionName
    ) const;
    
    // Validation execution
    ValidationResult performValidation(
        const ParsedAssembly& assembly,
        const std::string& targetName,
        const ABISpecification* abiSpec
    );
    
    ValidationResult validateFunction(
        const std::string& functionName,
        const std::vector<std::string>& instructions,
        const ABISpecification* abiSpec
    );
    
    // Specific validation methods
    ValidationResult validateCallingConvention(
        const std::vector<std::string>& instructions,
        const ABISpecification* abiSpec
    );
    
    ValidationResult validateStackAlignment(
        const std::vector<std::string>& instructions,
        const ABISpecification* abiSpec
    );
    
    ValidationResult validateRegisterPreservation(
        const std::vector<std::string>& instructions,
        const ABISpecification* abiSpec
    );
    
    ValidationResult validateInstructionSet(
        const std::vector<std::string>& instructions,
        const ABISpecification* abiSpec
    );
    
    // Error reporting helpers
    void addValidationError(
        ValidationResult& result,
        ErrorSeverity severity,
        const std::string& category,
        const std::string& message,
        const SourceLocation& location = SourceLocation()
    ) const;
    
    void addValidationWarning(
        ValidationResult& result,
        const std::string& category,
        const std::string& message,
        const SourceLocation& location = SourceLocation()
    ) const;
    
    // Performance measurement
    class ValidationTimer {
    public:
        ValidationTimer() : start_(std::chrono::high_resolution_clock::now()) {}
        
        double getElapsedMs() const {
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
            return duration.count() / 1000.0;
        }
        
    private:
        std::chrono::high_resolution_clock::time_point start_;
    };
    
    // Utility methods
    std::string generateCacheKey(const std::string& assembly, const std::string& targetName) const;
    bool shouldSkipValidation(const std::string& instruction) const;
    SourceLocation createSourceLocation(const std::string& line, size_t lineNumber) const;
};

// Validation framework for comprehensive testing
class ValidationFramework {
public:
    ValidationFramework();
    
    // Test execution
    struct ValidationTestResult {
        std::string testName;
        bool passed;
        ValidationResult validationResult;
        std::string errorMessage;
        double executionTimeMs;
    };
    
    // Run individual tests
    ValidationTestResult runABIComplianceTest(
        const std::string& testName,
        const std::string& assembly,
        const std::string& targetName
    );
    
    ValidationTestResult runInstructionCorrectnessTest(
        const std::string& testName,
        const std::string& assembly,
        const std::string& targetName
    );
    
    ValidationTestResult runCrossPlatformTest(
        const std::string& testName,
        const std::string& assembly,
        const std::vector<std::string>& targetNames
    );
    
    // Run test suites
    std::vector<ValidationTestResult> runABITestSuite(const std::string& targetName);
    std::vector<ValidationTestResult> runInstructionTestSuite(const std::string& targetName);
    std::vector<ValidationTestResult> runCrossPlatformTestSuite();
    std::vector<ValidationTestResult> runPerformanceTestSuite();
    
    // Test configuration
    void setTestTimeout(int timeoutMs) { testTimeoutMs_ = timeoutMs; }
    void enableVerboseOutput(bool enable) { verboseOutput_ = enable; }
    void setExpectedErrorRate(double rate) { expectedErrorRate_ = rate; }
    
    // Test reporting
    void generateTestReport(const std::vector<ValidationTestResult>& results, 
                          const std::string& reportPath) const;
    void printTestSummary(const std::vector<ValidationTestResult>& results) const;

private:
    std::unique_ptr<ASMValidator> validator_;
    int testTimeoutMs_ = 5000;  // 5 second timeout per test
    bool verboseOutput_ = false;
    double expectedErrorRate_ = 0.05;  // 5% expected error rate
    
    // Test data generation
    std::string generateABITestAssembly(const std::string& targetName, const std::string& testCase) const;
    std::string generateInstructionTestAssembly(const std::string& targetName, const std::string& instruction) const;
    std::string generateErrorTestAssembly(const std::string& targetName, const std::string& errorType) const;
    
    // Test execution helpers
    ValidationTestResult executeTest(
        const std::string& testName,
        std::function<ValidationResult()> testFunction
    );
    
    bool isTestExpectedToFail(const std::string& testName) const;
    void logTestExecution(const std::string& testName, bool started) const;
};

} // namespace validation
} // namespace codegen