#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "ir/PhiNode.h"
#include "codegen/CodeGen.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "target/architecture/x64/X64Architecture.h"
#include "target/os/linux/LinuxOS.h"
#include "target/core/CompositeTargetInfo.h"
#include "transforms/CFGBuilder.h"
#include "transforms/LoopVectorizer.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

using namespace ir;

enum class FPKind { F32, F64 };
enum class FPOp { Add, Sub, Mul, Div };

static const char* opName(FPOp op) {
    switch (op) { case FPOp::Add: return "add"; case FPOp::Sub: return "sub";
                  case FPOp::Mul: return "mul"; case FPOp::Div: return "div"; }
    return "unknown";
}

static Function* buildScalarLoop(Module& module, IRBuilder& builder, FPKind kind, FPOp op) {
    auto ctx = module.getContextShared();
    Type* elemTy = kind == FPKind::F32 ? static_cast<Type*>(ctx->getFloatType())
                                      : static_cast<Type*>(ctx->getDoubleType());
    auto* i32 = ctx->getIntegerType(32);
    auto* i64 = ctx->getIntegerType(64);
    std::string name = std::string(kind == FPKind::F32 ? "f32_" : "f64_") + opName(op);
    Function* fn = builder.createFunction(name, ctx->getVoidType(), {i64, i64, i64, i32, i32});
    auto p = fn->getParameters().begin();
    Value* out = (p++)->get(); Value* a = (p++)->get(); Value* b = (p++)->get();
    Value* n = (p++)->get(); Value* start = p->get();
    BasicBlock* entry = builder.createBasicBlock("entry", fn);
    BasicBlock* header = builder.createBasicBlock("loop", fn);
    BasicBlock* body = builder.createBasicBlock("body", fn);
    BasicBlock* exit = builder.createBasicBlock("exit", fn);
    builder.setInsertPoint(entry); builder.createJmp(header);
    builder.setInsertPoint(header);
    auto phiOwner = std::make_unique<PhiNode>(i32, 0, nullptr, header);
    PhiNode* phi = phiOwner.get(); header->getInstructions().push_back(std::move(phiOwner));
    phi->addIncoming(start, entry);
    builder.createBr(builder.createCslt(phi, n), body, exit);
    builder.setInsertPoint(body);
    Value* wide = builder.createExtSW(phi, i64);
    Value* offset = builder.createMul(wide, ctx->getConstantInt(i64, elemTy->getSize()));
    Value* va = kind == FPKind::F32 ? static_cast<Value*>(builder.createLoads(builder.createAdd(a, offset)))
                                    : static_cast<Value*>(builder.createLoadd(builder.createAdd(a, offset)));
    Value* vb = kind == FPKind::F32 ? static_cast<Value*>(builder.createLoads(builder.createAdd(b, offset)))
                                    : static_cast<Value*>(builder.createLoadd(builder.createAdd(b, offset)));
    Value* result = nullptr;
    if (op == FPOp::Add) result = builder.createFAdd(va, vb);
    else if (op == FPOp::Sub) result = builder.createFSub(va, vb);
    else if (op == FPOp::Mul) result = builder.createFMul(va, vb);
    else result = builder.createFDiv(va, vb);
    Value* outPtr = builder.createAdd(out, offset);
    if (kind == FPKind::F32) builder.createStores(result, outPtr); else builder.createStored(result, outPtr);
    Value* next = builder.createAdd(phi, ctx->getConstantInt(i32, 1));
    phi->addIncoming(next, body); builder.createJmp(header);
    builder.setInsertPoint(exit); builder.createRet(nullptr);
    transforms::CFGBuilder::run(*fn);
    return fn;
}

static Function* buildRejectedFPLoop(Module& module, IRBuilder& builder, bool reduction) {
    auto ctx = module.getContextShared(); auto* f32 = ctx->getFloatType();
    auto* i32 = ctx->getIntegerType(32); auto* i64 = ctx->getIntegerType(64);
    Function* fn = builder.createFunction(reduction ? "fp_reduction" : "fp_dependence",
                                          reduction ? static_cast<Type*>(f32) : ctx->getVoidType(),
                                          reduction ? std::vector<Type*>{i64, i32, f32}
                                                    : std::vector<Type*>{i64, i32});
    auto p = fn->getParameters().begin(); Value* base = (p++)->get(); Value* n = (p++)->get();
    Value* init = reduction ? p->get() : nullptr;
    BasicBlock* entry = builder.createBasicBlock("entry", fn); BasicBlock* header = builder.createBasicBlock("loop", fn);
    BasicBlock* body = builder.createBasicBlock("body", fn); BasicBlock* exit = builder.createBasicBlock("exit", fn);
    builder.setInsertPoint(entry); builder.createJmp(header); builder.setInsertPoint(header);
    auto iOwner = std::make_unique<PhiNode>(i32, 0, nullptr, header); PhiNode* index = iOwner.get();
    header->getInstructions().push_back(std::move(iOwner)); index->addIncoming(ctx->getConstantInt(i32, 0), entry);
    PhiNode* sum = nullptr;
    if (reduction) { auto owner = std::make_unique<PhiNode>(f32, 0, nullptr, header); sum = owner.get();
        header->getInstructions().push_back(std::move(owner)); sum->addIncoming(init, entry); }
    builder.createBr(builder.createCslt(index, n), body, exit); builder.setInsertPoint(body);
    Value* wide = builder.createExtSW(index, i64); Value* off = builder.createMul(wide, ctx->getConstantInt(i64, 4));
    Value* ptr = builder.createAdd(base, off); Value* loaded = builder.createLoads(ptr);
    if (reduction) { Value* nextSum = builder.createFAdd(sum, loaded); sum->addIncoming(nextSum, body); }
    else { Value* one = builder.createFAdd(loaded, loaded); builder.createStores(one, ptr); }
    Value* next = builder.createAdd(index, ctx->getConstantInt(i32, 1)); index->addIncoming(next, body); builder.createJmp(header);
    builder.setInsertPoint(exit); builder.createRet(reduction ? static_cast<Value*>(sum) : nullptr);
    transforms::CFGBuilder::run(*fn); return fn;
}

