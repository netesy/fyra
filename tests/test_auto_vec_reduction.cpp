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
using namespace ir;

enum class Kind { Add, Mul, Min, Max, Sub, Div };
static const char* name(Kind k) { switch(k){case Kind::Add:return "reduce_add";case Kind::Mul:return "reduce_mul";case Kind::Min:return "reduce_min";case Kind::Max:return "reduce_max";case Kind::Sub:return "reduce_sub";case Kind::Div:return "reduce_div";} return ""; }

static Function* build(Module& m, IRBuilder& b, Kind kind) {
 auto c=m.getContextShared(); auto*i32=c->getIntegerType(32); auto*i64=c->getIntegerType(64);
 Function*f=b.createFunction(name(kind),i32,{i64,i32,i32,i32}); auto p=f->getParameters().begin();
 Value*base=(p++)->get(); Value*n=(p++)->get(); Value*start=(p++)->get(); Value*init=p->get();
 auto*entry=b.createBasicBlock("entry",f);auto*h=b.createBasicBlock("loop",f);auto*body=b.createBasicBlock("body",f);auto*exit=b.createBasicBlock("exit",f);
 b.setInsertPoint(entry);b.createJmp(h);b.setInsertPoint(h);
 auto io=std::make_unique<PhiNode>(i32,0,nullptr,h);auto*i=io.get();h->getInstructions().push_back(std::move(io));
 auto ao=std::make_unique<PhiNode>(i32,0,nullptr,h);auto*a=ao.get();h->getInstructions().push_back(std::move(ao));
 i->addIncoming(start,entry);a->addIncoming(init,entry);b.createBr(b.createCslt(i,n),body,exit);
 b.setInsertPoint(body);Value*wi=b.createExtSW(i,i64);Value*off=b.createMul(wi,c->getConstantInt(i64,4));Value*x=b.createLoaduw(b.createAdd(base,off));Value*next=nullptr;
 switch(kind){case Kind::Add:next=b.createAdd(a,x);break;case Kind::Mul:next=b.createMul(a,x);break;case Kind::Min:next=b.createSMin(a,x);break;case Kind::Max:next=b.createSMax(a,x);break;case Kind::Sub:next=b.createSub(a,x);break;case Kind::Div:next=b.createDiv(a,x,i32);break;}
 Value*in=b.createAdd(i,c->getConstantInt(i32,1));i->addIncoming(in,body);a->addIncoming(next,body);b.createJmp(h);b.setInsertPoint(exit);b.createRet(a);transforms::CFGBuilder::run(*f);return f;
}

int main(){auto c=std::make_shared<IRContext>();Module m("reductions",c);IRBuilder b(c);b.setModule(&m);
 for(Kind k:{Kind::Add,Kind::Mul,Kind::Min,Kind::Max}){auto*f=build(m,b,k);transforms::LoopVectorizer v;assert(v.performTransformation(*f));transforms::LinearScanAllocator ra;ra.run(*f);}
 auto arch=std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);auto os=std::make_unique<target::LinuxOS>();std::unique_ptr<target::TargetInfo> ti=std::make_unique<target::CompositeTargetInfo>(std::move(arch),std::move(os));std::ostringstream ss;codegen::CodeGen cg(m,std::move(ti),&ss);cg.emit(false);std::string as=ss.str();
 for(const char*s:{"vpaddd","vpmulld","vpminsd","vpmaxsd"})assert(as.find(s)!=std::string::npos);std::ofstream("/tmp/reductions.s")<<as;
 std::ofstream h("/tmp/reductions.c");h<<R"C(
#include <stdint.h>
#include <stdio.h>
extern int32_t reduce_add(int32_t*,int32_t,int32_t,int32_t);extern int32_t reduce_mul(int32_t*,int32_t,int32_t,int32_t);extern int32_t reduce_min(int32_t*,int32_t,int32_t,int32_t);extern int32_t reduce_max(int32_t*,int32_t,int32_t,int32_t);
static int32_t addref(int32_t*a,int n,int s,int32_t v){for(int i=s;i<n;i++)v=(int32_t)((uint32_t)v+(uint32_t)a[i]);return v;}static int32_t mulref(int32_t*a,int n,int s,int32_t v){for(int i=s;i<n;i++)v=(int32_t)((uint32_t)v*(uint32_t)a[i]);return v;}static int32_t minref(int32_t*a,int n,int s,int32_t v){for(int i=s;i<n;i++)if(a[i]<v)v=a[i];return v;}static int32_t maxref(int32_t*a,int n,int s,int32_t v){for(int i=s;i<n;i++)if(a[i]>v)v=a[i];return v;}
#define CHECK(F,R,A,N,S,I) do{int32_t g=F(A,N,S,I),e=R(A,N,S,I);if(g!=e){printf("FAIL %s n=%d s=%d init=%d got=%d expected=%d\n",#F,N,S,I,g,e);return 1;}}while(0)
int main(){int32_t sum[40]={0,0,0,0,10,20,30,40};int32_t prod[40]={1,1,1,1,2,3,4,5};int32_t mn[40]={100,100,100,100,-50,-40,-30,-20};int32_t mx[40]={-100,-100,-100,-100,50,40,30,20};for(int i=8;i<40;i++){sum[i]=i-10;prod[i]=1;mn[i]=i-5;mx[i]=-i;}
 int ns[]={0,1,7,8,9,15,16,17,31};for(unsigned j=0;j<9;j++){int n=ns[j];CHECK(reduce_add,addref,sum,n,0,7);CHECK(reduce_add,addref,sum,n,0,-9);CHECK(reduce_mul,mulref,prod,n,0,2);CHECK(reduce_mul,mulref,prod,n,0,-3);CHECK(reduce_min,minref,mn,n,0,500);CHECK(reduce_min,minref,mn,n,0,-10);CHECK(reduce_max,maxref,mx,n,0,-500);CHECK(reduce_max,maxref,mx,n,0,10);}for(int n=20;n<=22;n+=2){CHECK(reduce_add,addref,sum,n,3,7);CHECK(reduce_min,minref,mn,n,3,500);CHECK(reduce_max,maxref,mx,n,3,-500);}puts("reduction execution passed");return 0;}
)C";h.close();int rc=std::system("gcc -O0 -no-pie /tmp/reductions.s /tmp/reductions.c -o /tmp/reductions && /tmp/reductions");assert(rc==0);
 for(Kind k:{Kind::Sub,Kind::Div}){Function*f=build(m,b,k);transforms::LoopVectorizer v;assert(!v.performTransformation(*f));}return 0;}
