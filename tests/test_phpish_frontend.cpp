#include <cassert>
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
#include "codegen/target/SystemV_x64.h"
#include "../src/codegen/execgen/elf.hh"

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

    if (!std::regex_search(source, m, classRegex)) throw std::runtime_error("Missing class");
    program.className = m[1].str();

    if (!std::regex_search(source, m, methodRegex)) throw std::runtime_error("Missing method");
    program.methodName = m[1].str();

    if (!std::regex_search(source, m, newRegex)) throw std::runtime_error("Missing object construction");
    if (m[1].str() != program.className) throw std::runtime_error("Mismatched class in constructor");
    program.initValue = std::stoi(m[2].str());

    if (!std::regex_search(source, m, callRegex)) throw std::runtime_error("Missing method call");
    if (m[1].str() != program.methodName) throw std::runtime_error("Mismatched method call");
    program.methodArg = std::stoi(m[2].str());

    return program;
}

int compileAndRun(const MiniProgram& program, const std::string& tag) {
    using namespace ir;
    using namespace codegen;
    using namespace codegen::target;

    Module module("phpish_frontend_test_" + tag);
    IRBuilder builder;
    builder.setModule(&module);

    auto* i32 = IntegerType::get(32);
    auto* i64 = IntegerType::get(64);

    Function* methodFn = builder.createFunction(program.className + "_" + program.methodName, i32, {i64, i32});
    BasicBlock* methodEntry = builder.createBasicBlock("entry", methodFn);
    builder.setInsertPoint(methodEntry);

    auto pit = methodFn->getParameters().begin();
    auto* thisPtr = pit->get();
    ++pit;
    auto* delta = pit->get();

    auto* current = builder.createLoad(thisPtr);
    auto* updated = builder.createAdd(current, delta);
    builder.createStore(updated, thisPtr);
    builder.createRet(updated);

    Function* mainFn = builder.createFunction("main", i32);
    mainFn->setExported(true);
    BasicBlock* entry = builder.createBasicBlock("entry", mainFn);
    builder.setInsertPoint(entry);

    auto* obj = builder.createAlloc(ConstantInt::get(i32, 4), i32);
    builder.createStore(ConstantInt::get(i32, program.initValue), obj);
    auto* call = builder.createCall(methodFn, {obj, ConstantInt::get(i32, program.methodArg)}, i32);
    builder.createRet(call);

    auto target = std::make_unique<SystemV_x64>();
    CodeGen cg(module, std::move(target), nullptr);
    cg.emit(true);

    std::map<std::string, std::vector<uint8_t>> sections;
    sections[".text"] = cg.getAssembler().getCode();
    sections[".data"] = cg.getRodataAssembler().getCode();

    ElfGenerator elfGen("phpish_frontend_test");
    elfGen.setMachine(62);
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

    const std::string out = "./phpish_frontend_test_exec_" + tag;
    if (!elfGen.generateFromCode(sections, symbols, relocs, out)) {
        throw std::runtime_error("ELF generation failed: " + elfGen.getLastError());
    }

    if (std::system(("chmod +x " + out).c_str()) != 0) {
        throw std::runtime_error("chmod failed");
    }

    int result = std::system(out.c_str());
    if (result == -1) throw std::runtime_error("execution failed");

    std::remove(out.c_str());
    return WEXITSTATUS(result);
}

void runCase(const std::string& name, const std::string& source, int expected) {
    MiniProgram p = parseMiniPhpLike(source);
    int rc = compileAndRun(p, name);
    std::cout << name << " => " << rc << "\n";
    assert(rc == expected);
}

} // namespace

int main() {
    runCase("counter_42", R"(
class Counter {
    var value;
    fn add($delta) { return $this->value + $delta; }
}
main {
    $c = new Counter(10);
    return $c->add(32);
}
)", 42);

    runCase("counter_8", R"(
class Counter {
    var value;
    fn add($d) { return $this->value + $d; }
}
main {
    $c = new Counter(3);
    return $c->add(5);
}
)", 8);

    bool parseFailed = false;
    try {
        parseMiniPhpLike(R"(
class A { fn add($x) { return $x; } }
main { $c = new B(1); return $c->add(1); }
)");
    } catch (const std::exception&) {
        parseFailed = true;
    }
    assert(parseFailed && "Frontend should reject mismatched class name in constructor");

    std::cout << "All phpish frontend tests passed.\n";
    return 0;
}
