#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/SIMDInstruction.h"
#include "ir/PhiNode.h"
#include "codegen/CodeGen.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "target/architecture/x64/X64Architecture.h"
#include "target/os/linux/LinuxOS.h"
#include "target/core/CompositeTargetInfo.h"
#include "ir/Use.h"
#include "transforms/CFGBuilder.h"
#include "transforms/LoopVectorizer.h"
#include "transforms/ScalarEvolution.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <atomic>
#include <unistd.h>
#include <cstdio>

using namespace ir;

std::string structuralSnapshot(Function& function) {
    std::ostringstream snapshot;
    snapshot << "blocks=" << function.getBasicBlocks().size() << ';';
    for (const auto& blockOwner : function.getBasicBlocks()) {
        BasicBlock* block = blockOwner.get();
        snapshot << "B" << block << " P";
        for (BasicBlock* predecessor : block->getPredecessors()) snapshot << predecessor << ',';
        snapshot << " S";
        for (BasicBlock* successor : block->getSuccessors()) snapshot << successor << ',';
        snapshot << " I" << block->getInstructions().size() << ':';
        for (const auto& instructionOwner : block->getInstructions()) {
            Instruction* instruction = instructionOwner.get();
            snapshot << instruction << '/' << static_cast<int>(instruction->getOpcode()) << '[';
            for (const auto& operand : instruction->getOperands())
                snapshot << operand->get() << ',';
            snapshot << "]";
        }
    }
    return snapshot.str();
}

void assertPureClosedFormQuery(transforms::ScalarEvolution& scev,
                               Function& function, BasicBlock* header,
                               bool expected) {
    const std::string before = structuralSnapshot(function);
    const bool firstResult = scev.canEliminateClosedForm(function, header);
    if (firstResult != expected)
        std::cerr << "Unexpected closed-form result for " << function.getName()
                  << ": " << firstResult << " expected " << expected << std::endl;
    assert(firstResult == expected);
    assert(structuralSnapshot(function) == before);
    for (unsigned iteration = 1; iteration < 10; ++iteration) {
        assert(scev.canEliminateClosedForm(function, header) == expected);
        assert(structuralSnapshot(function) == before);
    }
}

// Scalar reference implementation for sum = sum + 2*i for i in 0..n-1
int32_t scalar_loop_sum_ref(int32_t n, int32_t start = 0) {
    int32_t sum = 0;
    for (int32_t i = start; i < n; i++) {
        sum += i * 2;
    }
    return sum;
}

