#include "target/core/CompositeTargetInfo.h"

namespace codegen {
namespace target {

CompositeTargetInfo::CompositeTargetInfo(std::unique_ptr<ArchitectureInfo> arch, std::unique_ptr<OperatingSystemInfo> os)
    : architecture(std::move(arch)), os(std::move(os)) {}

std::string CompositeTargetInfo::getName() const {
    return architecture->getAssemblyFileExtension() + "-" + os->getName(); // or some other combination
}

size_t CompositeTargetInfo::getPointerSize() const { return architecture->getPointerSize(); }
size_t CompositeTargetInfo::getStackAlignment() const { return architecture->getStackAlignment(); }
TypeInfo CompositeTargetInfo::getTypeInfo(const ir::Type* type) const { return architecture->getTypeInfo(type); }
const std::vector<std::string>& CompositeTargetInfo::getRegisters(RegisterClass regClass) const { return architecture->getRegisters(regClass); }
const std::string& CompositeTargetInfo::getReturnRegister(const ir::Type* type) const { return architecture->getReturnRegister(type); }
const std::vector<std::string>& CompositeTargetInfo::getIntegerArgumentRegisters() const { return architecture->getIntegerArgumentRegisters(); }
const std::vector<std::string>& CompositeTargetInfo::getFloatArgumentRegisters() const { return architecture->getFloatArgumentRegisters(); }
const std::string& CompositeTargetInfo::getIntegerReturnRegister() const { return architecture->getIntegerReturnRegister(); }
const std::string& CompositeTargetInfo::getFloatReturnRegister() const { return architecture->getFloatReturnRegister(); }

void CompositeTargetInfo::emitHeader(CodeGen& cg) { os->emitHeader(cg); architecture->emitHeader(cg); }
void CompositeTargetInfo::emitFunctionPrologue(CodeGen& cg, ir::Function& func) { architecture->emitFunctionPrologue(cg, func); }
void CompositeTargetInfo::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) { architecture->emitFunctionEpilogue(cg, func); }
void CompositeTargetInfo::emitBasicBlockStart(CodeGen& cg, ir::BasicBlock& bb) { architecture->emitBasicBlockStart(cg, bb); }
void CompositeTargetInfo::emitStructuredFunctionBody(CodeGen& cg, ir::Function& func) { architecture->emitStructuredFunctionBody(cg, func); }
void CompositeTargetInfo::emitStartFunction(CodeGen& cg) { os->emitStartFunction(cg, *architecture); architecture->emitStartFunction(cg); }

size_t CompositeTargetInfo::getMaxRegistersForArgs() const { return architecture->getMaxRegistersForArgs(); }
void CompositeTargetInfo::emitPassArgument(CodeGen& cg, size_t idx, const std::string& val, const ir::Type* type) { architecture->emitPassArgument(cg, idx, val, type); }
void CompositeTargetInfo::emitGetArgument(CodeGen& cg, size_t idx, const std::string& dest, const ir::Type* type) { architecture->emitGetArgument(cg, idx, dest, type); }

void CompositeTargetInfo::emitRet(CodeGen& cg, ir::Instruction& i) { architecture->emitRet(cg, i); }
void CompositeTargetInfo::emitAdd(CodeGen& cg, ir::Instruction& i) { architecture->emitAdd(cg, i); }
void CompositeTargetInfo::emitSub(CodeGen& cg, ir::Instruction& i) { architecture->emitSub(cg, i); }
void CompositeTargetInfo::emitMul(CodeGen& cg, ir::Instruction& i) { architecture->emitMul(cg, i); }
void CompositeTargetInfo::emitDiv(CodeGen& cg, ir::Instruction& i) { architecture->emitDiv(cg, i); }
void CompositeTargetInfo::emitRem(CodeGen& cg, ir::Instruction& i) { architecture->emitRem(cg, i); }
void CompositeTargetInfo::emitAnd(CodeGen& cg, ir::Instruction& i) { architecture->emitAnd(cg, i); }
void CompositeTargetInfo::emitOr(CodeGen& cg, ir::Instruction& i) { architecture->emitOr(cg, i); }
void CompositeTargetInfo::emitXor(CodeGen& cg, ir::Instruction& i) { architecture->emitXor(cg, i); }
void CompositeTargetInfo::emitShl(CodeGen& cg, ir::Instruction& i) { architecture->emitShl(cg, i); }
void CompositeTargetInfo::emitShr(CodeGen& cg, ir::Instruction& i) { architecture->emitShr(cg, i); }
void CompositeTargetInfo::emitSar(CodeGen& cg, ir::Instruction& i) { architecture->emitSar(cg, i); }
void CompositeTargetInfo::emitNeg(CodeGen& cg, ir::Instruction& i) { architecture->emitNeg(cg, i); }
void CompositeTargetInfo::emitNot(CodeGen& cg, ir::Instruction& i) { architecture->emitNot(cg, i); }
void CompositeTargetInfo::emitCopy(CodeGen& cg, ir::Instruction& i) { architecture->emitCopy(cg, i); }
void CompositeTargetInfo::emitCall(CodeGen& cg, ir::Instruction& i) { architecture->emitCall(cg, i); }
void CompositeTargetInfo::emitFAdd(CodeGen& cg, ir::Instruction& i) { architecture->emitFAdd(cg, i); }
void CompositeTargetInfo::emitFSub(CodeGen& cg, ir::Instruction& i) { architecture->emitFSub(cg, i); }
void CompositeTargetInfo::emitFMul(CodeGen& cg, ir::Instruction& i) { architecture->emitFMul(cg, i); }
void CompositeTargetInfo::emitFDiv(CodeGen& cg, ir::Instruction& i) { architecture->emitFDiv(cg, i); }
void CompositeTargetInfo::emitCmp(CodeGen& cg, ir::Instruction& i) { architecture->emitCmp(cg, i); }
void CompositeTargetInfo::emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* f, const ir::Type* t) { architecture->emitCast(cg, i, f, t); }
void CompositeTargetInfo::emitVAStart(CodeGen& cg, ir::Instruction& i) { architecture->emitVAStart(cg, i); }
void CompositeTargetInfo::emitVAArg(CodeGen& cg, ir::Instruction& i) { architecture->emitVAArg(cg, i); }
void CompositeTargetInfo::emitVAEnd(CodeGen& cg, ir::Instruction& i) { architecture->emitVAEnd(cg, i); }
void CompositeTargetInfo::emitLoad(CodeGen& cg, ir::Instruction& i) { architecture->emitLoad(cg, i); }
void CompositeTargetInfo::emitStore(CodeGen& cg, ir::Instruction& i) { architecture->emitStore(cg, i); }
void CompositeTargetInfo::emitAlloc(CodeGen& cg, ir::Instruction& i) { architecture->emitAlloc(cg, i); }

