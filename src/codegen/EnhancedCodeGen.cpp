#include "codegen/EnhancedCodeGen.h"
#include "codegen/target/SystemV_x64.h"
#include "codegen/target/Windows_x64.h"
#include "codegen/target/Windows_AArch64.h"
#include "codegen/target/MacOS_x64.h"
#include "codegen/target/MacOS_AArch64.h"
#include "codegen/target/AArch64.h"
#include "codegen/target/Wasm32.h"
#include "codegen/target/RiscV64.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <iostream>

namespace codegen {

// EnhancedCodeGen implementation
EnhancedCodeGen::EnhancedCodeGen(ir::Module& module, std::unique_ptr<target::TargetInfo> targetInfo)
    : CodeGen(module, std::move(targetInfo)) {
    validator_ = std::make_unique<validation::ASMValidator>();
    objectGenerator_ = std::make_unique<objectgen::ObjectFileGenerator>();
}

EnhancedCodeGen::CompilationResult EnhancedCodeGen::compileToObject(
    const std::string& outputPrefix,
    bool validateASM,
    bool generateObject,
    bool generateExecutable) {
    
    CompilationTimer timer;
    CompilationResult result;
    result.targetName = getTargetInfo()->getName();
    
    // Generate assembly
    std::ostringstream assemblyStream;
    setStream(&assemblyStream);
    emit(generateExecutable);
    std::string assembly = assemblyStream.str();
    
    // Write assembly to file
    std::string asmPath = generateOutputPath(outputPrefix, "", ".s");
    result.assemblyPath = writeAssemblyToFile(assembly, asmPath);
    
    if (result.assemblyPath.empty()) {
        result.success = false;
        return result;
    }
    
    logVerbose("Generated assembly: " + result.assemblyPath);
    
    // Validate assembly if requested
    if (validateASM && validationEnabled_) {
        logVerbose("Validating assembly...");
        result.validation = validator_->validateAssembly(assembly, result.targetName, getTargetInfo());
        
        if (result.validation.hasCriticalErrors()) {
            logVerbose("Assembly validation failed with critical errors");
            result.success = false;
            return result;
        }
        
        if (!result.validation.errors.empty()) {
            logVerbose("Assembly validation completed with " + 
                      std::to_string(result.validation.errors.size()) + " errors");
        }
    }
    
    // Generate object file if requested
    if (generateObject && objectGenerationEnabled_) {
        logVerbose("Generating object file...");
        std::string objPath = generateOutputPath(outputPrefix, "", 
                                                getTargetInfo()->getObjectFileExtension());
        
        result.objGen = objectGenerator_->generateObject(result.assemblyPath, objPath, result.targetName);
        result.objectPath = result.objGen.objectPath;
        
        if (!result.objGen.success) {
            logVerbose("Object generation failed: " + result.objGen.errorOutput);
            result.success = false;
            return result;
        }
        
        logVerbose("Generated object: " + result.objectPath);
    }
    
    result.success = true;
    result.totalTimeMs = timer.getElapsedMs();
    
    logVerbose("Compilation completed in " + std::to_string(result.totalTimeMs) + "ms");
    
    return result;
}

EnhancedCodeGen::CompilationResult EnhancedCodeGen::compileToAssembly(
    const std::string& outputPath,
    bool validateASM) {
    
    CompilationTimer timer;
    CompilationResult result;
    result.targetName = getTargetInfo()->getName();
    
    // Generate assembly
    std::ostringstream assemblyStream;
    setStream(&assemblyStream);
    emit();
    std::string assembly = assemblyStream.str();
    
    // Write assembly to file
    result.assemblyPath = writeAssemblyToFile(assembly, outputPath);
    
    if (result.assemblyPath.empty()) {
        result.success = false;
        return result;
    }
    
    // Validate assembly if requested
    if (validateASM && validationEnabled_) {
        result.validation = validator_->validateAssembly(assembly, result.targetName, getTargetInfo());
        
        if (result.validation.hasCriticalErrors()) {
            result.success = false;
            return result;
        }
    }
    
    result.success = true;
    result.totalTimeMs = timer.getElapsedMs();
    
    return result;
}

std::map<std::string, EnhancedCodeGen::CompilationResult> EnhancedCodeGen::compileForAllTargets(
    const std::string& outputPrefix,
    bool validateASM,
    bool generateObjects) {
    
    std::map<std::string, CompilationResult> results;
    
    // Define supported targets
    std::vector<std::string> targets = {"linux", "windows", "windows-amd64", "windows-arm64", "aarch64", "wasm32", "riscv64"};
    
    for (const auto& target : targets) {
        try {
            CompilationResult result = compileForTarget(target, outputPrefix + "_" + target, 
                                                      validateASM, generateObjects);
            results[target] = result;
        } catch (const std::exception& e) {
            CompilationResult errorResult;
            errorResult.success = false;
            errorResult.targetName = target;
            validation::ValidationError error(validation::ErrorSeverity::Critical, "Compilation", 
                                            "Exception during compilation: " + std::string(e.what()));
            errorResult.validation.errors.push_back(error);
            results[target] = errorResult;
        }
    }
    
    return results;
}

EnhancedCodeGen::CompilationResult EnhancedCodeGen::compileForTarget(
    const std::string& targetName,
    const std::string& outputPrefix,
    bool validateASM,
    bool generateObject) {
    
    // Create target-specific code generator
    auto targetInfo = EnhancedCodeGenFactory::createTargetInfo(targetName);
    if (!targetInfo) {
        CompilationResult result;
        result.success = false;
        result.targetName = targetName;
        validation::ValidationError error(validation::ErrorSeverity::Critical, "Configuration", 
                                        "Unsupported target: " + targetName);
        result.validation.errors.push_back(error);
        return result;
    }
    
    EnhancedCodeGen targetCodeGen(module, std::move(targetInfo));
    targetCodeGen.setValidationLevel(validator_->getValidationLevel());
    targetCodeGen.enableVerboseOutput(verboseOutput_);
    
    return targetCodeGen.compileToObject(outputPrefix, validateASM, generateObject);
}

void EnhancedCodeGen::setValidationLevel(validation::ValidationLevel level) {
    if (validator_) {
        validator_->setValidationLevel(level);
    }
}

void EnhancedCodeGen::enableParallelObjectGeneration(bool enable) {
    if (objectGenerator_) {
        objectGenerator_->setParallelGeneration(enable);
    }
}

void EnhancedCodeGen::enableVerboseOutput(bool enable) {
    verboseOutput_ = enable;
    if (validator_) {
        validator_->enableDetailedReporting(enable);
    }
    if (objectGenerator_) {
        objectGenerator_->setVerboseOutput(enable);
    }
}

void EnhancedCodeGen::enableValidationCaching(bool enable) {
    if (validator_) {
        validator_->enableValidationCaching(enable);
    }
}

std::string EnhancedCodeGen::writeAssemblyToFile(const std::string& assembly, const std::string& path) {
    try {
        // Create directory if needed
        std::filesystem::path filePath(path);
        if (filePath.has_parent_path() && !filePath.parent_path().empty()) {
            std::filesystem::create_directories(filePath.parent_path());
        }
        
        std::ofstream file(path);
        if (!file.is_open()) {
            logVerbose("Failed to open file for writing: " + path);
            return "";
        }
        
        file << assembly;
        file.close();
        
        return path;
    } catch (const std::exception& e) {
        logVerbose("Exception writing assembly file: " + std::string(e.what()));
        return "";
    }
}

std::string EnhancedCodeGen::generateOutputPath(const std::string& prefix, 
                                              const std::string& suffix, 
                                              const std::string& extension) {
    return prefix + suffix + extension;
}

void EnhancedCodeGen::logVerbose(const std::string& message) const {
    if (verboseOutput_) {
        std::cout << "[EnhancedCodeGen] " << message << std::endl;
    }
}

// CompilationTimer implementation
EnhancedCodeGen::CompilationTimer::CompilationTimer() 
    : start_(std::chrono::high_resolution_clock::now()) {}

double EnhancedCodeGen::CompilationTimer::getElapsedMs() const {
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
    return duration.count() / 1000.0;
}

// EnhancedCodeGenFactory implementation
std::unique_ptr<EnhancedCodeGen> EnhancedCodeGenFactory::create(
    ir::Module& module,
    const std::string& targetName) {
    
    auto targetInfo = createTargetInfo(targetName);
    if (!targetInfo) {
        return nullptr;
    }
    
    return std::make_unique<EnhancedCodeGen>(module, std::move(targetInfo));
}

std::unique_ptr<EnhancedCodeGen> EnhancedCodeGenFactory::createWithTargetInfo(
    ir::Module& module,
    std::unique_ptr<target::TargetInfo> targetInfo) {
    
    return std::make_unique<EnhancedCodeGen>(module, std::move(targetInfo));
}

std::unique_ptr<target::TargetInfo> EnhancedCodeGenFactory::createTargetInfo(const std::string& targetName) {
    std::string normalized = targetName;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    
    if (normalized == "linux" || normalized == "systemv" || normalized == "x86_64-unknown-linux-gnu") {
        return std::make_unique<target::SystemV_x64>();
    } else if (normalized == "windows" || normalized == "win64" || normalized == "x86_64-pc-windows-msvc" ||
               normalized == "windows-amd64" || normalized == "amd64-windows" || normalized == "windows-x64") {
        return std::make_unique<target::Windows_x64>();
    } else if (normalized == "windows-arm64" || normalized == "arm64-windows") {
        return std::make_unique<target::Windows_AArch64>();
    } else if (normalized == "aarch64" || normalized == "aarch64-unknown-linux-gnu") {
        return std::make_unique<target::AArch64>();
    } else if (normalized == "wasm32" || normalized == "wasm32-unknown-unknown") {
        return std::make_unique<target::Wasm32>();
    } else if (normalized == "riscv64" || normalized == "riscv64-unknown-linux-gnu") {
        return std::make_unique<target::RiscV64>();
    } else if (normalized == "macos" || normalized == "darwin" || normalized == "apple-darwin" || normalized == "macos-amd64") {
        return std::make_unique<target::MacOS_x64>();
    } else if (normalized == "macos-aarch64" || normalized == "macos-arm64") {
        return std::make_unique<target::MacOS_AArch64>();
    }
    
    return nullptr;
}

// CodeGenPipeline implementation
CodeGenPipeline::CodeGenPipeline() {}

CodeGenPipeline::PipelineResult CodeGenPipeline::execute(ir::Module& module, const PipelineConfig& config) {
    auto startTime = std::chrono::high_resolution_clock::now();
    PipelineResult result;
    
    // Validate configuration
    if (!validateConfiguration(config, result.errors)) {
        result.success = false;
        return result;
    }
    
    // Setup output directories
    setupOutputDirectories(config);
    
    // Determine targets to process
    std::vector<std::string> targets = config.targetPlatforms;
    if (targets.empty()) {
        targets = {"linux", "windows", "aarch64", "wasm32", "riscv64"};
    }
    
    logPipelineProgress("Pipeline", "Processing " + std::to_string(targets.size()) + " targets");
    
    // Process each target
    for (const auto& target : targets) {
        logPipelineProgress("Target", "Processing " + target);
        
        try {
            auto targetResult = executeForTarget(module, target, config);
            result.targetResults[target] = targetResult.targetResults.at(target);
        } catch (const std::exception& e) {
            result.errors.push_back("Error processing target " + target + ": " + e.what());
            
            EnhancedCodeGen::CompilationResult errorResult;
            errorResult.success = false;
            errorResult.targetName = target;
            result.targetResults[target] = errorResult;
        }
    }
    
    // Calculate overall success
    result.success = result.getFailedTargets() == 0 && result.errors.empty();
    
    // Cleanup if requested
    if (config.cleanupIntermediateFiles) {
        cleanupIntermediateFiles(config, result);
    }
    
    // Calculate total time
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    result.totalTimeMs = duration.count() / 1000.0;
    
    logPipelineProgress("Pipeline", "Completed in " + std::to_string(result.totalTimeMs) + "ms");
    
    return result;
}

CodeGenPipeline::PipelineResult CodeGenPipeline::executeForTarget(
    ir::Module& module, 
    const std::string& targetName, 
    const PipelineConfig& config) {
    
    PipelineResult result;
    
    auto codeGen = EnhancedCodeGenFactory::create(module, targetName);
    if (!codeGen) {
        result.success = false;
        result.errors.push_back("Failed to create code generator for target: " + targetName);
        return result;
    }
    
    // Configure code generator
    codeGen->setValidationLevel(config.validationLevel);
    codeGen->enableVerboseOutput(config.enableVerboseOutput);
    codeGen->enableParallelObjectGeneration(config.enableParallelProcessing);
    
    // Generate output path
    std::string outputPath = config.outputDirectory + "/" + config.outputPrefix + "_" + targetName;
    
    // Compile
    auto compilationResult = codeGen->compileToObject(outputPath, 
                                                     config.enableValidation, 
                                                     config.enableObjectGeneration);
    
    result.targetResults[targetName] = compilationResult;
    result.success = compilationResult.success;
    
    return result;
}

bool CodeGenPipeline::validateConfiguration(const PipelineConfig& config, std::vector<std::string>& errors) {
    bool valid = true;
    
    // Check output directory
    if (!std::filesystem::exists(config.outputDirectory)) {
        try {
            std::filesystem::create_directories(config.outputDirectory);
        } catch (const std::exception& e) {
            errors.push_back("Cannot create output directory: " + config.outputDirectory);
            valid = false;
        }
    }
    
    // Validate target platforms
    std::vector<std::string> supportedTargets = {"linux", "windows", "windows-amd64", "windows-arm64", "aarch64", "wasm32", "riscv64"};
    for (const auto& target : config.targetPlatforms) {
        if (std::find(supportedTargets.begin(), supportedTargets.end(), target) == supportedTargets.end()) {
            errors.push_back("Unsupported target platform: " + target);
            valid = false;
        }
    }
    
    return valid;
}

void CodeGenPipeline::setupOutputDirectories(const PipelineConfig& config) {
    try {
        std::filesystem::create_directories(config.outputDirectory);
    } catch (const std::exception& e) {
        // Error already handled in validation
    }
}

void CodeGenPipeline::cleanupIntermediateFiles(const PipelineConfig& config, const PipelineResult& result) {
    for (const auto& [target, targetResult] : result.targetResults) {
        if (!targetResult.assemblyPath.empty() && std::filesystem::exists(targetResult.assemblyPath)) {
            try {
                std::filesystem::remove(targetResult.assemblyPath);
            } catch (const std::exception&) {
                // Ignore cleanup errors
            }
        }
    }
}

void CodeGenPipeline::logPipelineProgress(const std::string& stage, const std::string& message) {
    std::cout << "[Pipeline:" << stage << "] " << message << std::endl;
}

} // namespace codegen