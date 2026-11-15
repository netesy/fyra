#pragma once
#include "target/core/ArchitectureInfo.h"
#include "target/capabilities/Capabilities.h"
#include "target/architecture/spirv/SPIRVModule.h"
#include <array>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace target {

namespace spirv {
namespace Op {
    // Logical-layout opcodes (see SPIR-V spec section 3).
    constexpr uint16_t Nop                    = 0;
    constexpr uint16_t Undef                  = 1;
    constexpr uint16_t Name                   = 5;
    constexpr uint16_t ExtInstImport          = 11;
    constexpr uint16_t ExtInst                = 12;
    constexpr uint16_t MemoryModel            = 14;
    constexpr uint16_t EntryPoint             = 15;
    constexpr uint16_t ExecutionMode          = 16;
    constexpr uint16_t Capability             = 17;
    constexpr uint16_t TypeVoid               = 19;
    constexpr uint16_t TypeBool               = 20;
    constexpr uint16_t TypeInt                = 21;
    constexpr uint16_t TypeFloat              = 22;
    constexpr uint16_t TypeVector             = 23;
    constexpr uint16_t TypeArray              = 28;
    constexpr uint16_t TypeRuntimeArray       = 29;
    constexpr uint16_t TypeStruct             = 30;
    constexpr uint16_t TypePointer            = 32;
    constexpr uint16_t TypeFunction           = 33;
    constexpr uint16_t ConstantTrue           = 41;
    constexpr uint16_t ConstantFalse          = 42;
    constexpr uint16_t Constant               = 43;
    constexpr uint16_t ConstantComposite      = 44;
    constexpr uint16_t ConstantNull           = 46;
    constexpr uint16_t Function               = 54;
    constexpr uint16_t FunctionParameter      = 55;
    constexpr uint16_t FunctionEnd            = 56;
    constexpr uint16_t FunctionCall           = 57;
    constexpr uint16_t Variable               = 59;
    constexpr uint16_t Load                   = 61;
    constexpr uint16_t Store                  = 62;
    constexpr uint16_t AccessChain            = 65;
    constexpr uint16_t InBoundsAccessChain    = 66;
    constexpr uint16_t CopyMemory             = 63;
    constexpr uint16_t SetMeshOutputsCounts   = 5292;
    constexpr uint16_t ConvertFToU            = 109;
    constexpr uint16_t ConvertFToS            = 110;
    constexpr uint16_t ConvertSToF            = 111;
    constexpr uint16_t ConvertUToF            = 112;
    constexpr uint16_t UConvert               = 113;
    constexpr uint16_t SConvert               = 114;
    constexpr uint16_t FConvert               = 115;
    constexpr uint16_t ConvertPtrToU          = 117;
    constexpr uint16_t ConvertUToPtr          = 120;
    constexpr uint16_t Bitcast                = 124;
    constexpr uint16_t SNegate                = 126;
    constexpr uint16_t FNegate                = 127;
    constexpr uint16_t IAdd                   = 128;
    constexpr uint16_t FAdd                   = 129;
    constexpr uint16_t ISub                   = 130;
    constexpr uint16_t FSub                   = 131;
    constexpr uint16_t IMul                   = 132;
    constexpr uint16_t FMul                   = 133;
    constexpr uint16_t UDiv                   = 134;
    constexpr uint16_t SDiv                   = 135;
    constexpr uint16_t FDiv                   = 136;
    constexpr uint16_t UMod                   = 137;
    constexpr uint16_t SRem                   = 138;
    constexpr uint16_t SMod                   = 139;
    constexpr uint16_t FRem                   = 140;
    constexpr uint16_t FMod                   = 141;
    constexpr uint16_t VectorTimesScalar      = 142;
    constexpr uint16_t MatrixTimesScalar      = 143;
    constexpr uint16_t VectorTimesMatrix      = 144;
    constexpr uint16_t MatrixTimesVector      = 145;
    constexpr uint16_t MatrixTimesMatrix      = 146;
    constexpr uint16_t OuterProduct           = 147;
    constexpr uint16_t Dot                    = 148;
    constexpr uint16_t LogicalEqual           = 164;
    constexpr uint16_t LogicalNotEqual        = 165;
    constexpr uint16_t LogicalOr              = 166;
    constexpr uint16_t LogicalAnd             = 167;
    constexpr uint16_t LogicalNot             = 168;
    constexpr uint16_t Select                 = 169;
    constexpr uint16_t IEqual                 = 170;
    constexpr uint16_t INotEqual              = 171;
    constexpr uint16_t UGreaterThan           = 172;
    constexpr uint16_t SGreaterThan           = 173;
    constexpr uint16_t UGreaterThanEqual      = 174;
    constexpr uint16_t SGreaterThanEqual      = 175;
    constexpr uint16_t ULessThan              = 176;
    constexpr uint16_t SLessThan              = 177;
    constexpr uint16_t ULessThanEqual         = 178;
    constexpr uint16_t SLessThanEqual         = 179;
    constexpr uint16_t FOrdEqual              = 180;
    constexpr uint16_t FOrdNotEqual           = 182;
    constexpr uint16_t FOrdLessThan           = 184;
    constexpr uint16_t FOrdGreaterThan        = 186;
    constexpr uint16_t FOrdLessThanEqual      = 188;
    constexpr uint16_t FOrdGreaterThanEqual   = 190;
    constexpr uint16_t Ordered                = 162;
    constexpr uint16_t Unordered              = 163;
    constexpr uint16_t ShiftRightLogical      = 194;
    constexpr uint16_t ShiftRightArithmetic   = 195;
    constexpr uint16_t ShiftLeftLogical       = 196;
    constexpr uint16_t BitwiseOr              = 197;
    constexpr uint16_t BitwiseXor             = 198;
    constexpr uint16_t BitwiseAnd             = 199;
    constexpr uint16_t Not                    = 200;
    constexpr uint16_t BitFieldInsert         = 201;
    constexpr uint16_t BitFieldSExtract       = 202;
    constexpr uint16_t BitFieldUExtract       = 203;
    constexpr uint16_t VectorExtractDynamic   = 77;
    constexpr uint16_t VectorInsertDynamic    = 78;
    constexpr uint16_t VectorShuffle          = 79;
    constexpr uint16_t CompositeConstruct     = 80;
    constexpr uint16_t CompositeExtract       = 81;
    constexpr uint16_t CompositeInsert        = 82;
    constexpr uint16_t CopyObject             = 83;
    constexpr uint16_t ControlBarrier         = 224;
    constexpr uint16_t MemoryBarrier          = 225;
    constexpr uint16_t AtomicLoad             = 227;
    constexpr uint16_t AtomicStore            = 228;
    constexpr uint16_t AtomicExchange         = 229;
    constexpr uint16_t AtomicCompareExchange  = 230;
    constexpr uint16_t AtomicIIncrement       = 232;
    constexpr uint16_t AtomicIDecrement       = 233;
    constexpr uint16_t AtomicIAdd             = 234;
    constexpr uint16_t AtomicISub             = 235;
    constexpr uint16_t AtomicSMin             = 236;
    constexpr uint16_t AtomicUMin             = 237;
    constexpr uint16_t AtomicSMax             = 238;
    constexpr uint16_t AtomicUMax             = 239;
    constexpr uint16_t AtomicAnd              = 240;
    constexpr uint16_t AtomicOr               = 241;
    constexpr uint16_t AtomicXor              = 242;
    constexpr uint16_t Phi                    = 245;
    constexpr uint16_t LoopMerge              = 246;
    constexpr uint16_t SelectionMerge         = 247;
    constexpr uint16_t Label                  = 248;
    constexpr uint16_t Branch                 = 249;
    constexpr uint16_t BranchConditional      = 250;
    constexpr uint16_t Switch                 = 251;
    constexpr uint16_t Kill                   = 252;
    constexpr uint16_t Return                 = 253;
    constexpr uint16_t ReturnValue            = 254;
    constexpr uint16_t Unreachable            = 255;
} // namespace Op

// Execution models (SpvExecutionModel).
namespace ExecModel {
    constexpr uint32_t Vertex                    = 0;
    constexpr uint32_t TessellationControl       = 1;
    constexpr uint32_t TessellationEvaluation    = 2;
    constexpr uint32_t Geometry                  = 3;
    constexpr uint32_t Fragment                  = 4;
    constexpr uint32_t GLCompute                 = 5;
    constexpr uint32_t Kernel                    = 6;
    constexpr uint32_t TaskNV                    = 5267;
    constexpr uint32_t MeshNV                    = 5268;
    constexpr uint32_t TaskEXT                   = 5364;
    constexpr uint32_t MeshEXT                   = 5365;
} // namespace ExecModel

// Execution modes (SpvExecutionMode).
namespace ExecMode {
    constexpr uint32_t LocalSize       = 17;
    constexpr uint32_t LocalSizeId     = 38;
    constexpr uint32_t OutputVertices  = 26;
    constexpr uint32_t OutputPrimitivesEXT = 5270;
    constexpr uint32_t InputPoints     = 19;
    constexpr uint32_t InputLines      = 20;
    constexpr uint32_t Triangles       = 22;
    constexpr uint32_t Quads           = 24;
    constexpr uint32_t Isolines        = 25;
} // namespace ExecMode

// Storage classes (SpvStorageClass).
namespace Storage {
    constexpr uint32_t UniformConstant  = 0;
    constexpr uint32_t Input            = 1;
    constexpr uint32_t Uniform          = 2;
    constexpr uint32_t Output           = 3;
    constexpr uint32_t Workgroup        = 4;
    constexpr uint32_t CrossWorkgroup   = 5;
    constexpr uint32_t Private          = 6;
    constexpr uint32_t Function         = 7;
    constexpr uint32_t Generic          = 8;
    constexpr uint32_t PushConstant     = 9;
    constexpr uint32_t AtomicCounter    = 10;
    constexpr uint32_t Image            = 11;
    constexpr uint32_t StorageBuffer    = 12;
} // namespace Storage

// Addressing / memory models (SpvAddressingModel / SpvMemoryModel).
namespace Addr {
    constexpr uint32_t Logical    = 0;
    constexpr uint32_t Physical32 = 1;
    constexpr uint32_t Physical64 = 2;
}
namespace MemModel {
    constexpr uint32_t GLSL450 = 1;
    constexpr uint32_t OpenCL  = 2;
    constexpr uint32_t Vulkan  = 3;
}

// Capabilities (SpvCapability).
namespace Cap {
    constexpr uint32_t Shader                    = 1;
    constexpr uint32_t Geometry                  = 2;
    constexpr uint32_t Tessellation              = 3;
    constexpr uint32_t Addresses                 = 4;
    constexpr uint32_t Linkage                   = 5;
    constexpr uint32_t Kernel                    = 6;
    constexpr uint32_t Float16                   = 9;
    constexpr uint32_t Float64                   = 10;
    constexpr uint32_t Int64                     = 11;
    constexpr uint32_t Int16                     = 22;
    constexpr uint32_t Int8                      = 39;
    constexpr uint32_t GroupNonUniform           = 61;
    constexpr uint32_t VariablePointers          = 4442;
    constexpr uint32_t VulkanMemoryModel         = 5345;
}

// Scopes / memory semantics for barriers (SpvScope / SpvMemorySemantics masks).
namespace Scope {
    constexpr uint32_t CrossDevice    = 0;
    constexpr uint32_t Device         = 1;
    constexpr uint32_t Workgroup      = 2;
    constexpr uint32_t Subgroup       = 3;
    constexpr uint32_t Invocation     = 4;
}
namespace MemSem {
    constexpr uint32_t None               = 0x0;
    constexpr uint32_t Acquire            = 0x2;
    constexpr uint32_t Release            = 0x4;
    constexpr uint32_t AcquireRelease     = 0x8;
    constexpr uint32_t SequentiallyConsistent = 0x10;
    constexpr uint32_t UniformMemory      = 0x40;
    constexpr uint32_t WorkgroupMemory    = 0x100;
    constexpr uint32_t ImageMemory        = 0x800;
    constexpr uint32_t OutputMemory       = 0x1000;
    constexpr uint32_t MakeVisible        = 0x4000;
    constexpr uint32_t MakeAvailable      = 0x2000;
}

// Decorations (SpvDecoration).
namespace Deco {
    constexpr uint32_t Block               = 2;
    constexpr uint32_t BufferBlock         = 3;
    constexpr uint32_t ArrayStride         = 6;
    constexpr uint32_t MatrixStride        = 7;
    constexpr uint32_t BuiltIn             = 11;
    constexpr uint32_t NoPerspective       = 13;
    constexpr uint32_t Flat                = 14;
    constexpr uint32_t Centroid            = 16;
    constexpr uint32_t Sample              = 17;
    constexpr uint32_t Location            = 30;
    constexpr uint32_t Component           = 31;
    constexpr uint32_t Index               = 32;
    constexpr uint32_t Binding             = 33;
    constexpr uint32_t DescriptorSet       = 34;
    constexpr uint32_t Offset              = 35;
    constexpr uint32_t InputAttachmentIndex= 43;
}

// Built-ins (SpvBuiltIn).
namespace BuiltIn {
    constexpr uint32_t Position            = 0;
    constexpr uint32_t PointSize            = 1;
    constexpr uint32_t ClipDistance         = 3;
    constexpr uint32_t CullDistance         = 4;
    constexpr uint32_t VertexId             = 5;
    constexpr uint32_t InstanceId           = 6;
    constexpr uint32_t PrimitiveId          = 7;
    constexpr uint32_t InvocationId         = 8;
    constexpr uint32_t Layer                = 9;
    constexpr uint32_t ViewportIndex        = 10;
    constexpr uint32_t TessCoord            = 13;
    constexpr uint32_t PatchVertices        = 14;
    constexpr uint32_t FragCoord            = 15;
    constexpr uint32_t PointCoord           = 16;
    constexpr uint32_t FrontFacing          = 17;
    constexpr uint32_t SampleId             = 18;
    constexpr uint32_t SamplePosition       = 19;
    constexpr uint32_t SampleMask           = 20;
    constexpr uint32_t FragDepth            = 22;
    constexpr uint32_t HelperInvocation     = 23;
    constexpr uint32_t NumWorkgroups        = 24;
    constexpr uint32_t WorkgroupSize        = 25;
    constexpr uint32_t WorkgroupId          = 26;
    constexpr uint32_t LocalInvocationId    = 27;
    constexpr uint32_t GlobalInvocationId   = 28;
    constexpr uint32_t LocalInvocationIndex = 29;
    constexpr uint32_t VertexIndex          = 42;
    constexpr uint32_t InstanceIndex        = 43;
    constexpr uint32_t BaseVertex           = 4424;
    constexpr uint32_t BaseInstance         = 4425;
    constexpr uint32_t DrawIndex            = 4426;
    constexpr uint32_t DeviceIndex          = 4438;
}

} // namespace spirv

