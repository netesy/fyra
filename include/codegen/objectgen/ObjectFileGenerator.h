#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include "target/artifact/executable/assembler.hh"

namespace codegen {
namespace objectgen {

// Object generation result
struct ObjectGenResult {
    bool success = false;
    std::string objectPath;
    std::string assemblerOutput;
    std::string errorOutput;
    std::vector<std::string> warnings;
    int exitCode = 0;
    double generationTimeMs = 0.0;
    
    // Data for execgen
    std::vector<uint8_t> textSection;
    std::vector<uint8_t> dataSection;
    std::vector<uint8_t> rodataSection;
    std::unordered_map<std::string, SymbolEntry> symbols;
    std::vector<RelocationEntry> relocations;
    uint64_t bssSize = 0;

    void addWarning(const std::string& warning) {
        warnings.push_back(warning);
    }
    
    bool hasWarnings() const {
        return !warnings.empty();
    }
};

// Object file validation result
struct ObjectValidationResult {
    bool isValid = true;
    std::string format;         // ELF, COFF, Mach-O, WASM
    std::string architecture;   // x86_64, aarch64, etc.
    std::vector<std::string> sections;
    std::vector<std::string> symbols;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    void addError(const std::string& error) {
        errors.push_back(error);
        isValid = false;
    }
    
    void addWarning(const std::string& warning) {
        warnings.push_back(warning);
    }
};

// Platform-specific assembler information
struct AssemblerInfo {
    std::string name;           // "GNU as", "ML64", etc.
    std::string executable;     // Path to assembler executable
    std::vector<std::string> defaultArgs;
    std::string outputFlag;     // "-o", "/Fo", etc.
    std::string formatFlag;     // Format-specific flags
    bool isAvailable = false;
    
    AssemblerInfo(const std::string& n, const std::string& exe)
        : name(n), executable(exe), outputFlag("-o") {}
};

// Abstract base class for platform-specific object generators
class PlatformObjectGenerator {
public:
    virtual ~PlatformObjectGenerator() = default;
    
    // Main generation interface
    virtual ObjectGenResult generate(const std::string& asmPath, 
                                   const std::string& objPath) = 0;
    
    // Validation interface
    virtual ObjectValidationResult validateObject(const std::string& objPath) = 0;
    
    // Platform information
    virtual std::string getPlatformName() const = 0;
    virtual std::string getObjectFormat() const = 0;
    virtual std::string getDefaultExtension() const = 0;
    
    // Tool management
    virtual bool isToolchainAvailable() const = 0;
    virtual std::vector<std::string> getRequiredTools() const = 0;
    virtual AssemblerInfo getAssemblerInfo() const = 0;
    
protected:
    // Common utilities
    bool fileExists(const std::string& path) const;
    bool executeCommand(const std::string& command, std::string& output, std::string& errorOutput, int& exitCode) const;
    std::string getToolPath(const std::string& toolName) const;
    bool createDirectoryIfNeeded(const std::string& path) const;
};

// Main object file generator
class ObjectFileGenerator {
public:
    ObjectFileGenerator();
    ~ObjectFileGenerator() = default;
    
    // Main generation interface
    ObjectGenResult generateObject(
        const std::string& assemblyPath,
        const std::string& outputPath,
        const std::string& targetName
    );
    
    // Batch generation for multiple targets
    std::map<std::string, ObjectGenResult> generateForAllTargets(
        const std::string& assemblyPath,
        const std::string& outputPrefix
    );
    
    // Validation
    ObjectValidationResult validateGeneratedObject(
        const std::string& objectPath,
        const std::string& targetName
    );
    
    // Configuration
    void setVerboseOutput(bool verbose) { verboseOutput_ = verbose; }
    bool isVerboseOutput() const { return verboseOutput_; }
    
    void setCleanupTemporaryFiles(bool cleanup) { cleanupTempFiles_ = cleanup; }
    bool shouldCleanupTemporaryFiles() const { return cleanupTempFiles_; }
    
    void setParallelGeneration(bool parallel) { parallelGeneration_ = parallel; }
    bool isParallelGenerationEnabled() const { return parallelGeneration_; }
    
    // Platform management
    std::vector<std::string> getSupportedTargets() const;
    bool isTargetSupported(const std::string& targetName) const;
    void registerPlatformGenerator(const std::string& targetName, 
                                 std::unique_ptr<PlatformObjectGenerator> generator);
    
    // Tool availability
    struct ToolchainStatus {
        std::string targetName;
        bool isAvailable;
        std::vector<std::string> missingTools;
        std::vector<std::string> availableTools;
        std::string notes;
    };
    
    std::vector<ToolchainStatus> checkToolchainAvailability() const;
    ToolchainStatus checkTargetToolchain(const std::string& targetName) const;

private:
    std::map<std::string, std::unique_ptr<PlatformObjectGenerator>> generators_;
    bool verboseOutput_ = false;
    bool cleanupTempFiles_ = true;
    bool parallelGeneration_ = false;
    
    // Initialize default platform generators
    void initializeDefaultGenerators();
    
    // Target name normalization
    std::string normalizeTargetName(const std::string& targetName) const;
    PlatformObjectGenerator* findGenerator(const std::string& targetName) const;
    
    // Utilities
    std::string generateTemporaryPath(const std::string& basePath, const std::string& suffix) const;
    void logVerbose(const std::string& message) const;
    void cleanupFile(const std::string& path) const;
    bool fileExists(const std::string& path) const;
    bool createDirectoryIfNeeded(const std::string& path) const;
    std::string getToolPath(const std::string& toolName) const;
    bool executeCommand(const std::string& command, std::string& output, std::string& errorOutput, int& exitCode) const;
};

// Factory for creating platform-specific generators
class ObjectGeneratorFactory {
public:
    static std::unique_ptr<PlatformObjectGenerator> createLinuxGenerator();
    static std::unique_ptr<PlatformObjectGenerator> createWindowsGenerator();
    static std::unique_ptr<PlatformObjectGenerator> createAArch64Generator();
    static std::unique_ptr<PlatformObjectGenerator> createWasmGenerator();
    static std::unique_ptr<PlatformObjectGenerator> createRiscVGenerator();
    
    // Platform detection
    static std::string detectHostPlatform();
    static std::vector<std::string> getAvailablePlatforms();
    
private:
    ObjectGeneratorFactory() = default;
};

} // namespace objectgen
} // namespace codegen