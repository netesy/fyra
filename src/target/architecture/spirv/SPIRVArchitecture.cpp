#include "target/architecture/spirv/SPIRVArchitecture.h"
#include "codegen/CodeGen.h"
#include "ir/Instruction.h"
#include "ir/Function.h"
#include "ir/Constant.h"
#include "ir/BasicBlock.h"
#include "ir/Parameter.h"
#include "ir/GlobalVariable.h"
#include "ir/Module.h"
#include "ir/IRContext.h"
#include "ir/Use.h"
#include <cstring>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <set>

namespace target {

using namespace spirv;

SPIRVArchitecture::SPIRVArchitecture() {
    initRegisters();
}

void SPIRVArchitecture::initRegisters() {
    integerRegs = {};
    integerArgRegs = {};
    floatArgRegs = {};
    intReturnReg = "";
    floatReturnReg = "";

    module_.addCapability(Cap::Shader);
    module_.setMemoryModel(Addr::Logical, MemModel::GLSL450);
    capabilitiesUsed_.insert(Cap::Shader);

    // Pre-create the scalar types every module needs. Their ids are stable
    // because they are allocated before any user type.
    voidTypeId_ = module_.allocateId();
    module_.addGlobal(Op::TypeVoid, {voidTypeId_});

    boolTypeId_ = module_.allocateId();
    module_.addGlobal(Op::TypeBool, {boolTypeId_});

    auto makeInt = [&](unsigned width) -> uint32_t {
        uint32_t id = module_.allocateId();
        module_.addGlobal(Op::TypeInt, {id, width, 1}); // signed
        return id;
    };
    i32TypeId_ = makeInt(32);
    i64TypeId_ = makeInt(64);

    auto makeFloat = [&](unsigned width) -> uint32_t {
        uint32_t id = module_.allocateId();
        module_.addGlobal(Op::TypeFloat, {id, width});
        return id;
    };
    f32TypeId_ = makeFloat(32);
    f64TypeId_ = makeFloat(64);
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

void SPIRVArchitecture::diagnose(const std::string& msg) {
    module_.diagnostics().push_back(msg);
    std::cerr << "spirv: " << msg << "\n";
}

void SPIRVArchitecture::requireCapability(uint32_t cap) {
    if (capabilitiesUsed_.insert(cap).second) {
        module_.addCapability(cap);
    }
}

// ---------------------------------------------------------------------------
// Type system
// ---------------------------------------------------------------------------

TypeInfo SPIRVArchitecture::getTypeInfo(const ir::Type* type) const {
    if (!type || type->isVoidTy()) return {0, 0, RegisterClass::Integer, false, false};
    if (type->isFloatTy() || type->isDoubleTy()) {
        return {type->isDoubleTy() ? 8 : 4, type->isDoubleTy() ? 8 : 4,
                RegisterClass::Float, true, false};
    }
    if (type->isPointerTy()) return {8, 8, RegisterClass::Integer, false, false};
    return {4, 4, RegisterClass::Integer, false, true};
}

const std::vector<std::string>& SPIRVArchitecture::getRegisters(RegisterClass) const {
    return integerRegs;
}

const std::string& SPIRVArchitecture::getReturnRegister(const ir::Type*) const {
    return intReturnReg;
}

bool SPIRVArchitecture::isFloatTy(const ir::Type* t) const {
    return t && (t->isFloatTy() || t->isDoubleTy());
}

bool SPIRVArchitecture::isIntTy(const ir::Type* t) const {
    return t && t->isIntegerTy();
}

unsigned SPIRVArchitecture::bitWidthOf(const ir::Type* t) const {
    if (auto* it = dynamic_cast<const ir::IntegerType*>(t)) return it->getBitwidth();
    if (t && t->isFloatTy()) return 32;
    if (t && t->isDoubleTy()) return 64;
    if (t && t->isPointerTy()) return 64;
    return 64;
}

static uint32_t storageClassForAddressSpace(unsigned addrSpace) {
    switch (addrSpace) {
        case 1: return Storage::Workgroup;
        case 2: return Storage::StorageBuffer;
        case 3: return Storage::Uniform;
        case 4: return Storage::Input;
        case 5: return Storage::Output;
        case 7: return Storage::Function;
        default: return Storage::Private;
    }
}

uint32_t SPIRVArchitecture::getPointerTypeId(const ir::Type* pointee, uint32_t storageClass) {
    uint32_t elemId = getTypeId(pointee);
    // Cache pointer types per (storage, element) pair.
    static thread_local std::map<std::pair<uint32_t, uint32_t>, uint32_t> ptrCache;
    auto key = std::make_pair(storageClass, elemId);
    auto it = ptrCache.find(key);
    if (it != ptrCache.end()) return it->second;

    uint32_t id = module_.allocateId();
    module_.addGlobal(Op::TypePointer, {id, storageClass, elemId});
    ptrCache[key] = id;
    return id;
}

uint32_t SPIRVArchitecture::getTypeId(const ir::Type* type) {
    if (!type) {
        diagnose("null type in SPIR-V lowering");
        return voidTypeId_;
    }
    auto it = typeIds_.find(type);
    if (it != typeIds_.end()) return it->second;

    if (type->isVoidTy() || type->getTypeID() == ir::Type::LabelTyID) {
        typeIds_[type] = voidTypeId_;
        return voidTypeId_;
    }
    if (auto* it2 = dynamic_cast<const ir::IntegerType*>(type)) {
        unsigned w = it2->getBitwidth();
        if (w > 32) requireCapability(Cap::Int64);
        else if (w == 16) requireCapability(Cap::Int16);
        else if (w == 8) requireCapability(Cap::Int8);
        else if (w == 1) requireCapability(Cap::Int8); // i1 not supported; treat as i8
        uint32_t id = module_.allocateId();
        module_.addGlobal(Op::TypeInt, {id, w == 1 ? 8 : w, 1});
        typeIds_[type] = id;
        return id;
    }
    if (type->isFloatTy()) { typeIds_[type] = f32TypeId_; return f32TypeId_; }
    if (type->isDoubleTy()) {
        requireCapability(Cap::Float64);
        typeIds_[type] = f64TypeId_;
        return f64TypeId_;
    }
    if (auto* pt = dynamic_cast<const ir::PointerType*>(type)) {
        uint32_t storage = storageClassForAddressSpace(pt->getAddressSpace());
        uint32_t id = getPointerTypeId(pt->getElementType(), storage);
        typeIds_[type] = id;
        return id;
    }
    if (auto* vt = dynamic_cast<const ir::VectorType*>(type)) {
        uint32_t elem = getTypeId(vt->getElementType());
        if (vt->getElementType() && vt->getElementType()->isDoubleTy())
            requireCapability(Cap::Float64);
        uint32_t id = module_.allocateId();
        module_.addGlobal(Op::TypeVector, {id, elem, vt->getNumElements()});
        typeIds_[type] = id;
        return id;
    }
    if (auto* at = dynamic_cast<const ir::ArrayType*>(type)) {
        // Array length must be an <id> of a constant, emitted first.
        uint64_t len = at->getNumElements();
        uint32_t lenTy = i32TypeId_;
        if (len > 0xFFFFFFFFull) lenTy = i64TypeId_;
        uint32_t lenId = module_.allocateId();
        if (lenTy == i32TypeId_) {
            module_.addGlobal(Op::Constant, {lenTy, lenId, static_cast<uint32_t>(len)});
        } else {
            module_.addGlobal(Op::Constant, {lenTy, lenId,
                static_cast<uint32_t>(len & 0xFFFFFFFFull),
                static_cast<uint32_t>(len >> 32)});
        }
        uint32_t elem = getTypeId(at->getElementType());
        uint32_t id = module_.allocateId();
        module_.addGlobal(Op::TypeArray, {id, elem, lenId});
        typeIds_[type] = id;
        return id;
    }
    if (auto* rt = dynamic_cast<const ir::Type*>(type);
        type->isAggregate() && dynamic_cast<const ir::StructType*>(type)) {
        auto* st = static_cast<const ir::StructType*>(type);
        std::vector<uint32_t> members;
        for (auto* m : st->getElements()) members.push_back(getTypeId(m));
        uint32_t id = module_.allocateId();
        std::vector<uint32_t> ops = {id};
        ops.insert(ops.end(), members.begin(), members.end());
        module_.addGlobal(Op::TypeStruct, ops);
        typeIds_[type] = id;
        return id;
    }
    if (auto* ft = dynamic_cast<const ir::FunctionType*>(type)) {
        uint32_t ret = getTypeId(ft->getReturnType());
        std::vector<uint32_t> ops;
        for (auto* p : ft->getParamTypes()) ops.push_back(getTypeId(p));
        uint32_t id = module_.allocateId();
        std::vector<uint32_t> full = {id, ret};
        full.insert(full.end(), ops.begin(), ops.end());
        module_.addGlobal(Op::TypeFunction, full);
        typeIds_[type] = id;
        return id;
    }
    if (type->getTypeID() == ir::Type::UnionTyID) {
        diagnose("union types are not supported in SPIR-V lowering");
        typeIds_[type] = voidTypeId_;
        return voidTypeId_;
    }
    diagnose("unsupported type in SPIR-V lowering: " + type->toString());
    typeIds_[type] = voidTypeId_;
    return voidTypeId_;
}

// ---------------------------------------------------------------------------
// Constants and values
// ---------------------------------------------------------------------------

uint32_t SPIRVArchitecture::getConstantIntId(const ir::IntegerType* ty,
                                             uint64_t value, bool isSigned) {
    unsigned w = ty ? ty->getBitwidth() : 64;
    if (w == 1) w = 8;
    uint32_t typeId = getTypeId(ty);
    uint32_t id = module_.allocateId();
    std::vector<uint32_t> words;
    if (w <= 32) {
        words.push_back(static_cast<uint32_t>(value));
    } else {
        words.push_back(static_cast<uint32_t>(value & 0xFFFFFFFFull));
        words.push_back(static_cast<uint32_t>(value >> 32));
    }
    (void)isSigned;
    std::vector<uint32_t> ops = {typeId, id};
    ops.insert(ops.end(), words.begin(), words.end());
    module_.addGlobal(Op::Constant, ops);
    return id;
}

uint32_t SPIRVArchitecture::getConstantNullId(uint32_t typeId) {
    uint32_t id = module_.allocateId();
    module_.addGlobal(Op::ConstantNull, {typeId, id});
    return id;
}

uint32_t SPIRVArchitecture::getOrCreateValueId(codegen::CodeGen& cg, ir::Value* val) {
    (void)cg;
    if (!val) return 0;
    auto it = valueIds_.find(val);
    if (it != valueIds_.end()) return it->second;

    if (auto* ci = dynamic_cast<ir::ConstantInt*>(val)) {
        uint32_t id = getConstantIntId(ci->getType() ? dynamic_cast<ir::IntegerType*>(ci->getType()) : nullptr,
                                     ci->getValue(), true);
        valueIds_[val] = id;
        return id;
    }
    if (auto* cfp = dynamic_cast<ir::ConstantFP*>(val)) {
        uint32_t typeId = getTypeId(cfp->getType());
        uint32_t id = module_.allocateId();
        std::vector<uint32_t> ops = {typeId, id};
        if (cfp->getType() && cfp->getType()->isDoubleTy()) {
            double d = cfp->getValue();
            uint64_t bits;
            std::memcpy(&bits, &d, 8);
            ops.push_back(static_cast<uint32_t>(bits & 0xFFFFFFFFull));
            ops.push_back(static_cast<uint32_t>(bits >> 32));
        } else {
            float f = static_cast<float>(cfp->getValue());
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            ops.push_back(bits);
        }
        module_.addGlobal(Op::Constant, ops);
        valueIds_[val] = id;
        return id;
    }
    if (auto* gv = dynamic_cast<ir::GlobalVariable*>(val)) {
        // Globals are emitted in emitGlobals; allocate id lazily if missed.
        uint32_t id = module_.allocateId();
        valueIds_[val] = id;
        return id;
    }
    if (auto* fn = dynamic_cast<ir::Function*>(val)) {
        auto fit = functions_.find(fn);
        if (fit != functions_.end() && fit->second.fnId) {
            valueIds_[val] = fit->second.fnId;
            return fit->second.fnId;
        }
    }
    if (auto* bb = dynamic_cast<ir::BasicBlock*>(val)) {
        // Resolved via the owning function's label table.
        if (bb->getParent()) {
            auto fit = functions_.find(bb->getParent());
            if (fit != functions_.end()) {
                auto lit = fit->second.labelIds.find(bb);
                if (lit != fit->second.labelIds.end()) {
                    valueIds_[val] = lit->second;
                    return lit->second;
                }
            }
        }
    }
    if (auto* ca = dynamic_cast<ir::ConstantArray*>(val)) {
        uint32_t typeId = getTypeId(val->getType());
        uint32_t id = module_.allocateId();
        std::vector<uint32_t> ops = {typeId, id};
        for (auto* e : ca->getValues()) ops.push_back(getOrCreateValueId(cg, e));
        module_.addGlobal(Op::ConstantComposite, ops);
        valueIds_[val] = id;
        return id;
    }
    if (auto* cv = dynamic_cast<ir::ConstantVector*>(val)) {
        uint32_t typeId = getTypeId(val->getType());
        uint32_t id = module_.allocateId();
        std::vector<uint32_t> ops = {typeId, id};
        for (auto* e : cv->getElements()) ops.push_back(getOrCreateValueId(cg, e));
        module_.addGlobal(Op::ConstantComposite, ops);
        valueIds_[val] = id;
        return id;
    }
    if (dynamic_cast<ir::ConstantString*>(val)) {
        diagnose("string constants are not supported in SPIR-V lowering");
        uint32_t id = module_.allocateId();
        module_.addInstruction(Op::Undef, {getTypeId(val->getType()), id});
        valueIds_[val] = id;
        return id;
    }

    // Plain SSA value (instruction result or parameter): allocate its id.
    // The defining instruction fills in operands when emitted.
    uint32_t id = module_.allocateId();
    valueIds_[val] = id;
    return id;
}

uint32_t SPIRVArchitecture::valueIdOf(ir::Value* v) {
    if (!v) return 0;
    auto it = valueIds_.find(v);
    if (it != valueIds_.end()) return it->second;
    return getOrCreateValueId(*activeCg_, v);
}

uint32_t SPIRVArchitecture::labelIdOf(const ir::BasicBlock* bb) {
    if (!bb || !bb->getParent()) return 0;
    auto fit = functions_.find(bb->getParent());
    if (fit == functions_.end()) return 0;
    auto lit = fit->second.labelIds.find(bb);
    return lit != fit->second.labelIds.end() ? lit->second : 0;
}

// ---------------------------------------------------------------------------
// Emission helpers
// ---------------------------------------------------------------------------

uint32_t SPIRVArchitecture::emitResult(const ir::Instruction& i, uint16_t opcode,
                                       const std::vector<uint32_t>& tail) {
    // Result type first: the instruction's own type.
    uint32_t resType = getTypeId(i.getType());
    uint32_t resId = getOrCreateValueId(*activeCg_, const_cast<ir::Instruction*>(&i));
    std::vector<uint32_t> ops = {resType, resId};
    ops.insert(ops.end(), tail.begin(), tail.end());
    module_.addInstruction(opcode, ops);
    return resId;
}

uint32_t SPIRVArchitecture::emitUnary(uint16_t opcode, uint32_t typeId, ir::Value* operand) {
    uint32_t resId = module_.allocateId();
    module_.addInstruction(opcode, {typeId, resId, valueIdOf(operand)});
    return resId;
}

uint32_t SPIRVArchitecture::emitBinary(uint16_t opcode, uint32_t typeId, ir::Value* a, ir::Value* b) {
    uint32_t resId = module_.allocateId();
    module_.addInstruction(opcode, {typeId, resId, valueIdOf(a), valueIdOf(b)});
    return resId;
}

// ---------------------------------------------------------------------------
// Module preparation (pre-pass)
// ---------------------------------------------------------------------------

void SPIRVArchitecture::setEntryPoint(const std::string& functionName, uint32_t execModel,
                                      uint32_t x, uint32_t y, uint32_t z) {
    EntryConfig cfg{execModel, {x, y, z}, execModel == ExecModel::GLCompute};
    entryConfigs_[functionName] = cfg;
}

void SPIRVArchitecture::setEntryPoint(ir::Function* func, uint32_t execModel,
                                      uint32_t x, uint32_t y, uint32_t z) {
    if (func) setEntryPoint(func->getName(), execModel, x, y, z);
}

void SPIRVArchitecture::setLocalSize(ir::Function* func, uint32_t x, uint32_t y, uint32_t z) {
    if (!func) return;
    EntryConfig cfg{ExecModel::GLCompute, {x, y, z}, true};
    entryConfigs_[func->getName()] = cfg;
}

void SPIRVArchitecture::addInterfaceVariable(
        const std::string& name, const ir::Type* valueType, uint32_t storageClass,
        const std::vector<std::pair<uint32_t, std::vector<uint32_t>>>& decorations) {
    InterfaceVar spec{valueType, storageClass, {}};
    for (auto& [deco, ops] : decorations) spec.decorations.push_back({deco, ops});
    interfaceVars_.push_back({name, spec});
}

void SPIRVArchitecture::prepareFunction(codegen::CodeGen& cg, ir::Function& func) {
    SPIRVFunctionState st;
    st.func = &func;
    st.fnId = module_.allocateId();
    st.returnTypeId = getTypeId(func.getReturnType());
    st.fnTypeId = getFunctionTypeId(func);

    // Label ids for every basic block (needed by branches/phis before the
    // block is reached during emission).
    for (auto& bb : func.getBasicBlocks()) {
        st.labelIds[bb.get()] = module_.allocateId();
    }

    // Pre-scan for function-local storage so OpVariable Function instructions
    // can be emitted in the entry block (spec requirement) before use.
    for (auto& bb : func.getBasicBlocks()) {
        for (auto& instPtr : bb->getInstructions()) {
            ir::Instruction* inst = instPtr.get();
            auto op = inst->getOpcode();
            if (op == ir::Instruction::Alloc || op == ir::Instruction::Alloc4 ||
                op == ir::Instruction::Alloc16) {
                const ir::Type* varTy = inst->getType();
                const ir::Type* elemTy = varTy;
                if (auto* pt = dynamic_cast<const ir::PointerType*>(varTy))
                    elemTy = pt->getElementType();
                uint32_t ptrTy = getPointerTypeId(elemTy, Storage::Function);
                uint32_t varId = module_.allocateId();
                valueIds_[inst] = varId;
                st.allocas[inst] = varId;
                st.pendingVariables.push_back({Op::Variable,
                    {ptrTy, varId, Storage::Function}});
            } else if (op == ir::Instruction::Load || op == ir::Instruction::Store ||
                       op == ir::Instruction::Stored || op == ir::Instruction::Stores ||
                       op == ir::Instruction::Storel || op == ir::Instruction::Storeh ||
                       op == ir::Instruction::Storeb || op == ir::Instruction::Loadd ||
                       op == ir::Instruction::Loads || op == ir::Instruction::Loadl ||
                       op == ir::Instruction::Loaduw || op == ir::Instruction::Loadsh ||
                       op == ir::Instruction::Loaduh || op == ir::Instruction::Loadsb ||
                       op == ir::Instruction::Loadub) {
                if (inst->getOperands().empty()) continue;
                // Stack-slot pseudo-pointers are ConstantInt operands.
                ir::Value* ptrVal = (op == ir::Instruction::Store || op == ir::Instruction::Stored ||
                                     op == ir::Instruction::Stores || op == ir::Instruction::Storel ||
                                     op == ir::Instruction::Storeh || op == ir::Instruction::Storeb)
                                    ? inst->getOperands()[1]->get()
                                    : inst->getOperands()[0]->get();
                if (auto* ci = dynamic_cast<ir::ConstantInt*>(ptrVal)) {
                    int64_t slot = static_cast<int64_t>(ci->getValue());
                    if (!st.stackSlots.count(slot)) {
                        // i64 slot storage covers all tagged stack words.
                        auto* i64t = cg.module.getContext()->getIntegerType(64);
                        uint32_t ptrTy = getPointerTypeId(i64t, Storage::Function);
                        uint32_t varId = module_.allocateId();
                        st.stackSlots[slot] = varId;
                        st.pendingVariables.push_back({Op::Variable,
                            {ptrTy, varId, Storage::Function}});
                    }
                }
            }
        }
    }

    // Entry-point decision inputs are stored now; the actual OpEntryPoint /
    // OpExecutionMode instructions are emitted by emitEntryPoints() after the
    // call graph and interface variables are known.
    auto cfgIt = entryConfigs_.find(func.getName());
    bool forcedEntry = cfgIt != entryConfigs_.end();
    if (forcedEntry || func.isExported()) {
        st.isEntryPoint = true;
        uint32_t model = ExecModel::GLCompute;
        uint32_t sz[3] = {1, 1, 1};
        bool hasSize = false;
        if (forcedEntry) {
            model = cfgIt->second.execModel;
            sz[0] = cfgIt->second.size[0];
            sz[1] = cfgIt->second.size[1];
            sz[2] = cfgIt->second.size[2];
            hasSize = cfgIt->second.hasSize;
        }
        st.execModel = model;
        st.localSize[0] = sz[0]; st.localSize[1] = sz[1]; st.localSize[2] = sz[2];
        st.hasLocalSize = hasSize;
        if ((model == ExecModel::TaskEXT || model == ExecModel::MeshEXT ||
             model == ExecModel::TaskNV || model == ExecModel::MeshNV)) {
            // Mesh/task stages require the mesh-shading extension environment;
            // emit a clear diagnostic rather than invalid output.
            diagnose("mesh/task execution models require SPV_EXT_mesh_shader / "
                     "SPV_NV_mesh_shader capable environment: " + func.getName());
        }
    }

    functions_[&func] = st;
}

void SPIRVArchitecture::emitGlobals(codegen::CodeGen& cg) {
    // Interface variables declared through the public API.
    emitInterfaceVariables();

    for (auto& gvp : cg.module.getGlobalVariables()) {
        auto* gv = gvp.get();
        if (!gv) continue;
        // Stage metadata consumed by the entry-point pass; not shader data.
        if (gv->getName() == "__lymar_gpu_stages") continue;
        const ir::Type* gty = gv->getType();
        const ir::Type* elemTy = gty;
        uint32_t storage = Storage::Private;
        if (auto* pt = dynamic_cast<const ir::PointerType*>(gty)) {
            elemTy = pt->getElementType();
            storage = storageClassForAddressSpace(pt->getAddressSpace());
        }
        uint32_t ptrTy = getPointerTypeId(elemTy, storage);
        uint32_t varId = module_.allocateId();
        valueIds_[gv] = varId;

        // Optional initializer.
        uint32_t initId = 0;
        if (auto* init = gv->getInitializer()) {
            if (auto* ci = dynamic_cast<ir::ConstantInt*>(init)) {
                initId = getConstantIntId(dynamic_cast<ir::IntegerType*>(init->getType()),
                                          ci->getValue(), true);
            } else if (auto* cfp = dynamic_cast<ir::ConstantFP*>(init)) {
                initId = getOrCreateValueId(cg, init);
            } else if (dynamic_cast<ir::ConstantArray*>(init) ||
                       dynamic_cast<ir::ConstantVector*>(init)) {
                initId = getOrCreateValueId(cg, init);
            } else if (dynamic_cast<ir::ConstantString*>(init)) {
                diagnose("global string initializer unsupported in SPIR-V: " + gv->getName());
            } else {
                initId = getConstantNullId(getTypeId(elemTy));
            }
        }
        std::vector<uint32_t> ops = {ptrTy, varId, storage};
        if (initId) ops.push_back(initId);
        module_.addGlobal(Op::Variable, ops);

        // OpName for debuggability (harmless if unused). OpName is a debug
        // instruction; it belongs before the functions section.
        {
            std::vector<uint32_t> ops = {varId};
            std::string n = gv->getName();
            size_t len = n.size() + 1;
            size_t wc = (len + 3) / 4;
            std::vector<uint32_t> words(wc, 0);
            std::memcpy(words.data(), n.data(), n.size());
            ops.insert(ops.end(), words.begin(), words.end());
            module_.addDebug(Op::Name, ops);
        }
    }
}

void SPIRVArchitecture::emitInterfaceVariables() {
    for (auto& [name, spec] : interfaceVars_) {
        uint32_t ptrTy = getPointerTypeId(spec.type, spec.storage);
        uint32_t varId = module_.allocateId();
        module_.addGlobal(Op::Variable, {ptrTy, varId, spec.storage});
        // Only Input/Output variables participate in entry-point interfaces.
        if (spec.storage == Storage::Input || spec.storage == Storage::Output)
            interfaceIds_.push_back(varId);
        for (auto& d : spec.decorations) {
            module_.addDecoration(varId, d.deco, d.operands);
        }
    }

    // GPU builtin variables discovered in the pre-pass. These are Input
    // storage and must appear on every entry point's interface list.
    for (uint32_t b : neededBuiltins_) {
        uint32_t varId = getBuiltinVariableId(b);
        interfaceIds_.push_back(varId);
    }
}

uint32_t SPIRVArchitecture::getBuiltinVec3Type() {
    // The gpu.* id builtins are 3-component integer vectors. The IR has no
    // unsigned integer types, so the canonical type is ivec3; it is interned
    // through getTypeId so the variable, loads, and rewritten instruction
    // types all resolve to the same SPIR-V type id.
    if (!gpuVec3Ty_) {
        auto& ctx = ir::IRContext::getContext();
        gpuVec3Ty_ = ctx.getVectorType(ctx.getIntegerType(32), 3);
    }
    return getTypeId(gpuVec3Ty_);
}

uint32_t SPIRVArchitecture::getBuiltinVariableId(uint32_t builtIn) {
    auto it = builtinVarIds_.find(builtIn);
    if (it != builtinVarIds_.end()) return it->second;
    getBuiltinVec3Type(); // interns gpuVec3Ty_ and its SPIR-V type id
    // Pointer type interned through the same path as every other pointer.
    uint32_t ptrTypeId = getPointerTypeId(gpuVec3Ty_, Storage::Input);
    uint32_t varId = module_.allocateId();
    module_.addGlobal(Op::Variable, {ptrTypeId, varId, Storage::Input});
    module_.addDecoration(varId, Deco::BuiltIn, {builtIn});
    builtinVarIds_[builtIn] = varId;
    return varId;
}

void SPIRVArchitecture::emitEntryPoints() {
    if (meshExtRequested_)
        module_.addExtension("SPV_EXT_mesh_shader");
    for (auto& [fn, st] : functions_) {
        if (!st.isEntryPoint || !st.func) continue;
        module_.addEntryPoint(st.execModel, st.fnId, st.func->getName(), interfaceIds_);
        if (st.hasLocalSize && st.execModel == ExecModel::GLCompute) {
            module_.addExecutionMode(st.fnId, ExecMode::LocalSize,
                                     {st.localSize[0], st.localSize[1], st.localSize[2]});
        }
        if (st.execModel == ExecModel::MeshEXT ||
            st.execModel == ExecModel::MeshNV) {
            auto it = meshLimits_.find(st.func->getName());
            if (it != meshLimits_.end()) {
                module_.addExecutionMode(st.fnId, ExecMode::OutputVertices,
                                         {it->second.first});
                module_.addExecutionMode(st.fnId, ExecMode::OutputPrimitivesEXT,
                                         {it->second.second});
            }
        }
    }
}

uint32_t SPIRVArchitecture::getFunctionTypeId(ir::Function& func) {
    // OpTypeFunction <id> <return-type> <param-types...>
    std::vector<uint32_t> paramIds;
    for (auto& p : func.getParameters()) paramIds.push_back(getTypeId(p->getType()));
    uint32_t retId = getTypeId(func.getReturnType());
    uint32_t id = module_.allocateId();
    std::vector<uint32_t> ops = {id, retId};
    ops.insert(ops.end(), paramIds.begin(), paramIds.end());
    module_.addGlobal(Op::TypeFunction, ops);
    return id;
}

void SPIRVArchitecture::prepareModule(codegen::CodeGen& cg) {
    if (prepared_) return;
    prepared_ = true;
    activeCg_ = &cg;

    // Pass 1: collect the call graph so entry-point decisions are correct.
    std::map<std::string, ir::Function*> byName;
    for (auto& func : cg.module.getFunctions()) byName[func->getName()] = func.get();
    for (auto& func : cg.module.getFunctions()) {
        for (auto& bb : func->getBasicBlocks()) {
            for (auto& instPtr : bb->getInstructions()) {
                ir::Instruction* inst = instPtr.get();
                if (inst->getOpcode() == ir::Instruction::Call && !inst->getOperands().empty()) {
                    if (auto* callee = dynamic_cast<ir::Function*>(inst->getOperands()[0]->get()))
                        calledFunctions_.insert(callee);
                } else if (inst->getOpcode() == ir::Instruction::ExternCall) {
                    if (auto* ec = dynamic_cast<ir::ExternCallInstruction*>(inst)) {
                        const std::string& cap = ec->getCapability();
                        // GPU builtins lower to SpvBuiltIn interface variables;
                        // record which ones this module needs before entry
                        // points are emitted (interface lists are static).
                        // Capability names may be underscore or dot-mangled.
                        std::string norm = cap;
                        for (auto& ch : norm) if (ch == '_') ch = '.';
                        if (const CapabilitySpec* gspec = CapabilityRegistry::find(cap)) {
                            if (gspec->domain == CapabilityDomain::GPU) {
                                // Stage markers and execution parameters are
                                // per-function metadata collected for the
                                // entry-point pass.
                                switch (gspec->id) {
                                    case CapabilityId::GPU_TASK:
                                        stageMarkers_[func->getName()] = ExecModel::GLCompute; break;
                                    case CapabilityId::GPU_VERTEX:
                                        stageMarkers_[func->getName()] = ExecModel::Vertex; break;
                                    case CapabilityId::GPU_PIXEL:
                                        stageMarkers_[func->getName()] = ExecModel::Fragment; break;
                                    case CapabilityId::GPU_MESH:
                                        stageMarkers_[func->getName()] = ExecModel::MeshEXT;
                                        meshExtRequested_ = true; break;
                                    case CapabilityId::GPU_LOCAL_SIZE:
                                    case CapabilityId::GPU_MESH_LIMITS: {
                                        auto readConst = [&](size_t k, uint32_t& out) {
                                            if (k >= ec->getOperands().size()) return false;
                                            auto* ci = dynamic_cast<ir::ConstantInt*>(ec->getOperands()[k]->get());
                                            if (!ci) return false;
                                            out = static_cast<uint32_t>(ci->getValue());
                                            return true;
                                        };
                                        if (gspec->id == CapabilityId::GPU_LOCAL_SIZE) {
                                            std::array<uint32_t, 3> sz{1, 1, 1};
                                            for (size_t k = 0; k < 3; ++k) readConst(k, sz[k]);
                                            stageLocalSize_[func->getName()] = sz;
                                        } else {
                                            uint32_t v = 0, p = 0;
                                            readConst(0, v); readConst(1, p);
                                            meshLimits_[func->getName()] = {v, p};
                                        }
                                        break;
                                    }
                                    default: break;
                                }
                                if (gspec->id == CapabilityId::GPU_THREAD_ID)
                                    neededBuiltins_.insert(BuiltIn::GlobalInvocationId);
                                else if (gspec->id == CapabilityId::GPU_LOCAL_ID)
                                    neededBuiltins_.insert(BuiltIn::LocalInvocationId);
                                else if (gspec->id == CapabilityId::GPU_GROUP_ID)
                                    neededBuiltins_.insert(BuiltIn::WorkgroupId);
                                else if (gspec->id == CapabilityId::GPU_NUM_GROUPS)
                                    neededBuiltins_.insert(BuiltIn::NumWorkgroups);
                            }
                        } else if (norm == "lymar.aot.gpu.thread.id")
                            neededBuiltins_.insert(BuiltIn::GlobalInvocationId);
                        else if (norm == "lymar.aot.gpu.local.id")
                            neededBuiltins_.insert(BuiltIn::LocalInvocationId);
                        else if (norm == "lymar.aot.gpu.group.id")
                            neededBuiltins_.insert(BuiltIn::WorkgroupId);
                        else if (norm == "lymar.aot.gpu.num.groups")
                            neededBuiltins_.insert(BuiltIn::NumWorkgroups);
                        auto it = byName.find(cap);
                        if (it != byName.end()) calledFunctions_.insert(it->second);
                    }
                }
            }
        }
    }

    // Pass 1.5: recover GPU stage metadata from the __lymar_gpu_stages
    // manifest global ("name|stage|tx,ty,tz|max_vertices,max_primitives" per
    // line). This is the vendor-facing contract produced by the Lymar-side
    // lowering; it must be parsed before per-function entry decisions.
    for (auto& gvp : cg.module.getGlobalVariables()) {
        auto* gv = gvp.get();
        if (!gv || gv->getName() != "__lymar_gpu_stages") continue;
        auto* init = gv->getInitializer();
        auto* cs = init ? dynamic_cast<ir::ConstantString*>(init) : nullptr;
        if (!cs) continue;
        std::istringstream lines(cs->getValue());
        std::string line;
        while (std::getline(lines, line)) {
            if (line.empty()) continue;
            std::vector<std::string> parts;
            size_t pos = 0;
            while (true) {
                size_t bar = line.find('|', pos);
                if (bar == std::string::npos) { parts.push_back(line.substr(pos)); break; }
                parts.push_back(line.substr(pos, bar - pos));
                pos = bar + 1;
            }
            if (parts.size() < 3) continue;
            EntryConfig cfg{};
            cfg.hasSize = false;
            if (parts[1] == "task") {
                cfg.execModel = ExecModel::GLCompute;
            } else if (parts[1] == "vertex") {
                cfg.execModel = ExecModel::Vertex;
            } else if (parts[1] == "pixel") {
                cfg.execModel = ExecModel::Fragment;
            } else if (parts[1] == "mesh") {
                cfg.execModel = ExecModel::MeshEXT;
                meshExtRequested_ = true;
            } else {
                continue;
            }
            // threads: "tx,ty,tz"
            {
                std::istringstream ts(parts[2]);
                std::string c;
                int idx = 0;
                while (idx < 3 && std::getline(ts, c, ',')) {
                    try { cfg.size[idx] = static_cast<uint32_t>(std::stoul(c)); } catch (...) {}
                    ++idx;
                }
                cfg.hasSize = (idx == 3);
            }
            // mesh limits: "max_vertices,max_primitives" -> execution modes.
            if (parts.size() >= 4 && cfg.execModel == ExecModel::MeshEXT) {
                std::istringstream ms(parts[3]);
                std::string c;
                uint32_t lim[2] = {0, 0};
                int idx = 0;
                while (idx < 2 && std::getline(ms, c, ',')) {
                    try { lim[idx] = static_cast<uint32_t>(std::stoul(c)); } catch (...) {}
                    ++idx;
                }
                if (idx == 2) {
                    meshLimits_[parts[0]] = {lim[0], lim[1]};
                }
            }
            entryConfigs_[parts[0]] = cfg;
        }
    }

    // Pass 1.7: gpu.* stage-marker capabilities override manifest entries —
    // in-IR declarations are the authoritative contract.
    for (auto& [fname, model] : stageMarkers_) {
        EntryConfig cfg{};
        cfg.execModel = model;
        cfg.hasSize = false;
        auto lsIt = stageLocalSize_.find(fname);
        if (lsIt != stageLocalSize_.end()) {
            cfg.size[0] = lsIt->second[0];
            cfg.size[1] = lsIt->second[1];
            cfg.size[2] = lsIt->second[2];
            cfg.hasSize = true;
        }
        entryConfigs_[fname] = cfg;
    }

    // Pass 2: per-function states (ids, types, labels, allocas).
    for (auto& func : cg.module.getFunctions()) {
        prepareFunction(cg, *func);
    }
}

// ---------------------------------------------------------------------------
// Function emission
// ---------------------------------------------------------------------------

void SPIRVArchitecture::emitHeader(codegen::CodeGen& cg) {
    // Order matters: call graph -> function states -> globals/interface
    // variables -> entry points (which may reference interface ids).
    prepareModule(cg);
    emitGlobals(cg);
    emitEntryPoints();
}

void SPIRVArchitecture::emitFooter(codegen::CodeGen& cg) {
    auto bytes = SPIRVBinaryWriter::write(module_);
    cg.getAssembler().emitBytes(bytes);
}

void SPIRVArchitecture::emitFunctionPrologue(codegen::CodeGen& cg, ir::Function& func) {
    prepareModule(cg);
    auto it = functions_.find(&func);
    if (it == functions_.end()) {
        diagnose("function missing from SPIR-V pre-pass: " + func.getName());
        return;
    }
    SPIRVFunctionState& st = it->second;
    current_ = &st;

    module_.addInstruction(Op::Function,
        {st.returnTypeId, st.fnId, 0, st.fnTypeId});

    // Parameters.
    for (auto& p : func.getParameters()) {
        uint32_t pType = getTypeId(p->getType());
        uint32_t pId = module_.allocateId();
        module_.addInstruction(Op::FunctionParameter, {pType, pId});
        valueIds_[p.get()] = pId;
    }

    // CFG structuring analysis (needed by emitBr).
    analyzeCFG(st);
}

void SPIRVArchitecture::emitBasicBlockStart(codegen::CodeGen& cg, ir::BasicBlock& bb) {
    if (!current_) return;
    uint32_t labelId = labelIdOf(&bb);
    if (!labelId) return;
    module_.addInstruction(Op::Label, {labelId});

    // Merge annotations must be the first instruction(s) of a header block,
    // before any other instructions (including function-scope variables).
    emitMergeAnnotation(*current_, &bb);

    // First block of the function: flush pre-scanned Function-storage vars.
    if (!current_->func->getBasicBlocks().empty() &&
        current_->func->getBasicBlocks().front().get() == &bb) {
        for (auto& [op, ops] : current_->pendingVariables) {
            module_.addInstruction(op, ops);
        }
        current_->pendingVariables.clear();
    }
}

void SPIRVArchitecture::emitFunctionEpilogue(codegen::CodeGen& cg, ir::Function& func) {
    (void)cg;
    if (current_ && current_->sinkLabelId) {
        // Emit the synthetic merge sink if structuring needed it.
        module_.addInstruction(Op::Label, {current_->sinkLabelId});
        module_.addInstruction(Op::Unreachable, {});
    }
    module_.addInstruction(Op::FunctionEnd, {});
    current_ = nullptr;
}

// ---------------------------------------------------------------------------
// Control flow
// ---------------------------------------------------------------------------

std::vector<const ir::BasicBlock*> SPIRVArchitecture::successorsOf(const ir::BasicBlock* bb) {
    std::vector<const ir::BasicBlock*> out;
    if (!bb) return out;
    for (auto& instPtr : bb->getInstructions()) {
        ir::Instruction* inst = instPtr.get();
        auto op = inst->getOpcode();
        if (op == ir::Instruction::Jmp || op == ir::Instruction::Br ||
            op == ir::Instruction::Jnz || op == ir::Instruction::Jz) {
            for (auto& use : inst->getOperands()) {
                if (auto* target = dynamic_cast<ir::BasicBlock*>(use->get()))
                    out.push_back(target);
            }
        }
    }
    return out;
}

std::map<const ir::BasicBlock*, std::set<const ir::BasicBlock*>>
SPIRVArchitecture::computePostDominators(SPIRVFunctionState& st) {
    // Nodes: all blocks plus virtual EXIT (represented as nullptr).
    std::vector<const ir::BasicBlock*> blocks;
    for (auto& bb : st.func->getBasicBlocks()) blocks.push_back(bb.get());

    std::map<const ir::BasicBlock*, std::set<const ir::BasicBlock*>> succ, pred;
    for (auto* b : blocks) {
        auto s = successorsOf(b);
        if (s.empty()) s.push_back(nullptr); // return/unreachable -> EXIT
        for (auto* t : s) {
            succ[b].insert(t);
            pred[t].insert(b);
        }
    }
    succ[nullptr]; // EXIT has no successors

    std::set<const ir::BasicBlock*> allNodes(blocks.begin(), blocks.end());
    allNodes.insert(nullptr);

    std::map<const ir::BasicBlock*, std::set<const ir::BasicBlock*>> postdom;
    for (auto* n : allNodes) {
        if (n == nullptr) postdom[n] = {nullptr};
        else postdom[n] = allNodes; // init: everything
    }
    // Iterate to fixpoint.
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto* n : blocks) {
            auto& pd = postdom[n];
            std::set<const ir::BasicBlock*> next;
            bool first = true;
            for (auto* s : succ[n]) {
                if (first) { next = postdom[s]; first = false; }
                else {
                    std::set<const ir::BasicBlock*> inter;
                    std::set_intersection(next.begin(), next.end(),
                                          postdom[s].begin(), postdom[s].end(),
                                          std::inserter(inter, inter.begin()));
                    next = std::move(inter);
                }
            }
            next.insert(n);
            if (next != pd) { pd = std::move(next); changed = true; }
        }
    }
    return postdom;
}

