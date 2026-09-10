#include <iostream>
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <sys/wait.h>
#include <vector>

#include "ir/Constant.h"
#include "ir/IRBuilder.h"
#include "ir/Module.h"
#include "ir/Parameter.h"
#include "ir/Type.h"
#include "codegen/CodeGen.h"
#include "target/core/TargetResolver.h"
#include "target/core/TargetInfo.h"
#include "target/core/TargetDescriptor.h"

#include "target/artifact/executable/ElfImage.h"
#include "target/artifact/linker/InternalLinker.h"
#include "target/artifact/object/ObjectArtifact.h"

namespace {

struct MiniProgram {
    std::string className;
    std::string methodName;
    int initValue = 0;
    int methodArg = 0;
};

MiniProgram parseMiniPhpLike(const std::string& source) {
    MiniProgram program;

    std::smatch m;
    std::regex classRegex(R"(class\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{)");
    std::regex methodRegex(R"(fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*\$[A-Za-z_][A-Za-z0-9_]*\s*\))");
    std::regex newRegex(R"(new\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*([0-9]+)\s*\))");
    std::regex callRegex(R"(->\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*([0-9]+)\s*\))");

    if (!std::regex_search(source, m, classRegex)) {
        throw std::runtime_error("Expected a class declaration.");
    }
    program.className = m[1].str();

    if (!std::regex_search(source, m, methodRegex)) {
        throw std::runtime_error("Expected a method declaration.");
    }
    program.methodName = m[1].str();

    if (!std::regex_search(source, m, newRegex)) {
        throw std::runtime_error("Expected `new ClassName(<number>)` in main.");
    }
    const std::string classInMain = m[1].str();
    if (classInMain != program.className) {
        throw std::runtime_error("Class in `new` does not match declared class.");
    }
    program.initValue = std::stoi(m[2].str());

    if (!std::regex_search(source, m, callRegex)) {
        throw std::runtime_error("Expected `$obj->method(<number>)` call in main.");
    }
    const std::string calledMethod = m[1].str();
    if (calledMethod != program.methodName) {
        throw std::runtime_error("Method call does not match declared method.");
    }
    program.methodArg = std::stoi(m[2].str());

    return program;
}

int compileAndRun(const MiniProgram& program) {
    using namespace ir;
    using namespace codegen;
    using namespace target;

    auto ctx = std::make_shared<IRContext>();
    Module module("phpish_oop_inmemory", ctx);
    IRBuilder builder(ctx);
    builder.setModule(&module);

    auto* i32 = ctx->getIntegerType(32);
    auto* i64 = ctx->getIntegerType(64);

    // class method lowered to a standalone function: Counter_add(thisPtr, delta)
    const std::string loweredMethodName = program.className + "_" + program.methodName;
    Function* methodFn = builder.createFunction(loweredMethodName, i32, {i64, i32});
    BasicBlock* methodEntry = builder.createBasicBlock("entry", methodFn);
    builder.setInsertPoint(methodEntry);

    auto paramIt = methodFn->getParameters().begin();
    Parameter* thisPtr = paramIt->get();
    ++paramIt;
    Parameter* delta = paramIt->get();

    auto* curValue = builder.createLoad(thisPtr);
    auto* newValue = builder.createAdd(curValue, delta);
    builder.createStore(newValue, thisPtr);
    builder.createRet(newValue);

    // main: new Counter(init), call add(arg), return result.
    Function* mainFn = builder.createFunction("main", i32);
    mainFn->setExported(true);
    BasicBlock* mainEntry = builder.createBasicBlock("entry", mainFn);
    builder.setInsertPoint(mainEntry);

    Value* initVal = ConstantInt::get(i32, program.initValue);
    Value* argVal = ConstantInt::get(i32, program.methodArg);

    auto* obj = builder.createAlloc(ConstantInt::get(i32, 4), i32);
    builder.createStore(initVal, obj);

    auto* methodResult = builder.createCall(methodFn, {obj, argVal}, i32);
    builder.createRet(methodResult);

    auto target = target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux});

    CodeGen cg(module, std::move(target), nullptr);
    cg.emit(true);

    std::map<std::string, std::vector<uint8_t>> sections;
    sections[".text"] = cg.getAssembler().getCode();
    sections[".data"] = cg.getRodataAssembler().getCode();

    const std::string outputPath = "./example_phpish";
    target::artifact::object::ObjectArtifact artifact;
    artifact.format = target::artifact::object::ObjectFormat::ELF;
    artifact.arch = target::Arch::X64;
    artifact.os = target::OS::Linux;
    target::artifact::object::ObjectSection text;
    text.name = ".text"; text.data = sections[".text"]; text.alignment = 16; text.flags = 0x6;
    artifact.addSection(text);
    for (const auto& sym : cg.getSymbols()) {
        target::artifact::object::ObjectSymbol objectSymbol;
        objectSymbol.name = sym.name; objectSymbol.value = sym.value; objectSymbol.size = sym.size;
        objectSymbol.type = sym.type == 2 ? target::artifact::object::SymbolType::Function
                                          : target::artifact::object::SymbolType::NoType;
        objectSymbol.binding = sym.binding == 1 ? target::artifact::object::SymbolBinding::Global
                                                 : target::artifact::object::SymbolBinding::Local;
        objectSymbol.sectionName = sym.sectionName;
        artifact.addSymbol(objectSymbol);
    }
    for (const auto& reloc : cg.getRelocations()) {
        artifact.addRelocation({reloc.offset, reloc.type, reloc.addend, reloc.symbolName, reloc.sectionName});
    }
    target::artifact::linker::InternalLinker linker;
    target::artifact::linker::LinkedImage image;
    if (!linker.link({artifact}, image, target::artifact::linker::LinkOutputKind::Executable)) {
        throw std::runtime_error("ELF link failed: " + linker.getLastError());
    }
    target::artifact::linker::ElfExecutableImageBuilder imageBuilder;
    if (!imageBuilder.build(image, outputPath)) {
        throw std::runtime_error("ELF generation failed: " + imageBuilder.getLastError());
    }

    std::string runCmd = outputPath;
    int result = std::system(runCmd.c_str());
    if (result == -1) {
        throw std::runtime_error("Failed to run generated executable.");
    }

    return WEXITSTATUS(result);
}

} // namespace

int main() {
    const std::string source = R"(
class Counter {
    var value;

    fn add($delta) {
        $this->value = $this->value + $delta;
        return $this->value;
    }
}

main {
    $c = new Counter(10);
    return $c->add(32);
}
)";

    try {
        MiniProgram program = parseMiniPhpLike(source);
        int exitCode = compileAndRun(program);

        std::cout << "Linux program returned: " << exitCode << '\n';
        std::cout << "Expected: " << (program.initValue + program.methodArg) << '\n';

        if (exitCode != program.initValue + program.methodArg) {
            std::cerr << "Execution mismatch.\n";
            return 1;
        }

        std::cout << "Success: tiny PHP-like OOP frontend passes in-memory codegen + execution.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}
