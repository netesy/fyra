#include "target/artifact/apk/APKArtifact.h"
#include "target/artifact/apk/ManifestGen.h"
#include "target/artifact/apk/JavaStubGen.h"
#include "target/artifact/apk/APKPackager.h"
#include "codegen/CodeGen.h"
#include <fstream>
#include <filesystem>

namespace target {
namespace artifact {

APKArtifact::APKArtifact(std::unique_ptr<TargetInfo> base)
    : baseTarget(std::move(base)) {}

std::string APKArtifact::getName() const { return baseTarget->getName() + "-apk"; }
TypeInfo APKArtifact::getTypeInfo(const ir::Type* t) const { return baseTarget->getTypeInfo(t); }
const std::vector<std::string>& APKArtifact::getRegisters(RegisterClass rc) const { return baseTarget->getRegisters(rc); }
const std::string& APKArtifact::getReturnRegister(const ir::Type* t) const { return baseTarget->getReturnRegister(t); }
const std::vector<std::string>& APKArtifact::getIntegerArgumentRegisters() const { return baseTarget->getIntegerArgumentRegisters(); }
const std::vector<std::string>& APKArtifact::getFloatArgumentRegisters() const { return baseTarget->getFloatArgumentRegisters(); }
const std::string& APKArtifact::getIntegerReturnRegister() const { return baseTarget->getIntegerReturnRegister(); }
const std::string& APKArtifact::getFloatReturnRegister() const { return baseTarget->getFloatReturnRegister(); }

void APKArtifact::emitHeader(CodeGen& cg) { baseTarget->emitHeader(cg); }
void APKArtifact::emitFooter(CodeGen& cg) { baseTarget->emitFooter(cg); }
void APKArtifact::emitPrologue(CodeGen& cg, int i) { baseTarget->emitPrologue(cg, i); }
void APKArtifact::emitEpilogue(CodeGen& cg) { baseTarget->emitEpilogue(cg); }
void APKArtifact::emitFunctionPrologue(CodeGen& cg, ir::Function& f) { baseTarget->emitFunctionPrologue(cg, f); }
void APKArtifact::emitFunctionEpilogue(CodeGen& cg, ir::Function& f) { baseTarget->emitFunctionEpilogue(cg, f); }
void APKArtifact::emitBasicBlockStart(CodeGen& cg, ir::BasicBlock& bb) { baseTarget->emitBasicBlockStart(cg, bb); }
void APKArtifact::emitStructuredFunctionBody(CodeGen& cg, ir::Function& f) { baseTarget->emitStructuredFunctionBody(cg, f); }
void APKArtifact::emitStartFunction(CodeGen& cg) { baseTarget->emitStartFunction(cg); }
size_t APKArtifact::getMaxRegistersForArgs() const { return baseTarget->getMaxRegistersForArgs(); }
void APKArtifact::emitPassArgument(CodeGen& cg, size_t i, const std::string& v, const ir::Type* t) { baseTarget->emitPassArgument(cg, i, v, t); }
void APKArtifact::emitGetArgument(CodeGen& cg, size_t i, const std::string& d, const ir::Type* t) { baseTarget->emitGetArgument(cg, i, d, t); }
void APKArtifact::emitRet(CodeGen& cg, ir::Instruction& i) { baseTarget->emitRet(cg, i); }
void APKArtifact::emitAdd(CodeGen& cg, ir::Instruction& i) { baseTarget->emitAdd(cg, i); }
void APKArtifact::emitSub(CodeGen& cg, ir::Instruction& i) { baseTarget->emitSub(cg, i); }
void APKArtifact::emitMul(CodeGen& cg, ir::Instruction& i) { baseTarget->emitMul(cg, i); }
void APKArtifact::emitDiv(CodeGen& cg, ir::Instruction& i) { baseTarget->emitDiv(cg, i); }
void APKArtifact::emitRem(CodeGen& cg, ir::Instruction& i) { baseTarget->emitRem(cg, i); }
void APKArtifact::emitAnd(CodeGen& cg, ir::Instruction& i) { baseTarget->emitAnd(cg, i); }
void APKArtifact::emitOr(CodeGen& cg, ir::Instruction& i) { baseTarget->emitOr(cg, i); }
void APKArtifact::emitXor(CodeGen& cg, ir::Instruction& i) { baseTarget->emitXor(cg, i); }
void APKArtifact::emitShl(CodeGen& cg, ir::Instruction& i) { baseTarget->emitShl(cg, i); }
void APKArtifact::emitShr(CodeGen& cg, ir::Instruction& i) { baseTarget->emitShr(cg, i); }
void APKArtifact::emitSar(CodeGen& cg, ir::Instruction& i) { baseTarget->emitSar(cg, i); }
void APKArtifact::emitNeg(CodeGen& cg, ir::Instruction& i) { baseTarget->emitNeg(cg, i); }
void APKArtifact::emitNot(CodeGen& cg, ir::Instruction& i) { baseTarget->emitNot(cg, i); }
void APKArtifact::emitCopy(CodeGen& cg, ir::Instruction& i) { baseTarget->emitCopy(cg, i); }
void APKArtifact::emitCall(CodeGen& cg, ir::Instruction& i) { baseTarget->emitCall(cg, i); }
void APKArtifact::emitFAdd(CodeGen& cg, ir::Instruction& i) { baseTarget->emitFAdd(cg, i); }
void APKArtifact::emitFSub(CodeGen& cg, ir::Instruction& i) { baseTarget->emitFSub(cg, i); }
void APKArtifact::emitFMul(CodeGen& cg, ir::Instruction& i) { baseTarget->emitFMul(cg, i); }
void APKArtifact::emitFDiv(CodeGen& cg, ir::Instruction& i) { baseTarget->emitFDiv(cg, i); }
void APKArtifact::emitCmp(CodeGen& cg, ir::Instruction& i) { baseTarget->emitCmp(cg, i); }
void APKArtifact::emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* f, const ir::Type* t) { baseTarget->emitCast(cg, i, f, t); }
void APKArtifact::emitVAStart(CodeGen& cg, ir::Instruction& i) { baseTarget->emitVAStart(cg, i); }
void APKArtifact::emitVAArg(CodeGen& cg, ir::Instruction& i) { baseTarget->emitVAArg(cg, i); }
void APKArtifact::emitVAEnd(CodeGen& cg, ir::Instruction& i) { baseTarget->emitVAEnd(cg, i); }
void APKArtifact::emitLoad(CodeGen& cg, ir::Instruction& i) { baseTarget->emitLoad(cg, i); }
void APKArtifact::emitStore(CodeGen& cg, ir::Instruction& i) { baseTarget->emitStore(cg, i); }
void APKArtifact::emitAlloc(CodeGen& cg, ir::Instruction& i) { baseTarget->emitAlloc(cg, i); }
void APKArtifact::emitSyscall(CodeGen& cg, ir::Instruction& i) { baseTarget->emitSyscall(cg, i); }
void APKArtifact::emitExternCall(CodeGen& cg, ir::Instruction& i) { baseTarget->emitExternCall(cg, i); }
void APKArtifact::emitBr(CodeGen& cg, ir::Instruction& i) { baseTarget->emitBr(cg, i); }
void APKArtifact::emitJmp(CodeGen& cg, ir::Instruction& i) { baseTarget->emitJmp(cg, i); }

bool APKArtifact::supportsCapability(const CapabilitySpec& spec) const { return baseTarget->supportsCapability(spec); }
void APKArtifact::emitIOCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitIOCapability(cg, i, spec); }
void APKArtifact::emitFSCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitFSCapability(cg, i, spec); }
void APKArtifact::emitMemoryCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitMemoryCapability(cg, i, spec); }
void APKArtifact::emitProcessCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitProcessCapability(cg, i, spec); }
void APKArtifact::emitThreadCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitThreadCapability(cg, i, spec); }
void APKArtifact::emitSyncCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitSyncCapability(cg, i, spec); }
void APKArtifact::emitTimeCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitTimeCapability(cg, i, spec); }
void APKArtifact::emitEventCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitEventCapability(cg, i, spec); }
void APKArtifact::emitNetCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitNetCapability(cg, i, spec); }
void APKArtifact::emitIPCCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitIPCCapability(cg, i, spec); }
void APKArtifact::emitEnvCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitEnvCapability(cg, i, spec); }
void APKArtifact::emitSystemCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitSystemCapability(cg, i, spec); }
void APKArtifact::emitSignalCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitSignalCapability(cg, i, spec); }
void APKArtifact::emitRandomCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitRandomCapability(cg, i, spec); }
void APKArtifact::emitErrorCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitErrorCapability(cg, i, spec); }
void APKArtifact::emitDebugCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitDebugCapability(cg, i, spec); }
void APKArtifact::emitModuleCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitModuleCapability(cg, i, spec); }
void APKArtifact::emitTTYCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitTTYCapability(cg, i, spec); }
void APKArtifact::emitSecurityCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitSecurityCapability(cg, i, spec); }
void APKArtifact::emitGPUCapability(codegen::CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { baseTarget->emitGPUCapability(cg, i, spec); }

uint64_t APKArtifact::getSyscallNumber(ir::SyscallId id) const { return baseTarget->getSyscallNumber(id); }

std::string APKArtifact::formatStackOperand(int o) const { return baseTarget->formatStackOperand(o); }
std::string APKArtifact::formatGlobalOperand(const std::string& n) const { return baseTarget->formatGlobalOperand(n); }
bool APKArtifact::isCallerSaved(const std::string& r) const { return baseTarget->isCallerSaved(r); }
bool APKArtifact::isCalleeSaved(const std::string& r) const { return baseTarget->isCalleeSaved(r); }

void APKArtifact::buildAPK(const std::string& outputPrefix) {
    namespace fs = std::filesystem;
    std::string packageName = "com.fyra.app";
    std::string appName = "FyraApp";

    std::string manifest = ManifestGen::generate(packageName, appName);
    std::string manifestPath = outputPrefix + "_AndroidManifest.xml";
    std::ofstream mfile(manifestPath);
    mfile << manifest;
    mfile.close();

    std::string javaStub = JavaStubGen::generate(packageName, "MainActivity");
    std::string javaPath = outputPrefix + "_MainActivity.java";
    std::ofstream jfile(javaPath);
    jfile << javaStub;
    jfile.close();

    std::string libPath = outputPrefix + ".so";
    if (!fs::exists(libPath)) {
        std::ofstream dummySo(libPath);
        dummySo << "ELF_DUMMY_SO";
    }

    APKPackager::package(outputPrefix + ".apk", "", libPath, manifestPath);
}

}
}
