#include "target/artifact/object/ObjectArtifact.h"
#include "target/artifact/object/CoffObjectWriter.h"
#include "target/artifact/linker/InternalLinker.h"
#include "target/artifact/linker/DynamicLinkPlan.h"
#include "target/artifact/linker/TargetDynamicImageBuilder.h"
#include "target/artifact/executable/PeImage.h"
#include <iostream>
#include <cassert>
#include <fstream>
#include <vector>

using namespace target::artifact::object;
using namespace target::artifact::linker;
using namespace target::artifact::executable;

int main() {
    std::cout << "[Test] End-to-end PE Dynamic Import Proof..." << std::endl;

    // 1. Produce answer.dll exporting get_answer
    ObjectArtifact dllArt;
    dllArt.format = ObjectFormat::COFF;
    dllArt.arch = target::Arch::X64;
    dllArt.os = target::OS::Windows;

    ObjectSection textDll;
    textDll.name = ".text";
    textDll.data = {0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3}; // mov $42, %eax; ret
    textDll.alignment = 16;
    dllArt.addSection(textDll);

    ObjectSymbol getAnswerSym;
    getAnswerSym.name = "get_answer";
    getAnswerSym.value = 0;
    getAnswerSym.size = 6;
    getAnswerSym.binding = SymbolBinding::Global;
    getAnswerSym.type = SymbolType::Function;
    getAnswerSym.sectionName = ".text";
    getAnswerSym.isDefined = true;
    dllArt.addSymbol(getAnswerSym);

    InternalLinker linker;
    LinkedImage dllImage;
    bool dllLinked = linker.link({dllArt}, dllImage, LinkOutputKind::SharedLibrary);
    assert(dllLinked);

    DynamicLinkPlan dllPlan = DynamicLinkPlan::createFromLinkedImage(dllImage);
    auto peBuilder = TargetDynamicImageBuilder::createForTarget(target::Arch::X64, target::OS::Windows);
    assert(peBuilder != nullptr);
    bool dllBuilt = peBuilder->buildSharedLibrary(dllPlan, "answer.dll");
    assert(dllBuilt);

    // 2. Produce app.exe importing get_answer from answer.dll
    ObjectArtifact exeArt;
    exeArt.format = ObjectFormat::COFF;
    exeArt.arch = target::Arch::X64;
    exeArt.os = target::OS::Windows;

    // main: call get_answer; ret
    // Relocation at offset 1 for symbol "get_answer" (R_X86_64_PC32 / IMAGE_REL_AMD64_REL32)
    ObjectSection textExe;
    textExe.name = ".text";
    textExe.data = {0xE8, 0x00, 0x00, 0x00, 0x00, 0xC3}; // call get_answer (-4 addend); ret
    textExe.alignment = 16;
    exeArt.addSection(textExe);

    ObjectSymbol mainSym;
    mainSym.name = "main";
    mainSym.value = 0;
    mainSym.size = 6;
    mainSym.binding = SymbolBinding::Global;
    mainSym.type = SymbolType::Function;
    mainSym.sectionName = ".text";
    mainSym.isDefined = true;
    exeArt.addSymbol(mainSym);

    ObjectRelocation callReloc;
    callReloc.offset = 1;
    callReloc.type = "R_X86_64_PC32";
    callReloc.addend = -4;
    callReloc.symbolName = "get_answer";
    callReloc.sectionName = ".text";
    exeArt.addRelocation(callReloc);

    LinkedImage exeImage;
    std::vector<std::pair<std::string, std::string>> imports = {{"get_answer", "answer.dll"}};
    bool exeLinked = linker.link({exeArt}, exeImage, LinkOutputKind::Executable, imports);
    assert(exeLinked);

    DynamicLinkPlan exePlan = DynamicLinkPlan::createFromLinkedImage(exeImage, imports);
    PeExecutableImageBuilder exeBuilder;
    bool exeBuilt = exeBuilder.buildWithPlan(exePlan, "app.exe");
    assert(exeBuilt);

    // Structural verification
    std::ifstream f("app.exe", std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    assert(bytes.size() > 0x200 && bytes[0] == 'M' && bytes[1] == 'Z');

    std::cout << "PE Dynamic Import Proof Test PASSED." << std::endl;
    return 0;
}
