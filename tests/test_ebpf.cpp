#include "target/core/TargetResolver.h"
#include "target/architecture/ebpf/EBPFArchitecture.h"
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
    std::cout << "=== Running eBPF Target Verification Test Suite ===" << std::endl;

    auto ctx = std::make_shared<ir::IRContext>();

    // Test 1: Architecture creation and register model verification
    {
        target::EBPFArchitecture ebpfArch;
        assert(ebpfArch.getArch() == target::Arch::EBPF);
        assert(ebpfArch.getPointerSize() == 8); // 8 bytes (64 bits)

        const auto& intRegs = ebpfArch.getRegisters(target::RegisterClass::Integer);
        assert(intRegs.size() == 10); // r0-r9
        assert(ebpfArch.getIntegerReturnRegister() == "r0");
        assert(ebpfArch.getIntegerArgumentRegisters().size() == 5); // r1-r5
        std::cout << "eBPF architecture register model verified." << std::endl;
    }

    // Test 2: Program lowering and diagnostic rejection test
    {
        ir::Module module("test_ebpf_mod", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&module);

        auto* i64Ty = ctx->getIntegerType(64);
        ir::Function* fnMain = builder.createFunction("ebpf_main", i64Ty);
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fnMain);
        builder.setInsertPoint(entry);

        auto* c10 = ctx->getConstantInt(static_cast<ir::IntegerType*>(i64Ty), 10);
        auto* c20 = ctx->getConstantInt(static_cast<ir::IntegerType*>(i64Ty), 20);
        auto* addInst = builder.createAdd(c10, c20);
        builder.createRet(addInst);

        auto ebpfArch = std::make_unique<target::EBPFArchitecture>();
        std::string err;
        assert(ebpfArch->validateLegality(*fnMain, err) == true);

        auto targetInfo = std::make_unique<target::CompositeTargetInfo>(std::move(ebpfArch), std::make_unique<target::BareMetalOS>());

        std::stringstream ss;
        codegen::CodeGen codeGen(module, std::move(targetInfo), &ss);
        codeGen.emit(true);

        std::string asmOutput = ss.str();
        std::cout << "eBPF Assembly Output:\n" << asmOutput << std::endl;

        assert(asmOutput.find("eBPF function entry point: ebpf_main") != std::string::npos);
        assert(asmOutput.find("r1 += r2") != std::string::npos);
        assert(asmOutput.find("exit") != std::string::npos);
        std::cout << "eBPF code generation verified successfully." << std::endl;
    }

    std::cout << "=== All eBPF Target Verification Tests Passed! ===" << std::endl;
    return 0;
}