void test_loop_vectorizer_case(int32_t n_val, bool reversePhis = false,
                               int32_t start = 0, bool reverseCompare = false) {
    auto ctx = std::make_shared<IRContext>();
    Module module("test_vec_module", ctx);
    IRBuilder builder(ctx);
    builder.setModule(&module);

    Type* i32Ty = ctx->getIntegerType(32);
    Function* func = builder.createFunction("test_vec_func", i32Ty, {i32Ty});
    Value* pN = func->getParameters().front().get();

    BasicBlock* entry = builder.createBasicBlock("entry", func);
    BasicBlock* loopHeader = builder.createBasicBlock("loop", func);
    BasicBlock* loopBody = builder.createBasicBlock("body", func);
    BasicBlock* exit = builder.createBasicBlock("exit", func);

    builder.setInsertPoint(entry);
    builder.createJmp(loopHeader);

    builder.setInsertPoint(loopHeader);
    auto phiI = std::make_unique<PhiNode>(i32Ty, 0, nullptr, loopHeader);
    PhiNode* rawPhiI = phiI.get();

    auto phiSum = std::make_unique<PhiNode>(i32Ty, 0, nullptr, loopHeader);
    PhiNode* rawPhiSum = phiSum.get();

    if (reversePhis) {
        loopHeader->getInstructions().push_back(std::move(phiSum));
        loopHeader->getInstructions().push_back(std::move(phiI));
    } else {
        loopHeader->getInstructions().push_back(std::move(phiI));
        loopHeader->getInstructions().push_back(std::move(phiSum));
    }

    rawPhiI->addIncoming(ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), start), entry);
    rawPhiSum->addIncoming(ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 0), entry);

    Instruction* cond = reverseCompare ? builder.createCsgt(pN, rawPhiI)
                                       : builder.createCslt(rawPhiI, pN);
    builder.createBr(cond, loopBody, exit);

    builder.setInsertPoint(loopBody);
    Instruction* term = builder.createMul(rawPhiI, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 2));
    Instruction* sumNext = builder.createAdd(rawPhiSum, term);
    Instruction* iNext = builder.createAdd(rawPhiI, ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 1));

    rawPhiI->addIncoming(iNext, loopBody);
    rawPhiSum->addIncoming(sumNext, loopBody);

    builder.createJmp(loopHeader);

    builder.setInsertPoint(exit);
    builder.createRet(rawPhiSum);

    transforms::CFGBuilder::run(*func);

    // Run LoopVectorizer
    transforms::LoopVectorizer vectorizer;
    bool vectorized = vectorizer.performTransformation(*func);

    if (n_val >= 4) {
        assert(vectorized && "Loop should have been vectorized for n >= 4");
    }

    transforms::LinearScanAllocator allocator;
    allocator.run(*func);

    auto x64Arch = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    auto linuxOS = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> targetInfo = std::make_unique<target::CompositeTargetInfo>(std::move(x64Arch), std::move(linuxOS));

    std::ostringstream asmStream;
    codegen::CodeGen cg(module, std::move(targetInfo), &asmStream);
    cg.emit(false);

    std::string asmCode = asmStream.str();

    pid_t pid = getpid();
    static std::atomic<uint64_t> counter{0};
    uint64_t uid = counter.fetch_add(1);
    std::string idStr = std::to_string(pid) + "_" + std::to_string(uid);
    std::string asmFilePath = "/tmp/test_vec_gen_" + idStr + ".s";
    std::string binFilePath = "/tmp/test_vec_runner_" + idStr;
    std::string harnessPath = "/tmp/test_vec_harness_" + idStr + ".c";
    {
        std::ofstream asmFile(asmFilePath);
        asmFile << asmCode;
    }
    {
        std::ofstream hFile(harnessPath);
        hFile << R"(
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

extern int32_t test_vec_func(int32_t n);

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    int32_t n = atoi(argv[1]);
    int32_t res = test_vec_func(n);
    printf("RES:%d\n", res);
    return 0;
}
)";
    }

    std::string compileCmd = "gcc -no-pie " + asmFilePath + " " + harnessPath + " -o " + binFilePath;
    int compileRc = std::system(compileCmd.c_str());
    assert(compileRc == 0 && "Compilation of vectorized assembly failed");

    std::string runCmd = binFilePath + " " + std::to_string(n_val);
    FILE* pipe = popen(runCmd.c_str(), "r");
    assert(pipe != nullptr);
    char buffer[128];
    std::string resultOutput = "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        resultOutput += buffer;
    }
    int execRc = pclose(pipe);

    std::remove(asmFilePath.c_str());
    std::remove(harnessPath.c_str());
    std::remove(binFilePath.c_str());

    assert(execRc == 0 && "Execution of vectorized test binary failed");

    int32_t expectedRes = scalar_loop_sum_ref(n_val, start);
    std::string expectedStr = "RES:" + std::to_string(expectedRes);
    if (resultOutput.find(expectedStr) == std::string::npos) {
        std::cout << "DEBUG n_val=" << n_val << " expected=" << expectedStr << " got=" << resultOutput << std::endl;
    }
    assert(resultOutput.find(expectedStr) != std::string::npos && "Vectorized result mismatch!");

    std::cout << "Test start=" << start << " n=" << n_val
              << (reversePhis ? " (reversed PHIs)" : "")
              << (reverseCompare ? " (reversed compare)" : "")
              << " PASSED (res=" << expectedRes << ")" << std::endl;
}