// Per-function lowering state.
struct SPIRVFunctionState {
    ir::Function* func = nullptr;
    uint32_t fnId = 0;
    uint32_t fnTypeId = 0;
    uint32_t returnTypeId = 0;
    bool isEntryPoint = false;
    uint32_t execModel = spirv::ExecModel::GLCompute;
    uint32_t localSize[3] = {1, 1, 1};
    bool hasLocalSize = false;
    std::map<const ir::BasicBlock*, uint32_t> labelIds;
    // Pre-scanned function-local storage: alloc instruction -> variable id.
    std::map<const ir::Instruction*, uint32_t> allocas;
    // Stack slots keyed by constant slot index -> variable id.
    std::map<int64_t, uint32_t> stackSlots;
    // Pending OpVariable emissions for the entry block.
    std::vector<std::pair<uint16_t, std::vector<uint32_t>>> pendingVariables;
    // Structured-control-flow analysis (computed lazily).
    bool cfgAnalyzed = false;
    std::map<const ir::BasicBlock*, const ir::BasicBlock*> mergeTarget; // header -> merge block (nullptr = sink)
    std::map<const ir::BasicBlock*, bool> isLoopHeader;
    std::map<const ir::BasicBlock*, const ir::BasicBlock*> loopLatch;   // header -> back-edge source
    const ir::BasicBlock* sinkBlock = nullptr;
    uint32_t sinkLabelId = 0;
};

