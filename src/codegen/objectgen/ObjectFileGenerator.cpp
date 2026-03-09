#include "codegen/objectgen/ObjectFileGenerator.h"
#include "codegen/objectgen/PlatformGenerators.h"
#include <filesystem>
#include <iostream>
#include <thread>
#include <future>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace codegen {
namespace objectgen {

// PlatformObjectGenerator base implementation
bool PlatformObjectGenerator::fileExists(const std::string& path) const {
    return std::filesystem::exists(path);
}

bool PlatformObjectGenerator::executeCommand(const std::string& command, 
                                           std::string& output, 
                                           std::string& errorOutput, 
                                           int& exitCode) const {
#ifdef _WIN32
    // Windows implementation using CreateProcess
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESTDHANDLES;
    
    if (CreateProcessA(nullptr, const_cast<char*>(command.c_str()), 
                      nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        
        DWORD dwExitCode;
        GetExitCodeProcess(pi.hProcess, &dwExitCode);
        exitCode = static_cast<int>(dwExitCode);
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
#else
    // Unix implementation using popen
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (!pipe) {
        exitCode = -1;
        return false;
    }
    
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    
    exitCode = pclose(pipe);
    return true;
#endif
}

std::string PlatformObjectGenerator::getToolPath(const std::string& toolName) const {
    // Try to find tool in PATH
    std::string command = "which " + toolName;
#ifdef _WIN32
    command = "where " + toolName;
#endif
    
    std::string output, errorOutput;
    int exitCode;
    
    if (executeCommand(command, output, errorOutput, exitCode) && exitCode == 0) {
        // Extract first line as tool path
        size_t newlinePos = output.find('\n');
        if (newlinePos != std::string::npos) {
            return output.substr(0, newlinePos);
        }
        return output;
    }
    
    return ""; // Tool not found
}

bool PlatformObjectGenerator::createDirectoryIfNeeded(const std::string& path) const {
    std::filesystem::path dirPath = std::filesystem::path(path).parent_path();
    if (!dirPath.empty() && !std::filesystem::exists(dirPath)) {
        return std::filesystem::create_directories(dirPath);
    }
    return true;
}

// ObjectFileGenerator implementation
ObjectFileGenerator::ObjectFileGenerator() {
    initializeDefaultGenerators();
}

ObjectGenResult ObjectFileGenerator::generateObject(
    const std::string& assemblyPath,
    const std::string& outputPath,
    const std::string& targetName) {
    
    auto startTime = std::chrono::high_resolution_clock::now();
    ObjectGenResult result;
    
    if (!fileExists(assemblyPath)) {
        result.success = false;
        result.errorOutput = "Assembly file not found: " + assemblyPath;
        return result;
    }
    
    std::string normalizedTarget = normalizeTargetName(targetName);
    PlatformObjectGenerator* generator = findGenerator(normalizedTarget);
    
    if (!generator) {
        result.success = false;
        result.errorOutput = "No generator available for target: " + targetName;
        return result;
    }
    
    if (!generator->isToolchainAvailable()) {
        result.success = false;
        result.errorOutput = "Toolchain not available for " + generator->getPlatformName();
        auto requiredTools = generator->getRequiredTools();
        result.errorOutput += ". Required tools: ";
        for (size_t i = 0; i < requiredTools.size(); ++i) {
            result.errorOutput += requiredTools[i];
            if (i < requiredTools.size() - 1) result.errorOutput += ", ";
        }
        return result;
    }
    
    logVerbose("Generating object file for " + generator->getPlatformName());
    logVerbose("Input: " + assemblyPath);
    logVerbose("Output: " + outputPath);
    
    // Create output directory if needed
    if (!createDirectoryIfNeeded(outputPath)) {
        result.success = false;
        result.errorOutput = "Failed to create output directory";
        return result;
    }
    
    // Generate object file
    result = generator->generate(assemblyPath, outputPath);
    
    // Calculate generation time
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    result.generationTimeMs = duration.count() / 1000.0;
    
    if (result.success) {
        logVerbose("Object generation successful in " + std::to_string(result.generationTimeMs) + "ms");
        
        // Validate generated object
        ObjectValidationResult validation = generator->validateObject(outputPath);
        if (!validation.isValid) {
            result.addWarning("Generated object file has validation issues");
            for (const auto& error : validation.errors) {
                result.addWarning("Validation: " + error);
            }
        }
    } else {
        logVerbose("Object generation failed: " + result.errorOutput);
    }
    
    return result;
}

std::map<std::string, ObjectGenResult> ObjectFileGenerator::generateForAllTargets(
    const std::string& assemblyPath,
    const std::string& outputPrefix) {
    
    std::map<std::string, ObjectGenResult> results;
    
    if (parallelGeneration_) {
        // Parallel generation
        std::vector<std::future<std::pair<std::string, ObjectGenResult>>> futures;
        
        for (const auto& [targetName, generator] : generators_) {
            futures.emplace_back(std::async(std::launch::async, [this, assemblyPath, outputPrefix, targetName]() {
                std::string outputPath = outputPrefix + "_" + targetName + generators_.at(targetName)->getDefaultExtension();
                ObjectGenResult result = generateObject(assemblyPath, outputPath, targetName);
                return std::make_pair(targetName, result);
            }));
        }
        
        for (auto& future : futures) {
            auto [targetName, result] = future.get();
            results[targetName] = result;
        }
    } else {
        // Sequential generation
        for (const auto& [targetName, generator] : generators_) {
            std::string outputPath = outputPrefix + "_" + targetName + generator->getDefaultExtension();
            results[targetName] = generateObject(assemblyPath, outputPath, targetName);
        }
    }
    
    return results;
}

ObjectValidationResult ObjectFileGenerator::validateGeneratedObject(
    const std::string& objectPath,
    const std::string& targetName) {
    
    std::string normalizedTarget = normalizeTargetName(targetName);
    PlatformObjectGenerator* generator = findGenerator(normalizedTarget);
    
    if (!generator) {
        ObjectValidationResult result;
        result.addError("No generator available for target: " + targetName);
        return result;
    }
    
    return generator->validateObject(objectPath);
}

std::vector<std::string> ObjectFileGenerator::getSupportedTargets() const {
    std::vector<std::string> targets;
    for (const auto& [name, generator] : generators_) {
        targets.push_back(name);
    }
    return targets;
}

bool ObjectFileGenerator::isTargetSupported(const std::string& targetName) const {
    std::string normalized = normalizeTargetName(targetName);
    return generators_.find(normalized) != generators_.end();
}

void ObjectFileGenerator::registerPlatformGenerator(const std::string& targetName, 
                                                   std::unique_ptr<PlatformObjectGenerator> generator) {
    generators_[targetName] = std::move(generator);
}

std::vector<ObjectFileGenerator::ToolchainStatus> ObjectFileGenerator::checkToolchainAvailability() const {
    std::vector<ToolchainStatus> statuses;
    
    for (const auto& [targetName, generator] : generators_) {
        statuses.push_back(checkTargetToolchain(targetName));
    }
    
    return statuses;
}

ObjectFileGenerator::ToolchainStatus ObjectFileGenerator::checkTargetToolchain(const std::string& targetName) const {
    ToolchainStatus status;
    status.targetName = targetName;
    
    PlatformObjectGenerator* generator = findGenerator(targetName);
    if (!generator) {
        status.isAvailable = false;
        status.notes = "No generator available";
        return status;
    }
    
    status.isAvailable = generator->isToolchainAvailable();
    
    auto requiredTools = generator->getRequiredTools();
    for (const auto& tool : requiredTools) {
        std::string toolPath = getToolPath(tool);
        if (!toolPath.empty()) {
            status.availableTools.push_back(tool + " (" + toolPath + ")");
        } else {
            status.missingTools.push_back(tool);
        }
    }
    
    if (!status.missingTools.empty()) {
        status.isAvailable = false;
        status.notes = "Missing required tools";
    }
    
    return status;
}

void ObjectFileGenerator::initializeDefaultGenerators() {
    registerPlatformGenerator("linux", ObjectGeneratorFactory::createLinuxGenerator());
    registerPlatformGenerator("x86_64-unknown-linux-gnu", ObjectGeneratorFactory::createLinuxGenerator());
    registerPlatformGenerator("systemv", ObjectGeneratorFactory::createLinuxGenerator());
    
    registerPlatformGenerator("windows", ObjectGeneratorFactory::createWindowsGenerator());
    registerPlatformGenerator("x86_64-pc-windows-msvc", ObjectGeneratorFactory::createWindowsGenerator());
    registerPlatformGenerator("win64", ObjectGeneratorFactory::createWindowsGenerator());
    
    registerPlatformGenerator("aarch64", ObjectGeneratorFactory::createAArch64Generator());
    registerPlatformGenerator("aarch64-unknown-linux-gnu", ObjectGeneratorFactory::createAArch64Generator());
    
    registerPlatformGenerator("wasm32", ObjectGeneratorFactory::createWasmGenerator());
    registerPlatformGenerator("wasm32-unknown-unknown", ObjectGeneratorFactory::createWasmGenerator());
    
    registerPlatformGenerator("riscv64", ObjectGeneratorFactory::createRiscVGenerator());
    registerPlatformGenerator("riscv64-unknown-linux-gnu", ObjectGeneratorFactory::createRiscVGenerator());

    registerPlatformGenerator("macos", ObjectGeneratorFactory::createLinuxGenerator()); // Placeholder
    registerPlatformGenerator("macos-aarch64", ObjectGeneratorFactory::createLinuxGenerator()); // Placeholder
    registerPlatformGenerator("macos-amd64", ObjectGeneratorFactory::createLinuxGenerator()); // Placeholder
    registerPlatformGenerator("macos-arm64", ObjectGeneratorFactory::createLinuxGenerator()); // Placeholder
}

std::string ObjectFileGenerator::normalizeTargetName(const std::string& targetName) const {
    std::string normalized = targetName;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    return normalized;
}

PlatformObjectGenerator* ObjectFileGenerator::findGenerator(const std::string& targetName) const {
    auto it = generators_.find(targetName);
    return (it != generators_.end()) ? it->second.get() : nullptr;
}

std::string ObjectFileGenerator::generateTemporaryPath(const std::string& basePath, const std::string& suffix) const {
    std::filesystem::path path(basePath);
    std::string filename = path.stem().string() + "_tmp_" + suffix + path.extension().string();
    std::string returnPath = path.parent_path().string() + filename;
    return returnPath;
}

void ObjectFileGenerator::logVerbose(const std::string& message) const {
    if (verboseOutput_) {
        std::cout << "[ObjectGen] " << message << std::endl;
    }
}

void ObjectFileGenerator::cleanupFile(const std::string& path) const {
    if (cleanupTempFiles_ && std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
}

bool ObjectFileGenerator::fileExists(const std::string& path) const {
    return std::filesystem::exists(path);
}

bool ObjectFileGenerator::createDirectoryIfNeeded(const std::string& path) const {
    std::filesystem::path dirPath = std::filesystem::path(path).parent_path();
    if (!dirPath.empty() && !std::filesystem::exists(dirPath)) {
        return std::filesystem::create_directories(dirPath);
    }
    return true;
}

std::string ObjectFileGenerator::getToolPath(const std::string& toolName) const {
    std::string command = "which " + toolName;
#ifdef _WIN32
    command = "where " + toolName;
#endif
    
    std::string output, errorOutput;
    int exitCode;
    
    if (executeCommand(command, output, errorOutput, exitCode) && exitCode == 0) {
        size_t newlinePos = output.find('\n');
        if (newlinePos != std::string::npos) {
            return output.substr(0, newlinePos);
        }
        return output;
    }
    
    return "";
}

bool ObjectFileGenerator::executeCommand(const std::string& command, 
                                       std::string& output, 
                                       std::string& errorOutput, 
                                       int& exitCode) const {
#ifdef _WIN32
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESTDHANDLES;
    
    if (CreateProcessA(nullptr, const_cast<char*>(command.c_str()), 
                      nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        
        DWORD dwExitCode;
        GetExitCodeProcess(pi.hProcess, &dwExitCode);
        exitCode = static_cast<int>(dwExitCode);
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
#else
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (!pipe) {
        exitCode = -1;
        return false;
    }
    
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    
    exitCode = pclose(pipe);
    return true;
#endif
}

// ObjectGeneratorFactory implementation
std::unique_ptr<PlatformObjectGenerator> ObjectGeneratorFactory::createLinuxGenerator() {
    return std::make_unique<LinuxObjectGenerator>();
}

std::unique_ptr<PlatformObjectGenerator> ObjectGeneratorFactory::createWindowsGenerator() {
    return std::make_unique<WindowsObjectGenerator>();
}

std::unique_ptr<PlatformObjectGenerator> ObjectGeneratorFactory::createAArch64Generator() {
    return std::make_unique<AArch64ObjectGenerator>();
}

std::unique_ptr<PlatformObjectGenerator> ObjectGeneratorFactory::createWasmGenerator() {
    return std::make_unique<WasmObjectGenerator>();
}

std::unique_ptr<PlatformObjectGenerator> ObjectGeneratorFactory::createRiscVGenerator() {
    return std::make_unique<RiscVObjectGenerator>();
}

std::string ObjectGeneratorFactory::detectHostPlatform() {
#ifdef _WIN32
    return "windows";
#elif defined(__linux__)
    return "linux";
#elif defined(__APPLE__)
    return "macos";
#else
    return "unknown";
#endif
}

std::vector<std::string> ObjectGeneratorFactory::getAvailablePlatforms() {
    return {"linux", "windows", "aarch64", "wasm32", "riscv64"};
}

} // namespace objectgen
} // namespace codegen