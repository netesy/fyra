#include "ir/Module.h"
#include "ir/IRBuilder.h"
#include "ir/IRContext.h"
#include "codegen/CodeGen.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "codegen/regalloc/RegAllocRewriter.h"
#include "target/architecture/x64/X64Architecture.h"
#include "target/os/linux/LinuxOS.h"
#include "target/core/CompositeTargetInfo.h"
#include "transforms/CFGBuilder.h"
#include "transforms/LoopVectorizer.h"
#include "ir/PhiNode.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>

using namespace ir;

static Function* makeScalarClamp(IRBuilder& b, Module& m) {
    auto ctx = m.getContextShared(); auto* i32 = ctx->getIntegerType(32); auto* i64 = ctx->getIntegerType(64);
    auto* f = b.createFunction("scalar_clamp", ctx->getVoidType(), {i64, i64, i32, i32});
    auto p = f->getParameters().begin(); Value* out=(p++)->get(); Value* input=(p++)->get();
    Value* n=(p++)->get(); Value* start=p->get();
    auto* entry=b.createBasicBlock("entry",f); auto* header=b.createBasicBlock("loop",f);
    auto* body=b.createBasicBlock("conditional",f); auto* yes=b.createBasicBlock("then",f);
    auto* no=b.createBasicBlock("else",f); auto* merge=b.createBasicBlock("merge",f);
    auto* exit=b.createBasicBlock("exit",f);
    b.setInsertPoint(entry); b.createJmp(header); b.setInsertPoint(header);
    auto indexOwner=std::make_unique<PhiNode>(i32,0,nullptr,header); auto* index=indexOwner.get();
    header->getInstructions().push_back(std::move(indexOwner)); index->addIncoming(start,entry);
    b.createBr(b.createCslt(index,n),body,exit); b.setInsertPoint(body);
    Value* offset=b.createMul(b.createExtSW(index,i64),ctx->getConstantInt(i64,4));
    Value* av=b.createLoaduw(b.createAdd(input,offset)); Value* outp=b.createAdd(out,offset);
    b.createBr(b.createCsgt(av,ctx->getConstantInt(i32,0)),yes,no);
    b.setInsertPoint(yes); b.createJmp(merge); b.setInsertPoint(no); b.createJmp(merge);
    b.setInsertPoint(merge); auto valueOwner=std::make_unique<PhiNode>(i32,0,nullptr,merge);
    auto* value=valueOwner.get(); merge->getInstructions().push_back(std::move(valueOwner));
    value->addIncoming(av,yes); value->addIncoming(ctx->getConstantInt(i32,0),no); b.createStore(value,outp);
    Value* next=b.createAdd(index,ctx->getConstantInt(i32,1)); index->addIncoming(next,merge); b.createJmp(header);
    b.setInsertPoint(exit); b.createRet(nullptr); transforms::CFGBuilder::run(*f); return f;
}

enum class DiamondKind { Max, I32Arithmetic, F32Clamp, F32Arithmetic, F64Clamp };