void SPIRVArchitecture::analyzeCFG(SPIRVFunctionState& st) {
    if (st.cfgAnalyzed) return;
    st.cfgAnalyzed = true;

    std::vector<const ir::BasicBlock*> blocks;
    for (auto& bb : st.func->getBasicBlocks()) blocks.push_back(bb.get());
    if (blocks.empty()) return;

    // Dominators (forward) for back-edge detection.
    std::map<const ir::BasicBlock*, std::set<const ir::BasicBlock*>> preds;
    for (auto* b : blocks)
        for (auto* s : successorsOf(b)) preds[s].insert(b);

    std::set<const ir::BasicBlock*> allBlocks(blocks.begin(), blocks.end());
    std::map<const ir::BasicBlock*, std::set<const ir::BasicBlock*>> dom;
    for (auto* b : blocks) dom[b] = allBlocks;
    dom[blocks.front()] = {blocks.front()};
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto* b : blocks) {
            if (b == blocks.front()) continue;
            std::set<const ir::BasicBlock*> next = allBlocks;
            bool first = true;
            for (auto* p : preds[b]) {
                if (first) { next = dom[p]; first = false; }
                else {
                    std::set<const ir::BasicBlock*> inter;
                    std::set_intersection(next.begin(), next.end(),
                                          dom[p].begin(), dom[p].end(),
                                          std::inserter(inter, inter.begin()));
                    next = std::move(inter);
                }
            }
            next.insert(b);
            if (next != dom[b]) { dom[b] = std::move(next); changed = true; }
        }
    }

    // Loop headers: block H is a loop header if it is the target of a back
    // edge, i.e. some predecessor P of H is dominated by H (H in dom[P]).
    std::set<const ir::BasicBlock*> loopHeaders;
    for (auto* b : blocks) {
        for (auto* p : preds[b]) {
            auto dit = dom.find(p);
            if (dit != dom.end() && dit->second.count(b)) {
                loopHeaders.insert(b);
                break;
            }
        }
    }

    // Postdominators for merge targets.
    auto postdom = computePostDominators(st);

    // Immediate postdominator helper: strict postdominator closest to the node.
    auto ipdom = [&](const ir::BasicBlock* n) -> const ir::BasicBlock* {
        if (!n) return nullptr;
        auto it = postdom.find(n);
        if (it == postdom.end()) return nullptr;
        std::vector<const ir::BasicBlock*> strict;
        for (auto* x : it->second) {
            if (x != n) strict.push_back(x);
        }
        if (strict.empty()) return nullptr;
        // ipdom = strict postdom m such that every other strict postdom x
        // postdominates m, i.e. x is in m's postdominator set (m's postdom
        // set lists the nodes that postdominate m).
        for (auto* m : strict) {
            bool ok = true;
            auto mit = postdom.find(m);
            if (mit == postdom.end()) continue;
            for (auto* x : strict) {
                if (x == m) continue;
                if (!mit->second.count(x)) { ok = false; break; }
            }
            if (ok) return m;
        }
        return strict.front();
    };

    bool needSink = false;
    for (auto* b : blocks) {
        // Blocks needing merge annotations: conditional terminators (Br/Jnz/Jz)
        // and every loop header (which requires OpLoopMerge regardless of how
        // it terminates).
        bool conditional = loopHeaders.count(b) != 0;
        for (auto& instPtr : b->getInstructions()) {
            auto op = instPtr->getOpcode();
            if (op == ir::Instruction::Br || op == ir::Instruction::Jnz || op == ir::Instruction::Jz)
                conditional = true;
        }
        if (!conditional) continue;
        const ir::BasicBlock* m = ipdom(b);
        if (m == nullptr) {
            needSink = true;
            st.mergeTarget[b] = nullptr; // resolved to sink at emission
        } else {
            st.mergeTarget[b] = m;
        }
        st.isLoopHeader[b] = loopHeaders.count(b) != 0;
        if (st.isLoopHeader[b]) {
            // Continue target = the latch: a predecessor dominated by the
            // header (the back-edge source). Fall back to the header itself
            // for self-loops.
            const ir::BasicBlock* latch = b;
            for (auto* p : preds[b]) {
                auto dit = dom.find(p);
                if (dit != dom.end() && dit->second.count(b)) { latch = p; break; }
            }
            st.loopLatch[b] = latch;
        }
    }
    if (needSink) {
        st.sinkLabelId = module_.allocateId();
    }
}

