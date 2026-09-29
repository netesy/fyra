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
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>

using namespace ir;

enum class Element { I32, F32, F64 };

static Function* buildArrayLoop(Module& module, IRBuilder& builder,
                                const char* name, Element element,
                                uint32_t start = 0, bool runtimeStart = false,
                                bool duplicateRead = false) {
    auto ctx = module.getContextShared();
    auto* i32 = ctx->getIntegerType(32);
    auto* i64 = ctx->getIntegerType(64);
    Type* elem = element == Element::I32 ? static_cast<Type*>(i32)
               : element == Element::F32 ? static_cast<Type*>(ctx->getFloatType())
                                         : static_cast<Type*>(ctx->getDoubleType());
    const uint64_t size = elem->getSize();
    std::vector<Type*> parameters{i64, i64, i64, i32};
    if (runtimeStart) parameters.push_back(i32);
    Function* fn = builder.createFunction(name, ctx->getVoidType(), parameters);
    auto parameter = fn->getParameters().begin();
    Value* out = (parameter++)->get();
    Value* a = (parameter++)->get();
    Value* b = (parameter++)->get();
    Value* n = (parameter++)->get();
    Value* startValue = runtimeStart ? parameter->get()
                                     : static_cast<Value*>(ctx->getConstantInt(i32, start));
    out->setName("out"); a->setName("a"); b->setName("b"); n->setName("n");

    BasicBlock* entry = builder.createBasicBlock("entry", fn);
    BasicBlock* header = builder.createBasicBlock("loop", fn);
    BasicBlock* body = builder.createBasicBlock("body", fn);
    BasicBlock* exit = builder.createBasicBlock("exit", fn);
    builder.setInsertPoint(entry); builder.createJmp(header);
    builder.setInsertPoint(header);
    auto owner = std::make_unique<PhiNode>(i32, 0, nullptr, header);
    PhiNode* index = owner.get(); header->getInstructions().push_back(std::move(owner));
    index->addIncoming(startValue, entry);
    builder.createBr(builder.createCslt(index, n), body, exit);
    builder.setInsertPoint(body);
    Value* wide = builder.createExtSW(index, i64);
    Value* offset = builder.createMul(wide, ctx->getConstantInt(i64, size));
    Value* ap = builder.createAdd(a, offset);
    Value* bp = builder.createAdd(duplicateRead ? a : b, offset);
    Value* op = builder.createAdd(out, offset);
    Value* av = element == Element::I32 ? static_cast<Value*>(builder.createLoaduw(ap))
              : element == Element::F32 ? static_cast<Value*>(builder.createLoads(ap))
                                        : static_cast<Value*>(builder.createLoadd(ap));
    Value* bv = element == Element::I32 ? static_cast<Value*>(builder.createLoaduw(bp))
              : element == Element::F32 ? static_cast<Value*>(builder.createLoads(bp))
                                        : static_cast<Value*>(builder.createLoadd(bp));
    Value* result = element == Element::I32 ? static_cast<Value*>(builder.createAdd(av, bv))
                  : element == Element::F32 ? static_cast<Value*>(builder.createFAdd(av, bv))
                                            : static_cast<Value*>(builder.createFMul(av, bv));
    if (element == Element::I32) builder.createStore(result, op);
    else if (element == Element::F32) builder.createStores(result, op);
    else builder.createStored(result, op);
    Value* next = builder.createAdd(index, ctx->getConstantInt(i32, 1));
    index->addIncoming(next, body); builder.createJmp(header);
    builder.setInsertPoint(exit); builder.createRet(nullptr);
    transforms::CFGBuilder::run(*fn);
    return fn;
}

static size_t countNamedBlocks(Function& function, const std::string& needle) {
    size_t result = 0;
    for (const auto& block : function.getBasicBlocks())
        result += block->getName().find(needle) != std::string::npos;
    return result;
}

