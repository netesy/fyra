#include "codegen/CodeGen.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/Module.h"
#include "ir/SIMDInstruction.h"
#include "target/core/TargetResolver.h"
#include "transforms/CFGBuilder.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::unique_ptr<target::TargetInfo> resolve(target::Arch arch, target::OS os) {
    return target::TargetResolver::resolve({arch, os});
}

void testSSSE3Capabilities() {
    auto linuxX64 = resolve(target::Arch::X64, target::OS::Linux);
    auto windowsX64 = resolve(target::Arch::X64, target::OS::Windows);
    assert(linuxX64->getVectorCapabilities().supportsSSSE3);
    assert(windowsX64->getVectorCapabilities().supportsSSSE3);
    assert(linuxX64->getVectorCapabilities().supportsFloatVectors);
    assert(linuxX64->getVectorCapabilities().supportsDoubleVectors);
    assert(!linuxX64->getVectorCapabilities().supportsAVX);
    assert(!linuxX64->getVectorCapabilities().supportsAVX2);
    assert(!linuxX64->getVectorCapabilities().supportsAVX512);

    assert(!resolve(target::Arch::AArch64, target::OS::Linux)
                ->getVectorCapabilities().supportsSSSE3);
    assert(!resolve(target::Arch::RISCV64, target::OS::Linux)
                ->getVectorCapabilities().supportsSSSE3);
    assert(!resolve(target::Arch::WASM32, target::OS::WASI)
                ->getVectorCapabilities().supportsSSSE3);
}

void testVectorConstantPool() {
    auto context = std::make_shared<ir::IRContext>();
    ir::Module module("constant_pool", context);
    std::ostringstream assembly;
    codegen::CodeGen codegen(module,
        resolve(target::Arch::X64, target::OS::Linux), &assembly);

    const codegen::CodeGen::VectorConstant first = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const codegen::CodeGen::VectorConstant second = {
        15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

    const std::string firstLabel = codegen.getOrCreateVectorConstantLabel(first);
    assert(firstLabel == ".LCvec_0");
    assert(codegen.getOrCreateVectorConstantLabel(first) == firstLabel);
    const std::string secondLabel = codegen.getOrCreateVectorConstantLabel(second);
    assert(secondLabel == ".LCvec_1");
    assert(firstLabel != secondLabel);

    codegen.emit(false);
    const std::string output = assembly.str();
    assert(output.find(".section .rodata") != std::string::npos);
    assert(output.find(".balign 16\n.LCvec_0:\n  .byte 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15") != std::string::npos);
    assert(output.find(".balign 16\n.LCvec_1:\n  .byte 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0") != std::string::npos);

    codegen::CodeGen binaryCodegen(module,
        resolve(target::Arch::X64, target::OS::Linux), nullptr);
    binaryCodegen.getOrCreateVectorConstantLabel(first);
    binaryCodegen.getOrCreateVectorConstantLabel(second);
    binaryCodegen.emit(false);
    const auto& bytes = binaryCodegen.getRodataAssembler().getCode();
    assert(bytes.size() == 32);
    assert(std::equal(first.begin(), first.end(), bytes.begin()));
    assert(std::equal(second.begin(), second.end(), bytes.begin() + 16));
}

void testShuffleMaskAccess() {
    auto context = std::make_shared<ir::IRContext>();
    ir::Module module("shuffle_mask", context);
    ir::IRBuilder builder(context);
    builder.setModule(&module);
    auto* i32 = context->getIntegerType(32);
    auto* v4i32 = context->getVectorType(i32, 4);
    auto* function = builder.createFunction("mask_access", context->getVoidType(), {});
    auto* block = builder.createBasicBlock("entry", function);
    builder.setInsertPoint(block);
    auto* storage = builder.createAlloc16(context->getIntegerType(64));
    auto* lhs = builder.createVLoad(v4i32, storage);
    auto* rhs = builder.createVLoad(v4i32, storage);

    const ir::ShuffleMask expected({0, 0, 5, 7}, 4);
    auto* shuffle = builder.createVShuffle(lhs, rhs, expected);
    const ir::ShuffleMask* actual = shuffle->getShuffleMask();
    assert(actual != nullptr);
    assert(actual->resultElements == 4);
    assert((actual->indices == std::vector<int>{0, 0, 5, 7}));
    assert(actual->indices.front() == 0); // duplicate lhs lane
    assert(actual->indices[2] == 5);     // rhs lane 1
    assert(actual->indices.back() == 7); // highest legal v4i32 index
}

void testX64ShuffleExecution() {
    auto context = std::make_shared<ir::IRContext>();
    ir::Module module("shuffle_execution", context);
    ir::IRBuilder builder(context);
    builder.setModule(&module);
    auto* i32 = context->getIntegerType(32);
    auto* i64 = context->getIntegerType(64);
    auto* v4i32 = context->getVectorType(i32, 4);
    auto* function = builder.createFunction("shuffle_i32", context->getVoidType(),
                                            {i64, i64, i64});
    auto parameter = function->getParameters().begin();
    auto* lhsPointer = (parameter++)->get();
    auto* rhsPointer = (parameter++)->get();
    auto* outputPointer = parameter->get();
    auto* block = builder.createBasicBlock("entry", function);
    builder.setInsertPoint(block);
    auto* lhs = builder.createVLoad(v4i32, lhsPointer);
    auto* rhs = builder.createVLoad(v4i32, rhsPointer);
    auto* shuffled = builder.createVShuffle(lhs, rhs, {{0, 0, 5, 7}, 4});
    builder.createVStore(shuffled, outputPointer);
    builder.createRet(nullptr);

    transforms::CFGBuilder::run(*function);
    auto allocationTarget = resolve(target::Arch::X64, target::OS::Linux);
    transforms::LinearScanAllocator allocator;
    allocator.run(*function, allocationTarget.get());

    std::ostringstream assembly;
    codegen::CodeGen codegen(module,
        resolve(target::Arch::X64, target::OS::Linux), &assembly);
    codegen.emit(false);
    const std::string output = assembly.str();
    assert(output.find("pshufb") != std::string::npos);
    assert(output.find(".LCvec_0(%rip)") != std::string::npos);
    assert(output.find(".LCvec_1(%rip)") != std::string::npos);

    const std::string assemblyPath = "/tmp/fyra_vshuffle.s";
    const std::string harnessPath = "/tmp/fyra_vshuffle_harness.cpp";
    const std::string executablePath = "/tmp/fyra_vshuffle_exec";
    std::ofstream(assemblyPath) << output;
    std::ofstream(harnessPath) << R"(
#include <cstdint>
extern "C" void shuffle_i32(const int32_t*, const int32_t*, int32_t*);
int main() {
    alignas(16) int32_t lhs[4] = {10, 20, 30, 40};
    alignas(16) int32_t rhs[4] = {50, 60, 70, 80};
    alignas(16) int32_t result[4] = {};
    shuffle_i32(lhs, rhs, result);
    return result[0] == 10 && result[1] == 10 &&
           result[2] == 60 && result[3] == 80 ? 0 : 1;
}
)";
    const std::string compile = "c++ -no-pie " + assemblyPath + " " +
                                harnessPath + " -o " + executablePath;
    assert(std::system(compile.c_str()) == 0);
    assert(std::system(executablePath.c_str()) == 0);
}

} // namespace

int main() {
    testSSSE3Capabilities();
    testVectorConstantPool();
    testShuffleMaskAccess();
    testX64ShuffleExecution();
    return 0;
}
