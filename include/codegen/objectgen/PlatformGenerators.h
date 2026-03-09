#pragma once

#include "ObjectFileGenerator.h"

namespace codegen {
namespace objectgen {

// Linux ELF object generator
class LinuxObjectGenerator : public PlatformObjectGenerator {
public:
    LinuxObjectGenerator();
    
    // PlatformObjectGenerator interface
    ObjectGenResult generate(const std::string& asmPath, const std::string& objPath) override;
    ObjectValidationResult validateObject(const std::string& objPath) override;
    
    std::string getPlatformName() const override { return "Linux x86-64"; }
    std::string getObjectFormat() const override { return "ELF64"; }
    std::string getDefaultExtension() const override { return ".o"; }
    
    bool isToolchainAvailable() const override;
    std::vector<std::string> getRequiredTools() const override;
    AssemblerInfo getAssemblerInfo() const override;

private:
    bool invokeGNUAssembler(const std::string& asmPath, const std::string& objPath);
    ObjectValidationResult validateELFObject(const std::string& objPath);
    bool checkELFHeader(const std::string& objPath);
    std::vector<std::string> extractELFSections(const std::string& objPath);
    std::vector<std::string> extractELFSymbols(const std::string& objPath);
};

// Windows COFF object generator
class WindowsObjectGenerator : public PlatformObjectGenerator {
public:
    WindowsObjectGenerator();
    
    // PlatformObjectGenerator interface
    ObjectGenResult generate(const std::string& asmPath, const std::string& objPath) override;
    ObjectValidationResult validateObject(const std::string& objPath) override;
    
    std::string getPlatformName() const override { return "Windows x64"; }
    std::string getObjectFormat() const override { return "COFF"; }
    std::string getDefaultExtension() const override { return ".obj"; }
    
    bool isToolchainAvailable() const override;
    std::vector<std::string> getRequiredTools() const override;
    AssemblerInfo getAssemblerInfo() const override;

private:
    bool invokeMicrosoftAssembler(const std::string& asmPath, const std::string& objPath);
    bool invokeGNUAssemblerForWindows(const std::string& asmPath, const std::string& objPath);
    ObjectValidationResult validateCOFFObject(const std::string& objPath);
    bool checkCOFFHeader(const std::string& objPath);
    std::vector<std::string> extractCOFFSections(const std::string& objPath);
    std::vector<std::string> extractCOFFSymbols(const std::string& objPath);
    
    bool preferMicrosoftToolchain_ = true;
};

// AArch64 object generator
class AArch64ObjectGenerator : public PlatformObjectGenerator {
public:
    AArch64ObjectGenerator();
    
    // PlatformObjectGenerator interface
    ObjectGenResult generate(const std::string& asmPath, const std::string& objPath) override;
    ObjectValidationResult validateObject(const std::string& objPath) override;
    
    std::string getPlatformName() const override { return "AArch64"; }
    std::string getObjectFormat() const override { return "ELF64"; }
    std::string getDefaultExtension() const override { return ".o"; }
    
    bool isToolchainAvailable() const override;
    std::vector<std::string> getRequiredTools() const override;
    AssemblerInfo getAssemblerInfo() const override;

private:
    bool invokeAArch64Assembler(const std::string& asmPath, const std::string& objPath);
    ObjectValidationResult validateAArch64Object(const std::string& objPath);
    std::string findAArch64Toolchain() const;
};

// WebAssembly object generator
class WasmObjectGenerator : public PlatformObjectGenerator {
public:
    WasmObjectGenerator();
    
    // PlatformObjectGenerator interface
    ObjectGenResult generate(const std::string& asmPath, const std::string& objPath) override;
    ObjectValidationResult validateObject(const std::string& objPath) override;
    
    std::string getPlatformName() const override { return "WebAssembly"; }
    std::string getObjectFormat() const override { return "WASM"; }
    std::string getDefaultExtension() const override { return ".wasm"; }
    
    bool isToolchainAvailable() const override;
    std::vector<std::string> getRequiredTools() const override;
    AssemblerInfo getAssemblerInfo() const override;

private:
    bool invokeWasmAssembler(const std::string& asmPath, const std::string& objPath);
    ObjectValidationResult validateWasmObject(const std::string& objPath);
    bool checkWasmHeader(const std::string& objPath);
    std::vector<std::string> extractWasmSections(const std::string& objPath);
};

// RISC-V object generator
class RiscVObjectGenerator : public PlatformObjectGenerator {
public:
    RiscVObjectGenerator();
    
    // PlatformObjectGenerator interface
    ObjectGenResult generate(const std::string& asmPath, const std::string& objPath) override;
    ObjectValidationResult validateObject(const std::string& objPath) override;
    
    std::string getPlatformName() const override { return "RISC-V 64-bit"; }
    std::string getObjectFormat() const override { return "ELF64"; }
    std::string getDefaultExtension() const override { return ".o"; }
    
    bool isToolchainAvailable() const override;
    std::vector<std::string> getRequiredTools() const override;
    AssemblerInfo getAssemblerInfo() const override;

private:
    bool invokeRiscVAssembler(const std::string& asmPath, const std::string& objPath);
    ObjectValidationResult validateRiscVObject(const std::string& objPath);
    std::string findRiscVToolchain() const;
};

} // namespace objectgen
} // namespace codegen