static Function* makeScalarDiamond(IRBuilder& b, Module& m, const char* name, DiamondKind kind) {
    auto ctx=m.getContextShared(); auto* i32=ctx->getIntegerType(32); auto* i64=ctx->getIntegerType(64);
    Type* elem=(kind==DiamondKind::F32Clamp||kind==DiamondKind::F32Arithmetic)
        ? static_cast<Type*>(ctx->getFloatType()) : kind==DiamondKind::F64Clamp
        ? static_cast<Type*>(ctx->getDoubleType()) : static_cast<Type*>(i32);
    auto* f=b.createFunction(name,ctx->getVoidType(),{i64,i64,i64,i32}); auto p=f->getParameters().begin();
    Value* out=(p++)->get();Value* a=(p++)->get();Value* second=(p++)->get();Value* n=p->get();
    auto* entry=b.createBasicBlock("entry",f);auto* header=b.createBasicBlock("loop",f);
    auto* body=b.createBasicBlock("conditional",f);auto* yes=b.createBasicBlock("then",f);
    auto* no=b.createBasicBlock("else",f);auto* merge=b.createBasicBlock("merge",f);auto* exit=b.createBasicBlock("exit",f);
    b.setInsertPoint(entry);b.createJmp(header);b.setInsertPoint(header);auto io=std::make_unique<PhiNode>(i32,0,nullptr,header);
    auto* index=io.get();header->getInstructions().push_back(std::move(io));index->addIncoming(ctx->getConstantInt(i32,0),entry);
    b.createBr(b.createCslt(index,n),body,exit);b.setInsertPoint(body);
    auto address=[&](Value* base)->Value*{Value* off=b.createMul(b.createExtSW(index,i64),ctx->getConstantInt(i64,elem->getSize()));return b.createAdd(base,off);};
    auto load=[&](Value* base)->Value*{Value* ptr=address(base);return elem->isFloatTy()?static_cast<Value*>(b.createLoads(ptr)):elem->isDoubleTy()?static_cast<Value*>(b.createLoadd(ptr)):static_cast<Value*>(b.createLoaduw(ptr));};
    Value* av=load(a);Value* bv=(kind==DiamondKind::Max||!elem->isIntegerTy())?load(second):nullptr;Value* zero=elem->isIntegerTy()?static_cast<Value*>(ctx->getConstantInt(i32,0)):bv;
    b.createBr(elem->isIntegerTy()?b.createCsgt(av,bv?bv:zero):b.createCgt(av,zero),yes,no);
    b.setInsertPoint(yes);Value* tv=av;if(kind==DiamondKind::I32Arithmetic)tv=b.createMul(av,ctx->getConstantInt(i32,2));
    if(kind==DiamondKind::F32Arithmetic)tv=b.createFMul(av,ctx->getConstantFP(elem,2.0));b.createJmp(merge);
    b.setInsertPoint(no);Value* fv=(kind==DiamondKind::Max)?bv:zero;if(kind==DiamondKind::I32Arithmetic)fv=b.createNeg(av);
    if(kind==DiamondKind::F32Arithmetic)fv=b.createFAdd(av,ctx->getConstantFP(elem,1.0));b.createJmp(merge);
    b.setInsertPoint(merge);auto vo=std::make_unique<PhiNode>(elem,0,nullptr,merge);auto* value=vo.get();merge->getInstructions().push_back(std::move(vo));
    value->addIncoming(tv,yes);value->addIncoming(fv,no);Value* outp=address(out);if(elem->isFloatTy())b.createStores(value,outp);else if(elem->isDoubleTy())b.createStored(value,outp);else b.createStore(value,outp);
    Value* next=b.createAdd(index,ctx->getConstantInt(i32,1));index->addIncoming(next,merge);b.createJmp(header);b.setInsertPoint(exit);b.createRet(nullptr);transforms::CFGBuilder::run(*f);return f;
}

static Function* makeSelect(IRBuilder& b, Module& m, const char* name,
                            Type* element, unsigned lanes, VectorCompareOp pred) {
    auto ctx = m.getContextShared();
    Type* i64 = ctx->getIntegerType(64);
    auto* f = b.createFunction(name, ctx->getVoidType(), {i64, i64, i64});
    auto p = f->getParameters().begin();
    Value* a = (p++)->get(); Value* other = (p++)->get(); Value* out = (p++)->get();
    b.setInsertPoint(b.createBasicBlock("entry", f));
    auto* vt = ctx->getVectorType(element, lanes);
    auto* va = b.createVLoad(vt, a);
    auto* vb = b.createVLoad(vt, other);
    auto* mask = b.createVCmp(va, vb, pred);
    b.createVStore(b.createVSelect(mask, va, vb), out);
    b.createRet(nullptr);
    transforms::CFGBuilder::run(*f);
    transforms::LinearScanAllocator alloc; alloc.run(*f);
    transforms::RegAllocRewriter rewrite; rewrite.run(*f);
    return f;
}

