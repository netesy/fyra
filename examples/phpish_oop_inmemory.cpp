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

#include "target/artifact/executable/elf.hh"
#include "target/artifact/executable/pe.hh"

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

int compileAndRun(const MiniProgram& program, bool isWindows = false) {
    using namespace ir;
    using namespace codegen;
    using namespace codegen::target;

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

    std::unique_ptr<TargetInfo> target;
    if (isWindows) {
        target = codegen::target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Windows});
    } else {
        target = codegen::target::TargetResolver::resolve({::target::Arch::X64, ::target::OS::Linux});
    }

    CodeGen cg(module, std::move(target), nullptr);
    cg.emit(true);

    std::map<std::string, std::vector<uint8_t>> sections;
    sections[".text"] = cg.getAssembler().getCode();
    sections[".data"] = cg.getRodataAssembler().getCode();

    const std::string outputPath = isWindows ? "./example_phpish.exe" : "./example_phpish";

    if (isWindows) {
        PEGenerator peGen(true); // 64-bit
        std::vector<PEGenerator::Symbol> symbols;
        for (const auto& sym : cg.getSymbols()) {
            symbols.push_back({sym.name, sym.value, sym.size, static_cast<uint8_t>(sym.type), static_cast<uint8_t>(sym.binding), sym.sectionName});
        }
        std::vector<PEGenerator::Relocation> relocs;
        for (const auto& reloc : cg.getRelocations()) {
            relocs.push_back({reloc.offset, reloc.type, reloc.addend, reloc.symbolName, reloc.sectionName});
        }
        if (!peGen.generateFromCode(sections, symbols, relocs, outputPath)) {
             throw std::runtime_error("PE generation failed: " + peGen.getLastError());
        }
    } else {
        ElfGenerator elfGen("phpish_oop_inmemory");
        elfGen.setMachine(62); // EM_X86_64
        elfGen.setBaseAddress(0x400000);

        std::vector<ElfGenerator::Symbol> symbols;
        for (const auto& sym : cg.getSymbols()) {
            symbols.push_back({sym.name, sym.value, sym.size,
                               static_cast<uint8_t>(sym.type), static_cast<uint8_t>(sym.binding), sym.sectionName});
        }

        std::vector<ElfGenerator::Relocation> relocs;
        for (const auto& reloc : cg.getRelocations()) {
            relocs.push_back({reloc.offset, reloc.type, reloc.addend, reloc.symbolName, reloc.sectionName});
        }

        if (!elfGen.generateFromCode(sections, symbols, relocs, outputPath)) {
            throw std::runtime_error("ELF generation failed: " + elfGen.getLastError());
        }
        std::string chmodCmd = "chmod +x " + outputPath;
        if (std::system(chmodCmd.c_str()) != 0) {
            throw std::runtime_error("chmod failed for generated executable.");
        }
    }

    std::string runCmd = isWindows ? "wine " + outputPath : outputPath;
    int result = std::system(runCmd.c_str());
    if (result == -1) {
        throw std::runtime_error("Failed to run generated executable.");
    }

    return isWindows ? (result & 0xFF) : WEXITSTATUS(result);
}

} // namespace

int main(int argc, char** argv) {
    bool testWindows = false;
    if (argc > 1 && std::string(argv[1]) == "--windows") {
        testWindows = true;
    }
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
        int exitCode = compileAndRun(program, testWindows);

        std::cout << (testWindows ? "Windows" : "Linux") << " program returned: " << exitCode << '\n';
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
