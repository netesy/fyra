#include "target/core/TargetResolver.h"
#include "target/architecture/bpf/BPFArchitecture.h"
#include "target/os/baremetal/BareMetalOS.h"
#include "target/core/CompositeTargetInfo.h"
#include "codegen/CodeGen.h"
#include "ir/IRContext.h"
#include "ir/IRBuilder.h"
#include "ir/Module.h"
#include <cassert>
#include <array>
#include <iostream>
#include <sstream>

int main() {
    std::cout << "=== Running eBPF Target Verification Test Suite ===" << std::endl;

    auto ctx = std::make_shared<ir::IRContext>();

    // Test 1: Architecture creation and register model verification
    {
        target::BPFArchitecture ebpfArch;
        assert(ebpfArch.getArch() == target::Arch::BPF);
        assert(ebpfArch.getPointerSize() == 8); // 8 bytes (64 bits)

        const auto& intRegs = ebpfArch.getRegisters(target::RegisterClass::Integer);
        assert(intRegs.size() == 11); // r0-r10
        assert(ebpfArch.getIntegerReturnRegister() == "r0");
        assert(ebpfArch.getIntegerArgumentRegisters().size() == 5); // r1-r5
        std::cout << "eBPF architecture register model verified." << std::endl;
    }

    // Test 2: Program lowering, binary instruction encoding, and disassembly verification
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

        auto ebpfArch = std::make_unique<target::BPFArchitecture>();
        std::string err;
        assert(ebpfArch->validateLegality(*fnMain, err) == true);

        auto targetInfo = std::make_unique<target::CompositeTargetInfo>(std::move(ebpfArch), std::make_unique<target::BareMetalOS>());

        // Test textual assembly emission
        std::stringstream ss;
        codegen::CodeGen textCodeGen(module, std::move(targetInfo), &ss);
        textCodeGen.emit(true);

        std::string asmOutput = ss.str();
        std::cout << "eBPF Assembly Output:\n" << asmOutput << std::endl;

        assert(asmOutput.find(".text") != std::string::npos);
        assert(asmOutput.find("bpf_inst") != std::string::npos);

        // Test 64-bit eBPF binary instruction encoding into Assembler
        auto ebpfArchBin = std::make_unique<target::BPFArchitecture>();
        auto targetInfoBin = std::make_unique<target::CompositeTargetInfo>(std::move(ebpfArchBin), std::make_unique<target::BareMetalOS>());
        codegen::CodeGen binCodeGen(module, std::move(targetInfoBin), nullptr);
        binCodeGen.emit(false);

        const auto& codeBytes = binCodeGen.getAssembler().getCode();
        assert(!codeBytes.empty());
        assert(codeBytes.size() % 8 == 0 && "eBPF instructions must be 8-byte aligned");

        // Independently interpret the straight-line subset emitted here.  This
        // checks semantics (r0 == 30), rather than accepting any non-empty,
        // instruction-aligned byte stream.
        std::array<uint64_t, 11> regs{};
        for (size_t pc = 0; pc < codeBytes.size(); pc += 8) {
            const uint8_t op = codeBytes[pc];
            const uint8_t dst = codeBytes[pc + 1] & 0xf;
            int32_t imm = static_cast<int32_t>(
                static_cast<uint32_t>(codeBytes[pc + 4]) |
                (static_cast<uint32_t>(codeBytes[pc + 5]) << 8) |
                (static_cast<uint32_t>(codeBytes[pc + 6]) << 16) |
                (static_cast<uint32_t>(codeBytes[pc + 7]) << 24));
            if (op == 0xb7) regs[dst] = static_cast<int64_t>(imm);
            else if (op == 0x07) regs[dst] += static_cast<int64_t>(imm);
            else assert(op == 0x95 && "unexpected opcode in arithmetic smoke program");
        }
        assert(regs[0] == 30 && "encoded eBPF program must return 10 + 20");

        // Verify eBPF exit instruction (0x95) encoded at end
        assert(codeBytes[codeBytes.size() - 8] == 0x95 && "Final instruction must be BPF_EXIT (0x95)");

        std::cout << "eBPF 64-bit binary instruction encoding verified (" << codeBytes.size() << " bytes)." << std::endl;
    }

    // Test 3: Correct rejection of unsupported eBPF programs
    {
        ir::Module moduleReject("test_ebpf_reject", ctx);
        ir::IRBuilder builder(ctx);
        builder.setModule(&moduleReject);

        auto* f32Ty = ctx->getFloatType();
        ir::Function* fnReject = builder.createFunction("ebpf_float_func", f32Ty);
        ir::BasicBlock* entry = builder.createBasicBlock("entry", fnReject);
        builder.setInsertPoint(entry);
        auto* c1 = ctx->getConstantFP(f32Ty, 3.14);
        builder.createFAdd(c1, c1);
        builder.createRet(nullptr);

        target::BPFArchitecture ebpfArch;
        std::string errMsg;
        bool legal = ebpfArch.validateLegality(*fnReject, errMsg);
        assert(!legal && "Floating-point eBPF programs must be rejected!");
        assert(errMsg.find("floating-point") != std::string::npos);
        std::cout << "eBPF rejection of unsupported floating-point program verified: " << errMsg << std::endl;
    }

    std::cout << "=== All eBPF Target Verification Tests Passed! ===" << std::endl;
    return 0;
}