void SPIRVArchitecture::emitMergeAnnotation(SPIRVFunctionState& st, const ir::BasicBlock* bb) {
    auto it = st.mergeTarget.find(bb);
    if (it == st.mergeTarget.end()) return;
    uint32_t mergeId;
    const ir::BasicBlock* m = it->second;
    if (m == nullptr) {
        mergeId = st.sinkLabelId;
    } else {
        mergeId = labelIdOf(m);
    }
    if (st.isLoopHeader[bb]) {
        uint32_t contId = st.loopLatch.count(bb) ? labelIdOf(st.loopLatch.at(bb))
                                                 : labelIdOf(bb);
        module_.addInstruction(Op::LoopMerge, {mergeId, contId});
    } else {
        module_.addInstruction(Op::SelectionMerge, {mergeId});
    }
}

// ---------------------------------------------------------------------------
// Terminators and control flow instructions
// ---------------------------------------------------------------------------

void SPIRVArchitecture::emitRet(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    // The return value (if any) is operand 0; the ret instruction itself is
    // always void-typed.
    if (!i.getOperands().empty() && i.getOperands()[0] && i.getOperands()[0]->get()) {
        ir::Value* rv = i.getOperands()[0]->get();
        if (rv->getType() && !rv->getType()->isVoidTy()) {
            module_.addInstruction(Op::ReturnValue, {valueIdOf(rv)});
            return;
        }
    }
    module_.addInstruction(Op::Return, {});
}