void test_rejection_cases() {
    std::cout << "--- Testing Negative Rejection Cases ---" << std::endl;

    auto makeBaseModule = [](std::shared_ptr<IRContext>& ctx, Module*& module, Function*& func, BasicBlock*& entry, BasicBlock*& loopHeader, BasicBlock*& loopBody, BasicBlock*& exit, PhiNode*& rawPhiI, PhiNode*& rawPhiSum) {
        ctx = std::make_shared<IRContext>();
        module = new Module("test_rej_mod", ctx);
        IRBuilder builder(ctx);
        builder.setModule(module);

        Type* i32Ty = ctx->getIntegerType(32);
        func = builder.createFunction("test_func", i32Ty, {i32Ty});
        Value* pN = func->getParameters().front().get();

        entry = builder.createBasicBlock("entry", func);
        loopHeader = builder.createBasicBlock("loop", func);
        loopBody = builder.createBasicBlock("body", func);
        exit = builder.createBasicBlock("exit", func);

        builder.setInsertPoint(entry);
        builder.createJmp(loopHeader);

        builder.setInsertPoint(loopHeader);
        auto phiI = std::make_unique<PhiNode>(i32Ty, 0, nullptr, loopHeader);
        rawPhiI = phiI.get();
        loopHeader->getInstructions().push_back(std::move(phiI));

        auto phiSum = std::make_unique<PhiNode>(i32Ty, 0, nullptr, loopHeader);
        rawPhiSum = phiSum.get();
        loopHeader->getInstructions().push_back(std::move(phiSum));

        rawPhiI->addIncoming(ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 0), entry);
        rawPhiSum->addIncoming(ctx->getConstantInt(dynamic_cast<IntegerType*>(i32Ty), 0), entry);

        Instruction* cond = builder.createCslt(rawPhiI, pN);
        builder.createBr(cond, loopBody, exit);
    };

    // 1. Side effect Alloc in body
    {
        std::shared_ptr<IRContext> ctx; Module* mod; Function* func; BasicBlock *entry, *header, *body, *exit; PhiNode *pI, *pSum;
        makeBaseModule(ctx, mod, func, entry, header, body, exit, pI, pSum);
        IRBuilder builder(ctx); builder.setModule(mod); builder.setInsertPoint(body);
        Instruction* dummyAlloc = builder.createAlloc4(ctx->getIntegerType(32));
        Instruction* term = builder.createMul(pI, ctx->getConstantInt(ctx->getIntegerType(32), 2));
        Instruction* sumNext = builder.createAdd(pSum, term);
        Instruction* iNext = builder.createAdd(pI, ctx->getConstantInt(ctx->getIntegerType(32), 1));
        pI->addIncoming(iNext, body); pSum->addIncoming(sumNext, body); builder.createJmp(header);
        builder.setInsertPoint(exit); builder.createRet(pSum);
        transforms::CFGBuilder::run(*func);
        transforms::ScalarEvolution scev;
        assertPureClosedFormQuery(scev, *func, header, false);
        assert(!scev.run(*func));
        transforms::LoopVectorizer vec; assert(!vec.performTransformation(*func) && "Must reject side effect alloc in body");
        delete mod;
    }

    // 2. Call in body
    {
        std::shared_ptr<IRContext> ctx; Module* mod; Function* func; BasicBlock *entry, *header, *body, *exit; PhiNode *pI, *pSum;
        makeBaseModule(ctx, mod, func, entry, header, body, exit, pI, pSum);
        IRBuilder builder(ctx); builder.setModule(mod); builder.setInsertPoint(body);
        builder.createExternCall("dummy_cap", {}, ctx->getVoidType());
        Instruction* term = builder.createMul(pI, ctx->getConstantInt(ctx->getIntegerType(32), 2));
        Instruction* sumNext = builder.createAdd(pSum, term);
        Instruction* iNext = builder.createAdd(pI, ctx->getConstantInt(ctx->getIntegerType(32), 1));
        pI->addIncoming(iNext, body); pSum->addIncoming(sumNext, body); builder.createJmp(header);
        builder.setInsertPoint(exit); builder.createRet(pSum);
        transforms::CFGBuilder::run(*func);
        transforms::LoopVectorizer vec; assert(!vec.performTransformation(*func) && "Must reject Call in body");
        delete mod;
    }

    std::cout << "All Rejection Test Cases Passed Successfully!" << std::endl;
}

