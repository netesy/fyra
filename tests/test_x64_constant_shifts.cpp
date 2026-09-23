#include "codegen/CodeGen.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "ir/Constant.h"
#include "ir/IRBuilder.h"
#include "ir/Module.h"
#include "target/core/TargetResolver.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::abort();
    }
}

ir::Instruction* createShift(ir::IRBuilder& builder, ir::Instruction::Opcode opcode,
                             ir::Value* value, ir::Value* count) {
    if (opcode == ir::Instruction::Shl) return builder.createShl(value, count);
    if (opcode == ir::Instruction::Shr) return builder.createShr(value, count);
    return builder.createSar(value, count);
}

std::string functionAssembly(const std::string& assembly, const std::string& name) {
    const size_t begin = assembly.find(name + ":");
    require(begin != std::string::npos, "shift test function missing from assembly");
    const size_t end = assembly.find(".Lfunc_end_" + name, begin);
    require(end != std::string::npos, "shift test function end missing from assembly");
    return assembly.substr(begin, end - begin);
}

} // namespace

int main() {
    auto context = std::make_shared<ir::IRContext>();
    ir::Module module("x64_constant_shifts", context);
    ir::IRBuilder builder(context);
    builder.setModule(&module);
    const std::vector<unsigned> widths = {8, 16, 32, 64};
    const std::vector<uint64_t> counts = {0, 1, 2, 7, 15, 31, 32, 63, UINT64_MAX};
    const std::vector<std::pair<ir::Instruction::Opcode, std::string>> operations = {
        {ir::Instruction::Shl, "shl"},
        {ir::Instruction::Shr, "shr"},
        {ir::Instruction::Sar, "sar"},
    };

    for (unsigned width : widths) {
        auto* type = context->getIntegerType(width);
        for (const auto& [opcode, operation] : operations) {
            for (uint64_t count : counts) {
                const std::string name = operation + std::to_string(width) + "_" + std::to_string(count);
                auto* function = builder.createFunction(name, type, {type});
                auto* block = builder.createBasicBlock("entry", function);
                builder.setInsertPoint(block);
                auto* result = createShift(builder, opcode, function->getParameters().front().get(),
                                           context->getConstantInt(type, count));
                builder.createRet(result);
                transforms::LinearScanAllocator allocator;
                allocator.run(*function);
            }
        }
    }

    auto* i32 = context->getIntegerType(32);
    for (const auto& [opcode, operation] : operations) {
        const std::string name = operation + std::string("32_variable");
        auto* function = builder.createFunction(name, i32, {i32, i32});
        auto* block = builder.createBasicBlock("entry", function);
        builder.setInsertPoint(block);
        auto parameter = function->getParameters().begin();
        ir::Value* value = parameter->get();
        ir::Value* count = (++parameter)->get();
        builder.createRet(createShift(builder, opcode, value, count));
        transforms::LinearScanAllocator allocator;
        allocator.run(*function);
    }

    std::ostringstream assembly;
    codegen::CodeGen codegen(module,
        target::TargetResolver::resolve({target::Arch::X64, target::OS::Linux}), &assembly);
    codegen.emit(false);
    const std::string text = assembly.str();

    for (unsigned width : widths) {
        const bool uses32BitStorage = width <= 32;
        for (const auto& [opcode, operation] : operations) {
            const std::string mnemonic = operation + (uses32BitStorage ? "l" : "q");
            for (uint64_t count : counts) {
                const uint64_t masked = count & (uses32BitStorage ? 31ULL : 63ULL);
                const std::string body = functionAssembly(
                    text, operation + std::to_string(width) + "_" + std::to_string(count));
                require(body.find(mnemonic + " $" + std::to_string(masked)) != std::string::npos,
                        "constant shift did not use a width-correct immediate");
                require(body.find("%cl") == std::string::npos,
                        "constant shift unexpectedly used %cl");
            }
        }
    }
    for (const auto& [opcode, operation] : operations) {
        const std::string body = functionAssembly(text, operation + std::string("32_variable"));
        require(body.find(operation + "l %cl") != std::string::npos,
                "variable shift did not use %cl");
    }

    const std::string stem = "/tmp/fyra_x64_constant_shifts_" + std::to_string(getpid());
    const std::string assemblyPath = stem + ".s";
    const std::string harnessPath = stem + ".c";
    const std::string binaryPath = stem;
    { std::ofstream output(assemblyPath); output << text; }
    {
        std::ofstream output(harnessPath);
        output << "#include <stdint.h>\n#include <limits.h>\n";
        for (unsigned width : widths) {
            const char* unsignedType = width == 8 ? "uint8_t" : width == 16 ? "uint16_t" :
                                       width == 32 ? "uint32_t" : "uint64_t";
            const char* signedType = width == 8 ? "int8_t" : width == 16 ? "int16_t" :
                                     width == 32 ? "int32_t" : "int64_t";
            for (const auto& [opcode, operation] : operations) {
                const char* type = opcode == ir::Instruction::Sar ? signedType : unsignedType;
                for (uint64_t count : counts)
                    output << "extern " << type << ' ' << operation << width << '_' << count
                           << '(' << type << ");\n";
            }
        }
        output << "int main(void) {\n";
        for (uint64_t count : counts) {
            const uint64_t c32 = count & 31;
            const uint64_t c64 = count & 63;
            output << "  { uint8_t u=UINT8_C(0x81); int8_t s=(int8_t)u;\n"
                   << "    if (shl8_" << count << "(u) != (uint8_t)((uint32_t)u << " << c32 << ")) return 1;\n"
                   << "    if (shr8_" << count << "(u) != (uint8_t)((uint32_t)u >> " << c32 << ")) return 2;\n"
                   << "    if (sar8_" << count << "(s) != (int8_t)((int32_t)s >> " << c32 << ")) return 3; }\n"
                   << "  { uint16_t u=UINT16_C(0x8001); int16_t s=(int16_t)u;\n"
                   << "    if (shl16_" << count << "(u) != (uint16_t)((uint32_t)u << " << c32 << ")) return 4;\n"
                   << "    if (shr16_" << count << "(u) != (uint16_t)((uint32_t)u >> " << c32 << ")) return 5;\n"
                   << "    if (sar16_" << count << "(s) != (int16_t)((int32_t)s >> " << c32 << ")) return 6; }\n"
                   << "  { uint32_t u=UINT32_C(0x80000001); int32_t s=(int32_t)u;\n"
                   << "    if (shl32_" << count << "(u) != (uint32_t)(u << " << c32 << ")) return 1;\n"
                   << "    if (shr32_" << count << "(u) != (uint32_t)(u >> " << c32 << ")) return 2;\n"
                   << "    if ((int32_t)sar32_" << count << "(u) != (int32_t)(s >> " << c32 << ")) return 3; }\n"
                   << "  { uint64_t u=UINT64_C(0x8000000000000001); int64_t s=(int64_t)u;\n"
                   << "    if (shl64_" << count << "(u) != (uint64_t)(u << " << c64 << ")) return 4;\n"
                   << "    if (shr64_" << count << "(u) != (uint64_t)(u >> " << c64 << ")) return 5;\n"
                   << "    if ((int64_t)sar64_" << count << "(u) != (int64_t)(s >> " << c64 << ")) return 6; }\n";
        }
        output << "  return 0;\n}\n";
    }
    const std::string compile = "gcc -O2 -no-pie " + assemblyPath + " " + harnessPath + " -o " + binaryPath;
    require(std::system(compile.c_str()) == 0, "failed to compile constant-shift execution harness");
    require(std::system(binaryPath.c_str()) == 0, "constant-shift execution mismatch");
    std::remove(assemblyPath.c_str());
    std::remove(harnessPath.c_str());
    std::remove(binaryPath.c_str());
    return 0;
}