void SPIRVArchitecture::emitJmp(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    for (auto& use : i.getOperands()) {
        if (auto* bb = dynamic_cast<ir::BasicBlock*>(use->get())) {
            module_.addInstruction(Op::Branch, {labelIdOf(bb)});
            return;
        }
    }
    diagnose("OpJmp with non-block operand in SPIR-V lowering");
}

void SPIRVArchitecture::emitBr(codegen::CodeGen& cg, ir::Instruction& i) {
    // Operand layout: conditional -> {cond, trueBB, falseBB}; unconditional
    // (single block operand) -> {targetBB}.
    std::vector<ir::BasicBlock*> targets;
    ir::Value* cond = nullptr;
    for (size_t k = 0; k < i.getOperands().size(); ++k) {
        auto* v = i.getOperands()[k]->get();
        if (auto* bb = dynamic_cast<ir::BasicBlock*>(v)) targets.push_back(bb);
        else if (k == 0) cond = v;
    }
    if (targets.empty()) {
        diagnose("conditional branch without target blocks");
        return;
    }
    if (cond && targets.size() >= 2) {
        uint32_t condId = valueIdOf(cond);
        // SPIR-V requires a boolean condition; IR conditions are integers.
        uint32_t condType = getTypeId(cond->getType());
        uint32_t boolCond;
        if (condType == boolTypeId_) {
            boolCond = condId;
        } else {
            // cond != 0  ->  OpINotEqual bool, cond, 0
            auto* intTy = dynamic_cast<ir::IntegerType*>(cond->getType());
            uint32_t zero = getConstantIntId(intTy, 0, true);
            boolCond = module_.allocateId();
            module_.addInstruction(Op::INotEqual, {boolTypeId_, boolCond, condId, zero});
        }
        module_.addInstruction(Op::BranchConditional,
            {boolCond, labelIdOf(targets[0]), labelIdOf(targets[1])});
    } else {
        module_.addInstruction(Op::Branch, {labelIdOf(targets[0])});
    }
}

