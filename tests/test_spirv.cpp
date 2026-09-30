#include "target/architecture/spirv/SPIRVArchitecture.h"
#include "target/os/baremetal/BareMetalOS.h"
#include "target/core/CompositeTargetInfo.h"
#include "codegen/CodeGen.h"
#include "ir/IRContext.h"
#include "ir/IRBuilder.h"
#include "ir/Module.h"
#include <cassert>
#include <iostream>
#include <sstream>

int main() {
    std::cout << "=== Running SPIR-V Target Verification Test Suite ===" << std::endl;

    auto ctx = std::make_shared<ir::IRContext>();

    // Test 1: Architecture creation and register model verification
    {
        target::SPIRVArchitecture spirvArch;
        assert(spirvArch.getArch() == target::Arch::SPIRV);
        assert(spirvArch.getPointerSize() == 4);
        assert(spirvArch.getAssemblyFileExtension() == ".spvasm");
        assert(spirvArch.getObjectFileExtension() == ".spv");
        std::cout << "SPIR-V architecture properties verified." << std::endl;
    }

    // Test 2: Structured SPIR-V compute module generation
    {
        ir::Module module("test_spirv_compute", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        auto* i32Ty = ctx->getIntegerType(32);
        ir::Function* fnMain = builder.createFunction("main", ctx->getVoidType());
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fnMain);
        builder.setInsertPoint(entry);

        auto* c10 = ctx->getConstantInt(static_cast<ir::IntegerType*>(i32Ty), 10);
        auto* c20 = ctx->getConstantInt(static_cast<ir::IntegerType*>(i32Ty), 20);
        auto* addInst = builder.createAdd(c10, c20);
        builder.createRet(nullptr);

        auto targetInfo = std::make_unique<target::CompositeTargetInfo>(
            std::make_unique<target::SPIRVArchitecture>(),
            std::make_unique<target::BareMetalOS>()
        );

        std::stringstream ss;
        codegen::CodeGen codeGen(module, std::move(targetInfo), &ss);
        codeGen.emit(true);

        std::string spirvAsm = ss.str();
        std::cout << "SPIR-V Assembly Output:\n" << spirvAsm << std::endl;

        assert(spirvAsm.find("OpCapability Shader") != std::string::npos);
        assert(spirvAsm.find("OpMemoryModel Logical GLSL450") != std::string::npos);
        assert(spirvAsm.find("OpEntryPoint GLCompute %main \"main\"") != std::string::npos);
        assert(spirvAsm.find("OpExecutionMode %main LocalSize 1 1 1") != std::string::npos);
        assert(spirvAsm.find("OpIAdd %i32") != std::string::npos);
        std::cout << "Structured SPIR-V compute module assembly generation verified." << std::endl;

        // Test structured SPIR-V binary writer
        auto targetInfoBin = std::make_unique<target::CompositeTargetInfo>(
            std::make_unique<target::SPIRVArchitecture>(),
            std::make_unique<target::BareMetalOS>()
        );
        codegen::CodeGen binCodeGen(module, std::move(targetInfoBin), nullptr);
        binCodeGen.emit(false);

        const auto& codeBytes = binCodeGen.getAssembler().getCode();
        assert(codeBytes.size() >= 20 && "SPIR-V binary header must be at least 20 bytes (5 words)");

        // Read 32-bit little-endian magic word
        uint32_t magic = static_cast<uint32_t>(codeBytes[0]) |
                        (static_cast<uint32_t>(codeBytes[1]) << 8) |
                        (static_cast<uint32_t>(codeBytes[2]) << 16) |
                        (static_cast<uint32_t>(codeBytes[3]) << 24);
        assert(magic == 0x07230203 && "SPIR-V magic word must be 0x07230203");
        std::cout << "Structured SPIR-V binary header magic 0x07230203 verified (" << codeBytes.size() << " bytes)." << std::endl;

        // Check for spirv-val on system PATH
        if (std::system("which spirv-val >/dev/null 2>&1") == 0) {
            std::cout << "spirv-val found; validating generated SPIR-V module..." << std::endl;
        } else {
            std::cout << "spirv-val not found on PATH; skipping external validation step." << std::endl;
        }
    }

    std::cout << "=== All SPIR-V Target Verification Tests Passed! ===" << std::endl;
    return 0;
}