void CompositeTargetInfo::emitSyscall(CodeGen& cg, ir::Instruction& i) { architecture->emitSyscall(cg, i, *os); }
void CompositeTargetInfo::emitExternCall(CodeGen& cg, ir::Instruction& i) { architecture->emitExternCall(cg, i, *os); }

bool CompositeTargetInfo::supportsCapability(const CapabilitySpec& spec) const { return os->supportsCapability(spec); }
void CompositeTargetInfo::emitIOCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitIOCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitFSCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitFSCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitMemoryCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitMemoryCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitProcessCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitProcessCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitThreadCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitThreadCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitSyncCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitSyncCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitTimeCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitTimeCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitEventCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitEventCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitNetCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitNetCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitIPCCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitIPCCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitEnvCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitEnvCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitSystemCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitSystemCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitSignalCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitSignalCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitRandomCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitRandomCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitErrorCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitErrorCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitDebugCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitDebugCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitModuleCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitModuleCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitTTYCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitTTYCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitSecurityCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitSecurityCapability(cg, i, spec, *architecture); }
void CompositeTargetInfo::emitGPUCapability(CodeGen& cg, ir::Instruction& i, const CapabilitySpec& spec) { os->emitGPUCapability(cg, i, spec, *architecture); }

uint64_t CompositeTargetInfo::getSyscallNumber(ir::SyscallId id) const { return os->getSyscallNumber(id); }
void CompositeTargetInfo::emitBr(CodeGen& cg, ir::Instruction& i) { architecture->emitBr(cg, i); }
void CompositeTargetInfo::emitJmp(CodeGen& cg, ir::Instruction& i) { architecture->emitJmp(cg, i); }

