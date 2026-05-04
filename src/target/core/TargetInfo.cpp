#include "target/core/TargetInfo.h"
#include "codegen/CodeGen.h"
#include "ir/Type.h"
#include "ir/Instruction.h"
#include <iostream>

namespace target {

const CapabilitySpec* TargetInfo::findCapability(std::string_view name) const {
    return CapabilityRegistry::find(name);
}

bool TargetInfo::supportsCapability(const CapabilitySpec&) const {
    return false;
}

bool TargetInfo::validateCapability(ir::Instruction& instr, const CapabilitySpec& spec) const {
    const auto argc = static_cast<int>(instr.getOperands().size());
    if (argc < spec.minArgs || argc > spec.maxArgs) return false;
    const bool hasReturn = instr.getType() && instr.getType()->getTypeID() != ir::Type::VoidTyID;
    if (spec.returnsValue != hasReturn) return false;
    return supportsCapability(spec);
}

void TargetInfo::emitUnsupportedCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec* spec) const {
    uint64_t domain = spec ? static_cast<uint64_t>(spec->domain) : static_cast<uint64_t>(CapabilityDomain::SYSTEM);
    uint64_t category_unsupported = 0x01;
    uint64_t code = spec ? static_cast<uint64_t>(spec->id) : 0;
    uint64_t error = (domain << 24) | (category_unsupported << 16) | code;

    if (auto* os = cg.getTextStream()) {
        std::string rax = getRegisterName("rax", instr.getType());
        *os << "  movq $" << error << ", " << rax << "\n";
        if (instr.getType() && instr.getType()->getTypeID() != ir::Type::VoidTyID) {
            *os << "  movq " << rax << ", " << cg.getValueAsOperand(&instr) << "\n";
        }
    }
}

void TargetInfo::emitDomainCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec& spec) {
    switch (spec.domain) {
        case CapabilityDomain::IO: emitIOCapability(cg, instr, spec); break;
        case CapabilityDomain::FS: emitFSCapability(cg, instr, spec); break;
        case CapabilityDomain::MEMORY: emitMemoryCapability(cg, instr, spec); break;
        case CapabilityDomain::PROCESS: emitProcessCapability(cg, instr, spec); break;
        case CapabilityDomain::THREAD: emitThreadCapability(cg, instr, spec); break;
        case CapabilityDomain::SYNC: emitSyncCapability(cg, instr, spec); break;
        case CapabilityDomain::TIME: emitTimeCapability(cg, instr, spec); break;
        case CapabilityDomain::EVENT: emitEventCapability(cg, instr, spec); break;
        case CapabilityDomain::NET: emitNetCapability(cg, instr, spec); break;
        case CapabilityDomain::IPC: emitIPCCapability(cg, instr, spec); break;
        case CapabilityDomain::ENV: emitEnvCapability(cg, instr, spec); break;
        case CapabilityDomain::SYSTEM: emitSystemCapability(cg, instr, spec); break;
        case CapabilityDomain::SIGNAL: emitSignalCapability(cg, instr, spec); break;
        case CapabilityDomain::RANDOM: emitRandomCapability(cg, instr, spec); break;
        case CapabilityDomain::ERROR: emitErrorCapability(cg, instr, spec); break;
        case CapabilityDomain::DEBUG: emitDebugCapability(cg, instr, spec); break;
        case CapabilityDomain::MODULE: emitModuleCapability(cg, instr, spec); break;
        case CapabilityDomain::TTY: emitTTYCapability(cg, instr, spec); break;
        case CapabilityDomain::SECURITY: emitSecurityCapability(cg, instr, spec); break;
        case CapabilityDomain::GPU: emitGPUCapability(cg, instr, spec); break;
    }
}

void TargetInfo::emitExternCall(codegen::CodeGen& cg, ir::Instruction& instr) {
    auto* ei = dynamic_cast<ir::ExternCallInstruction*>(&instr);
    if (!ei) return;
    const auto* spec = findCapability(ei->getCapability());
    if (!spec || !validateCapability(instr, *spec)) {
        emitUnsupportedCapability(cg, instr, spec);
        return;
    }
    emitDomainCapability(cg, instr, *spec);
}

void TargetInfo::emitIOCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitFSCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitMemoryCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitProcessCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitThreadCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitSyncCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitTimeCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitEventCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitNetCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitIPCCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitEnvCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitSystemCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitSignalCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitRandomCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitErrorCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitDebugCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitModuleCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitTTYCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitSecurityCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }
void TargetInfo::emitGPUCapability(codegen::CodeGen& cg, ir::Instruction& instr, const CapabilitySpec&) { emitUnsupportedCapability(cg, instr, nullptr); }

SIMDContext TargetInfo::createSIMDContext(const ir::VectorType* type) const {
    SIMDContext ctx;
    ctx.vectorWidth = type->getBitWidth();
    ctx.vectorType = const_cast<ir::VectorType*>(type);
    return ctx;
}

std::string TargetInfo::getVectorRegister(const std::string& baseReg, unsigned) const {
    return baseReg;
}

std::string TargetInfo::getVectorInstruction(const std::string& baseInstr, const SIMDContext&) const {
    return baseInstr;
}

std::string TargetInfo::formatConstant(const ir::ConstantInt* C) const {
    return getImmediatePrefix() + std::to_string(C->getValue());
}


int32_t TargetInfo::getStackOffset(const codegen::CodeGen& cg, ir::Value* val) const {
    auto it = cg.getStackOffsets().find(val);
    if (it != cg.getStackOffsets().end()) return it->second;
    return 0;
}

}
