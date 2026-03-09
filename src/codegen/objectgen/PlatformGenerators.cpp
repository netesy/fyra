#include "codegen/objectgen/PlatformGenerators.h"
#include <iostream>
#include <fstream>
#include <sstream>

namespace codegen {
namespace objectgen {

// LinuxObjectGenerator implementation
LinuxObjectGenerator::LinuxObjectGenerator() {}

ObjectGenResult LinuxObjectGenerator::generate(const std::string& asmPath, const std::string& objPath) {
    ObjectGenResult result;
    
    if (!invokeGNUAssembler(asmPath, objPath)) {
        result.success = false;
        result.errorOutput = "Failed to invoke GNU assembler";
        return result;
    }
    
    result.success = fileExists(objPath);
    result.objectPath = objPath;
    
    if (result.success) {
        ObjectValidationResult validation = validateObject(objPath);
        if (!validation.isValid) {
            for (const auto& error : validation.errors) {
                result.addWarning("Validation: " + error);
            }
        }
    }
    
    return result;
}

ObjectValidationResult LinuxObjectGenerator::validateObject(const std::string& objPath) {
    ObjectValidationResult result;
    result.format = "ELF64";
    result.architecture = "x86_64";
    
    if (!fileExists(objPath)) {
        result.addError("Object file does not exist: " + objPath);
        return result;
    }
    
    if (!checkELFHeader(objPath)) {
        result.addError("Invalid ELF header");
    }
    
    result.sections = extractELFSections(objPath);
    result.symbols = extractELFSymbols(objPath);
    
    // Basic validation checks
    bool hasTextSection = std::find(result.sections.begin(), result.sections.end(), ".text") != result.sections.end();
    if (!hasTextSection) {
        result.addWarning("No .text section found");
    }
    
    return result;
}

bool LinuxObjectGenerator::isToolchainAvailable() const {
    return !getToolPath("as").empty();
}

std::vector<std::string> LinuxObjectGenerator::getRequiredTools() const {
    return {"as", "objdump"};
}

AssemblerInfo LinuxObjectGenerator::getAssemblerInfo() const {
    AssemblerInfo info("GNU as", getToolPath("as"));
    info.defaultArgs = {"--64"};
    info.outputFlag = "-o";
    info.isAvailable = !info.executable.empty();
    return info;
}

bool LinuxObjectGenerator::invokeGNUAssembler(const std::string& asmPath, const std::string& objPath) {
    std::string command = "as --64 -o " + objPath + " " + asmPath;
    
    std::string output, errorOutput;
    int exitCode;
    
    return executeCommand(command, output, errorOutput, exitCode) && exitCode == 0;
}

bool LinuxObjectGenerator::checkELFHeader(const std::string& objPath) {
    std::ifstream file(objPath, std::ios::binary);
    if (!file.is_open()) return false;
    
    // Check ELF magic number
    char magic[4];
    file.read(magic, 4);
    
    return magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
}

std::vector<std::string> LinuxObjectGenerator::extractELFSections(const std::string& objPath) {
    std::vector<std::string> sections;
    
    std::string command = "objdump -h " + objPath;
    std::string output, errorOutput;
    int exitCode;
    
    if (executeCommand(command, output, errorOutput, exitCode) && exitCode == 0) {
        std::istringstream iss(output);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.find(".text") != std::string::npos) sections.push_back(".text");
            if (line.find(".data") != std::string::npos) sections.push_back(".data");
            if (line.find(".rodata") != std::string::npos) sections.push_back(".rodata");
            if (line.find(".bss") != std::string::npos) sections.push_back(".bss");
        }
    }
    
    return sections;
}

std::vector<std::string> LinuxObjectGenerator::extractELFSymbols(const std::string& objPath) {
    std::vector<std::string> symbols;
    
    std::string command = "objdump -t " + objPath;
    std::string output, errorOutput;
    int exitCode;
    
    if (executeCommand(command, output, errorOutput, exitCode) && exitCode == 0) {
        std::istringstream iss(output);
        std::string line;
        while (std::getline(iss, line)) {
            // Parse symbol table output (simplified)
            if (line.length() > 24 && std::isxdigit(line[0])) {
                size_t lastSpace = line.find_last_of(' ');
                if (lastSpace != std::string::npos) {
                    symbols.push_back(line.substr(lastSpace + 1));
                }
            }
        }
    }
    
    return symbols;
}

// WindowsObjectGenerator implementation
WindowsObjectGenerator::WindowsObjectGenerator() {}

ObjectGenResult WindowsObjectGenerator::generate(const std::string& asmPath, const std::string& objPath) {
    ObjectGenResult result;
    
    bool success = false;
    if (preferMicrosoftToolchain_ && !getToolPath("ml64").empty()) {
        success = invokeMicrosoftAssembler(asmPath, objPath);
    } else {
        success = invokeGNUAssemblerForWindows(asmPath, objPath);
    }
    
    if (!success) {
        result.success = false;
        result.errorOutput = "Failed to invoke assembler";
        return result;
    }
    
    result.success = fileExists(objPath);
    result.objectPath = objPath;
    
    return result;
}

