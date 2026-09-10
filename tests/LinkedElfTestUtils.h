#pragma once

#include "codegen/CodeGen.h"
#include "target/artifact/executable/ElfImage.h"
#include "target/artifact/linker/InternalLinker.h"
#include "target/artifact/object/ObjectArtifact.h"

#include <string>

inline bool writeLinkedX64Elf(codegen::CodeGen& codegen, const std::string& output,
                              std::string& error) {
    target::artifact::object::ObjectArtifact artifact;
    artifact.format = target::artifact::object::ObjectFormat::ELF;
    artifact.arch = target::Arch::X64;
    artifact.os = target::OS::Linux;
    target::artifact::object::ObjectSection text;
    text.name = ".text";
    text.data = codegen.getAssembler().getCode();
    text.alignment = 16;
    text.flags = 0x6;
    artifact.addSection(text);
    for (const auto& symbol : codegen.getSymbols()) {
        target::artifact::object::ObjectSymbol out;
        out.name = symbol.name; out.value = symbol.value; out.size = symbol.size;
        out.type = symbol.type == 2 ? target::artifact::object::SymbolType::Function
                                    : target::artifact::object::SymbolType::NoType;
        out.binding = symbol.binding == 1 ? target::artifact::object::SymbolBinding::Global
                                          : target::artifact::object::SymbolBinding::Local;
        out.sectionName = symbol.sectionName;
        artifact.addSymbol(out);
    }
    for (const auto& relocation : codegen.getRelocations()) {
        artifact.addRelocation({relocation.offset, relocation.type, relocation.addend,
                                relocation.symbolName, relocation.sectionName});
    }
    target::artifact::linker::InternalLinker linker;
    target::artifact::linker::LinkedImage image;
    if (!linker.link({artifact}, image, target::artifact::linker::LinkOutputKind::Executable)) {
        error = linker.getLastError();
        return false;
    }
    target::artifact::linker::ElfExecutableImageBuilder builder;
    if (!builder.build(image, output)) {
        error = builder.getLastError();
        return false;
    }
    return true;
}