void SPIRVArchitecture::emitPhi(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    uint32_t typeId = getTypeId(i.getType());
    uint32_t resId = getOrCreateValueId(cg, &i);
    std::vector<uint32_t> ops = {typeId, resId};
    // Operand pairs: (BasicBlock*, value) repeated.
    auto& operands = i.getOperands();
    for (size_t k = 0; k + 1 < operands.size(); k += 2) {
        ir::Value* bv = operands[k]->get();
        ir::Value* vv = operands[k + 1]->get();
        uint32_t lid = 0;
        if (auto* bb = dynamic_cast<ir::BasicBlock*>(bv)) lid = labelIdOf(bb);
        ops.push_back(valueIdOf(vv));
        ops.push_back(lid);
    }
    module_.addInstruction(Op::Phi, ops);
}

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------

namespace {
    bool isFloatInstr(const ir::Instruction& i) {
        if (i.getOperands().size() >= 2) {
            auto* t = i.getOperands()[0]->get() ? i.getOperands()[0]->get()->getType() : nullptr;
            return t && (t->isFloatTy() || t->isDoubleTy());
        }
        auto* t = i.getType();
        return t && (t->isFloatTy() || t->isDoubleTy());
    }
}

void SPIRVArchitecture::emitAdd(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    emitResult(i, isFloatInstr(i) ? Op::FAdd : Op::IAdd,
               {valueIdOf(i.getOperands()[0]->get()), valueIdOf(i.getOperands()[1]->get())});
}
void SPIRVArchitecture::emitSub(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    emitResult(i, isFloatInstr(i) ? Op::FSub : Op::ISub,
               {valueIdOf(i.getOperands()[0]->get()), valueIdOf(i.getOperands()[1]->get())});
}
void SPIRVArchitecture::emitMul(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    emitResult(i, isFloatInstr(i) ? Op::FMul : Op::IMul,
               {valueIdOf(i.getOperands()[0]->get()), valueIdOf(i.getOperands()[1]->get())});
}
void SPIRVArchitecture::emitDiv(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    uint16_t op;
    if (isFloatInstr(i)) op = Op::FDiv;
    else if (i.getOpcode() == ir::Instruction::Udiv) op = Op::UDiv;
    else op = Op::SDiv;
    emitResult(i, op, {valueIdOf(i.getOperands()[0]->get()), valueIdOf(i.getOperands()[1]->get())});
}
void SPIRVArchitecture::emitRem(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    uint16_t op;
    if (isFloatInstr(i)) op = Op::FRem;
    else if (i.getOpcode() == ir::Instruction::Urem) op = Op::UMod;
    else op = Op::SRem;
    emitResult(i, op, {valueIdOf(i.getOperands()[0]->get()), valueIdOf(i.getOperands()[1]->get())});
}
void SPIRVArchitecture::emitAnd(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    emitResult(i, Op::BitwiseAnd, {valueIdOf(i.getOperands()[0]->get()), valueIdOf(i.getOperands()[1]->get())});
}
void SPIRVArchitecture::emitOr(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    emitResult(i, Op::BitwiseOr, {valueIdOf(i.getOperands()[0]->get()), valueIdOf(i.getOperands()[1]->get())});
}
void SPIRVArchitecture::emitXor(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    emitResult(i, Op::BitwiseXor, {valueIdOf(i.getOperands()[0]->get()), valueIdOf(i.getOperands()[1]->get())});
}
void SPIRVArchitecture::emitShl(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    emitResult(i, Op::ShiftLeftLogical, {valueIdOf(i.getOperands()[0]->get()), valueIdOf(i.getOperands()[1]->get())});
}
void SPIRVArchitecture::emitShr(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    emitResult(i, Op::ShiftRightLogical, {valueIdOf(i.getOperands()[0]->get()), valueIdOf(i.getOperands()[1]->get())});
}
void SPIRVArchitecture::emitSar(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    emitResult(i, Op::ShiftRightArithmetic, {valueIdOf(i.getOperands()[0]->get()), valueIdOf(i.getOperands()[1]->get())});
}
void SPIRVArchitecture::emitNeg(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    emitResult(i, isFloatInstr(i) ? Op::FNegate : Op::SNegate,
               {valueIdOf(i.getOperands()[0]->get())});
}
void SPIRVArchitecture::emitNot(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    emitResult(i, Op::Not, {valueIdOf(i.getOperands()[0]->get())});
}
void SPIRVArchitecture::emitCopy(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    if (!i.getOperands().empty() && i.getOperands()[0])
        valueIds_[&i] = valueIdOf(i.getOperands()[0]->get());
}

