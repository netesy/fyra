#include "fyra/BackendBuilder.h"
#include "ir/IRBuilder.h"
#include "ir/Module.h"
#include "ir/Use.h"
#include "transforms/DeadInstructionElimination.h"
#include "transforms/FunctionInliner.h"
#include "target/artifact/object/ElfObjectWriter.h"
#include "target/artifact/object/ElfObjectReader.h"
#include <stdexcept>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

static void require(bool condition) {
    if (!condition) throw std::runtime_error("Fyra object runtime regression failed");
}

static void section_relocations() {
    using namespace target::artifact::object;
    ObjectArtifact object;
    object.format = ObjectFormat::ELF;
    object.arch = target::Arch::X64;
    object.os = target::OS::Linux;
    ObjectSection text; text.name = ".text"; text.data.resize(8); object.addSection(text);
    ObjectSection data; data.name = ".data"; data.data.resize(8); object.addSection(data);
    ObjectSymbol symbol; symbol.name = "external"; symbol.binding = SymbolBinding::Global;
    symbol.isDefined = false; object.addSymbol(symbol);
    for (auto section : {".text", ".data"}) {
        ObjectRelocation relocation;
        relocation.sectionName = section; relocation.symbolName = "external";
        relocation.type = "R_X86_64_64"; object.addRelocation(relocation);
    }
    ElfObjectWriter writer; ElfObjectReader reader; ObjectArtifact roundtrip;
    require(reader.parse(writer.serialize(object), roundtrip));
    require(roundtrip.relocations.size() == 2);
    bool text_found = false, data_found = false;
    for (const auto& relocation : roundtrip.relocations) {
        text_found |= relocation.sectionName == ".text";
        data_found |= relocation.sectionName == ".data";
    }
    require(text_found && data_found);
}

static void capability_side_effects() {
    auto context = std::make_shared<ir::IRContext>();
    ir::Module module("capability-cloning", context);
    ir::IRBuilder builder(context); builder.setModule(&module);
    auto i64 = context->getIntegerType(64);
    auto* callee = builder.createFunction("exit_probe", context->getVoidType(), {i64});
    builder.setInsertPoint(builder.createBasicBlock("entry", callee));
    builder.createExternCall("process.exit", {callee->getParameters().front().get()});
    builder.createRet(nullptr);
    auto* caller = builder.createFunction("main", context->getVoidType(), {});
    builder.setInsertPoint(builder.createBasicBlock("entry", caller));
    builder.createCall(callee, {context->getConstantInt(i64, 1)});
    builder.createRet(nullptr);
    transforms::FunctionInliner inliner; require(inliner.runOnModule(module));
    transforms::DeadInstructionElimination dce; dce.run(*caller);
    bool found = false;
    for (const auto& block : caller->getBasicBlocks())
        for (const auto& instruction : block->getInstructions())
            if (auto* external = dynamic_cast<ir::ExternCallInstruction*>(instruction.get()))
                found |= external->getCapability() == "process.exit";
    require(found);
}

static void execute_object(fyra::OptimizationLevel level, const std::filesystem::path& directory) {
    auto context = std::make_shared<ir::IRContext>();
    ir::Module module("object-runtime", context);
    ir::IRBuilder builder(context); builder.setModule(&module);
    auto i64 = context->getIntegerType(64);
    auto f64 = context->getDoubleType();
    auto* mixed = builder.createFunction("mixed", f64, {f64, i64, f64, i64, i64, i64, i64, i64, i64});
    builder.setInsertPoint(builder.createBasicBlock("entry", mixed));
    auto parameters = mixed->getParameters().begin();
    auto* a = parameters++->get(); auto* b = parameters++->get(); auto* c = parameters++->get();
    ir::Value* sum = b;
    for (; parameters != mixed->getParameters().end(); ++parameters)
        sum = builder.createAdd(sum, parameters->get());
    auto* slot = builder.createAlloc(context->getConstantInt(i64, 8), f64);
    builder.createStored(builder.createFAdd(builder.createFAdd(a, c), builder.createSltof(sum, f64)), slot);
    builder.createRet(builder.createLoadd(slot));
    auto* verify = builder.createFunction("verify", context->getVoidType(), {f64});
    auto* verify_word = builder.createFunction("verify_word", context->getVoidType(), {i64});
    auto* poke = builder.createFunction("poke", context->getVoidType(), {context->getPointerType(i64)});
    auto* ordered = builder.createFunction("ordered", i64, {});
    builder.setInsertPoint(builder.createBasicBlock("entry", ordered));
    auto* local = builder.createAlloc(context->getConstantInt(i64, 8), i64);
    builder.createStore(context->getConstantInt(i64, 7), local);
    auto* first = builder.createLoad(local);
    builder.createStore(context->getConstantInt(i64, 9), local);
    builder.createRet(builder.createAdd(first, builder.createLoad(local)));
    auto* main = builder.createFunction("main", i64, {});
    builder.setInsertPoint(builder.createBasicBlock("entry", main));
    std::vector<ir::Value*> args{context->getConstantFP(f64, 1.5), context->getConstantInt(i64, 1),
                               context->getConstantFP(f64, 2.5)};
    for (int n = 2; n <= 7; ++n) args.push_back(context->getConstantInt(i64, n));
    builder.createCall(verify, {builder.createCall(mixed, args)});
    auto* observable = builder.createAlloc(context->getConstantInt(i64, 8), i64);
    builder.createStore(context->getConstantInt(i64, 7), observable);
    builder.createCall(poke, {observable});
    builder.createCall(verify_word, {builder.createAdd(builder.createLoad(observable), context->getConstantInt(i64, 2))});
    builder.createCall(verify_word, {builder.createAdd(builder.createCall(ordered, {}), context->getConstantInt(i64, 26))});
    builder.createRet(context->getConstantInt(i64, 0));
    fyra::BackendBuilder backend(module);
    backend.target("x64-linux-bin").optimize(level).validate(false);
    auto object = directory / "program.o";
    auto result = backend.emitObject(object.string());
    if (!result.success) for (const auto& error : result.errors) std::cerr << error << '\n';
    require(result.success);
    auto harness = directory / "harness.c", executable = directory / "program";
    std::ofstream(harness) << "#include <stdlib.h>\nvoid verify(double v){if(v != 32.0)abort();}\nvoid verify_word(long v){if(v != 42)abort();}\nvoid poke(long* p){if(*p != 7)abort(); *p=40;}\n";
    auto quote = [](const std::filesystem::path& path) {
        std::string result = "'";
        for (char c : path.string()) result += c == '\'' ? "'\\''" : std::string(1, c);
        return result + "'";
    };
    std::string command = "cc -no-pie -Wl,-z,noexecstack " + quote(object) + " " + quote(harness) + " -o " + quote(executable);
    require(std::system(command.c_str()) == 0);
    require(std::system(quote(executable).c_str()) == 0);
}

int main() {
    section_relocations();
    capability_side_effects();
#if defined(__linux__) && defined(__x86_64__)
    auto directory = std::filesystem::temp_directory_path() / ("fyra-object-runtime-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    execute_object(fyra::OptimizationLevel::O0, directory);
    execute_object(fyra::OptimizationLevel::O2, directory);
    std::filesystem::remove_all(directory);
#endif
    std::cout << "ELF section relocations, capability side effects, stack allocation and mixed call ABI passed\n";
}