ObjectValidationResult WindowsObjectGenerator::validateObject(const std::string& objPath) {
    ObjectValidationResult result;
    result.format = "COFF";
    result.architecture = "x86_64";
    
    if (!fileExists(objPath)) {
        result.addError("Object file does not exist");
        return result;
    }
    
    if (!checkCOFFHeader(objPath)) {
        result.addError("Invalid COFF header");
    }
    
    return result;
}

bool WindowsObjectGenerator::isToolchainAvailable() const {
    return !getToolPath("ml64").empty() || !getToolPath("as").empty();
}

std::vector<std::string> WindowsObjectGenerator::getRequiredTools() const {
    return {"ml64", "dumpbin"};
}

AssemblerInfo WindowsObjectGenerator::getAssemblerInfo() const {
    std::string toolPath = getToolPath("ml64");
    if (toolPath.empty()) {
        toolPath = getToolPath("as");
    }
    
    AssemblerInfo info("ML64/GNU as", toolPath);
    info.outputFlag = "/Fo";
    info.isAvailable = !toolPath.empty();
    return info;
}

bool WindowsObjectGenerator::invokeMicrosoftAssembler(const std::string& asmPath, const std::string& objPath) {
    std::string command = "ml64 /c /Fo" + objPath + " " + asmPath;
    
    std::string output, errorOutput;
    int exitCode;
    
    return executeCommand(command, output, errorOutput, exitCode) && exitCode == 0;
}

bool WindowsObjectGenerator::invokeGNUAssemblerForWindows(const std::string& asmPath, const std::string& objPath) {
    std::string command = "as --64 -o " + objPath + " " + asmPath;
    
    std::string output, errorOutput;
    int exitCode;
    
    return executeCommand(command, output, errorOutput, exitCode) && exitCode == 0;
}

bool WindowsObjectGenerator::checkCOFFHeader(const std::string& objPath) {
    std::ifstream file(objPath, std::ios::binary);
    if (!file.is_open()) return false;
    
    // Read COFF header signature (simplified check)
    uint16_t machine;
    file.read(reinterpret_cast<char*>(&machine), sizeof(machine));
    
    // Check for x86-64 machine type
    return machine == 0x8664;
}

std::vector<std::string> WindowsObjectGenerator::extractCOFFSections(const std::string& objPath) {
    std::vector<std::string> sections;
    // Implementation would use dumpbin or similar tool
    return sections;
}

std::vector<std::string> WindowsObjectGenerator::extractCOFFSymbols(const std::string& objPath) {
    std::vector<std::string> symbols;
    // Implementation would use dumpbin or similar tool
    return symbols;
}

// AArch64ObjectGenerator implementation
AArch64ObjectGenerator::AArch64ObjectGenerator() {}

ObjectGenResult AArch64ObjectGenerator::generate(const std::string& asmPath, const std::string& objPath) {
    ObjectGenResult result;
    
    if (!invokeAArch64Assembler(asmPath, objPath)) {
        result.success = false;
        result.errorOutput = "Failed to invoke AArch64 assembler";
        return result;
    }
    
    result.success = fileExists(objPath);
    result.objectPath = objPath;
    
    return result;
}

ObjectValidationResult AArch64ObjectGenerator::validateObject(const std::string& objPath) {
    ObjectValidationResult result;
    result.format = "ELF64";
    result.architecture = "aarch64";
    
    if (!fileExists(objPath)) {
        result.addError("Object file does not exist");
    }
    
    return result;
}

bool AArch64ObjectGenerator::isToolchainAvailable() const {
    return !findAArch64Toolchain().empty();
}

std::vector<std::string> AArch64ObjectGenerator::getRequiredTools() const {
    return {"aarch64-linux-gnu-as", "aarch64-linux-gnu-objdump"};
}

AssemblerInfo AArch64ObjectGenerator::getAssemblerInfo() const {
    std::string toolchain = findAArch64Toolchain();
    AssemblerInfo info("AArch64 as", toolchain + "as");
    info.isAvailable = !toolchain.empty();
    return info;
}

bool AArch64ObjectGenerator::invokeAArch64Assembler(const std::string& asmPath, const std::string& objPath) {
    std::string toolchain = findAArch64Toolchain();
    if (toolchain.empty()) return false;
    
    std::string command = toolchain + "as -o " + objPath + " " + asmPath;
    
    std::string output, errorOutput;
    int exitCode;
    
    return executeCommand(command, output, errorOutput, exitCode) && exitCode == 0;
}

