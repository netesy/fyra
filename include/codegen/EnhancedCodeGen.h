#pragma once

#include "codegen/CodeGen.h"
#include "codegen/validation/ASMValidator.h"
#include "codegen/objectgen/ObjectFileGenerator.h"
#include <memory>

namespace codegen {

// Enhanced CodeGen with integrated validation and object generation
class EnhancedCodeGen : public CodeGen {
public:
    EnhancedCodeGen(ir::Module& module, std::unique_ptr<target::TargetInfo> targetInfo);
    
    // Compilation result with validation and object generation
    struct CompilationResult {
        bool success = false;
        std::string assemblyPath;
        std::string objectPath;
        validation::ValidationResult validation;
        objectgen::ObjectGenResult objGen;
        std::string targetName;
        double totalTimeMs = 0.0;
        
        // Helper methods
        bool hasValidationErrors() const {
            return validation.hasCriticalErrors();
        }
        
        bool hasObjectGenerationErrors() const {
            return !objGen.success;
        }
        
        std::vector<std::string> getAllErrors() const {
            std::vector<std::string> errors;
            
            // Add validation errors
            for (const auto& error : validation.errors) {
                errors.push_back("[Validation] " + error.message);
            }
            
            // Add object generation errors
            if (!objGen.success) {
                errors.push_back("[ObjectGen] " + objGen.errorOutput);
            }
            
            return errors;
        }
        
        std::vector<std::string> getAllWarnings() const {
            std::vector<std::string> warnings;
            
            // Add validation warnings
            for (const auto& warning : validation.warnings) {
                warnings.push_back("[Validation] " + warning.message);
            }
            
            // Add object generation warnings
            for (const auto& warning : objGen.warnings) {
                warnings.push_back("[ObjectGen] " + warning);
            }
            
            return warnings;
        }
    };
    
    // Main compilation interface
    CompilationResult compileToObject(
        const std::string& outputPrefix,
        bool validateASM = true,
        bool generateObject = true,
        bool generateExecutable = false
    );
    
    CompilationResult compileToAssembly(
        const std::string& outputPath,
        bool validateASM = true
    );
    
    // Batch compilation for multiple targets
    std::map<std::string, CompilationResult> compileForAllTargets(
        const std::string& outputPrefix,
        bool validateASM = true,
        bool generateObjects = true
    );
    
    // Configuration
    void setValidationLevel(validation::ValidationLevel level);
    void enableParallelObjectGeneration(bool enable);
    void enableVerboseOutput(bool enable);
    void enableValidationCaching(bool enable);
    
    // Access to subsystems
    validation::ASMValidator* getValidator() { return validator_.get(); }
    objectgen::ObjectFileGenerator* getObjectGenerator() { return objectGenerator_.get(); }
    
    // Target-specific compilation
    CompilationResult compileForTarget(
        const std::string& targetName,
        const std::string& outputPrefix,
        bool validateASM = true,
        bool generateObject = true
    );

private:
    std::unique_ptr<validation::ASMValidator> validator_;
    std::unique_ptr<objectgen::ObjectFileGenerator> objectGenerator_;
    
    bool verboseOutput_ = false;
    bool validationEnabled_ = true;
    bool objectGenerationEnabled_ = true;
    
    // Helper methods
    std::string writeAssemblyToFile(const std::string& assembly, const std::string& path);
    std::string generateOutputPath(const std::string& prefix, const std::string& suffix, const std::string& extension);
    void logVerbose(const std::string& message) const;
    
    // Timing utilities
    class CompilationTimer {
    public:
        CompilationTimer();
        double getElapsedMs() const;
        
    private:
        std::chrono::high_resolution_clock::time_point start_;
    };
};

// Factory for creating enhanced code generators
class EnhancedCodeGenFactory {
public:
    static std::unique_ptr<EnhancedCodeGen> create(
        ir::Module& module,
        const std::string& targetName
    );
    
    static std::unique_ptr<EnhancedCodeGen> createWithTargetInfo(
        ir::Module& module,
        std::unique_ptr<target::TargetInfo> targetInfo
    );
    
    // Target info factory methods
    static std::unique_ptr<target::TargetInfo> createTargetInfo(const std::string& targetName);
    
private:
    EnhancedCodeGenFactory() = default;
};

// Pipeline for comprehensive code generation workflow
class CodeGenPipeline {
public:
    CodeGenPipeline();
    
    // Pipeline configuration
    struct PipelineConfig {
        bool enableValidation = true;
        bool enableObjectGeneration = true;
        bool enableParallelProcessing = false;
        bool enableVerboseOutput = false;
        bool enableOptimizations = true;
        bool cleanupIntermediateFiles = true;
        
        validation::ValidationLevel validationLevel = validation::ValidationLevel::Standard;
        std::vector<std::string> targetPlatforms;
        std::string outputDirectory = ".";
        std::string outputPrefix = "output";
    };
    
    // Pipeline execution
    struct PipelineResult {
        bool success = false;
        std::map<std::string, EnhancedCodeGen::CompilationResult> targetResults;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        double totalTimeMs = 0.0;
        
        // Summary methods
        size_t getSuccessfulTargets() const {
            size_t count = 0;
            for (const auto& [target, result] : targetResults) {
                if (result.success) count++;
            }
            return count;
        }
        
        size_t getFailedTargets() const {
            return targetResults.size() - getSuccessfulTargets();
        }
        
        bool hasAnyErrors() const {
            if (!errors.empty()) return true;
            for (const auto& [target, result] : targetResults) {
                if (!result.getAllErrors().empty()) return true;
            }
            return false;
        }
    };
    
    // Execute pipeline
    PipelineResult execute(ir::Module& module, const PipelineConfig& config);
    
    // Individual pipeline stages
    PipelineResult executeForTarget(ir::Module& module, const std::string& targetName, const PipelineConfig& config);
    
    // Configuration
    void setConfig(const PipelineConfig& config) { config_ = config; }
    const PipelineConfig& getConfig() const { return config_; }

private:
    PipelineConfig config_;
    
    // Pipeline stages
    bool validateConfiguration(const PipelineConfig& config, std::vector<std::string>& errors);
    void setupOutputDirectories(const PipelineConfig& config);
    void cleanupIntermediateFiles(const PipelineConfig& config, const PipelineResult& result);
    
    // Reporting
    void generatePipelineReport(const PipelineResult& result, const std::string& reportPath);
    void logPipelineProgress(const std::string& stage, const std::string& message);
};

} // namespace codegen