void test_register_expression_widening_execution() {
    auto ctx = std::make_shared<IRContext>();
    Module module("register_widening", ctx);
    IRBuilder builder(ctx); builder.setModule(&module);
    auto* i32 = ctx->getIntegerType(32);
    auto* i64 = ctx->getIntegerType(64);
    Function* function = builder.createFunction("register_widening_sum", i64, {i32});
    Value* bound = function->getParameters().front().get();
    BasicBlock* entry = builder.createBasicBlock("entry", function);
    BasicBlock* header = builder.createBasicBlock("loop", function);
    BasicBlock* body = builder.createBasicBlock("body", function);
    BasicBlock* exit = builder.createBasicBlock("exit", function);
    builder.setInsertPoint(entry); builder.createJmp(header);
    builder.setInsertPoint(header);
    auto iOwner = std::make_unique<PhiNode>(i32, 0, nullptr, header);
    PhiNode* i = iOwner.get(); header->getInstructions().push_back(std::move(iOwner));
    auto sumOwner = std::make_unique<PhiNode>(i64, 0, nullptr, header);
    PhiNode* sum = sumOwner.get(); header->getInstructions().push_back(std::move(sumOwner));
    i->addIncoming(ctx->getConstantInt(i32, 50000), entry);
    sum->addIncoming(ctx->getConstantInt(i64, 0), entry);
    builder.createBr(builder.createCslt(i, bound), body, exit);
    builder.setInsertPoint(body);
    // Match the non-polynomial register-pressure shape: this is deliberately
    // outside SCEV's linear/quadratic closed forms, while still overflowing
    // i32 for some tested starts. ExtSW must observe the wrapped lane result.
    Value* i1 = builder.createAdd(i, ctx->getConstantInt(i32, 1));
    Value* i2 = builder.createAdd(i, ctx->getConstantInt(i32, 2));
    Value* i3 = builder.createAdd(i, ctx->getConstantInt(i32, 3));
    Value* i4 = builder.createAdd(i, ctx->getConstantInt(i32, 4));
    Value* i5 = builder.createAdd(i, ctx->getConstantInt(i32, 5));
    Value* i6 = builder.createAdd(i, ctx->getConstantInt(i32, 6));
    Value* i7 = builder.createAdd(i, ctx->getConstantInt(i32, 7));
    Value* v0 = builder.createAdd(i, i1);
    Value* v1 = builder.createAdd(i2, i3);
    Value* v2 = builder.createAdd(i4, i5);
    Value* v3 = builder.createAdd(i6, i7);
    Value* products = builder.createAdd(builder.createMul(v0, v1),
                                        builder.createMul(v2, v3));
    Value* difference = builder.createSub(builder.createAdd(v0, v2),
                                          builder.createAdd(v1, v3));
    Value* product = builder.createMul(products, difference);
    Value* wide = builder.createExtSW(product, i64);
    Value* sumNext = builder.createAdd(sum, wide);
    Value* iNext = builder.createAdd(i, ctx->getConstantInt(i32, 1));
    i->addIncoming(iNext, body); sum->addIncoming(sumNext, body);
    builder.createJmp(header);
    builder.setInsertPoint(exit); builder.createRet(sum);
    transforms::CFGBuilder::run(*function);
    transforms::ScalarEvolution scev;
    assertPureClosedFormQuery(scev, *function, header, false);
    transforms::LoopVectorizer vectorizer;
    assert(vectorizer.performTransformation(*function));
    transforms::LinearScanAllocator allocator; allocator.run(*function);
    auto architecture = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    auto os = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> target =
        std::make_unique<target::CompositeTargetInfo>(std::move(architecture), std::move(os));
    std::ostringstream assembly;
    codegen::CodeGen codegen(module, std::move(target), &assembly); codegen.emit(false);
    const std::string text = assembly.str();
    assert(text.find("paddd") != std::string::npos);
    assert(text.find("pmulld") != std::string::npos);
    assert(text.find("%ymm") == std::string::npos);
    const std::string stem = "/tmp/fyra_register_widening_" + std::to_string(getpid());
    const std::string asmPath = stem + ".s", harnessPath = stem + ".c", binaryPath = stem;
    { std::ofstream output(asmPath); output << text; }
    { std::ofstream output(harnessPath); output << R"(
#include <stdint.h>
#include <stdio.h>
extern int64_t register_widening_sum(int32_t);
static uint32_t add32(uint32_t a, uint32_t b) { return a + b; }
static int64_t reference(int32_t start, int32_t bound) {
  int64_t sum = 0;
  for (int32_t i = start; i < bound; ++i) {
    uint32_t u=(uint32_t)i;
    uint32_t v0=add32(u,u+1), v1=add32(u+2,u+3);
    uint32_t v2=add32(u+4,u+5), v3=add32(u+6,u+7);
    uint32_t products=add32(v0*v1,v2*v3);
    uint32_t difference=add32(v0,v2)-add32(v1,v3);
    sum += (int32_t)(products*difference);
  }
  return sum;
}

int main(void) {
  const int lengths[] = { 0,1,2,3,4,5,7,8,9 };
  for (unsigned n=0;n<9;n++) {
    int32_t bound = 50000 + lengths[n];
    if (register_widening_sum(bound) != reference(50000, bound)) return 1;
  }
  return 0;
})"; }
    const std::string command = "gcc -no-pie " + asmPath + " " + harnessPath + " -o " + binaryPath;
    assert(std::system(command.c_str()) == 0);
    assert(std::system(binaryPath.c_str()) == 0);
    std::remove(asmPath.c_str()); std::remove(harnessPath.c_str()); std::remove(binaryPath.c_str());
}

