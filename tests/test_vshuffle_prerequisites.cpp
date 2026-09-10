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
#include <cstring>
#include <sys/mman.h>
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

    auto addShuffleFunction = [&](const std::string& name, ir::VectorType* type,
                                  const std::vector<int>& indices) {
        auto* fn = builder.createFunction(name, context->getVoidType(), {i64, i64, i64});
        auto parameterIt = fn->getParameters().begin();
        auto* left = (parameterIt++)->get();
        auto* right = (parameterIt++)->get();
        auto* result = parameterIt->get();
        auto* entry = builder.createBasicBlock("entry", fn);
        builder.setInsertPoint(entry);
        auto* leftVector = builder.createVLoad(type, left);
        auto* rightVector = builder.createVLoad(type, right);
        auto* value = builder.createVShuffle(leftVector, rightVector,
                                              {indices, type->getNumElements()});
        builder.createVStore(value, result);
        builder.createRet(nullptr);
        transforms::CFGBuilder::run(*fn);
        return fn;
    };
    auto* i8Function = addShuffleFunction("shuffle_i8", context->getVectorType(context->getIntegerType(8), 16),
        {0, 31, 1, 30, 2, 29, 3, 28, 4, 27, 5, 26, 6, 25, 7, 24});
    auto* i16Function = addShuffleFunction("shuffle_i16", context->getVectorType(context->getIntegerType(16), 8),
        {0, 15, 1, 14, 2, 13, 3, 12});
    auto* i64Function = addShuffleFunction("shuffle_i64", context->getVectorType(context->getIntegerType(64), 2), {3, 0});
    auto* f32Function = addShuffleFunction("shuffle_f32_bits", context->getVectorType(context->getFloatType(), 4), {0, 5, 2, 7});
    auto* f64Function = addShuffleFunction("shuffle_f64_bits", context->getVectorType(context->getDoubleType(), 2), {2, 1});

    transforms::CFGBuilder::run(*function);
    auto allocationTarget = resolve(target::Arch::X64, target::OS::Linux);
    transforms::LinearScanAllocator allocator;
    allocator.run(*function, allocationTarget.get());
    allocator.run(*i8Function, allocationTarget.get());
    allocator.run(*i16Function, allocationTarget.get());
    allocator.run(*i64Function, allocationTarget.get());
    allocator.run(*f32Function, allocationTarget.get());
    allocator.run(*f64Function, allocationTarget.get());

    std::ostringstream assembly;
    codegen::CodeGen codegen(module,
        resolve(target::Arch::X64, target::OS::Linux), &assembly);
    codegen.emit(false);
    const std::string output = assembly.str();
    assert(output.find("pshufb") != std::string::npos);
    assert(output.find(".LCvec_0(%rip)") != std::string::npos);
    assert(output.find(".LCvec_1(%rip)") != std::string::npos);
    assert(output.find("movdqu %xmm0, %xmm0") == std::string::npos);

    std::ostringstream windowsAssembly;
    codegen::CodeGen windowsCodegen(module,
        resolve(target::Arch::X64, target::OS::Windows), &windowsAssembly);
    windowsCodegen.emit(false);
    const std::string windowsOutput = windowsAssembly.str();
    assert(windowsOutput.find("pshufb xmm5, [rip + .LCvec_") != std::string::npos);
    assert(windowsOutput.find("por xmm0, xmm5") != std::string::npos);
    const std::string windowsAssemblyPath = "/tmp/fyra_vshuffle_windows.s";
    std::ofstream(windowsAssemblyPath) << windowsOutput;
    assert(std::system(("clang --target=x86_64-w64-windows-gnu -c " +
                        windowsAssemblyPath + " -o /tmp/fyra_vshuffle_windows.o").c_str()) == 0);

    const std::string assemblyPath = "/tmp/fyra_vshuffle.s";
    const std::string harnessPath = "/tmp/fyra_vshuffle_harness.cpp";
    const std::string executablePath = "/tmp/fyra_vshuffle_exec";
    std::ofstream(assemblyPath) << output;
    std::ofstream(harnessPath) << R"(
