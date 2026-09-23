#include "fyra/BackendBuilder.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetDescriptor.h"
#include "target/artifact/object/ObjectReader.h"
#include "target/artifact/object/ObjectWriter.h"
#include "target/artifact/archive/ArchiveReader.h"
#include "target/artifact/archive/ArchiveWriter.h"
#include "target/artifact/linker/InternalLinker.h"
#include "target/artifact/linker/DynamicLinkPlan.h"
#include "target/artifact/linker/TargetDynamicImageBuilder.h"
#include "target/artifact/executable/ElfImage.h"
#include "target/artifact/executable/MachOImage.h"
#include "target/artifact/executable/PeImage.h"
#include "target/architecture/wasm32/WasmModule.h"
#include <fstream>
#include <iostream>
#include <cstring>

namespace fyra {

BackendBuilder::BackendBuilder(ir::Module& module)
    : srcModule_(module) {
    config_.targetTriple = "x64-linux-bin";
    config_.optLevel = OptimizationLevel::O2;
    config_.validate = true;
    config_.enableSLP = true;
    config_.enableLoopVectorization = true;
    config_.enableLoopUnroll = true;
}

std::string BackendBuilder::resolveTargetTriple(const std::string& triple) {
    if (triple.empty()) return "x64-linux-bin";
    auto desc = target::TargetDescriptor::fromString(triple);
    if (desc) return triple;

    std::string canonical = triple;
    if (triple == "linux") canonical = "x64-linux-bin";
    else if (triple == "windows" || triple == "windows-amd64" || triple == "win32" || triple == "win64") canonical = "x64-windows-bin";
    else if (triple == "windows-arm64") canonical = "aarch64-windows-bin";
    else if (triple == "aarch64") canonical = "aarch64-linux-bin";
    else if (triple == "wasm32" || triple == "wasm") canonical = "wasm32-wasi-wasm";
    else if (triple == "riscv64") canonical = "riscv64-linux-bin";
    else {
        canonical += "-bin";
    }

    desc = target::TargetDescriptor::fromString(canonical);
    if (desc) return canonical;

    return triple;
}

BackendBuilder& BackendBuilder::target(const std::string& triple) {
    std::string res = resolveTargetTriple(triple);
    if (res != config_.targetTriple) {
        config_.targetTriple = res;
        invalidatePrepared();
    }
    return *this;
}

BackendBuilder& BackendBuilder::optimize(OptimizationLevel level) {
    if (config_.optLevel != level) {
        config_.optLevel = level;
        invalidatePrepared();
    }
    return *this;
}

BackendBuilder& BackendBuilder::validate(bool enabled) {
    if (config_.validate != enabled) {
        config_.validate = enabled;
        invalidatePrepared();
    }
    return *this;
}

BackendBuilder& BackendBuilder::enableSLP(bool enabled) {
    if (config_.enableSLP != enabled) {
        config_.enableSLP = enabled;
        invalidatePrepared();
    }
    return *this;
}

BackendBuilder& BackendBuilder::enableLoopVectorization(bool enabled) {
    if (config_.enableLoopVectorization != enabled) {
        config_.enableLoopVectorization = enabled;
        invalidatePrepared();
    }
    return *this;
}

BackendBuilder& BackendBuilder::enableLoopUnroll(bool enabled) {
    if (config_.enableLoopUnroll != enabled) {
        config_.enableLoopUnroll = enabled;
        invalidatePrepared();
    }
    return *this;
}

BackendBuilder& BackendBuilder::addObject(const std::string& path) {
    inputObjectPaths_.push_back(path);
    return *this;
}

BackendBuilder& BackendBuilder::addStaticLibrary(const std::string& path) {
    inputStaticLibPaths_.push_back(path);
    return *this;
}

BackendBuilder& BackendBuilder::importSymbol(const std::string& symbol, const std::string& dependencyLibrary) {
    target::artifact::linker::DynamicImport imp;
    imp.symbol = symbol;
    imp.dependencyLibrary = dependencyLibrary;
    dynamicImports_.push_back(imp);
    return *this;
}

void BackendBuilder::invalidatePrepared() {
    preparedModule_.reset();
    isPrepared_ = false;
}

void BackendBuilder::ensurePrepared(BuildResult& result) {
    if (isPrepared_ && preparedModule_) return;

    preparedModule_ = cloneModule(srcModule_);
    PipelineResult pRes = pipeline_.run(*preparedModule_, config_);
    if (!pRes.success) {
        result.success = false;
        result.errors = pRes.errors;
        return;
    }
    isPrepared_ = true;
}

target::artifact::object::ObjectArtifact BackendBuilder::buildModuleObjectArtifact(BuildResult& result) {
    target::artifact::object::ObjectArtifact artifact;
    auto desc = target::TargetDescriptor::fromString(config_.targetTriple);
    if (!desc) {
        result.errors.push_back("Invalid target triple: " + config_.targetTriple);
        return artifact;
    }

    artifact.arch = desc->arch;
    artifact.os = desc->os;

    if (desc->os == target::OS::Windows) {
        artifact.format = target::artifact::object::ObjectFormat::COFF;
    } else if (desc->os == target::OS::MacOS) {
        artifact.format = target::artifact::object::ObjectFormat::MachO;
    } else if (desc->arch == target::Arch::WASM32) {
        artifact.format = target::artifact::object::ObjectFormat::Unknown;
    } else {
        artifact.format = target::artifact::object::ObjectFormat::ELF;
    }

    auto targetInfo = target::TargetResolver::resolve(*desc);
    if (!targetInfo) {
        result.errors.push_back("Failed to resolve target info for: " + config_.targetTriple);
        return artifact;
    }

    codegen::CodeGen codeGenerator(*preparedModule_, std::move(targetInfo), nullptr);
    codeGenerator.emit(false);

    std::map<std::string, std::vector<uint8_t>> sections;
    sections[".text"] = codeGenerator.getAssembler().getCode();
    sections[".rodata"] = codeGenerator.getRodataAssembler().getCode();

    target::artifact::object::ObjectSection textSec;
    textSec.name = ".text";
    textSec.data = sections[".text"];
    textSec.alignment = 16;
    textSec.flags = 0x6; // SHF_ALLOC | SHF_EXECINSTR
    artifact.addSection(textSec);

    if (!sections[".rodata"].empty()) {
        target::artifact::object::ObjectSection rodataSec;
        rodataSec.name = (desc->os == target::OS::Windows) ? ".rdata" : ".rodata";
        rodataSec.data = sections[".rodata"];
        rodataSec.alignment = 8;
        rodataSec.flags = 0x2; // SHF_ALLOC
        artifact.addSection(rodataSec);
    }

    for (const auto& sym : codeGenerator.getSymbols()) {
        target::artifact::object::ObjectSymbol out;
        out.name = sym.name;
        out.value = sym.value;
        out.size = sym.size;
        out.type = (sym.type == 2) ? target::artifact::object::SymbolType::Function
                                  : target::artifact::object::SymbolType::NoType;
        out.binding = (sym.binding == 1) ? target::artifact::object::SymbolBinding::Global
                                        : target::artifact::object::SymbolBinding::Local;
        out.sectionName = sym.sectionName;
        out.isDefined = true;
        artifact.addSymbol(out);
    }

    for (auto& func : preparedModule_->getFunctions()) {
        if (func->getBasicBlocks().empty()) continue;
        std::string fnName = func->getName();
        if (!artifact.findSymbol(fnName)) {
            target::artifact::object::ObjectSymbol s;
            s.name = fnName;
            s.value = 0;
            s.size = 0;
            s.sectionName = ".text";
            s.binding = target::artifact::object::SymbolBinding::Global;
            s.type = target::artifact::object::SymbolType::Function;
            s.isDefined = true;
            artifact.addSymbol(s);
        }
    }

    for (const auto& reloc : codeGenerator.getRelocations()) {
        if (!reloc.symbolName.empty() && !artifact.findSymbol(reloc.symbolName)) {
            target::artifact::object::ObjectSymbol s;
            s.name = reloc.symbolName;
            s.value = 0;
            s.size = 0;
            s.sectionName = "";
            s.binding = target::artifact::object::SymbolBinding::Global;
            s.type = target::artifact::object::SymbolType::Function;
            s.isDefined = false;
            artifact.addSymbol(s);
        }
        artifact.addRelocation({reloc.offset, reloc.type, reloc.addend,
                                reloc.symbolName, reloc.sectionName});
    }

    return artifact;
}

BuildResult BackendBuilder::emitAssembly(const std::string& path) {
    BuildResult result;
    result.kind = OutputKind::Assembly;
    result.outputPath = path;

    ensurePrepared(result);
    if (!result.errors.empty()) return result;

    auto desc = target::TargetDescriptor::fromString(config_.targetTriple);
    if (!desc) {
        result.errors.push_back("Invalid target triple: " + config_.targetTriple);
        return result;
    }

    if (desc->arch == target::Arch::WASM32) {
        target::wasm::WasmModule wasmMod = target::wasm::WasmLowering::lower(*preparedModule_);
        std::string wat = target::wasm::WasmWatWriter::write(wasmMod);
        std::ofstream outFile(path);
        if (!outFile.is_open()) {
            result.errors.push_back("Could not open output path: " + path);
            return result;
        }
        outFile << wat;
        outFile.close();
        result.success = true;
        return result;
    }

    auto targetInfo = target::TargetResolver::resolve(*desc);
    if (!targetInfo) {
        result.errors.push_back("Failed to resolve target info for: " + config_.targetTriple);
        return result;
    }

    codegen::CodeGen codeGen(*preparedModule_, std::move(targetInfo));
    auto cRes = codeGen.compileToAssembly(path, config_.validate);
    if (cRes.success) {
        result.success = true;
    } else {
        result.success = false;
        for (const auto& err : cRes.getAllErrors()) {
            result.errors.push_back(err);
        }
    }
    return result;
}

BuildResult BackendBuilder::emitObject(const std::string& path) {
    BuildResult result;
    result.kind = OutputKind::Object;
    result.outputPath = path;

    ensurePrepared(result);
    if (!result.errors.empty()) return result;

    auto desc = target::TargetDescriptor::fromString(config_.targetTriple);
    if (!desc) {
        result.errors.push_back("Invalid target triple: " + config_.targetTriple);
        return result;
    }

    auto targetInfo = target::TargetResolver::resolve(*desc);
    if (!targetInfo) {
        result.errors.push_back("Failed to resolve target info for: " + config_.targetTriple);
        return result;
    }

    codegen::CodeGen codeGen(*preparedModule_, std::move(targetInfo));
    auto cRes = codeGen.compileToObject(path, config_.validate, true, false);
    if (cRes.success) {
        result.success = true;
    } else {
        result.success = false;
        for (const auto& err : cRes.getAllErrors()) {
            result.errors.push_back(err);
        }
    }
    return result;
}

BuildResult BackendBuilder::emitStaticLibrary(const std::string& path) {
    BuildResult result;
    result.kind = OutputKind::StaticLibrary;
    result.outputPath = path;

    ensurePrepared(result);
    if (!result.errors.empty()) return result;

    target::artifact::object::ObjectArtifact art = buildModuleObjectArtifact(result);
    if (!result.errors.empty()) return result;

    auto desc = target::TargetDescriptor::fromString(config_.targetTriple);
    if (!desc) {
        result.errors.push_back("Invalid target triple: " + config_.targetTriple);
        return result;
    }

    auto writer = target::artifact::archive::ArchiveWriter::createForTarget(*desc);
    if (!writer) {
        result.errors.push_back("Failed to create ArchiveWriter for target: " + config_.targetTriple);
        return result;
    }

    target::artifact::archive::ArchiveMember member;
    member.name = "module.o";
    member.artifact = art;

    for (const auto& sym : art.symbols) {
        if (sym.isDefined && sym.binding == target::artifact::object::SymbolBinding::Global) {
            member.exportedSymbols.push_back(sym.name);
        }
    }

    auto objWriter = target::artifact::object::ObjectWriter::createForTargetTriple(config_.targetTriple);
    if (objWriter) {
        std::string tmpPath = path + ".tmp.o";
        if (objWriter->write(art, tmpPath)) {
            std::ifstream tf(tmpPath, std::ios::binary);
            if (tf.is_open()) {
                member.bytes = std::vector<uint8_t>((std::istreambuf_iterator<char>(tf)), std::istreambuf_iterator<char>());
                tf.close();
            }
            std::remove(tmpPath.c_str());
        }
    }

    if (writer->write({member}, path)) {
        result.success = true;
    } else {
        result.errors.push_back("ArchiveWriter failed: " + writer->getLastError());
    }

    return result;
}

BuildResult BackendBuilder::emitExecutable(const std::string& path) {
    BuildResult result;
    result.kind = OutputKind::Executable;
    result.outputPath = path;

    ensurePrepared(result);
    if (!result.errors.empty()) return result;

    target::artifact::object::ObjectArtifact moduleArt = buildModuleObjectArtifact(result);
    if (!result.errors.empty()) return result;

    std::vector<target::artifact::object::ObjectArtifact> artifacts;
    artifacts.push_back(moduleArt);

    std::vector<std::vector<target::artifact::archive::ArchiveObjectMember>> archives;

    for (const auto& inputObjPath : inputObjectPaths_) {
        std::ifstream f(inputObjPath, std::ios::binary);
        if (!f.is_open()) {
            result.errors.push_back("Cannot open input object file: " + inputObjPath);
            return result;
        }
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();

        auto objReader = target::artifact::object::ObjectReader::detectAndCreate(bytes);
        target::artifact::object::ObjectArtifact art;
        if (objReader && objReader->parse(bytes, art)) {
            artifacts.push_back(art);
        } else {
            result.errors.push_back("Failed to parse input object file: " + inputObjPath);
            return result;
        }
    }

    for (const auto& libPath : inputStaticLibPaths_) {
        std::ifstream f(libPath, std::ios::binary);
        if (!f.is_open()) {
            result.errors.push_back("Cannot open input static library file: " + libPath);
            return result;
        }
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();

        target::artifact::archive::ArchiveReader arReader;
        std::vector<target::artifact::archive::ArchiveObjectMember> members;
        if (arReader.parse(bytes, members)) {
            archives.push_back(std::move(members));
        } else {
            result.errors.push_back("Failed to parse static archive file: " + libPath + ": " + arReader.getLastError());
            return result;
        }
    }

    target::artifact::linker::InternalLinker linker;
    linker.extractLazyArchiveMembers(artifacts, archives);

    target::artifact::linker::LinkedImage image;
    if (!linker.link(artifacts, image, target::artifact::linker::LinkOutputKind::Executable, dynamicImports_)) {
        result.errors.push_back("Linker error: " + linker.getLastError());
        return result;
    }

    auto desc = target::TargetDescriptor::fromString(config_.targetTriple);
    if (!desc) {
        result.errors.push_back("Invalid target triple: " + config_.targetTriple);
        return result;
    }

    if (desc->os == target::OS::Windows) {
        target::artifact::executable::PeExecutableImageBuilder builder;
        bool ok = false;
        if (!dynamicImports_.empty()) {
            auto plan = target::artifact::linker::DynamicLinkPlan::createFromLinkedImage(image, dynamicImports_);
            ok = builder.buildWithPlan(plan, path);
        } else {
            ok = builder.build(image, path);
        }
        if (!ok) {
            result.errors.push_back("PE Executable generation failed: " + builder.getLastError());
            return result;
        }
    } else if (desc->os == target::OS::MacOS) {
        target::artifact::linker::MachOExecutableImageBuilder builder;
        if (!builder.build(image, path)) {
            result.errors.push_back("Mach-O Executable generation failed: " + builder.getLastError());
            return result;
        }
    } else {
        target::artifact::linker::ElfExecutableImageBuilder builder;
        if (!builder.build(image, path)) {
            result.errors.push_back("ELF Executable generation failed: " + builder.getLastError());
            return result;
        }
    }

    result.success = true;
    return result;
}

BuildResult BackendBuilder::emitSharedLibrary(const std::string& path) {
    BuildResult result;
    result.kind = OutputKind::SharedLibrary;
    result.outputPath = path;

    ensurePrepared(result);
    if (!result.errors.empty()) return result;

    target::artifact::object::ObjectArtifact moduleArt = buildModuleObjectArtifact(result);
    if (!result.errors.empty()) return result;

    std::vector<target::artifact::object::ObjectArtifact> artifacts;
    artifacts.push_back(moduleArt);

    std::vector<std::vector<target::artifact::archive::ArchiveObjectMember>> archives;

    for (const auto& inputObjPath : inputObjectPaths_) {
        std::ifstream f(inputObjPath, std::ios::binary);
        if (!f.is_open()) {
            result.errors.push_back("Cannot open input object file: " + inputObjPath);
            return result;
        }
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();

        auto objReader = target::artifact::object::ObjectReader::detectAndCreate(bytes);
        target::artifact::object::ObjectArtifact art;
        if (objReader && objReader->parse(bytes, art)) {
            artifacts.push_back(art);
        } else {
            result.errors.push_back("Failed to parse input object file: " + inputObjPath);
            return result;
        }
    }

    for (const auto& libPath : inputStaticLibPaths_) {
        std::ifstream f(libPath, std::ios::binary);
        if (!f.is_open()) {
            result.errors.push_back("Cannot open input static library file: " + libPath);
            return result;
        }
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();

        target::artifact::archive::ArchiveReader arReader;
        std::vector<target::artifact::archive::ArchiveObjectMember> members;
        if (arReader.parse(bytes, members)) {
            archives.push_back(std::move(members));
        } else {
            result.errors.push_back("Failed to parse static archive file: " + libPath + ": " + arReader.getLastError());
            return result;
        }
    }

    target::artifact::linker::InternalLinker linker;
    linker.extractLazyArchiveMembers(artifacts, archives);

    target::artifact::linker::LinkedImage image;
    if (!linker.link(artifacts, image, target::artifact::linker::LinkOutputKind::SharedLibrary, dynamicImports_)) {
        result.errors.push_back("Linker error: " + linker.getLastError());
        return result;
    }

    auto desc = target::TargetDescriptor::fromString(config_.targetTriple);
    if (!desc) {
        result.errors.push_back("Invalid target triple: " + config_.targetTriple);
        return result;
    }

    auto plan = target::artifact::linker::DynamicLinkPlan::createFromLinkedImage(image, dynamicImports_);
    auto builder = target::artifact::linker::TargetDynamicImageBuilder::createForTarget(desc->arch, desc->os);
    if (!builder || !builder->buildSharedLibrary(plan, path)) {
        result.errors.push_back("Shared library generation failed: " + (builder ? builder->getLastError() : "Unsupported target"));
        return result;
    }

    result.success = true;
    return result;
}

BuildResult BackendBuilder::emitWAT(const std::string& path) {
    BuildResult result;
    result.kind = OutputKind::WAT;
    result.outputPath = path;

    ensurePrepared(result);
    if (!result.errors.empty()) return result;

    target::wasm::WasmModule wasmMod = target::wasm::WasmLowering::lower(*preparedModule_);
    std::string wat = target::wasm::WasmWatWriter::write(wasmMod);

    std::ofstream outFile(path);
    if (!outFile.is_open()) {
        result.errors.push_back("Could not open output path: " + path);
        return result;
    }
    outFile << wat;
    outFile.close();

    result.success = true;
    return result;
}

BuildResult BackendBuilder::emitWasm(const std::string& path) {
    BuildResult result;
    result.kind = OutputKind::Wasm;
    result.outputPath = path;

    ensurePrepared(result);
    if (!result.errors.empty()) return result;

    target::wasm::WasmModule wasmMod = target::wasm::WasmLowering::lower(*preparedModule_);
    std::vector<uint8_t> bytes = target::wasm::WasmBinaryWriter::write(wasmMod);

    std::ofstream outFile(path, std::ios::binary);
    if (!outFile.is_open()) {
        result.errors.push_back("Could not open output path: " + path);
        return result;
    }
    outFile.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    outFile.close();

    result.success = true;
    return result;
}

} // namespace fyra
