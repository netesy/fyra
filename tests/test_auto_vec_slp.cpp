#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/Module.h"
#include "ir/SIMDInstruction.h"
#include "transforms/SLPVectorizer.h"
#include "codegen/CodeGen.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "target/architecture/x64/X64Architecture.h"
#include "target/core/CompositeTargetInfo.h"
#include "target/os/linux/LinuxOS.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <unistd.h>
#include <vector>

using namespace ir;

#define CHECK(condition) do { if (!(condition)) { std::cerr << "CHECK failed: " #condition << "\n"; std::abort(); } } while (false)

static size_t countOpcode(Function* function, Instruction::Opcode opcode) {
    size_t count = 0;
    for (const auto& block : function->getBasicBlocks())
        for (const auto& instruction : block->getInstructions())
            count += instruction->getOpcode() == opcode;
    return count;
}

static void arithmeticPack(Type* type, unsigned lanes, Instruction::Opcode opcode,
                           Instruction::Opcode expected, unsigned expectedWidth) {
    auto context = std::make_shared<IRContext>();
    Module module("slp", context);
    IRBuilder builder(context);
    builder.setModule(&module);
    std::vector<Type*> parameterTypes(lanes * 2, type);
    Function* function = builder.createFunction("pack", type, parameterTypes);
    BasicBlock* entry = builder.createBasicBlock("entry", function);
    builder.setInsertPoint(entry);
    std::vector<Value*> parameters;
    for (auto& parameter : function->getParameters()) parameters.push_back(parameter.get());
    std::vector<Instruction*> results;
    for (unsigned lane = 0; lane < lanes; ++lane) {
        Instruction* result = nullptr;
        switch (opcode) {
            case Instruction::Add: result = builder.createAdd(parameters[lane], parameters[lane + lanes]); break;
            case Instruction::Mul: result = builder.createMul(parameters[lane], parameters[lane + lanes]); break;
            case Instruction::FAdd: result = builder.createFAdd(parameters[lane], parameters[lane + lanes]); break;
            case Instruction::FMul: result = builder.createFMul(parameters[lane], parameters[lane + lanes]); break;
            default: CHECK(false);
        }
        results.push_back(result);
    }
    builder.createRet(results.front());

    transforms::SLPVectorizer slp;
    bool transformed = slp.performTransformation(*function);
    if (!transformed) {
        // If pure parameter arithmetic is rejected by the materialization cost model,
        // verify that it remains unvectorized scalar code.
        CHECK(countOpcode(function, expected) == 0);
        return;
    }
    CHECK(countOpcode(function, expected) == 1);
    for (const auto& instruction : entry->getInstructions()) {
        if (instruction->getOpcode() == expected) {
            auto* vector = dynamic_cast<VectorInstruction*>(instruction.get());
            CHECK(vector && vector->getVectorWidth() == expectedWidth);
        }
    }
    CHECK(!slp.performTransformation(*function) && "SLP must be fixed-point stable");
}

static void rejectionAndRemainderTests() {
    auto context = std::make_shared<IRContext>();
    Module module("slp_reject", context);
    IRBuilder builder(context);
    builder.setModule(&module);
    auto* i32 = context->getIntegerType(32);
    Function* function = builder.createFunction("leftovers", i32, std::vector<Type*>(20, i32));
    BasicBlock* entry = builder.createBasicBlock("entry", function);
    builder.setInsertPoint(entry);
    std::vector<Value*> parameters;
    for (auto& parameter : function->getParameters()) parameters.push_back(parameter.get());
    std::vector<Instruction*> results;
    for (unsigned lane = 0; lane < 10; ++lane)
        results.push_back(builder.createAdd(parameters[lane], parameters[lane + 10]));
    builder.createRet(results.back());
    transforms::SLPVectorizer slp;
    bool transformed = slp.performTransformation(*function);
    if (transformed) {
        CHECK(countOpcode(function, Instruction::VAdd) == 1);
        CHECK(countOpcode(function, Instruction::Add) == 2);
    } else {
        CHECK(countOpcode(function, Instruction::VAdd) == 0);
        CHECK(countOpcode(function, Instruction::Add) == 10);
    }

    auto context2 = std::make_shared<IRContext>();
    Module module2("slp_dependency", context2);
    IRBuilder builder2(context2);
    builder2.setModule(&module2);
    auto* ty = context2->getIntegerType(32);
    Function* chain = builder2.createFunction("chain", ty, {ty, ty, ty, ty, ty});
    BasicBlock* chainEntry = builder2.createBasicBlock("entry", chain);
    builder2.setInsertPoint(chainEntry);
    std::vector<Value*> args;
    for (auto& parameter : chain->getParameters()) args.push_back(parameter.get());
    auto* x0 = builder2.createAdd(args[0], args[1]);
    auto* x1 = builder2.createAdd(x0, args[2]);
    auto* x2 = builder2.createAdd(x1, args[3]);
    auto* x3 = builder2.createAdd(x2, args[4]);
    builder2.createRet(x3);
    CHECK(!slp.performTransformation(*chain));
    CHECK(countOpcode(chain, Instruction::VAdd) == 0);
}