void SPIRVArchitecture::emitFAdd(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg; emitResult(i, Op::FAdd, {valueIdOf(i.getOperands()[0]->get()), valueIdOf(i.getOperands()[1]->get())});
}
void SPIRVArchitecture::emitFSub(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg; emitResult(i, Op::FSub, {valueIdOf(i.getOperands()[0]->get()), valueIdOf(i.getOperands()[1]->get())});
}
void SPIRVArchitecture::emitFMul(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg; emitResult(i, Op::FMul, {valueIdOf(i.getOperands()[0]->get()), valueIdOf(i.getOperands()[1]->get())});
}
void SPIRVArchitecture::emitFDiv(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg; emitResult(i, Op::FDiv, {valueIdOf(i.getOperands()[0]->get()), valueIdOf(i.getOperands()[1]->get())});
}

void SPIRVArchitecture::emitSMin(codegen::CodeGen& cg, ir::Instruction& i) {
    emitMinMax(cg, i, true, true);
}
void SPIRVArchitecture::emitSMax(codegen::CodeGen& cg, ir::Instruction& i) {
    emitMinMax(cg, i, false, true);
}

void SPIRVArchitecture::emitMinMax(codegen::CodeGen& cg, ir::Instruction& i, bool isMin, bool isSigned) {
    (void)isSigned;
    // SPIR-V has no integer min/max opcodes: select(cmp, a, b).
    ir::Value* a = i.getOperands()[0]->get();
    ir::Value* b = i.getOperands()[1]->get();
    uint32_t typeId = getTypeId(i.getType());
    uint32_t cmpOp = isMin ? Op::SLessThan : Op::SGreaterThan;
    if (isFloatInstr(i)) cmpOp = isMin ? Op::FOrdLessThan : Op::FOrdGreaterThan;
    uint32_t boolId = module_.allocateId();
    module_.addInstruction(cmpOp, {boolTypeId_, boolId, valueIdOf(a), valueIdOf(b)});
    uint32_t resId = getOrCreateValueId(cg, &i);
    module_.addInstruction(Op::Select, {typeId, resId, boolId, valueIdOf(a), valueIdOf(b)});
}

// ---------------------------------------------------------------------------
// Comparisons
// ---------------------------------------------------------------------------