int main() {
    auto ctx = std::make_shared<IRContext>(); Module m("predication", ctx);
    IRBuilder b(ctx); b.setModule(&m);
    makeSelect(b, m, "select_i32", ctx->getIntegerType(32), 8, VectorCompareOp::GT);
    makeSelect(b, m, "select_f32", ctx->getFloatType(), 8, VectorCompareOp::GT);
    makeSelect(b, m, "select_f64", ctx->getDoubleType(), 4, VectorCompareOp::GT);
    Function* scalarClamp = makeScalarClamp(b, m);
    Function* scalarMax = makeScalarDiamond(b,m,"scalar_max",DiamondKind::Max);
    Function* scalarArithmetic = makeScalarDiamond(b,m,"scalar_arithmetic",DiamondKind::I32Arithmetic);
    Function* scalarF32 = makeScalarDiamond(b,m,"scalar_f32_clamp",DiamondKind::F32Clamp);
    Function* scalarF64 = makeScalarDiamond(b,m,"scalar_f64_clamp",DiamondKind::F64Clamp);
    transforms::LoopVectorizer vectorizer; assert(vectorizer.performTransformation(*scalarClamp));
    const size_t vectorizedBlocks = scalarClamp->getBasicBlocks().size();
    assert(!vectorizer.performTransformation(*scalarClamp));
    assert(scalarClamp->getBasicBlocks().size() == vectorizedBlocks);
    unsigned compares=0, selects=0;
    for (auto& block : scalarClamp->getBasicBlocks()) for (auto& inst : block->getInstructions()) {
        compares += inst->getOpcode()==Instruction::VCmp;
        selects += inst->getOpcode()==Instruction::VSelect;
    }
    assert(compares==1 && selects==1);
    transforms::LinearScanAllocator scalarAlloc; scalarAlloc.run(*scalarClamp);
    transforms::RegAllocRewriter scalarRewrite; scalarRewrite.run(*scalarClamp);
    for(Function* function:{scalarMax,scalarArithmetic,scalarF32,scalarF64}){
        transforms::LoopVectorizer pass;assert(pass.performTransformation(*function));size_t blocks=function->getBasicBlocks().size();
        assert(!pass.performTransformation(*function)&&function->getBasicBlocks().size()==blocks);
        unsigned c=0,s=0;for(auto& block:function->getBasicBlocks())for(auto& inst:block->getInstructions()){c+=inst->getOpcode()==Instruction::VCmp;s+=inst->getOpcode()==Instruction::VSelect;}
        assert(c==1&&s==1);transforms::LinearScanAllocator allocation;allocation.run(*function);transforms::RegAllocRewriter rewriting;rewriting.run(*function);
    }

    auto arch = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    assert(arch->supportsVectorOperation(Instruction::VCmp,
        ctx->getVectorType(ctx->getIntegerType(32), 8)));
    assert(arch->supportsVectorOperation(Instruction::VSelect,
        ctx->getVectorType(ctx->getDoubleType(), 4)));
    auto os = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> target =
        std::make_unique<target::CompositeTargetInfo>(std::move(arch), std::move(os));
    std::ostringstream assembly; codegen::CodeGen cg(m, std::move(target), &assembly); cg.emit(false);
    const std::string text = assembly.str();
    assert(text.find("vpcmpgtd") != std::string::npos);
    assert(text.find("vpblendvb") != std::string::npos);
    assert(text.find("vcmpps $1") != std::string::npos);
    assert(text.find("vblendvps") != std::string::npos);
    assert(text.find("vcmppd $1") != std::string::npos);
    assert(text.find("vblendvpd") != std::string::npos);

    const std::string base = "/tmp/fyra_predication_" + std::to_string(::getpid());
    std::ofstream(base + ".s") << text;
    std::ofstream(base + ".c") << R"(
#include <assert.h>
#include <limits.h>
#include <stdio.h>
extern void select_i32(int*,int*,int*); extern void select_f32(float*,float*,float*);
extern void select_f64(double*,double*,double*);
extern void scalar_clamp(int*,int*,int,int);
extern void scalar_max(int*,int*,int*,int);extern void scalar_arithmetic(int*,int*,int*,int);
extern void scalar_f32_clamp(float*,float*,float*,int);
extern void scalar_f64_clamp(double*,double*,double*,int);
int main(void) {
 int a[8]={INT_MIN,-100,-1,0,1,100,INT_MAX,-7}, z[8]={0}, o[8]; select_i32(a,z,o);
 for(int i=0;i<8;i++) assert(o[i]==(a[i]>0?a[i]:0));
 float af[8]={-3,-0.0f,0,1,9,-2,4,-8},zf[8]={0},of[8]; select_f32(af,zf,of);
 for(int i=0;i<8;i++) assert(of[i]==(af[i]>0?af[i]:0));
 double ad[4]={-3,-0.0,1,9},zd[4]={0},od[4]; select_f64(ad,zd,od);
 for(int i=0;i<4;i++) assert(od[i]==(ad[i]>0?ad[i]:0));
 int ai[40],oi[40]; for(int n=0;n<40;n++){for(int i=0;i<40;i++){ai[i]=(i%3)?i:-i;oi[i]=-77;}ai[3]=INT_MIN;ai[4]=INT_MAX;
  scalar_clamp(oi,ai,n,3); for(int i=0;i<40;i++)assert(oi[i]==(i>=3&&i<n?(ai[i]>0?ai[i]:0):-77));}
 int overlap[48],reference[48];for(int i=0;i<48;i++)overlap[i]=reference[i]=i%4?i:-i;
 for(int i=0;i<31;i++)reference[i+1]=reference[i]>0?reference[i]:0;
 scalar_clamp(overlap+1,overlap,31,0);for(int i=0;i<48;i++)assert(overlap[i]==reference[i]);
 return 0;
})";
    std::string command = "cc -mavx2 -no-pie " + base + ".s " + base + ".c -o " + base + " && " + base;
    assert(std::system(command.c_str()) == 0);
    std::remove((base + ".s").c_str()); std::remove((base + ".c").c_str()); std::remove(base.c_str());
}