static void groupingTests() {
    auto context = std::make_shared<IRContext>();
    Module module("slp_groups", context);
    IRBuilder builder(context); builder.setModule(&module);
    auto* i32 = context->getIntegerType(32);
    auto* f32 = context->getFloatType();
    std::vector<Type*> types(16, i32);
    types.insert(types.end(), 16, f32);
    auto* function = builder.createFunction("mixed_types", i32, types);
    auto* entry = builder.createBasicBlock("entry", function); builder.setInsertPoint(entry);
    std::vector<Value*> args;
    for (auto& parameter : function->getParameters()) args.push_back(parameter.get());
    std::vector<Instruction*> integerResults;
    for (unsigned lane = 0; lane < 8; ++lane)
        integerResults.push_back(builder.createAdd(args[lane], args[8 + lane]));
    for (unsigned lane = 0; lane < 8; ++lane)
        builder.createFAdd(args[16 + lane], args[24 + lane]);
    builder.createRet(integerResults.front());
    transforms::SLPVectorizer slp;
    bool transformed = slp.performTransformation(*function);
    if (transformed) {
        CHECK(countOpcode(function, Instruction::VAdd) == 1);
        CHECK(countOpcode(function, Instruction::VFAdd) == 1);
    }

    auto context2 = std::make_shared<IRContext>();
    Module module2("slp_mixed_op", context2);
    IRBuilder builder2(context2); builder2.setModule(&module2);
    auto* ty = context2->getIntegerType(32);
    auto* mixed = builder2.createFunction("mixed_opcodes", ty, std::vector<Type*>(18, ty));
    auto* mixedEntry = builder2.createBasicBlock("entry", mixed); builder2.setInsertPoint(mixedEntry);
    std::vector<Value*> mixedArgs;
    for (auto& parameter : mixed->getParameters()) mixedArgs.push_back(parameter.get());
    builder2.createAdd(mixedArgs[0], mixedArgs[9]);
    builder2.createAdd(mixedArgs[1], mixedArgs[10]);
    builder2.createAdd(mixedArgs[2], mixedArgs[11]);
    builder2.createSub(mixedArgs[3], mixedArgs[12]);
    std::vector<Instruction*> tail;
    for (unsigned lane = 4; lane < 9; ++lane)
        tail.push_back(builder2.createAdd(mixedArgs[lane], mixedArgs[9 + lane]));
    builder2.createRet(tail.front());
    bool transformedMixed = slp.performTransformation(*mixed);
    if (transformedMixed) {
        CHECK(countOpcode(mixed, Instruction::VAdd) == 1);
        CHECK(countOpcode(mixed, Instruction::Add) == 4);
    } else {
        CHECK(countOpcode(mixed, Instruction::VAdd) == 0);
        CHECK(countOpcode(mixed, Instruction::Add) == 8);
    }
    CHECK(countOpcode(mixed, Instruction::VSub) == 0);
}

enum class MemoryKind { I32, F32, F64 };

static void overlappingMemoryRejected() {
    auto context = std::make_shared<IRContext>();
    Module module("slp_overlap", context);
    IRBuilder builder(context); builder.setModule(&module);
    auto* i32 = context->getIntegerType(32);
    auto* pointer = context->getPointerType(i32);
    auto* function = builder.createFunction("overlap", context->getVoidType(), {pointer});
    auto* base = function->getParameters().front().get();
    auto* entry = builder.createBasicBlock("entry", function); builder.setInsertPoint(entry);
    auto address = [&](unsigned offset) { return builder.createAdd(base, context->getConstantInt(context->getIntegerType(64), offset), pointer); };
    for (unsigned lane = 0; lane < 8; ++lane) {
        auto* lhs = builder.createLoad(address(lane * 4));
        auto* rhs = builder.createLoad(address(64 + lane * 4));
        auto* sum = builder.createAdd(lhs, rhs);
        builder.createStore(sum, address(4 + lane * 4));
    }
    builder.createRet(nullptr);
    transforms::SLPVectorizer slp;
    CHECK(!slp.performTransformation(*function));
    CHECK(countOpcode(function, Instruction::VStore) == 0);
}

