#include "codegen/CodeGen.h"
#include "codegen/regalloc/LinearScanAllocator.h"
#include "ir/IRBuilder.h"
#include "target/core/TargetResolver.h"
#include "transforms/CFGBuilder.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/mman.h>

using namespace ir;

static std::unique_ptr<target::TargetInfo> x64() {
    return target::TargetResolver::resolve({target::Arch::X64, target::OS::Linux});
}

int main() {
    auto context = std::make_shared<IRContext>();
    Module module("compare_select", context);
    IRBuilder builder(context); builder.setModule(&module);
    auto* i64 = context->getIntegerType(64);
    auto* v4i32 = context->getVectorType(context->getIntegerType(32), 4);

    auto makeCompare = [&](const char* name, VectorCompareOp predicate) {
        auto* fn = builder.createFunction(name, context->getVoidType(), {i64, i64, i64});
        auto it = fn->getParameters().begin(); auto* a=(it++)->get(); auto* b=(it++)->get(); auto* out=it->get();
        auto* bb=builder.createBasicBlock("entry", fn); builder.setInsertPoint(bb);
        auto* lhs=builder.createVLoad(v4i32,a); auto* rhs=builder.createVLoad(v4i32,b);
        builder.createVStore(builder.createVCmp(lhs,rhs,predicate),out); builder.createRet(nullptr);
        transforms::CFGBuilder::run(*fn); transforms::LinearScanAllocator allocator; allocator.run(*fn,x64().get());
    };
    makeCompare("cmp_eq", VectorCompareOp::EQ);
    makeCompare("cmp_gt", VectorCompareOp::GT);

    auto* select=builder.createFunction("select_i32",context->getVoidType(),{i64,i64,i64,i64});
    auto it=select->getParameters().begin(); auto* mp=(it++)->get(); auto* tp=(it++)->get(); auto* fp=(it++)->get(); auto* op=it->get();
    auto* bb=builder.createBasicBlock("entry",select); builder.setInsertPoint(bb);
    auto* mask=builder.createVLoad(v4i32,mp); auto* yes=builder.createVLoad(v4i32,tp); auto* no=builder.createVLoad(v4i32,fp);
    builder.createVStore(builder.createVSelect(mask,yes,no),op); builder.createRet(nullptr);
    transforms::CFGBuilder::run(*select); transforms::LinearScanAllocator allocator; allocator.run(*select,x64().get());

    std::ostringstream assembly; codegen::CodeGen text(module,x64(),&assembly); text.emit(false);
    std::ofstream("/tmp/fyra_cmp_select.s") << assembly.str();
    std::ofstream("/tmp/fyra_cmp_select_harness.cpp") << R"(
#include <cstdint>
extern "C" void cmp_eq(const int32_t*,const int32_t*,uint32_t*);
extern "C" void cmp_gt(const int32_t*,const int32_t*,uint32_t*);
extern "C" void select_i32(const uint32_t*,const uint32_t*,const uint32_t*,uint32_t*);
int main(){alignas(16) int32_t a[4]={-1,0,1,0x7fffffff},b[4]={-1,1,0,(int32_t)0x80000000}; alignas(16) uint32_t r[4];
cmp_eq(a,b,r); if(r[0]!=~0u||r[1]||r[2]||r[3])return 1; cmp_gt(a,b,r); if(r[0]||r[1]||r[2]!=~0u||r[3]!=~0u)return 2;
uint32_t m[4]={0,~0u,0,~0u},t[4]={1,2,3,4},f[4]={5,6,7,8}; select_i32(m,t,f,r);
return !(r[0]==5&&r[1]==2&&r[2]==7&&r[3]==4);})";
    assert(std::system("c++ -no-pie /tmp/fyra_cmp_select.s /tmp/fyra_cmp_select_harness.cpp -o /tmp/fyra_cmp_select && /tmp/fyra_cmp_select") == 0);

    codegen::CodeGen binary(module,x64(),nullptr); binary.emit(false);
    const auto& bytes=binary.getAssembler().getCode(); auto* image=(uint8_t*)mmap(nullptr,bytes.size(),PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(image!=MAP_FAILED); std::memcpy(image,bytes.data(),bytes.size());
    auto address=[&](const char* name){for(auto& s:binary.getSymbols())if(s.name==name)return image+s.value; return (uint8_t*)nullptr;};
    int32_t a[4]={-1,0,1,0x7fffffff},b[4]={-1,1,0,(int32_t)0x80000000}; uint32_t r[4];
    ((void(*)(const int32_t*,const int32_t*,uint32_t*))address("cmp_eq"))(a,b,r); assert(r[0]==~0u&&!r[1]&&!r[2]&&!r[3]);
    uint32_t m[4]={0,~0u,0,~0u},t[4]={1,2,3,4},f[4]={5,6,7,8}; ((void(*)(uint32_t*,uint32_t*,uint32_t*,uint32_t*))address("select_i32"))(m,t,f,r);
    assert(r[0]==5&&r[1]==2&&r[2]==7&&r[3]==4); munmap(image,bytes.size());
}