static Function* buildDependentLoop(Module& module, IRBuilder& builder) {
    auto ctx = module.getContextShared(); auto* i32 = ctx->getIntegerType(32);
    auto* i64 = ctx->getIntegerType(64);
    Function* fn = builder.createFunction("inherent_dependence", ctx->getVoidType(), {i64, i32});
    auto parameter = fn->getParameters().begin(); Value* base = (parameter++)->get();
    Value* n = parameter->get();
    BasicBlock* entry = builder.createBasicBlock("entry", fn);
    BasicBlock* header = builder.createBasicBlock("loop", fn);
    BasicBlock* body = builder.createBasicBlock("body", fn);
    BasicBlock* exit = builder.createBasicBlock("exit", fn);
    builder.setInsertPoint(entry); builder.createJmp(header); builder.setInsertPoint(header);
    auto owner = std::make_unique<PhiNode>(i32, 0, nullptr, header);
    PhiNode* index = owner.get(); header->getInstructions().push_back(std::move(owner));
    index->addIncoming(ctx->getConstantInt(i32, 1), entry);
    builder.createBr(builder.createCslt(index, n), body, exit); builder.setInsertPoint(body);
    Value* wide = builder.createExtSW(index, i64);
    Value* offset = builder.createMul(wide, ctx->getConstantInt(i64, 4));
    Value* previousOffset = builder.createAdd(offset, ctx->getConstantInt(i64, uint64_t(-4)));
    Value* previous = builder.createLoaduw(builder.createAdd(base, previousOffset));
    Value* nextValue = builder.createAdd(previous, ctx->getConstantInt(i32, 1));
    builder.createStore(nextValue, builder.createAdd(base, offset));
    Value* next = builder.createAdd(index, ctx->getConstantInt(i32, 1));
    index->addIncoming(next, body); builder.createJmp(header);
    builder.setInsertPoint(exit); builder.createRet(nullptr); transforms::CFGBuilder::run(*fn);
    return fn;
}