void test_register_widening_rejects_non_i32_leaf() {
    auto ctx = std::make_shared<IRContext>();
    Module module("narrow_register_widening", ctx);
    IRBuilder builder(ctx); builder.setModule(&module);
    auto* i16 = ctx->getIntegerType(16);
    auto* i64 = ctx->getIntegerType(64);
    Function* function = builder.createFunction("narrow_sum", i64, {i16});
    Value* bound = function->getParameters().front().get();
    BasicBlock* entry = builder.createBasicBlock("entry", function);
    BasicBlock* header = builder.createBasicBlock("loop", function);
    BasicBlock* body = builder.createBasicBlock("body", function);
    BasicBlock* exit = builder.createBasicBlock("exit", function);
    builder.setInsertPoint(entry); builder.createJmp(header);
    builder.setInsertPoint(header);
    auto iOwner = std::make_unique<PhiNode>(i16, 0, nullptr, header);
    PhiNode* i = iOwner.get(); header->getInstructions().push_back(std::move(iOwner));
    auto sumOwner = std::make_unique<PhiNode>(i64, 0, nullptr, header);
    PhiNode* sum = sumOwner.get(); header->getInstructions().push_back(std::move(sumOwner));
    i->addIncoming(ctx->getConstantInt(i16, 0), entry);
    sum->addIncoming(ctx->getConstantInt(i64, 0), entry);
    builder.createBr(builder.createCslt(i, bound), body, exit);
    builder.setInsertPoint(body);
    Value* nextSum = builder.createAdd(sum, builder.createExtSW(i, i64));
    Value* nextI = builder.createAdd(i, ctx->getConstantInt(i16, 1));
    i->addIncoming(nextI, body); sum->addIncoming(nextSum, body);
    builder.createJmp(header);
    builder.setInsertPoint(exit); builder.createRet(sum);
    transforms::CFGBuilder::run(*function);

    transforms::LoopVectorizer vectorizer;
    assert(!vectorizer.performTransformation(*function));
}

void test_closed_form_has_priority_over_vectorization() {
    auto ctx = std::make_shared<IRContext>();
    Module module("closed_form_priority", ctx);
    IRBuilder builder(ctx); builder.setModule(&module);
    auto* i32 = ctx->getIntegerType(32);
    auto* i64 = ctx->getIntegerType(64);
    Function* function = builder.createFunction("closed_form_priority", i64, {});
    BasicBlock* entry = builder.createBasicBlock("entry", function);
    BasicBlock* header = builder.createBasicBlock("loop", function);
    BasicBlock* body = builder.createBasicBlock("body", function);
    BasicBlock* exit = builder.createBasicBlock("exit", function);
    builder.setInsertPoint(entry); builder.createJmp(header);
    builder.setInsertPoint(header);
    auto iOwner = std::make_unique<PhiNode>(i32, 0, nullptr, header);
    PhiNode* i = iOwner.get(); header->getInstructions().push_back(std::move(iOwner));
    auto sumOwner = std::make_unique<PhiNode>(i64, 0, nullptr, header);
    PhiNode* sum = sumOwner.get(); header->getInstructions().push_back(std::move(sumOwner));
    i->addIncoming(ctx->getConstantInt(i32, 0), entry);
    sum->addIncoming(ctx->getConstantInt(i64, 0), entry);
    builder.createBr(builder.createCslt(i, ctx->getConstantInt(i32, 2000000)), body, exit);
    builder.setInsertPoint(body);
    Value* term = builder.createMul(i, ctx->getConstantInt(i32, 2));
    Value* nextSum = builder.createAdd(sum, builder.createExtSW(term, i64));
    Value* nextI = builder.createAdd(i, ctx->getConstantInt(i32, 1));
    i->addIncoming(nextI, body); sum->addIncoming(nextSum, body);
    builder.createJmp(header);
    builder.setInsertPoint(exit); builder.createRet(sum);
    transforms::CFGBuilder::run(*function);

    transforms::ScalarEvolution scev;
    assertPureClosedFormQuery(scev, *function, header, true);
    transforms::LoopVectorizer vectorizer;
    assert(!vectorizer.performTransformation(*function));
    assert(scev.run(*function));
    assert(function->getBasicBlocks().size() == 2);
}