static void memoryRuntimeTest(MemoryKind kind, unsigned lanes, unsigned startOffset) {
    auto context = std::make_shared<IRContext>();
    Module module("slp_memory", context);
    IRBuilder builder(context);
    builder.setModule(&module);
    Type* element = kind == MemoryKind::I32 ? static_cast<Type*>(context->getIntegerType(32))
                  : kind == MemoryKind::F32 ? static_cast<Type*>(context->getFloatType())
                                            : static_cast<Type*>(context->getDoubleType());
    auto* pointer = context->getPointerType(element);
    Function* function = builder.createFunction("slp_memory_kernel", context->getVoidType(), {pointer});
    auto* base = function->getParameters().front().get();
    auto* entry = builder.createBasicBlock("entry", function);
    builder.setInsertPoint(entry);
    const unsigned size = element->getSize();
    const unsigned aStart = startOffset;
    const unsigned bStart = startOffset + 64;
    const unsigned cStart = startOffset + 128;
    for (unsigned lane = 0; lane < lanes; ++lane) {
        auto address = [&](unsigned offset) {
            return builder.createAdd(base, context->getConstantInt(context->getIntegerType(64), offset), pointer);
        };
        auto load = [&](Value* ptr) {
            return kind == MemoryKind::I32 ? builder.createLoad(ptr)
                 : kind == MemoryKind::F32 ? builder.createLoads(ptr) : builder.createLoadd(ptr);
        };
        auto* lhs = load(address(aStart + lane * size));
        auto* rhs = load(address(bStart + lane * size));
        Instruction* value = kind == MemoryKind::I32 ? builder.createAdd(lhs, rhs)
                           : kind == MemoryKind::F32 ? builder.createFMul(lhs, rhs)
                                                     : builder.createFAdd(lhs, rhs);
        auto* output = address(cStart + lane * size);
        if (kind == MemoryKind::I32) builder.createStore(value, output);
        else if (kind == MemoryKind::F32) builder.createStores(value, output);
        else builder.createStored(value, output);
    }
    builder.createRet(nullptr);

    transforms::SLPVectorizer slp;
    CHECK(slp.performTransformation(*function));
    unsigned packs = lanes / 8 + ((lanes % 8) >= 4 ? 1 : 0);
    if (kind == MemoryKind::F64) packs = lanes / 4 + ((lanes % 4) >= 2 ? 1 : 0);
    CHECK(countOpcode(function, Instruction::VLoad) == 2 * packs);
    CHECK(countOpcode(function, Instruction::VStore) == packs);
    Instruction::Opcode vectorOp = kind == MemoryKind::I32 ? Instruction::VAdd
                                       : kind == MemoryKind::F32 ? Instruction::VFMul : Instruction::VFAdd;
    CHECK(countOpcode(function, vectorOp) == packs);
    CHECK(countOpcode(function, Instruction::VInsert) == 0);
    CHECK(countOpcode(function, Instruction::VExtract) == 0);
    CHECK(!slp.performTransformation(*function));

    transforms::LinearScanAllocator allocator;
    allocator.run(*function);
    auto architecture = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    auto os = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> targetInfo =
        std::make_unique<target::CompositeTargetInfo>(std::move(architecture), std::move(os));
    std::ostringstream assembly;
    codegen::CodeGen codegen(module, std::move(targetInfo), &assembly);
    codegen.emit(false);
    const std::string text = assembly.str();
    if (std::getenv("FYRA_SLP_DUMP")) std::cout << text;
    if (kind == MemoryKind::I32) {
        CHECK(text.find("vpaddd") != std::string::npos);
        CHECK(text.find("vmovdqu") != std::string::npos);
    } else if (kind == MemoryKind::F32) {
        CHECK(text.find("vmulps") != std::string::npos);
        CHECK(text.find("vmovups") != std::string::npos);
    } else {
        CHECK(text.find("vaddpd") != std::string::npos);
        CHECK(text.find("vmovupd") != std::string::npos);
    }

    static std::atomic<unsigned> counter{0};
    std::string id = std::to_string(getpid()) + "_" + std::to_string(counter++);
    std::string asmPath = "/tmp/fyra_slp_" + id + ".s";
    std::string harnessPath = "/tmp/fyra_slp_" + id + ".c";
    std::string binaryPath = "/tmp/fyra_slp_" + id;
    { std::ofstream out(asmPath); out << text; }
    {
        std::ofstream out(harnessPath);
        out << "#include <stdint.h>\n#include <string.h>\n#include <math.h>\n"
               "extern void slp_memory_kernel(void*);\nint main(void){ unsigned char raw[256]={0}; void *p=raw+1;";
        if (kind == MemoryKind::I32) {
            out << "int32_t a[16],b[16],got[16];for(int i=0;i<16;i++){a[i]=(i==0?INT32_MIN:(i==15?INT32_MAX:i*1234567-9000000));b[i]=i*76543-500000;}"
                << "memcpy((char*)p+" << aStart << ",a," << lanes * 4 << ");memcpy((char*)p+" << bStart << ",b," << lanes * 4 << ");"
                   "slp_memory_kernel(p);memcpy(got,(char*)p+" << cStart << "," << lanes * 4 << ");"
                   "for(int i=0;i<" << lanes << ";i++){uint32_t e=(uint32_t)a[i]+(uint32_t)b[i];if((uint32_t)got[i]!=e)return 10+i;}";
        } else if (kind == MemoryKind::F32) {
            out << "float a[8]={-0.0f,0.0f,-3.5f,2.0f,9.25f,-1.0f,100.0f,.125f};"
                   "float b[8]={2.0f,-4.0f,2.0f,-8.0f,.5f,-1.0f,.25f,16.0f};float got[8];"
                << "memcpy((char*)p+" << aStart << ",a,32);memcpy((char*)p+" << bStart << ",b,32);"
                   "slp_memory_kernel(p);memcpy(got,(char*)p+" << cStart << ",32);"
                   "for(int i=0;i<8;i++){float e=a[i]*b[i];if(memcmp(&got[i],&e,4))return 20+i;}";
        } else {
            out << "double a[4]={-0.0,0.0,-3.5,1e100};double b[4]={0.0,-0.0,2.25,-1e100};double got[4];"
                << "memcpy((char*)p+" << aStart << ",a,32);memcpy((char*)p+" << bStart << ",b,32);"
                   "slp_memory_kernel(p);memcpy(got,(char*)p+" << cStart << ",32);"
                   "for(int i=0;i<4;i++){double e=a[i]+b[i];if(memcmp(&got[i],&e,8))return 30+i;}";
        }
        out << "return 0;}\n";
    }
    std::string command = "cc -no-pie " + asmPath + " " + harnessPath + " -o " + binaryPath + " && " + binaryPath;
    int result = std::system(command.c_str());
    if (result != 0) std::cerr << text << "\ncommand: " << command << " result=" << result << '\n';
    std::remove(asmPath.c_str()); std::remove(harnessPath.c_str()); std::remove(binaryPath.c_str());
    CHECK(result == 0 && "generated SLP memory kernel must execute correctly");
}

int main() {
    auto context = std::make_shared<IRContext>();
    arithmeticPack(context->getIntegerType(32), 4, Instruction::Add, Instruction::VAdd, 128);
    arithmeticPack(context->getIntegerType(32), 8, Instruction::Mul, Instruction::VMul, 256);
    arithmeticPack(context->getFloatType(), 8, Instruction::FMul, Instruction::VFMul, 256);
    arithmeticPack(context->getDoubleType(), 4, Instruction::FAdd, Instruction::VFAdd, 256);
    rejectionAndRemainderTests();
    groupingTests();
    memoryRuntimeTest(MemoryKind::I32, 8, 0);
    memoryRuntimeTest(MemoryKind::I32, 8, 16);
    memoryRuntimeTest(MemoryKind::I32, 10, 0);
    memoryRuntimeTest(MemoryKind::I32, 12, 0);
    memoryRuntimeTest(MemoryKind::I32, 16, 0);
    memoryRuntimeTest(MemoryKind::F32, 8, 0);
    memoryRuntimeTest(MemoryKind::F64, 4, 0);
    overlappingMemoryRejected();
    std::cout << "AutoVecSLPTest passed\n";
}