int main() {
    auto ctx = std::make_shared<IRContext>(); Module module("auto_vec_alias", ctx);
    IRBuilder builder(ctx); builder.setModule(&module);
    Function* i32 = buildArrayLoop(module, builder, "alias_i32", Element::I32);
    Function* start3 = buildArrayLoop(module, builder, "alias_i32_start3", Element::I32, 3);
    Function* dynamicStart = buildArrayLoop(module, builder, "alias_i32_dynamic", Element::I32, 0, true);
    Function* readRead = buildArrayLoop(module, builder, "alias_readread", Element::I32, 0, false, true);
    Function* f32 = buildArrayLoop(module, builder, "alias_f32", Element::F32);
    Function* f64 = buildArrayLoop(module, builder, "alias_f64", Element::F64);
    Function* dependent = buildDependentLoop(module, builder);
    for (Function* function : {i32, start3, dynamicStart, readRead, f32, f64}) {
        transforms::LoopVectorizer vectorizer;
        assert(vectorizer.performTransformation(*function));
        const size_t blocks = function->getBasicBlocks().size();
        assert(countNamedBlocks(*function, "alias.runtime_check") == 1);
        assert(countNamedBlocks(*function, "alias.scalar_fallback.loop") == 1);
        assert(!vectorizer.performTransformation(*function));
        assert(function->getBasicBlocks().size() == blocks);
        assert(countNamedBlocks(*function, "alias.runtime_check") == 1);
        transforms::LinearScanAllocator allocator; allocator.run(*function);
    }
    transforms::LoopVectorizer dependenceVectorizer;
    assert(!dependenceVectorizer.performTransformation(*dependent));
    transforms::LinearScanAllocator dependenceAllocator; dependenceAllocator.run(*dependent);

    auto arch = std::make_unique<target::X64Architecture>(target::X64ABI::SystemV);
    auto os = std::make_unique<target::LinuxOS>();
    std::unique_ptr<target::TargetInfo> target =
        std::make_unique<target::CompositeTargetInfo>(std::move(arch), std::move(os));
    std::ostringstream stream; codegen::CodeGen generator(module, std::move(target), &stream);
    generator.emit(false); std::string assembly = stream.str();
    assert(assembly.find("vmovdqu") != std::string::npos);
    assert(assembly.find("vpaddd") != std::string::npos);
    assert(assembly.find("vaddps") != std::string::npos);
    assert(assembly.find("vmulpd") != std::string::npos);
    assert(assembly.find("alias.runtime_check") != std::string::npos);
    assert(assembly.find("alias.scalar_fallback") != std::string::npos);
    auto instrumentPath = [&](const std::string& function) {
        const std::string vectorLabel = function + "_v_loop_body:\n";
        const std::string scalarLabel = function + "_alias.scalar_fallback.body.body:\n";
        auto vectorPos = assembly.find(vectorLabel);
        auto scalarPos = assembly.find(scalarLabel);
        assert(vectorPos != std::string::npos && scalarPos != std::string::npos);
        assembly.insert(vectorPos + vectorLabel.size(), "  movl $1, path_counter(%rip)\n");
        scalarPos = assembly.find(scalarLabel);
        assembly.insert(scalarPos + scalarLabel.size(), "  movl $2, path_counter(%rip)\n");
    };
    for (const char* function : {"alias_i32", "alias_i32_start3", "alias_i32_dynamic",
                                 "alias_readread", "alias_f32", "alias_f64"})
        instrumentPath(function);

    static std::atomic<uint64_t> counter{0};
    const std::string id = std::to_string(getpid()) + "_" + std::to_string(counter++);
    const std::string asmPath = "/tmp/auto_vec_alias_" + id + ".s";
    const std::string cPath = "/tmp/auto_vec_alias_" + id + ".c";
    const std::string binPath = "/tmp/auto_vec_alias_" + id;
    std::ofstream(asmPath) << assembly;
    std::ofstream harness(cPath);
    harness << R"C(
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
volatile int path_counter;
extern void alias_i32(int32_t*,int32_t*,int32_t*,int32_t);
extern void alias_i32_start3(int32_t*,int32_t*,int32_t*,int32_t);
extern void alias_i32_dynamic(int32_t*,int32_t*,int32_t*,int32_t,int32_t);
extern void alias_readread(int32_t*,int32_t*,int32_t*,int32_t);
extern void alias_f32(float*,float*,float*,int32_t);
extern void alias_f64(double*,double*,double*,int32_t);
#define CHECK(C,M) do { if (!(C)) { fprintf(stderr,"FAIL: %s (path=%d)\n",M,path_counter); return 1; } } while (0)
static int independent(int n) {
 int count=n+8>1100?n+8:1100;int32_t *a=malloc(count*4),*b=malloc(count*4),*o=malloc(count*4); CHECK(a&&b&&o,"alloc");
 for(int i=0;i<count;i++){a[i]=i*3-7;b[i]=i+11;o[i]=-1;} path_counter=0; alias_i32(o,a,b,n);
 CHECK(path_counter==(n>=8?1:0),"independent routing"); for(int i=0;i<n;i++)CHECK(o[i]==a[i]+b[i],"independent value"); free(a);free(b);free(o);return 0;
}
static int exact_alias(void) { int32_t x[80],b[80],ref[80]; for(int i=0;i<80;i++){x[i]=ref[i]=i;b[i]=5;} for(int i=0;i<31;i++)ref[i]+=b[i]; path_counter=0;alias_i32(x,x,b,31);CHECK(path_counter==2,"exact alias routing");CHECK(!memcmp(x,ref,sizeof(x)),"exact alias value");return 0; }
static int forward_overlap(void) { int32_t x[96],b[96],ref[96]; for(int i=0;i<96;i++){x[i]=ref[i]=i+1;b[i]=2;} for(int i=0;i<31;i++)ref[i+1]=ref[i]+b[i]; path_counter=0;alias_i32(x+1,x,b,31);CHECK(path_counter==2,"forward routing");CHECK(!memcmp(x,ref,sizeof(x)),"forward value");return 0; }
static int reverse_overlap(void) { int32_t x[96],b[96],ref[96]; for(int i=0;i<96;i++){x[i]=ref[i]=i+1;b[i]=3;} for(int i=0;i<31;i++)ref[i]=ref[i+1]+b[i]; path_counter=0;alias_i32(x,x+1,b,31);CHECK(path_counter==2,"reverse routing");CHECK(!memcmp(x,ref,sizeof(x)),"reverse value");return 0; }
static int touching(void) { int32_t x[128],b[64],ref[128]; for(int i=0;i<128;i++)x[i]=ref[i]=i+4;for(int i=0;i<64;i++)b[i]=i;for(int i=0;i<31;i++)ref[31+i]=ref[i]+b[i];path_counter=0;alias_i32(x+31,x,b,31);CHECK(path_counter==1,"touching routing");CHECK(!memcmp(x,ref,sizeof(x)),"touching value");return 0; }
static int zero_length(void) { int32_t x[4]={1,2,3,4};path_counter=0;alias_i32(x,x,x,0);CHECK(path_counter==0,"zero routing");CHECK(x[0]==1&&x[3]==4,"zero value");return 0; }
static int dynamic_cases(void) { int32_t a[96],b[96],o[96],x[128],ref[128];
 for(int n=20;n<=22;n+=2){for(int i=0;i<96;i++){a[i]=i;b[i]=3*i;o[i]=-7;}path_counter=0;alias_i32_dynamic(o,a,b,n,3);CHECK(path_counter==1,"dynamic independent routing");for(int i=0;i<96;i++)CHECK(o[i]==(i>=3&&i<n?4*i:-7),"dynamic independent value");}
 for(int i=0;i<128;i++){x[i]=ref[i]=i+1;}for(int i=0;i<96;i++)b[i]=2;for(int i=3;i<20;i++)ref[17+i]=ref[i]+b[i];path_counter=0;alias_i32_dynamic(x+17,x,b,20,3);CHECK(path_counter==1,"outside-range routing");CHECK(!memcmp(x,ref,sizeof(x)),"outside-range value");
 for(int i=0;i<128;i++){x[i]=ref[i]=i+1;}for(int i=3;i<20;i++)ref[1+i]=ref[i]+b[i];path_counter=0;alias_i32_dynamic(x+1,x,b,20,3);CHECK(path_counter==2,"dynamic overlap routing");CHECK(!memcmp(x,ref,sizeof(x)),"dynamic overlap value");return 0; }
static int readread(void){int32_t a[64],b[64],o[64];for(int i=0;i<64;i++){a[i]=i-9;b[i]=0;o[i]=0;}path_counter=0;alias_readread(o,a,b,24);CHECK(path_counter==1,"read/read routing");for(int i=0;i<24;i++)CHECK(o[i]==2*a[i],"read/read value");return 0;}
static int floating(void){float af[64],bf[64],of[64],xf[80],rf[80];double ad[64],bd[64],od[64],xd[80],rd[80];for(int i=0;i<64;i++){af[i]=i+1;bf[i]=2;of[i]=0;ad[i]=i+1;bd[i]=3;od[i]=0;}path_counter=0;alias_f32(of,af,bf,31);CHECK(path_counter==1,"f32 vector routing");for(int i=0;i<31;i++)CHECK(of[i]==af[i]+bf[i],"f32 value");path_counter=0;alias_f64(od,ad,bd,31);CHECK(path_counter==1,"f64 vector routing");for(int i=0;i<31;i++)CHECK(od[i]==ad[i]*bd[i],"f64 value");
 for(int i=0;i<80;i++){xf[i]=rf[i]=i+1;}for(int i=0;i<31;i++)rf[i+1]=rf[i]+bf[i];path_counter=0;alias_f32(xf+1,xf,bf,31);CHECK(path_counter==2,"f32 scalar routing");CHECK(!memcmp(xf,rf,sizeof(xf)),"f32 overlap value");for(int i=0;i<80;i++){xd[i]=rd[i]=i+1;}for(int i=0;i<31;i++)rd[i+1]=rd[i]*bd[i];path_counter=0;alias_f64(xd+1,xd,bd,31);CHECK(path_counter==2,"f64 scalar routing");CHECK(!memcmp(xd,rd,sizeof(xd)),"f64 overlap value");return 0;}
int main(void){const int ns[]={0,1,7,8,9,31,1024,100000};for(unsigned i=0;i<sizeof(ns)/sizeof(ns[0]);i++)if(independent(ns[i]))return 1;if(exact_alias()||forward_overlap()||reverse_overlap()||touching()||zero_length()||dynamic_cases()||readread()||floating())return 2;puts("alias versioning execution and routing passed");return 0;}
)C";
    harness.close();
    const std::string command = "gcc -O0 -no-pie " + asmPath + " " + cPath +
                                " -o " + binPath + " && " + binPath;
    const int rc = std::system(command.c_str());
    if (rc != 0) std::fprintf(stderr, "alias harness status=%d\n", rc);
    if (std::getenv("FYRA_KEEP_ALIAS_ARTIFACTS"))
        std::fprintf(stderr, "alias artifacts: %s %s %s\n", asmPath.c_str(), cPath.c_str(), binPath.c_str());
    else {
        std::remove(asmPath.c_str()); std::remove(cPath.c_str()); std::remove(binPath.c_str());
    }
    assert(rc == 0);
    return 0;
}
