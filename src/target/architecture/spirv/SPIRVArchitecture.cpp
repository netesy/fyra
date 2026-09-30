#include "target/architecture/spirv/SPIRVArchitecture.h"
#include "ir/Instruction.h"
#include "ir/Use.h"
#include "ir/Constant.h"
#include "ir/Function.h"
#include "ir/BasicBlock.h"
#include "codegen/CodeGen.h"
#include "target/core/OperatingSystemInfo.h"
#include <ostream>

namespace target {

SPIRVArchitecture::SPIRVArchitecture() {}

TypeInfo SPIRVArchitecture::getTypeInfo(const ir::Type* type) const {
    return {type->getSize() * 8, type->getAlignment() * 8, type->isFloatingPoint() ? RegisterClass::Float : RegisterClass::Integer, type->isFloatingPoint(), true};
}

const std::vector<std::string>& SPIRVArchitecture::getRegisters(RegisterClass regClass) const {
    static const std::vector<std::string> emptyRegs = {};
    return emptyRegs;
}

const std::string& SPIRVArchitecture::getReturnRegister(const ir::Type* type) const {
    static const std::string r = "%v0"; return r;
}

const std::vector<std::string>& SPIRVArchitecture::getIntegerArgumentRegisters() const {
    static const std::vector<std::string> r = {}; return r;
}

const std::vector<std::string>& SPIRVArchitecture::getFloatArgumentRegisters() const {
    static const std::vector<std::string> r = {}; return r;
}

const std::string& SPIRVArchitecture::getIntegerReturnRegister() const {
    static const std::string r = "%v0"; return r;
}

const std::string& SPIRVArchitecture::getFloatReturnRegister() const {
    static const std::string r = "%v0"; return r;
}

void SPIRVArchitecture::emitHeader(CodeGen& cg) {
    if (auto* os = cg.getTextStream()) {
        *os << "; SPIR-V\n";
        *os << "; Version: 1.5\n";
        *os << "; Generator: Fyra Compiler Backend; 1\n";
        *os << "; Bound: 100\n";
        *os << "; Schema: 0\n";
        *os << "OpCapability Shader\n";
        *os << "%1 = OpExtInstImport \"GLSL.std.450\"\n";
        *os << "OpMemoryModel Logical GLSL450\n";
        *os << "OpEntryPoint GLCompute %main \"main\"\n";
        *os << "OpExecutionMode %main LocalSize 1 1 1\n";
        *os << "%void = OpTypeVoid\n";
        *os << "%fnTy = OpTypeFunction %void\n";
        *os << "%i32 = OpTypeInt 32 1\n";
        *os << "%f32 = OpTypeFloat 32\n";
    }
}

void SPIRVArchitecture::emitFooter(CodeGen& cg) {}

void SPIRVArchitecture::emitFunctionPrologue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << "%" << func.getName() << " = OpFunction %void None %fnTy\n";
        *os << "%entry_" << func.getName() << " = OpLabel\n";
    }
}

void SPIRVArchitecture::emitFunctionEpilogue(CodeGen& cg, ir::Function& func) {
    if (auto* os = cg.getTextStream()) {
        *os << "OpReturn\n";
        *os << "OpFunctionEnd\n";
    }
}

void SPIRVArchitecture::emitStartFunction(CodeGen& cg) {}

void SPIRVArchitecture::emitRet(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        if (!i.getOperands().empty() && i.getOperands()[0] && i.getOperands()[0]->get() != nullptr) {
            *os << "OpReturnValue " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
        } else {
            *os << "OpReturn\n";
        }
    }
}

void SPIRVArchitecture::emitAdd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpIAdd %i32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << " " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitSMin(CodeGen& cg, ir::Instruction& i) {}
void SPIRVArchitecture::emitSMax(CodeGen& cg, ir::Instruction& i) {}

void SPIRVArchitecture::emitSub(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpISub %i32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << " " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitMul(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpIMul %i32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << " " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitDiv(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpSDiv %i32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << " " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitRem(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpSRem %i32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << " " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitAnd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpBitwiseAnd %i32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << " " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitOr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpBitwiseOr %i32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << " " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitXor(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpBitwiseXor %i32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << " " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitShl(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpShiftLeftLogical %i32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << " " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitShr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpShiftRightLogical %i32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << " " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitSar(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpShiftRightArithmetic %i32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << " " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitNeg(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpSNegate %i32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitNot(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpNot %i32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitCopy(CodeGen& cg, ir::Instruction& i) {}

void SPIRVArchitecture::emitCall(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpFunctionCall %void %" << i.getOperands()[0]->get()->getName() << "\n";
    }
}

void SPIRVArchitecture::emitFAdd(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpFAdd %f32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << " " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitFSub(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpFSub %f32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << " " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitFMul(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpFMul %f32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << " " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitFDiv(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        std::string res = "%v" + std::to_string(nextId++);
        *os << res << " = OpFDiv %f32 " << cg.getValueAsOperand(i.getOperands()[0]->get()) << " " << cg.getValueAsOperand(i.getOperands()[1]->get()) << "\n";
    }
}

void SPIRVArchitecture::emitCmp(CodeGen& cg, ir::Instruction& i) {}
void SPIRVArchitecture::emitCast(CodeGen& cg, ir::Instruction& i, const ir::Type* from, const ir::Type* to) {}
void SPIRVArchitecture::emitVAStart(CodeGen& cg, ir::Instruction& i) {}
void SPIRVArchitecture::emitVAArg(CodeGen& cg, ir::Instruction& i) {}
void SPIRVArchitecture::emitLoad(CodeGen& cg, ir::Instruction& i) {}
void SPIRVArchitecture::emitStore(CodeGen& cg, ir::Instruction& i) {}
void SPIRVArchitecture::emitAlloc(CodeGen& cg, ir::Instruction& i) {}

void SPIRVArchitecture::emitBr(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        if (i.getOperands().size() == 1) {
            *os << "OpBranch %" << i.getOperands()[0]->get()->getName() << "\n";
        } else if (i.getOperands().size() >= 3) {
            *os << "OpBranchConditional " << cg.getValueAsOperand(i.getOperands()[0]->get()) << " %" << i.getOperands()[1]->get()->getName() << " %" << i.getOperands()[2]->get()->getName() << "\n";
        }
    }
}

void SPIRVArchitecture::emitJmp(CodeGen& cg, ir::Instruction& i) {
    if (auto* os = cg.getTextStream()) {
        *os << "OpBranch %" << i.getOperands()[0]->get()->getName() << "\n";
    }
}

void SPIRVArchitecture::emitSyscall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {}
void SPIRVArchitecture::emitExternCall(CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) {}
void SPIRVArchitecture::emitNativeSyscall(CodeGen& cg, uint64_t syscallNum, const std::vector<ir::Value*>& args) {}
void SPIRVArchitecture::emitNativeLibraryCall(CodeGen& cg, const std::string& name, const std::vector<ir::Value*>& args) {}

std::string SPIRVArchitecture::formatStackOperand(int offset) const { return "%stack_" + std::to_string(offset); }
std::string SPIRVArchitecture::formatGlobalOperand(const std::string& name) const { return "%" + name; }

void SPIRVArchitecture::emitPassArgument(CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) {}
void SPIRVArchitecture::emitGetArgument(CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) {}

}