class SPIRVArchitecture : public ArchitectureInfo {
public:
    SPIRVArchitecture();
    ~SPIRVArchitecture() override = default;

    Arch getArch() const override { return Arch::SPIRV; }
    size_t getPointerSize() const override { return 8; }
    size_t getStackAlignment() const override { return 16; }
    std::string getAssemblyFileExtension() const override { return ".spvasm"; }
    std::string getObjectFileExtension() const override { return ".spv"; }

    TypeInfo getTypeInfo(const ir::Type* type) const override;
    const std::vector<std::string>& getRegisters(RegisterClass regClass) const override;
    const std::string& getReturnRegister(const ir::Type* type) const override;
    const std::vector<std::string>& getIntegerArgumentRegisters() const override { return integerArgRegs; }
    const std::vector<std::string>& getFloatArgumentRegisters() const override { return floatArgRegs; }
    const std::string& getIntegerReturnRegister() const override { return intReturnReg; }
    const std::string& getFloatReturnRegister() const override { return floatReturnReg; }
    size_t getMaxRegistersForArgs() const override { return 0; }

    void emitHeader(codegen::CodeGen& cg) override;
    void emitFooter(codegen::CodeGen& cg) override;
    void emitFunctionPrologue(codegen::CodeGen& cg, ir::Function& func) override;
    void emitFunctionEpilogue(codegen::CodeGen& cg, ir::Function& func) override;
    void emitBasicBlockStart(codegen::CodeGen& cg, ir::BasicBlock& bb) override;