void SPIRVArchitecture::emitSelectToInt(codegen::CodeGen& cg, ir::Instruction& i, uint32_t boolId) {
    (void)cg;
    // Convert the boolean comparison result into the IR result type (integer).
    uint32_t typeId = getTypeId(i.getType());
    if (typeId == boolTypeId_) return; // already boolean
    uint32_t resId = getOrCreateValueId(cg, &i);
    // one / zero constants of the result type
    uint32_t oneId, zeroId;
    if (auto* it = dynamic_cast<ir::IntegerType*>(i.getType())) {
        oneId = getConstantIntId(it, 1, true);
        zeroId = getConstantIntId(it, 0, true);
    } else if (i.getType() && i.getType()->isFloatTy()) {
        // shouldn't happen for comparisons; fall back to i32
        oneId = getConstantIntId(nullptr, 1, true);
        zeroId = getConstantIntId(nullptr, 0, true);
        typeId = i32TypeId_;
    } else {
        oneId = getConstantIntId(nullptr, 1, true);
        zeroId = getConstantIntId(nullptr, 0, true);
        typeId = i32TypeId_;
    }
    module_.addInstruction(Op::Select, {typeId, resId, boolId, oneId, zeroId});
}

void SPIRVArchitecture::emitCmp(codegen::CodeGen& cg, ir::Instruction& i) {
    using OpCode = ir::Instruction::Opcode;
    OpCode opc = i.getOpcode();
    bool isFloat = false;
    if (i.getOperands().size() >= 2) {
        auto* t = i.getOperands()[0]->get() ? i.getOperands()[0]->get()->getType() : nullptr;
        isFloat = t && (t->isFloatTy() || t->isDoubleTy());
    }
    uint16_t op = 0;
    switch (opc) {
        case OpCode::Ceq: case OpCode::Ceqf: op = isFloat ? Op::FOrdEqual : Op::IEqual; break;
        case OpCode::Cne: case OpCode::Cnef: op = isFloat ? Op::FOrdNotEqual : Op::INotEqual; break;
        case OpCode::Cslt: op = Op::SLessThan; break;
        case OpCode::Csle: op = Op::SLessThanEqual; break;
        case OpCode::Csgt: op = Op::SGreaterThan; break;
        case OpCode::Csge: op = Op::SGreaterThanEqual; break;
        case OpCode::Cult: op = Op::ULessThan; break;
        case OpCode::Cule: op = Op::ULessThanEqual; break;
        case OpCode::Cugt: op = Op::UGreaterThan; break;
        case OpCode::Cuge: op = Op::UGreaterThanEqual; break;
        case OpCode::Cle: op = Op::FOrdLessThanEqual; break;
        case OpCode::Clt: op = Op::FOrdLessThan; break;
        case OpCode::Cge: op = Op::FOrdGreaterThanEqual; break;
        case OpCode::Cgt: op = Op::FOrdGreaterThan; break;
        case OpCode::Co:  op = Op::Ordered; break;
        case OpCode::Cuo: op = Op::Unordered; break;
        default:
            diagnose("unsupported comparison opcode in SPIR-V lowering");
            return;
    }
    uint32_t boolId = module_.allocateId();
    module_.addInstruction(op, {boolTypeId_, boolId,
        valueIdOf(i.getOperands()[0]->get()), valueIdOf(i.getOperands()[1]->get())});
    emitSelectToInt(cg, i, boolId);
}

// ---------------------------------------------------------------------------
// Casts
// ---------------------------------------------------------------------------

void SPIRVArchitecture::emitIntTrunc(codegen::CodeGen& cg, ir::Instruction& i,
                                     unsigned fromBits, unsigned toBits) {
    (void)cg;
    // SPIR-V cannot narrow integers directly; keep the low toBits via mask.
    ir::Value* src = i.getOperands()[0]->get();
    uint32_t srcId = valueIdOf(src);
    uint64_t mask = (toBits >= 64) ? ~0ull : ((1ull << toBits) - 1);
    auto* intTy = dynamic_cast<ir::IntegerType*>(i.getType());
    uint32_t maskId = getConstantIntId(intTy, mask, false);
    uint32_t resId = getOrCreateValueId(cg, &i);
    uint32_t typeId = getTypeId(i.getType());
    module_.addInstruction(Op::BitwiseAnd, {typeId, resId, srcId, maskId});
    (void)fromBits;
}

