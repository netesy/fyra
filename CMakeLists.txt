cmake_minimum_required(VERSION 3.10)
project(fyra CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(include src)

# Library source files
set(LIB_SOURCES
    src/ir/Type.cpp
    src/ir/Value.cpp
    src/ir/User.cpp
    src/ir/Use.cpp
    src/ir/Constant.cpp
    src/ir/Instruction.cpp
    src/ir/SIMDInstruction.cpp
    src/ir/GlobalVariable.cpp
    src/ir/Parameter.cpp
    src/ir/PhiNode.cpp
    src/ir/BasicBlock.cpp
    src/ir/Function.cpp
    src/ir/Module.cpp
    src/ir/FunctionType.cpp
    src/ir/IRBuilder.cpp
    src/parser/Lexer.cpp
    src/parser/Parser.cpp
    src/codegen/CodeGen.cpp
    src/codegen/EnhancedCodeGen.cpp
    src/codegen/validation/TargetABIRegistry.cpp
    src/codegen/validation/ASMValidator.cpp
    src/codegen/validation/Validators.cpp
    src/codegen/objectgen/ObjectFileGenerator.cpp
    src/codegen/objectgen/PlatformGenerators.cpp
    src/codegen/InstructionFusion.cpp
    src/codegen/debug/DWARFGenerator.cpp
    src/codegen/debug/DebugInfoManager.cpp
    src/codegen/target/FusionCoordinator.cpp
    src/codegen/profiling/RuntimeProfiler.cpp
    src/transforms/CFGBuilder.cpp
    src/transforms/DominatorTree.cpp
    src/transforms/DominanceFrontier.cpp
    src/transforms/PhiInsertion.cpp
    src/transforms/SSARenamer.cpp
    src/transforms/Mem2Reg.cpp
    src/transforms/CopyElimination.cpp
    src/transforms/GVN.cpp
    src/transforms/DeadInstructionElimination.cpp
    src/transforms/LivenessAnalysis.cpp
    src/transforms/LiveIntervalAnalysis.cpp
    src/transforms/LinearScanAllocator.cpp
    src/transforms/RegAllocRewriter.cpp
    src/transforms/SCCP.cpp
    src/transforms/LoopInvariantCodeMotion.cpp
    src/transforms/ControlFlowSimplification.cpp
    src/transforms/ABIAnalysis.cpp
    src/codegen/target/TargetInfo.cpp
    src/codegen/target/X86_64Base.cpp
    src/codegen/target/SystemV_x64.cpp
    src/codegen/target/Windows_x64.cpp
    src/codegen/target/Windows_AArch64.cpp
    src/codegen/target/MacOS_x64.cpp
    src/codegen/target/MacOS_AArch64.cpp
    src/codegen/target/AArch64.cpp
    src/codegen/target/Wasm32.cpp
    src/codegen/target/RiscV64.cpp
    src/codegen/execgen/binary_writer.cpp
    src/codegen/execgen/elf.cpp
    src/codegen/execgen/macho.cpp
    src/codegen/execgen/Assembler.cpp
    src/codegen/execgen/pe.cpp
    src/codegen/execgen/platform_utils.cpp
)

# Add the library
add_library(fyra ${LIB_SOURCES})

# --- Compiler Executable ---
add_executable(fyra_compiler main.cpp)
target_link_libraries(fyra_compiler fyra)


# --- Testing ---
enable_testing()
add_subdirectory(tests)

# --- Examples ---
add_subdirectory(examples)