int main() {
    auto ctx = std::make_shared<IRContext>(); Module module("auto_vec_float", ctx);
    IRBuilder builder(ctx); builder.setModule(&module);
    for (FPKind kind : {FPKind::F32, FPKind::F64}) for (FPOp op : {FPOp::Add, FPOp::Sub, FPOp::Mul, FPOp::Div}) {
        Function* fn = buildScalarLoop(module, builder, kind, op);
        transforms::LoopVectorizer vectorizer;
        assert(vectorizer.performTransformation(*fn) && "scalar floating loop must vectorize");
        transforms::LinearScanAllocator allocator; allocator.run(*fn);
    }
    auto arch = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    auto os = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> targetInfo =
        std::make_unique<target::CompositeTargetInfo>(std::move(arch), std::move(os));
    std::ostringstream assembly; codegen::CodeGen cg(module, std::move(targetInfo), &assembly); cg.emit(false);
    const std::string text = assembly.str();
    for (const char* mnemonic : {"vaddps", "vsubps", "vmulps", "vdivps", "vaddpd", "vsubpd", "vmulpd", "vdivpd"})
        assert(text.find(mnemonic) != std::string::npos);
    std::ofstream("/tmp/auto_vec_float.s") << text;
    std::ofstream harness("/tmp/auto_vec_float.c");
    harness << R"C(
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define DECL(T,N) extern void N(T*,T*,T*,int32_t,int32_t)
DECL(float,f32_add); DECL(float,f32_sub); DECL(float,f32_mul); DECL(float,f32_div);
DECL(double,f64_add); DECL(double,f64_sub); DECL(double,f64_mul); DECL(double,f64_div);
#define RUN(T,F,EXPR,N,S) do { T a[40],b[40],out[40],ref[40]; for(int i=0;i<40;i++){a[i]=(T)((i-11)*1.25);b[i]=(T)((i+3)*0.75+1.0);out[i]=ref[i]=(T)-9876.5;} for(int i=S;i<N;i++)ref[i]=(EXPR); F(out,a,b,N,S); if(memcmp(out,ref,sizeof(out))){printf("FAIL %s N=%d S=%d\n",#F,N,S);return 1;}}while(0)
#define CASES(T,F,EXPR,VF) do { const int ns[]={0,1,VF-1,VF,VF+1,2*VF-1,2*VF,2*VF+1,31}; for(unsigned k=0;k<sizeof(ns)/sizeof(ns[0]);k++)RUN(T,F,EXPR,ns[k],0); RUN(T,F,EXPR,20,3); RUN(T,F,EXPR,22,3);}while(0)
int main(){
 CASES(float,f32_add,a[i]+b[i],8); CASES(float,f32_sub,a[i]-b[i],8); CASES(float,f32_mul,a[i]*b[i],8); CASES(float,f32_div,a[i]/b[i],8);
 CASES(double,f64_add,a[i]+b[i],4); CASES(double,f64_sub,a[i]-b[i],4); CASES(double,f64_mul,a[i]*b[i],4); CASES(double,f64_div,a[i]/b[i],4);
 puts("floating auto-vectorization execution passed"); return 0;
})C";
    harness.close();
    int rc = std::system("gcc -O0 -no-pie /tmp/auto_vec_float.s /tmp/auto_vec_float.c -o /tmp/auto_vec_float && /tmp/auto_vec_float");
    assert(rc == 0 && "floating vectorized execution mismatch");
    for (bool reduction : {false, true}) {
        Function* rejected = buildRejectedFPLoop(module, builder, reduction);
        transforms::LoopVectorizer vectorizer;
        assert(!vectorizer.performTransformation(*rejected));
    }
    return 0;
}
