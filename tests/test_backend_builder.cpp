#include "fyra/BackendBuilder.h"
#include "ir/IRContext.h"
#include "ir/IRBuilder.h"
#include "ir/Module.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <vector>
#include <unistd.h>

static void runNodeVerification(const std::string& wasmPath, const std::string& checkJs) {
    std::string cmd = "node -e 'const fs=require(\"fs\"); const bytes=fs.readFileSync(\"" + wasmPath + "\"); if (!WebAssembly.validate(bytes)) { console.error(\"INVALID WASM\"); process.exit(1); } const m=new WebAssembly.Module(bytes); const i=new WebAssembly.Instance(m); " + checkJs + "' > /tmp/node_test.log 2>&1";
    int res = std::system(cmd.c_str());
    if (res != 0) {
        std::cerr << "Node verification failed. Output log:" << std::endl;
        std::ifstream logFile("/tmp/node_test.log");
        std::cerr << logFile.rdbuf() << std::endl;
    }
    assert(res == 0);
}

int main() {
    std::cout << "=== Running BackendBuilder API Integration Test Suite ===" << std::endl;

    auto ctx = std::make_shared<ir::IRContext>();

    // Test 1: Basic Module (main -> 42) for Assembly, Object, Executable
    {
        ir::Module module("test_basic", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        auto* i32 = ctx->getIntegerType(32);
        ir::Function* fn = builder.createFunction("main", i32);
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fn);
        builder.setInsertPoint(entry);
        builder.createRet(ctx->getConstantInt(i32, 42));

        fyra::BackendBuilder backend(module);
        backend.target("x64-linux-bin")
               .optimize(fyra::OptimizationLevel::O2);

        std::cout << "Emitting assembly..." << std::endl;
        fyra::BuildResult resAsm = backend.emitAssembly("/tmp/test_basic.s");
        std::cout << "Asm res: " << resAsm.success << std::endl;
        assert(resAsm.success);

        std::cout << "Emitting object..." << std::endl;
        fyra::BuildResult resObj = backend.emitObject("/tmp/test_basic.o");
        std::cout << "Obj res: " << resObj.success << std::endl;
        assert(resObj.success);

        std::cout << "Emitting executable..." << std::endl;
        fyra::BuildResult resExec = backend.emitExecutable("/tmp/test_basic_exec");
        std::cout << "Exec res: " << resExec.success << std::endl;
        assert(resExec.success);

        // Execute generated host executable and check exit code
        int rc = std::system("/tmp/test_basic_exec");
        int exitCode = WEXITSTATUS(rc);
        std::cout << "Host executable exit status: " << exitCode << std::endl;
        assert(exitCode == 42);
    }

    // Test 2: WAT and WASM Generation + Node Verification
    {
        ir::Module module("test_wasm_mod", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        auto* i32 = ctx->getIntegerType(32);
        ir::Function* fn = builder.createFunction("main", i32);
        fn->setExported(true);
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fn);
        builder.setInsertPoint(entry);
        builder.createRet(ctx->getConstantInt(i32, 42));

        fyra::BackendBuilder backend(module);
        backend.target("wasm32-wasi-wasm");

        // WAT
        fyra::BuildResult resWat = backend.emitWAT("/tmp/test_wasm_mod.wat");
        assert(resWat.success);
        assert(resWat.kind == fyra::OutputKind::WAT);

        std::ifstream watFile("/tmp/test_wasm_mod.wat");
        std::stringstream watBuf;
        watBuf << watFile.rdbuf();
        std::string watText = watBuf.str();
        assert(watText.find("module") != std::string::npos);
        assert(watText.find("export \"main\"") != std::string::npos);
        std::cout << "Generated WAT output:\n" << watText << std::endl;

        // WASM
        fyra::BuildResult resWasm = backend.emitWasm("/tmp/test_wasm_mod.wasm");
        assert(resWasm.success);
        assert(resWasm.kind == fyra::OutputKind::Wasm);

        std::ifstream wasmFile("/tmp/test_wasm_mod.wasm", std::ios::binary);
        std::vector<uint8_t> wasmBytes((std::istreambuf_iterator<char>(wasmFile)), std::istreambuf_iterator<char>());
        std::cout << "Generated WASM binary size: " << wasmBytes.size() << " bytes, prefix: 0x";
        for (size_t i = 0; i < 4 && i < wasmBytes.size(); ++i) {
            std::cout << std::hex << (int)wasmBytes[i];
        }
        std::cout << std::dec << std::endl;
        assert(wasmBytes.size() > 8);
        assert(wasmBytes[0] == 0x00 && wasmBytes[1] == 0x61 && wasmBytes[2] == 0x73 && wasmBytes[3] == 0x6d);

        runNodeVerification("/tmp/test_wasm_mod.wasm", "if (i.exports.main() !== 42) process.exit(1);");
        std::cout << "WASM Node.js execution verification passed!" << std::endl;
    }

    // Test 3: Static-Library Integration Test
    {
        // Module A: add(a, b) -> a + b
        ir::Module modA("mod_a", ctx);
        ir::IRBuilder builderA(ctx);
        builderA.setModule(&modA);

        auto* i32 = ctx->getIntegerType(32);
        ir::Function* fnAdd = builderA.createFunction("add_numbers", i32, {i32, i32});
        ir::BasicBlock* entryA = builderA.createBasicBlock("entry", fnAdd);
        builderA.setInsertPoint(entryA);
        auto paramIt = fnAdd->getParameters().begin();
        ir::Value* p1 = (paramIt++)->get();
        ir::Value* p2 = paramIt->get();
        ir::Value* sum = builderA.createAdd(p1, p2);
        builderA.createRet(sum);

        fyra::BackendBuilder backendA(modA);
        backendA.target("x64-linux-bin");
        fyra::BuildResult resLib = backendA.emitStaticLibrary("/tmp/libmod_a.a");
        assert(resLib.success);

        // Module B: main() calls add_numbers(20, 22) -> 42
        ir::Module modB("mod_b", ctx);
        ir::IRBuilder builderB(ctx);
        builderB.setModule(&modB);

        ir::Function* fnMain = builderB.createFunction("main", i32);
        ir::BasicBlock* entryB = builderB.createBasicBlock("entry", fnMain);
        builderB.setInsertPoint(entryB);

        ir::Function* fnAddDecl = builderB.createFunction("add_numbers", i32, {i32, i32});
        ir::Value* callRes = builderB.createCall(fnAddDecl, {ctx->getConstantInt(i32, 20), ctx->getConstantInt(i32, 22)}, i32);
        builderB.createRet(callRes);

        fyra::BackendBuilder backendB(modB);
        backendB.target("x64-linux-bin")
                .addStaticLibrary("/tmp/libmod_a.a");

        fyra::BuildResult resExec = backendB.emitExecutable("/tmp/test_static_lib_exec");
        if (!resExec.success) {
            std::cerr << "resExec failed with errors:" << std::endl;
            for (const auto& err : resExec.errors) {
                std::cerr << "  " << err << std::endl;
            }
        }
        assert(resExec.success);

        int rc = std::system("/tmp/test_static_lib_exec");
        int exitCode = WEXITSTATUS(rc);
        std::cout << "Static library integration test exit status: " << exitCode << std::endl;
        assert(exitCode == 42);
    }

    // Test 4: Object-Link Integration Test
    {
        // Object A: helper() -> 42
        ir::Module modA("mod_obj_a", ctx);
        ir::IRBuilder builderA(ctx);
        builderA.setModule(&modA);

        auto* i32 = ctx->getIntegerType(32);
        ir::Function* fnHelper = builderA.createFunction("helper_val", i32);
        ir::BasicBlock* entryA = builderA.createBasicBlock("entry", fnHelper);
        builderA.setInsertPoint(entryA);
        builderA.createRet(ctx->getConstantInt(i32, 42));

        fyra::BackendBuilder backendA(modA);
        backendA.target("x64-linux-bin");
        fyra::BuildResult resObjA = backendA.emitObject("/tmp/obj_a.o");
        assert(resObjA.success);

        // Object B: main() calls helper_val()
        ir::Module modB("mod_obj_b", ctx);
        ir::IRBuilder builderB(ctx);
        builderB.setModule(&modB);

        ir::Function* fnMain = builderB.createFunction("main", i32);
        ir::BasicBlock* entryB = builderB.createBasicBlock("entry", fnMain);
        builderB.setInsertPoint(entryB);

        ir::Function* fnHelperDecl = builderB.createFunction("helper_val", i32);
        ir::Value* val = builderB.createCall(fnHelperDecl, {}, i32);
        builderB.createRet(val);

        fyra::BackendBuilder backendB(modB);
        backendB.target("x64-linux-bin");
        fyra::BuildResult resObjB = backendB.emitObject("/tmp/obj_b.o");
        assert(resObjB.success);

        // Link Object A + Object B into executable using BackendBuilder
        ir::Module dummyMod("link_mod", ctx);
        fyra::BackendBuilder linkBuilder(dummyMod);
        linkBuilder.target("x64-linux-bin")
                   .addObject("/tmp/obj_a.o")
                   .addObject("/tmp/obj_b.o");

        fyra::BuildResult resLink = linkBuilder.emitExecutable("/tmp/test_obj_link_exec");
        assert(resLink.success);

        int rc = std::system("/tmp/test_obj_link_exec");
        int exitCode = WEXITSTATUS(rc);
        std::cout << "Object-link integration test exit status: " << exitCode << std::endl;
        assert(exitCode == 42);
    }

    // Test 5: Shared Library Generation & Dynamic Import Test
    {
        ir::Module modA("mod_sh_a", ctx);
        ir::IRBuilder builderA(ctx);
        builderA.setModule(&modA);

        auto* i32 = ctx->getIntegerType(32);
        ir::Function* fnSh = builderA.createFunction("sh_func", i32);
        ir::BasicBlock* entryA = builderA.createBasicBlock("entry", fnSh);
        builderA.setInsertPoint(entryA);
        builderA.createRet(ctx->getConstantInt(i32, 42));

        fyra::BackendBuilder backendA(modA);
        backendA.target("x64-linux-bin");
        fyra::BuildResult resSo = backendA.emitSharedLibrary("/tmp/libshared_a.so");
        assert(resSo.success);
        assert(resSo.kind == fyra::OutputKind::SharedLibrary);
        std::cout << "Shared library generated successfully: /tmp/libshared_a.so" << std::endl;
    }

    std::cout << "=== All BackendBuilder API direct C++ tests passed successfully! ===" << std::endl;
    return 0;
}
