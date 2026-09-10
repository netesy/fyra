#include "codegen/objectgen/ObjectFileGenerator.h"
#include "codegen/objectgen/PlatformGenerators.h"
#include "target/artifact/archive/ArchiveWriter.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <thread>
#include <future>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace codegen {
namespace objectgen {

namespace {

std::vector<std::string> extractExportedSymbolsFromObjectBytes(const std::vector<uint8_t>& bytes) {
    std::vector<std::string> symbols;
    if (bytes.size() < 16) return symbols;

    // Check for ELF (0x7F 'E' 'L' 'F')
    if (bytes[0] == 0x7F && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F') {
        if (bytes.size() < 64) return symbols;
        uint64_t shoff = *reinterpret_cast<const uint64_t*>(&bytes[40]);
        uint16_t shentsize = *reinterpret_cast<const uint16_t*>(&bytes[58]);
        uint16_t shnum = *reinterpret_cast<const uint16_t*>(&bytes[60]);

        if (shentsize < 64 || shoff + static_cast<uint64_t>(shnum) * shentsize > bytes.size()) return symbols;

        int symtabIdx = -1;
        for (uint16_t i = 0; i < shnum; ++i) {
            const uint8_t* shdr = &bytes[shoff + i * shentsize];
            uint32_t sh_type = *reinterpret_cast<const uint32_t*>(shdr + 4);
            if (sh_type == 2) { // SHT_SYMTAB
                symtabIdx = i;
                break;
            }
        }

        if (symtabIdx != -1) {
            const uint8_t* symshdr = &bytes[shoff + symtabIdx * shentsize];
            uint64_t sym_offset = *reinterpret_cast<const uint64_t*>(symshdr + 24);
            uint64_t sym_size = *reinterpret_cast<const uint64_t*>(symshdr + 32);
            uint32_t strtabIdx = *reinterpret_cast<const uint32_t*>(symshdr + 40);

            if (strtabIdx < shnum) {
                const uint8_t* strshdr = &bytes[shoff + strtabIdx * shentsize];
                uint64_t str_offset = *reinterpret_cast<const uint64_t*>(strshdr + 24);
                uint64_t str_size = *reinterpret_cast<const uint64_t*>(strshdr + 32);

                if (sym_offset + sym_size <= bytes.size() && str_offset + str_size <= bytes.size()) {
                    size_t numSyms = sym_size / 24;
                    for (size_t i = 0; i < numSyms; ++i) {
                        const uint8_t* sym = &bytes[sym_offset + i * 24];
                        uint32_t st_name = *reinterpret_cast<const uint32_t*>(sym + 0);
                        uint8_t st_info = sym[4];
                        uint16_t st_shndx = *reinterpret_cast<const uint16_t*>(sym + 6);

                        uint8_t binding = st_info >> 4;
                        if ((binding == 1 || binding == 2) && st_shndx != 0 && st_name < str_size) {
                            const char* name = reinterpret_cast<const char*>(&bytes[str_offset + st_name]);
                            symbols.push_back(name);
                        }
                    }
                }
            }
        }
    }
    // Check for COFF x64 (Machine == 0x8664)
    else if (*reinterpret_cast<const uint16_t*>(&bytes[0]) == 0x8664) {
        if (bytes.size() < 20) return symbols;
        uint32_t ptrToSymTable = *reinterpret_cast<const uint32_t*>(&bytes[8]);
        uint32_t numSyms = *reinterpret_cast<const uint32_t*>(&bytes[12]);
        uint64_t strTableOffset = ptrToSymTable + static_cast<uint64_t>(numSyms) * 18;

        if (ptrToSymTable + numSyms * 18 <= bytes.size()) {
            for (uint32_t i = 0; i < numSyms; ++i) {
                const uint8_t* sym = &bytes[ptrToSymTable + i * 18];
                int16_t sectionNumber = *reinterpret_cast<const int16_t*>(sym + 12);
                uint8_t storageClass = sym[16];
                uint8_t numAuxSymbols = sym[17];

                if (storageClass == 2 && sectionNumber > 0) { // IMAGE_SYM_CLASS_EXTERNAL and defined
                    uint32_t zeroCheck = *reinterpret_cast<const uint32_t*>(sym);
                    std::string symName;
                    if (zeroCheck == 0) {
                        uint32_t strOffset = *reinterpret_cast<const uint32_t*>(sym + 4);
                        if (strTableOffset + strOffset < bytes.size()) {
                            symName = reinterpret_cast<const char*>(&bytes[strTableOffset + strOffset]);
                        }
                    } else {
                        char shortName[9] = {0};
                        std::memcpy(shortName, sym, 8);
                        symName = shortName;
                    }
                    if (!symName.empty()) {
                        symbols.push_back(symName);
                    }
                }
                i += numAuxSymbols;
            }
        }
    }
    // Check for Mach-O 64-bit (0xFEEDFACF)
    else if (*reinterpret_cast<const uint32_t*>(&bytes[0]) == 0xFEEDFACF) {
        if (bytes.size() < 32) return symbols;
        uint32_t ncmds = *reinterpret_cast<const uint32_t*>(&bytes[16]);
        uint64_t cmdOffset = 32;

        for (uint32_t c = 0; c < ncmds && cmdOffset + 8 <= bytes.size(); ++c) {
            uint32_t cmd = *reinterpret_cast<const uint32_t*>(&bytes[cmdOffset]);
            uint32_t cmdsize = *reinterpret_cast<const uint32_t*>(&bytes[cmdOffset + 4]);

            if (cmd == 0x02) { // LC_SYMTAB
                if (cmdOffset + 24 <= bytes.size()) {
                    uint32_t symoff = *reinterpret_cast<const uint32_t*>(&bytes[cmdOffset + 8]);
                    uint32_t nsyms = *reinterpret_cast<const uint32_t*>(&bytes[cmdOffset + 12]);
                    uint32_t stroff = *reinterpret_cast<const uint32_t*>(&bytes[cmdOffset + 16]);
                    uint32_t strsize = *reinterpret_cast<const uint32_t*>(&bytes[cmdOffset + 20]);

                    if (symoff + nsyms * 16 <= bytes.size() && stroff + strsize <= bytes.size()) {
                        for (uint32_t i = 0; i < nsyms; ++i) {
                            const uint8_t* nlist = &bytes[symoff + i * 16];
                            uint32_t n_strx = *reinterpret_cast<const uint32_t*>(nlist);
                            uint8_t n_type = nlist[4];
                            uint8_t n_sect = nlist[5];

                            if ((n_type & 0x01) && n_sect != 0 && n_strx < strsize) { // N_EXT and defined
                                const char* name = reinterpret_cast<const char*>(&bytes[stroff + n_strx]);
                                if (name[0] == '_') name++; // Mach-O leading underscore strip
                                symbols.push_back(name);
                            }
                        }
                    }
                }
                break;
            }
            cmdOffset += cmdsize;
        }
    }

    return symbols;
}

} // namespace

// PlatformObjectGenerator base implementation
ObjectGenResult PlatformObjectGenerator::createStaticLibrary(const std::vector<std::string>& objPaths,
                                                              const std::string& libPath,
                                                              const std::string& targetName) {
    ObjectGenResult result;
    std::vector<target::artifact::archive::ArchiveMember> members;

    for (const auto& objPath : objPaths) {
        std::ifstream file(objPath, std::ios::binary);
        if (!file.is_open()) {
            result.success = false;
            result.errorOutput = "Could not open object file for archive member: " + objPath;
            return result;
        }

        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        target::artifact::archive::ArchiveMember member;
        std::filesystem::path p(objPath);
        member.name = p.filename().string();
        member.bytes = std::move(bytes);
        member.exportedSymbols = extractExportedSymbolsFromObjectBytes(member.bytes);

        members.push_back(std::move(member));
    }

    auto writer = target::artifact::archive::ArchiveWriter::createForTargetTriple(targetName.empty() ? getPlatformName() : targetName);
    if (!writer) {
        result.success = false;
        result.errorOutput = "Failed to create archive writer for target: " + targetName;
        return result;
    }

    if (writer->write(members, libPath)) {
        result.success = true;
        result.objectPath = libPath;
    } else {
        result.success = false;
        result.errorOutput = "Archive serialization failed: " + writer->getLastError();
    }

    return result;
}

bool PlatformObjectGenerator::fileExists(const std::string& path) const {
    return std::filesystem::exists(path);
}

bool PlatformObjectGenerator::executeCommand(const std::string& command, 
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

std::string PlatformObjectGenerator::getToolPath(const std::string& toolName) const {
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
    
    if (!createDirectoryIfNeeded(outputPath)) {
        result.success = false;
        result.errorOutput = "Failed to create output directory";
        return result;
    }
    
    result = generator->generate(assemblyPath, outputPath);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    result.generationTimeMs = duration.count() / 1000.0;
    
    if (result.success) {
        logVerbose("Object generation successful in " + std::to_string(result.generationTimeMs) + "ms");
        
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

ObjectGenResult ObjectFileGenerator::createStaticLibrary(
    const std::vector<std::string>& objectPaths,
    const std::string& outputPath,
    const std::string& targetName) {

    std::string normalizedTarget = normalizeTargetName(targetName);
    PlatformObjectGenerator* generator = findGenerator(normalizedTarget);

    if (!generator) {
        ObjectGenResult result;
        result.success = false;
        result.errorOutput = "No generator available for target: " + targetName;
        return result;
    }

    return generator->createStaticLibrary(objectPaths, outputPath, targetName);
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
    registerPlatformGenerator("x86_64-linux-bin", ObjectGeneratorFactory::createLinuxGenerator());
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

    registerPlatformGenerator("macos", ObjectGeneratorFactory::createLinuxGenerator());
    registerPlatformGenerator("macos-aarch64", ObjectGeneratorFactory::createLinuxGenerator());
    registerPlatformGenerator("macos-amd64", ObjectGeneratorFactory::createLinuxGenerator());
    registerPlatformGenerator("macos-arm64", ObjectGeneratorFactory::createLinuxGenerator());
}

std::string ObjectFileGenerator::normalizeTargetName(const std::string& targetName) const {
    std::string normalized = targetName;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    if (normalized.find("linux") != std::string::npos || normalized == "x86_64-linux-bin" || normalized == "x64-linux-bin") {
        return "linux";
    }
    if (normalized.find("windows") != std::string::npos || normalized.find("win") != std::string::npos) {
        return "windows";
    }
    if (normalized.find("aarch64") != std::string::npos || normalized.find("arm64") != std::string::npos) {
        return "aarch64";
    }
    if (normalized.find("riscv") != std::string::npos) {
        return "riscv64";
    }
    if (normalized.find("wasm") != std::string::npos) {
        return "wasm32";
    }
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