#include <cstdint>
extern "C" void shuffle_i32(const int32_t*, const int32_t*, int32_t*);
extern "C" void shuffle_i8(const uint8_t*, const uint8_t*, uint8_t*);
extern "C" void shuffle_i16(const uint16_t*, const uint16_t*, uint16_t*);
extern "C" void shuffle_i64(const uint64_t*, const uint64_t*, uint64_t*);
extern "C" void shuffle_f32_bits(const uint32_t*, const uint32_t*, uint32_t*);
extern "C" void shuffle_f64_bits(const uint64_t*, const uint64_t*, uint64_t*);
int main() {
    alignas(16) int32_t lhs[4] = {10, 20, 30, 40};
    alignas(16) int32_t rhs[4] = {50, 60, 70, 80};
    alignas(16) int32_t result[4] = {};
    shuffle_i32(lhs, rhs, result);
    if (!(result[0] == 10 && result[1] == 10 && result[2] == 60 && result[3] == 80)) return 1;
    alignas(16) uint8_t a8[16] = {0x00,0x01,0x7f,0x80,0xff,5,6,7,8,9,10,11,12,13,14,15};
    alignas(16) uint8_t b8[16] = {16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,0xff}, r8[16] = {};
    shuffle_i8(a8,b8,r8); if (!(r8[0]==0 && r8[1]==0xff && r8[2]==1 && r8[15]==24)) return 2;
    alignas(16) uint16_t a16[8] = {0,1,0x7fff,0x8000,0xffff,5,6,7};
    alignas(16) uint16_t b16[8] = {8,9,10,11,12,13,14,0xffff}, r16[8] = {};
    shuffle_i16(a16,b16,r16); if (!(r16[0]==0 && r16[1]==0xffff && r16[6]==0x8000 && r16[7]==12)) return 3;
    alignas(16) uint64_t a64[2] = {0,0x7fffffffffffffffULL};
    alignas(16) uint64_t b64[2] = {0x8000000000000000ULL,0xffffffffffffffffULL}, r64[2] = {};
    shuffle_i64(a64,b64,r64); if (!(r64[0]==0xffffffffffffffffULL && r64[1]==0)) return 4;
    alignas(16) uint32_t af[4] = {0x00000000,0x80000000,0x3f800000,0xff800000};
    alignas(16) uint32_t bf[4] = {0x7f800000,0x7fc12345,0xbf800000,0xffc54321}, rf[4] = {};
    shuffle_f32_bits(af,bf,rf); if (!(rf[0]==0 && rf[1]==0x7fc12345 && rf[2]==0x3f800000 && rf[3]==0xffc54321)) return 5;
    alignas(16) uint64_t ad[2] = {0x0000000000000000ULL,0x8000000000000000ULL};
    alignas(16) uint64_t bd[2] = {0x7ff8123456789abcULL,0x7ff0000000000000ULL}, rd[2] = {};
    shuffle_f64_bits(ad,bd,rd); if (!(rd[0]==0x7ff8123456789abcULL && rd[1]==0x8000000000000000ULL)) return 6;
    return 0;
}
)";
    const std::string compile = "c++ -no-pie " + assemblyPath + " " +
                                harnessPath + " -o " + executablePath;
    assert(std::system(compile.c_str()) == 0);
    assert(std::system(executablePath.c_str()) == 0);

    // Exercise the direct binary path by laying its text and rodata buffers
    // together and resolving the same PC-relative fixups used by ELF/PE.
    codegen::CodeGen binaryCodegen(module,
        resolve(target::Arch::X64, target::OS::Linux), nullptr);
    binaryCodegen.emit(false);
    const auto& text = binaryCodegen.getAssembler().getCode();
    const auto& rodata = binaryCodegen.getRodataAssembler().getCode();
    const size_t rodataBase = (text.size() + 15) & ~size_t(15);
    const size_t imageSize = rodataBase + rodata.size();
    auto* image = static_cast<uint8_t*>(mmap(nullptr, imageSize,
        PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    assert(image != MAP_FAILED);
    std::memcpy(image, text.data(), text.size());
    std::memcpy(image + rodataBase, rodata.data(), rodata.size());

    auto symbolAddress = [&](const std::string& name) -> size_t {
        for (const auto& symbol : binaryCodegen.getSymbols()) {
            if (symbol.name == name)
                return (symbol.sectionName == ".rodata" ? rodataBase : 0) + symbol.value;
        }
        assert(false && "unresolved binary VShuffle symbol");
        return 0;
    };
    for (const auto& relocation : binaryCodegen.getRelocations()) {
        assert(relocation.sectionName == ".text");
        const int64_t value = static_cast<int64_t>(symbolAddress(relocation.symbolName)) +
                              relocation.addend - static_cast<int64_t>(relocation.offset);
        const int32_t displacement = static_cast<int32_t>(value);
        std::memcpy(image + relocation.offset, &displacement, sizeof(displacement));
    }

    alignas(16) int32_t binaryResult[4] = {};
    using ShuffleFn = void (*)(const int32_t*, const int32_t*, int32_t*);
    auto binaryShuffle = reinterpret_cast<ShuffleFn>(image + symbolAddress("shuffle_i32"));
    const int32_t binaryLhs[4] = {10, 20, 30, 40};
    const int32_t binaryRhs[4] = {50, 60, 70, 80};
    binaryShuffle(binaryLhs, binaryRhs, binaryResult);
    assert((std::equal(std::begin(binaryResult), std::end(binaryResult),
                       std::array<int32_t, 4>{10, 10, 60, 80}.begin())));
    munmap(image, imageSize);
}

} // namespace

int main() {
    testSSSE3Capabilities();
    testVectorConstantPool();
    testShuffleMaskAccess();
    testX64ShuffleExecution();
    return 0;
}