void test_runtime_closed_form_shape_remains_vectorizable() {
    auto ctx = std::make_shared<IRContext>();
    Module module("runtime_recurrence", ctx);
    IRBuilder builder(ctx); builder.setModule(&module);
    auto* i32 = ctx->getIntegerType(32);
    auto* i64 = ctx->getIntegerType(64);
    Function* function = builder.createFunction("runtime_sum", i64, {i32});
    Value* bound = function->getParameters().front().get();
    BasicBlock* entry = builder.createBasicBlock("entry", function);
    BasicBlock* header = builder.createBasicBlock("loop", function);
    BasicBlock* body = builder.createBasicBlock("body", function);
    BasicBlock* exit = builder.createBasicBlock("exit", function);
    builder.setInsertPoint(entry); builder.createJmp(header);
    builder.setInsertPoint(header);
    auto iOwner = std::make_unique<PhiNode>(i32, 0, nullptr, header);
    PhiNode* i = iOwner.get(); header->getInstructions().push_back(std::move(iOwner));
    auto sumOwner = std::make_unique<PhiNode>(i64, 0, nullptr, header);
    PhiNode* sum = sumOwner.get(); header->getInstructions().push_back(std::move(sumOwner));
    i->addIncoming(ctx->getConstantInt(i32, 0), entry);
    sum->addIncoming(ctx->getConstantInt(i64, 0), entry);
    builder.createBr(builder.createCslt(i, bound), body, exit);
    builder.setInsertPoint(body);
    Value* term = builder.createMul(i, ctx->getConstantInt(i32, 2));
    Value* nextSum = builder.createAdd(sum, builder.createExtSW(term, i64));
    Value* nextI = builder.createAdd(i, ctx->getConstantInt(i32, 1));
    i->addIncoming(nextI, body); sum->addIncoming(nextSum, body);
    builder.createJmp(header);
    builder.setInsertPoint(exit); builder.createRet(sum);
    transforms::CFGBuilder::run(*function);

    transforms::ScalarEvolution scev;
    assertPureClosedFormQuery(scev, *function, header, false);
    assert(!scev.run(*function));
    transforms::LoopVectorizer vectorizer;
    assert(vectorizer.performTransformation(*function));
    transforms::LinearScanAllocator allocator; allocator.run(*function);
    auto architecture = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    auto os = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> target =
        std::make_unique<target::CompositeTargetInfo>(std::move(architecture), std::move(os));
    std::ostringstream assembly;
    codegen::CodeGen codegen(module, std::move(target), &assembly); codegen.emit(false);
    assert(assembly.str().find("%xmm") != std::string::npos);
    assert(assembly.str().find("paddd") != std::string::npos);
    assert(assembly.str().find("pmulld") != std::string::npos);
    assert(assembly.str().find("%ymm") == std::string::npos);
}

int main() {
    std::cout << "=== Running Loop Vectorizer Tests ===" << std::endl;

    test_loop_vectorizer_case(-1);
    test_loop_vectorizer_case(-2147483647 - 1); // INT_MIN
    test_loop_vectorizer_case(0);
    test_loop_vectorizer_case(1);
    test_loop_vectorizer_case(2);
    test_loop_vectorizer_case(3);
    test_loop_vectorizer_case(4);
    test_loop_vectorizer_case(4, true); // Reverse PHI order test
    test_loop_vectorizer_case(5);
    test_loop_vectorizer_case(7);
    test_loop_vectorizer_case(8);
    test_loop_vectorizer_case(9);
    test_loop_vectorizer_case(10000);
    test_loop_vectorizer_case(100000); // i32 wraparound test
    test_loop_vectorizer_case(20, false, 3);
    test_loop_vectorizer_case(22, false, 3);
    test_loop_vectorizer_case(17, false, 0, true);
    test_closed_form_has_priority_over_vectorization();
    test_runtime_closed_form_shape_remains_vectorizable();
    test_register_expression_widening_execution();
    test_register_widening_rejects_non_i32_leaf();

    test_rejection_cases();

    std::cout << "=== All Loop Vectorizer Unit Tests Passed Successfully ===" << std::endl;
    return 0;
}