std::string AArch64ObjectGenerator::findAArch64Toolchain() const {
    std::vector<std::string> prefixes = {
        "aarch64-linux-gnu-",
        "aarch64-unknown-linux-gnu-",
        "aarch64-none-linux-gnu-"
    };
    
    for (const auto& prefix : prefixes) {
        if (!getToolPath(prefix + "as").empty()) {
            return prefix;
        }
    }
    
    return "";
}

// WasmObjectGenerator implementation
WasmObjectGenerator::WasmObjectGenerator() {}

ObjectGenResult WasmObjectGenerator::generate(const std::string& asmPath, const std::string& objPath) {
    ObjectGenResult result;
    
    if (!invokeWasmAssembler(asmPath, objPath)) {
        result.success = false;
        result.errorOutput = "Failed to invoke WebAssembly assembler";
        return result;
    }
    
    result.success = fileExists(objPath);
    result.objectPath = objPath;
    
    return result;
}

ObjectValidationResult WasmObjectGenerator::validateObject(const std::string& objPath) {
    ObjectValidationResult result;
    result.format = "WASM";
    result.architecture = "wasm32";
    
    if (!fileExists(objPath)) {
        result.addError("Object file does not exist");
        return result;
    }
    
    if (!checkWasmHeader(objPath)) {
        result.addError("Invalid WebAssembly header");
    }
    
    return result;
}

bool WasmObjectGenerator::isToolchainAvailable() const {
    return !getToolPath("wat2wasm").empty();
}

std::vector<std::string> WasmObjectGenerator::getRequiredTools() const {
    return {"wat2wasm", "wasm-objdump"};
}

AssemblerInfo WasmObjectGenerator::getAssemblerInfo() const {
    AssemblerInfo info("wat2wasm", getToolPath("wat2wasm"));
    info.outputFlag = "-o";
    info.isAvailable = !info.executable.empty();
    return info;
}

bool WasmObjectGenerator::invokeWasmAssembler(const std::string& asmPath, const std::string& objPath) {
    std::string command = "wat2wasm -o " + objPath + " " + asmPath;
    
    std::string output, errorOutput;
    int exitCode;
    
    return executeCommand(command, output, errorOutput, exitCode) && exitCode == 0;
}

bool WasmObjectGenerator::checkWasmHeader(const std::string& objPath) {
    std::ifstream file(objPath, std::ios::binary);
    if (!file.is_open()) return false;
    
    // Check WebAssembly magic number
    char magic[4];
    file.read(magic, 4);
    
    return magic[0] == 0x00 && magic[1] == 'a' && magic[2] == 's' && magic[3] == 'm';
}

std::vector<std::string> WasmObjectGenerator::extractWasmSections(const std::string& objPath) {
    std::vector<std::string> sections;
    // Implementation would parse WASM sections
    return sections;
}

// RiscVObjectGenerator implementation
RiscVObjectGenerator::RiscVObjectGenerator() {}

ObjectGenResult RiscVObjectGenerator::generate(const std::string& asmPath, const std::string& objPath) {
    ObjectGenResult result;
    
    if (!invokeRiscVAssembler(asmPath, objPath)) {
        result.success = false;
        result.errorOutput = "Failed to invoke RISC-V assembler";
        return result;
    }
    
    result.success = fileExists(objPath);
    result.objectPath = objPath;
    
    return result;
}

ObjectValidationResult RiscVObjectGenerator::validateObject(const std::string& objPath) {
    ObjectValidationResult result;
    result.format = "ELF64";
    result.architecture = "riscv64";
    
    if (!fileExists(objPath)) {
        result.addError("Object file does not exist");
    }
    
    return result;
}

bool RiscVObjectGenerator::isToolchainAvailable() const {
    return !findRiscVToolchain().empty();
}

std::vector<std::string> RiscVObjectGenerator::getRequiredTools() const {
    return {"riscv64-linux-gnu-as", "riscv64-linux-gnu-objdump"};
}

AssemblerInfo RiscVObjectGenerator::getAssemblerInfo() const {
    std::string toolchain = findRiscVToolchain();
    AssemblerInfo info("RISC-V as", toolchain + "as");
    info.isAvailable = !toolchain.empty();
    return info;
}

bool RiscVObjectGenerator::invokeRiscVAssembler(const std::string& asmPath, const std::string& objPath) {
    std::string toolchain = findRiscVToolchain();
    if (toolchain.empty()) return false;
    
    std::string command = toolchain + "as -o " + objPath + " " + asmPath;
    
    std::string output, errorOutput;
    int exitCode;
    
    return executeCommand(command, output, errorOutput, exitCode) && exitCode == 0;
}

std::string RiscVObjectGenerator::findRiscVToolchain() const {
    std::vector<std::string> prefixes = {
        "riscv64-linux-gnu-",
        "riscv64-unknown-linux-gnu-",
        "riscv64-unknown-elf-"
    };
    
    for (const auto& prefix : prefixes) {
        if (!getToolPath(prefix + "as").empty()) {
            return prefix;
        }
    }
    
    return "";
}

} // namespace objectgen
} // namespace codegen