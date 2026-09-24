#include <iostream>
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
#include "fyra/BackendBuilder.h"

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
    auto ctx = std::make_shared<IRContext>();
    Module module("phpish_oop_inmemory", ctx);
    IRBuilder builder(ctx);
    builder.setModule(&module);

    auto* i32 = ctx->getIntegerType(32);

    // Class method lowered to a standalone function: Counter_add(thisValue, delta).
    // This toy frontend models the object's single field as an SSA value.
    const std::string loweredMethodName = program.className + "_" + program.methodName;
    Function* methodFn = builder.createFunction(loweredMethodName, i32, {i32, i32});
    BasicBlock* methodEntry = builder.createBasicBlock("entry", methodFn);
    builder.setInsertPoint(methodEntry);

    auto paramIt = methodFn->getParameters().begin();
    Parameter* thisValue = paramIt->get();
    ++paramIt;
    Parameter* delta = paramIt->get();

    auto* newValue = builder.createAdd(thisValue, delta);
    builder.createRet(newValue);

    // main: new Counter(init), call add(arg), return result.
    Function* mainFn = builder.createFunction("main", i32);
    mainFn->setExported(true);
    BasicBlock* mainEntry = builder.createBasicBlock("entry", mainFn);
    builder.setInsertPoint(mainEntry);

    Value* initVal = ConstantInt::get(i32, program.initValue);
    Value* argVal = ConstantInt::get(i32, program.methodArg);

    auto* methodResult = builder.createCall(methodFn, {initVal, argVal}, i32);
    builder.createRet(methodResult);

    const std::string outputPath = "./example_phpish";
    fyra::BackendBuilder backend(module);
    fyra::BuildResult build = backend.target("x64-linux-bin")
                                  .optimize(fyra::OptimizationLevel::O0)
                                  .emitExecutable(outputPath);
    if (!build.success) {
        const std::string detail = build.errors.empty() ? "unknown error" : build.errors.front();
        throw std::runtime_error("ELF generation failed: " + detail);
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

        std::cout << "Success: tiny PHP-like OOP frontend passes BackendBuilder codegen + execution.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
}