void SPIRVArchitecture::emitCast(codegen::CodeGen& cg, ir::Instruction& i,
                                 const ir::Type* from, const ir::Type* to) {
    if (i.getOperands().empty() || !i.getOperands()[0]) return;
    ir::Value* src = i.getOperands()[0]->get();
    uint32_t srcId = valueIdOf(src);
    uint32_t dstTypeId = getTypeId(to);
    uint32_t resId = getOrCreateValueId(cg, &i);

    bool fromFloat = from && (from->isFloatTy() || from->isDoubleTy());
    bool toFloat = to && (to->isFloatTy() || to->isDoubleTy());
    bool fromInt = from && from->isIntegerTy();
    bool toInt = to && to->isIntegerTy();
    unsigned fromW = bitWidthOf(from);
    unsigned toW = bitWidthOf(to);

    auto emit1 = [&](uint16_t op) {
        module_.addInstruction(op, {dstTypeId, resId, srcId});
    };

    using OC = ir::Instruction::Opcode;
    OC opc = i.getOpcode();

    if (fromFloat && toInt) {
        // DToUI -> unsigned, DToSI and friends -> signed.
        emit1(opc == OC::DToUI ? Op::ConvertFToU : Op::ConvertFToS);
        return;
    }
    if (fromInt && toFloat) {
        bool uns = (opc == OC::UWtoF || opc == OC::Ultof || opc == OC::SToUI);
        emit1(uns ? Op::ConvertUToF : Op::ConvertSToF);
        return;
    }
    if (fromFloat && toFloat) {
        if (fromW == toW) { valueIds_[&i] = srcId; return; }
        emit1(Op::FConvert);
        return;
    }
    if (fromInt && toInt) {
        if (fromW == toW) {
            // Same width: sign reinterpret -> bitcast.
            emit1(Op::Bitcast);
            return;
        }
        if (toW > fromW) {
            // Extension: opcode hints signedness.
            bool signExt = (opc == OC::ExtSB || opc == OC::ExtSH || opc == OC::ExtSW ||
                            opc == OC::ExtS || opc == OC::SToSI || opc == OC::Sltof);
            emit1(signExt ? Op::SConvert : Op::UConvert);
            return;
        }
        // Narrowing: mask to low bits.
        uint64_t mask = (toW >= 64) ? ~0ull : ((1ull << toW) - 1);
        auto* intTy = dynamic_cast<const ir::IntegerType*>(to);
        uint32_t maskId = getConstantIntId(intTy, mask, false);
        module_.addInstruction(Op::BitwiseAnd, {dstTypeId, resId, srcId, maskId});
        return;
    }
    if (from && from->isPointerTy() && toInt) {
        diagnose("pointer-to-integer cast requires physical addressing model");
        module_.addInstruction(Op::Undef, {dstTypeId, resId});
        return;
    }
    if (fromInt && to && to->isPointerTy()) {
        diagnose("integer-to-pointer cast requires physical addressing model");
        module_.addInstruction(Op::Undef, {dstTypeId, resId});
        return;
    }
    if (from && from->isPointerTy() && to && to->isPointerTy()) {
        emit1(Op::Bitcast);
        return;
    }
    diagnose("unsupported cast in SPIR-V lowering");
    module_.addInstruction(Op::Undef, {dstTypeId, resId});
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------

uint32_t SPIRVArchitecture::pointerValueId(ir::Value* ptrVal, bool& ok) {
    ok = true;
    if (!ptrVal) { ok = false; return 0; }
    if (auto* ci = dynamic_cast<ir::ConstantInt*>(ptrVal)) {
        if (current_) {
            int64_t slot = static_cast<int64_t>(ci->getValue());
            auto it = current_->stackSlots.find(slot);
            if (it != current_->stackSlots.end()) return it->second;
        }
        ok = false;
        return 0;
    }
    return valueIdOf(ptrVal);
}

uint32_t SPIRVArchitecture::slotVariableId(int64_t slot) {
    if (!current_) return 0;
    auto it = current_->stackSlots.find(slot);
    return it != current_->stackSlots.end() ? it->second : 0;
}

void SPIRVArchitecture::emitLoad(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    if (i.getOperands().empty()) return;
    bool ok = false;
    uint32_t ptrId = pointerValueId(i.getOperands()[0]->get(), ok);
    if (!ok) {
        diagnose("load through unsupported pointer in SPIR-V lowering");
        return;
    }
    uint32_t typeId = getTypeId(i.getType());
    uint32_t resId = getOrCreateValueId(cg, &i);
    module_.addInstruction(Op::Load, {typeId, resId, ptrId});
}

void SPIRVArchitecture::emitStore(codegen::CodeGen& cg, ir::Instruction& i) {
    (void)cg;
    if (i.getOperands().size() < 2) return;
    ir::Value* val = i.getOperands()[0]->get();
    ir::Value* ptr = i.getOperands()[1]->get();
    bool ok = false;
    uint32_t ptrId = pointerValueId(ptr, ok);
    if (!ok) {
        diagnose("store through unsupported pointer in SPIR-V lowering");
        return;
    }
    module_.addInstruction(Op::Store, {ptrId, valueIdOf(val)});
}

void SPIRVArchitecture::emitAlloc(codegen::CodeGen& cg, ir::Instruction& i) {
    // Allocas were pre-scanned and emitted as OpVariable in the entry block.
    (void)cg;
    if (!valueIds_.count(&i)) {
        // Fallback: allocate lazily (may produce invalid placement; diagnose).
        diagnose("alloca missed pre-scan in SPIR-V lowering");
    }
}

// ---------------------------------------------------------------------------
// Calls and externs
// ---------------------------------------------------------------------------

void SPIRVArchitecture::emitCall(codegen::CodeGen& cg, ir::Instruction& i) {
    if (i.getOperands().empty()) return;
    ir::Value* callee = i.getOperands()[0]->get();
    uint32_t fnId = 0;
    if (auto* fn = dynamic_cast<ir::Function*>(callee)) {
        fnId = valueIdOf(fn);
        calledFunctions_.insert(fn);
    } else {
        diagnose("indirect calls are not supported in SPIR-V lowering");
        return;
    }
    uint32_t typeId = getTypeId(i.getType());
    uint32_t resId = getOrCreateValueId(cg, &i);
    std::vector<uint32_t> ops = {typeId, resId, fnId};
    for (size_t k = 1; k < i.getOperands().size(); ++k)
        ops.push_back(valueIdOf(i.getOperands()[k]->get()));
    module_.addInstruction(Op::FunctionCall, ops);
}

uint32_t SPIRVArchitecture::lowerGpuCapability(codegen::CodeGen& cg,
                                                ir::Instruction& i,
                                                CapabilityId capId,
                                                const std::string& name) {
    (void)cg;
    auto hasResult = [&]() {
        return i.getType() && i.getType()->getTypeID() != ir::Type::VoidTyID;
    };
    switch (capId) {
        case CapabilityId::GPU_THREAD_ID:
        case CapabilityId::GPU_LOCAL_ID:
        case CapabilityId::GPU_GROUP_ID:
        case CapabilityId::GPU_NUM_GROUPS: {
            // Thread-space builtins are 3-component integer vectors. The
            // oracle ABI declares these externs as scalar i64; rewrite the
            // instruction's declared type to the ivec3 it actually lowers
            // to. SSA order guarantees every consumer is emitted after this
            // point, so they all observe the corrected type.
            if (i.getType() && i.getType()->isIntegerTy()) {
                if (!gpuVec3Ty_) {
                    auto& ctx = ir::IRContext::getContext();
                    gpuVec3Ty_ = ctx.getVectorType(ctx.getIntegerType(32), 3);
                }
                i.setType(gpuVec3Ty_);
            }
            uint32_t builtIn =
                capId == CapabilityId::GPU_THREAD_ID    ? BuiltIn::GlobalInvocationId :
                capId == CapabilityId::GPU_LOCAL_ID     ? BuiltIn::LocalInvocationId  :
                capId == CapabilityId::GPU_GROUP_ID     ? BuiltIn::WorkgroupId        :
                                                        BuiltIn::NumWorkgroups;
            uint32_t varId = getBuiltinVariableId(builtIn);
            uint32_t resId = module_.allocateId();
            module_.addInstruction(Op::Load, {getBuiltinVec3Type(), resId, varId});
            valueIds_[&i] = resId;
            return resId;
        }
        case CapabilityId::GPU_SYNC: {
            // Workgroup barrier + memory fence (syncthreads equivalent).
            module_.addInstruction(Op::ControlBarrier,
                {Scope::Workgroup, MemSem::WorkgroupMemory | MemSem::SequentiallyConsistent});
            module_.addInstruction(Op::MemoryBarrier,
                {Scope::Workgroup, MemSem::WorkgroupMemory | MemSem::SequentiallyConsistent});
            if (hasResult()) {
                uint32_t resId = module_.allocateId();
                module_.addGlobal(Op::Constant, {i64TypeId_, resId, 0});
                valueIds_[&i] = resId;
                return resId;
            }
            return 0;
        }
        case CapabilityId::GPU_SET_MESH_COUNTS: {
            // Mesh shader primitive amplification: OpSetMeshOutputsCounts.
            if (i.getOperands().size() < 2) {
                diagnose("gpu.set.mesh.counts requires 2 operands");
                return 0;
            }
            module_.addInstruction(Op::SetMeshOutputsCounts,
                {valueIdOf(i.getOperands()[0]->get()),
                 valueIdOf(i.getOperands()[1]->get())});
            return 0;
        }
        case CapabilityId::GPU_MEMCPY: {
            if (i.getOperands().size() < 3) {
                diagnose("gpu.memcpy requires 3 operands");
                return 0;
            }
            // Size operand must be a constant integer instruction.
            uint32_t sizeId = 0;
            if (auto* ci = dynamic_cast<ir::ConstantInt*>(i.getOperands()[2]->get())) {
                sizeId = module_.allocateId();
                module_.addGlobal(Op::Constant, {i32TypeId_, sizeId,
                    static_cast<uint32_t>(ci->getValue())});
            } else {
                diagnose("gpu.memcpy size operand must be a constant integer");
                return 0;
            }
            module_.addInstruction(Op::CopyMemory,
                {valueIdOf(i.getOperands()[0]->get()),
                 valueIdOf(i.getOperands()[1]->get()),
                 sizeId});
            return 0;
        }
        case CapabilityId::GPU_MALLOC:
            diagnose("gpu.malloc has no direct SPIR-V lowering (use workgroup/private storage)");
            return 0;
        case CapabilityId::GPU_TASK:
        case CapabilityId::GPU_VERTEX:
        case CapabilityId::GPU_PIXEL:
        case CapabilityId::GPU_MESH:
        case CapabilityId::GPU_LOCAL_SIZE:
        case CapabilityId::GPU_MESH_LIMITS:
            // Stage/execution-mode markers: metadata consumed by the
            // entry-point pass; they emit no shader code.
            return 0;
        case CapabilityId::GPU_COMPUTE:
            diagnose("gpu.compute is a host-side dispatch; it cannot appear inside shader code");
            return 0;
        default:
            diagnose("unhandled gpu capability in SPIR-V lowering");
            return 0;
    }
}

void SPIRVArchitecture::emitExternCall(codegen::CodeGen& cg, ir::Instruction& i,
                                       const OperatingSystemInfo&) {
    auto* ec = dynamic_cast<ir::ExternCallInstruction*>(&i);
    if (!ec) return;
    const std::string& cap = ec->getCapability();

    // Canonical path: gpu.* capabilities registered in CapabilityRegistry
    // lower natively — no runtime/extern names involved.
    if (const CapabilitySpec* spec = CapabilityRegistry::find(cap)) {
        if (spec->domain == CapabilityDomain::GPU) {
            lowerGpuCapability(cg, i, spec->id, cap);
            return;
        }
    }

    // Compatibility aliases: the software oracle's runtime names. Normalized
    // (underscores -> dots) and mapped onto the same native lowering. These
    // disappear once producers emit gpu.* capability externs directly.
    std::string normCap = cap;
    for (auto& ch : normCap) if (ch == '_') ch = '.';
    static const struct { const char* dotted; CapabilityId id; } aliases[] = {
        {"lymar.aot.gpu.thread.id",      CapabilityId::GPU_THREAD_ID},
        {"lymar.aot.gpu.local.id",       CapabilityId::GPU_LOCAL_ID},
        {"lymar.aot.gpu.group.id",       CapabilityId::GPU_GROUP_ID},
        {"lymar.aot.gpu.num.groups",     CapabilityId::GPU_NUM_GROUPS},
        {"lymar.aot.gpu.sync",           CapabilityId::GPU_SYNC},
        {"lymar.aot.gpu.set.mesh.counts",CapabilityId::GPU_SET_MESH_COUNTS},
        {"lymar.aot.gpu.memcpy",         CapabilityId::GPU_MEMCPY},
    };
    for (auto& a : aliases) {
        if (normCap == a.dotted) {
            lowerGpuCapability(cg, i, a.id, cap);
            return;
        }
    }
    // Host-side oracle calls carry no shader meaning.
    if (normCap == "lymar.aot.gpu.register" ||
        normCap == "lymar.aot.gpu.dispatch") {
        if (normCap == "lymar.aot.gpu.dispatch")
            diagnose("host-side GPU dispatch cannot appear inside shader code");
        return;
    }

    // Extern calls naming a module function become regular calls.
    for (auto& [fname, st] : functions_) {
        if (st.func && st.func->getName() == ec->getCapability()) {
            uint32_t typeId = getTypeId(i.getType());
            uint32_t resId = getOrCreateValueId(cg, &i);
            std::vector<uint32_t> ops = {typeId, resId, st.fnId};
            for (auto& use : i.getOperands()) ops.push_back(valueIdOf(use->get()));
            module_.addInstruction(Op::FunctionCall, ops);
            return;
        }
    }
    diagnose("extern call '" + ec->getCapability() + "' is not supported in SPIR-V lowering");
    uint32_t typeId = getTypeId(i.getType());
    uint32_t resId = getOrCreateValueId(cg, &i);
    module_.addInstruction(Op::Undef, {typeId, resId});
}

void SPIRVArchitecture::emitSyscall(codegen::CodeGen&, ir::Instruction& i,
                                    const OperatingSystemInfo&) {
    diagnose("syscalls are not supported in SPIR-V lowering");
    uint32_t typeId = getTypeId(i.getType());
    uint32_t resId = getOrCreateValueId(*activeCg_, &i);
    module_.addInstruction(Op::Undef, {typeId, resId});
}

void SPIRVArchitecture::emitNativeSyscall(codegen::CodeGen&, uint64_t, const std::vector<ir::Value*>&) {
    diagnose("native syscalls are not supported in SPIR-V lowering");
}

void SPIRVArchitecture::emitNativeLibraryCall(codegen::CodeGen&, const std::string& name,
                                              const std::vector<ir::Value*>&) {
    diagnose("native library call '" + name + "' is not supported in SPIR-V lowering");
}

} // namespace target