    void emitPassArgument(codegen::CodeGen& cg, size_t argIndex, const std::string& value, const ir::Type* type) override {}
    void emitGetArgument(codegen::CodeGen& cg, size_t argIndex, const std::string& dest, const ir::Type* type) override {}

    void emitRet(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitAdd(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitSub(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitMul(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitDiv(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitRem(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitAnd(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitOr(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitXor(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitShl(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitShr(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitSar(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitNeg(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitNot(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitCopy(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitCall(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitPhi(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitFAdd(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitFSub(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitFMul(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitFDiv(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitSMin(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitSMax(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitCmp(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitCast(codegen::CodeGen& cg, ir::Instruction& i, const ir::Type* from, const ir::Type* to) override;
    void emitVAStart(codegen::CodeGen& cg, ir::Instruction& i) override {}
    void emitVAArg(codegen::CodeGen& cg, ir::Instruction& i) override {}
    void emitLoad(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitStore(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitAlloc(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitBr(codegen::CodeGen& cg, ir::Instruction& i) override;
    void emitJmp(codegen::CodeGen& cg, ir::Instruction& i) override;

    void emitSyscall(codegen::CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) override;
    void emitExternCall(codegen::CodeGen& cg, ir::Instruction& i, const OperatingSystemInfo& osInfo) override;
    void emitNativeSyscall(codegen::CodeGen& cg, uint64_t syscallNum, const std::vector<ir::Value*>& args) override;
    void emitNativeLibraryCall(codegen::CodeGen& cg, const std::string& name, const std::vector<ir::Value*>& args) override;

    // --- Public configuration API ------------------------------------------
    // Marks a function as a shader entry point. Execution model defaults to
    // GLCompute; localSize applies to compute-style stages. Functions never
    // called by another function are automatically treated as entry points.
    void setEntryPoint(const std::string& functionName, uint32_t execModel,
                       uint32_t x = 1, uint32_t y = 1, uint32_t z = 1);
    void setEntryPoint(ir::Function* func, uint32_t execModel,
                       uint32_t x = 1, uint32_t y = 1, uint32_t z = 1);
    void setLocalSize(ir::Function* func, uint32_t x, uint32_t y, uint32_t z);

    // Declares a module-scope interface variable (e.g. stage input/output).
    // storageClass is a SpvStorageClass. decorations is a list of
    // (decoration, operands) pairs applied via OpDecorate.
    void addInterfaceVariable(const std::string& name, const ir::Type* valueType,
                              uint32_t storageClass,
                              const std::vector<std::pair<uint32_t, std::vector<uint32_t>>>& decorations = {});

    std::string formatStackOperand(int offset) const override { (void)offset; return ""; }
    std::string formatGlobalOperand(const std::string& name) const override { return name; }
    bool isCallerSaved(const std::string&) const override { return false; }
    bool isCalleeSaved(const std::string&) const override { return false; }

    spirv::SPIRVModule& getModule() { return module_; }
    const spirv::SPIRVModule& getModule() const { return module_; }

private:
    std::vector<std::string> integerRegs;
    std::vector<std::string> integerArgRegs;
    std::vector<std::string> floatArgRegs;
    std::string intReturnReg;
    std::string floatReturnReg;

    spirv::SPIRVModule module_;

    // Type interning: ir::Type* -> SPIR-V type id.
    std::map<const ir::Type*, uint32_t> typeIds_;
    // SSA value -> SPIR-V id (constants, params, instructions, functions).
    std::map<const ir::Value*, uint32_t> valueIds_;
    // Function -> lowering state (allocated lazily, filled by pre-pass).
    std::map<ir::Function*, SPIRVFunctionState> functions_;
    // Functions referenced by OpFunctionCall / declared externs.
    std::set<ir::Function*> calledFunctions_;
    // Explicit entry-point overrides keyed by function name.
    struct EntryConfig { uint32_t execModel; uint32_t size[3]; bool hasSize; };
    std::map<std::string, EntryConfig> entryConfigs_;
    // Mesh stage limits recovered from the manifest: name -> {maxVertices, maxPrimitives}.
    std::map<std::string, std::pair<uint32_t, uint32_t>> meshLimits_;
    bool meshExtRequested_ = false;
    // Stage markers declared via gpu.task/vertex/pixel/mesh capabilities and
    // their execution parameters (gpu.local.size / gpu.mesh.limits).
    std::map<std::string, uint32_t> stageMarkers_;
    std::map<std::string, std::array<uint32_t, 3>> stageLocalSize_;
    // Interface variables to declare (name -> pending variable spec).
    struct DecoSpec { uint32_t deco; std::vector<uint32_t> operands; };
    struct InterfaceVar { const ir::Type* type; uint32_t storage; std::vector<DecoSpec> decorations; };
    std::vector<std::pair<std::string, InterfaceVar>> interfaceVars_;
    // Ids of declared Input/Output interface variables, listed on entry points.
    std::vector<uint32_t> interfaceIds_;
    // GPU builtin interface variables (SpvBuiltIn -> OpVariable id). Created
    // during emitGlobals for every builtin referenced by a lymar_aot_gpu_*
    // extern call discovered in the pre-pass.
    std::map<uint32_t, uint32_t> builtinVarIds_;
    std::set<uint32_t> neededBuiltins_;
    // ivec3 IR type shared by gpu.* id builtins and their rewritten extern
    // result types (interned through getTypeId so all ids agree).
    ir::VectorType* gpuVec3Ty_ = nullptr;
    bool prepared_ = false;
    codegen::CodeGen* activeCg_ = nullptr;

    uint32_t voidTypeId_ = 0;
    uint32_t boolTypeId_ = 0;
    uint32_t i32TypeId_ = 0;
    uint32_t i64TypeId_ = 0;
    uint32_t f32TypeId_ = 0;
    uint32_t f64TypeId_ = 0;

    SPIRVFunctionState* current_ = nullptr;
    std::set<uint32_t> capabilitiesUsed_;

    void initRegisters();
    void diagnose(const std::string& msg);

    // Type system.
    uint32_t getTypeId(const ir::Type* type);
    uint32_t getPointerTypeId(const ir::Type* pointee, uint32_t storageClass);
    uint32_t getFunctionTypeId(ir::Function& func);
    void requireCapability(uint32_t cap);

    // Values / constants.
    uint32_t getOrCreateValueId(codegen::CodeGen& cg, ir::Value* val);
    uint32_t getConstantIntId(const ir::IntegerType* ty, uint64_t value, bool isSigned);
    uint32_t getConstantNullId(uint32_t typeId);

    // Emission helpers.
    uint32_t emitUnary(uint16_t opcode, uint32_t typeId, ir::Value* operand);
    uint32_t emitBinary(uint16_t opcode, uint32_t typeId, ir::Value* a, ir::Value* b);
    uint32_t emitResult(const ir::Instruction& i, uint16_t opcode,
                        const std::vector<uint32_t>& operands);
    uint32_t valueIdOf(ir::Value* v);
    uint32_t labelIdOf(const ir::BasicBlock* bb);
    bool isFloatTy(const ir::Type* t) const;
    bool isIntTy(const ir::Type* t) const;
    unsigned bitWidthOf(const ir::Type* t) const;

    // Control-flow structuring.
    void analyzeCFG(SPIRVFunctionState& st);
    void emitMergeAnnotation(SPIRVFunctionState& st, const ir::BasicBlock* bb);
    std::vector<const ir::BasicBlock*> successorsOf(const ir::BasicBlock* bb);
    std::map<const ir::BasicBlock*, std::set<const ir::BasicBlock*>> computePostDominators(SPIRVFunctionState& st);

    // Memory helpers.
    uint32_t pointerValueId(ir::Value* ptrVal, bool& ok);
    uint32_t slotVariableId(int64_t slot);
    void emitIntTrunc(codegen::CodeGen& cg, ir::Instruction& i, unsigned fromBits, unsigned toBits);
    void emitMinMax(codegen::CodeGen& cg, ir::Instruction& i, bool isMin, bool isSigned);
    void emitSelectToInt(codegen::CodeGen& cg, ir::Instruction& i, uint32_t boolId);

    // GPU builtins: uvec3-typed Input variables decorated with SpvBuiltIn.
    uint32_t getBuiltinVec3Type();
    uint32_t getBuiltinVariableId(uint32_t builtIn);
    // Native lowering for CapabilityDomain::GPU capabilities and the
    // lymar_aot_gpu_* compatibility aliases. Returns the result id (0 for
    // void), or 0 after diagnosing unsupported operations.
    uint32_t lowerGpuCapability(codegen::CodeGen& cg, ir::Instruction& i,
                                CapabilityId capId, const std::string& name);

    // Pre-pass: assigns function/type/label/alloca ids before emission.
    void prepareModule(codegen::CodeGen& cg);
    void prepareFunction(codegen::CodeGen& cg, ir::Function& func);
    void emitEntryPoints();
    void emitGlobals(codegen::CodeGen& cg);
    void emitInterfaceVariables();
};

} // namespace target