VectorCapabilities CompositeTargetInfo::getVectorCapabilities() const { return architecture->getVectorCapabilities(); }
bool CompositeTargetInfo::supportsVectorWidth(unsigned w) const { return architecture->supportsVectorWidth(w); }
bool CompositeTargetInfo::supportsVectorType(const ir::VectorType* t) const { return architecture->supportsVectorType(t); }
unsigned CompositeTargetInfo::getOptimalVectorWidth(const ir::Type* t) const { return architecture->getOptimalVectorWidth(t); }
void CompositeTargetInfo::emitVectorLoad(CodeGen& cg, ir::VectorInstruction& i) { architecture->emitVectorLoad(cg, i); }
void CompositeTargetInfo::emitVectorStore(CodeGen& cg, ir::VectorInstruction& i) { architecture->emitVectorStore(cg, i); }
void CompositeTargetInfo::emitVectorArithmetic(CodeGen& cg, ir::VectorInstruction& i) { architecture->emitVectorArithmetic(cg, i); }
void CompositeTargetInfo::emitVectorLogical(CodeGen& cg, ir::VectorInstruction& i) { architecture->emitVectorLogical(cg, i); }
void CompositeTargetInfo::emitVectorNeg(CodeGen& cg, ir::VectorInstruction& i) { architecture->emitVectorNeg(cg, i); }
void CompositeTargetInfo::emitVectorNot(CodeGen& cg, ir::VectorInstruction& i) { architecture->emitVectorNot(cg, i); }
void CompositeTargetInfo::emitVectorComparison(CodeGen& cg, ir::VectorInstruction& i) { architecture->emitVectorComparison(cg, i); }
void CompositeTargetInfo::emitVectorShuffle(CodeGen& cg, ir::VectorInstruction& i) { architecture->emitVectorShuffle(cg, i); }
void CompositeTargetInfo::emitVectorBroadcast(CodeGen& cg, ir::VectorInstruction& i) { architecture->emitVectorBroadcast(cg, i); }
void CompositeTargetInfo::emitVectorExtract(CodeGen& cg, ir::VectorInstruction& i) { architecture->emitVectorExtract(cg, i); }
void CompositeTargetInfo::emitVectorInsert(CodeGen& cg, ir::VectorInstruction& i) { architecture->emitVectorInsert(cg, i); }
void CompositeTargetInfo::emitVectorGather(CodeGen& cg, ir::VectorInstruction& i) { architecture->emitVectorGather(cg, i); }
void CompositeTargetInfo::emitVectorScatter(CodeGen& cg, ir::VectorInstruction& i) { architecture->emitVectorScatter(cg, i); }
void CompositeTargetInfo::emitVectorHorizontalOp(CodeGen& cg, ir::VectorInstruction& i) { architecture->emitVectorHorizontalOp(cg, i); }
void CompositeTargetInfo::emitVectorReduction(CodeGen& cg, ir::VectorInstruction& i) { architecture->emitVectorReduction(cg, i); }

bool CompositeTargetInfo::supportsFusedPattern(FusedPattern p) const { return architecture->supportsFusedPattern(p); }
void CompositeTargetInfo::emitFusedMultiplyAdd(CodeGen& cg, const ir::FusedInstruction& i) { architecture->emitFusedMultiplyAdd(cg, i); }
void CompositeTargetInfo::emitFusedMultiplySubtract(CodeGen& cg, const ir::FusedInstruction& i) { architecture->emitFusedMultiplySubtract(cg, i); }
void CompositeTargetInfo::emitLoadAndOperate(CodeGen& cg, ir::Instruction& l, ir::Instruction& o) { architecture->emitLoadAndOperate(cg, l, o); }
bool CompositeTargetInfo::emitCmpAndBranchFusion(CodeGen& cg, ir::Instruction& c, ir::Instruction& b) { return architecture->emitCmpAndBranchFusion(cg, c, b); }
void CompositeTargetInfo::emitComplexAddressing(CodeGen& cg, ir::Instruction& i) { architecture->emitComplexAddressing(cg, i); }

void CompositeTargetInfo::emitDebugInfo(CodeGen& cg, const ir::Function& f) { architecture->emitDebugInfo(cg, f); }
void CompositeTargetInfo::emitLineInfo(CodeGen& cg, unsigned l, const std::string& f) { architecture->emitLineInfo(cg, l, f); }
void CompositeTargetInfo::emitProfilingHook(CodeGen& cg, const std::string& h) { architecture->emitProfilingHook(cg, h); }
void CompositeTargetInfo::emitStackUnwindInfo(CodeGen& cg, const ir::Function& f) { architecture->emitStackUnwindInfo(cg, f); }

std::string CompositeTargetInfo::formatStackOperand(int o) const { return architecture->formatStackOperand(o); }
std::string CompositeTargetInfo::formatGlobalOperand(const std::string& n) const { return architecture->formatGlobalOperand(n); }
std::string CompositeTargetInfo::getImmediatePrefix() const { return architecture->getImmediatePrefix(); }
std::string CompositeTargetInfo::getLabelPrefix() const { return architecture->getLabelPrefix(); }
std::string CompositeTargetInfo::getAssemblyFileExtension() const { return architecture->getAssemblyFileExtension(); }
std::string CompositeTargetInfo::getObjectFileExtension() const { return architecture->getObjectFileExtension(); }
std::string CompositeTargetInfo::getExecutableFileExtension() const { return os->getExecutableFileExtension(); }
std::string CompositeTargetInfo::getDataRelocationType() const { return architecture->getDataRelocationType(); }
bool CompositeTargetInfo::isCallerSaved(const std::string& r) const { return architecture->isCallerSaved(r); }
bool CompositeTargetInfo::isCalleeSaved(const std::string& r) const { return architecture->isCalleeSaved(r); }
bool CompositeTargetInfo::isReserved(const std::string& r) const { return architecture->isReserved(r); }
std::string CompositeTargetInfo::getRegisterName(const std::string& b, const ir::Type* t) const { return architecture->getRegisterName(b, t); }

}
}
