set(CODEGEN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/CodeGen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/validation/TargetABIRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/validation/ASMValidator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/validation/Validators.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/objectgen/ObjectFileGenerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/objectgen/PlatformGenerators.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/optimize/InstructionFusion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/optimize/FusionCoordinator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/DWARFGenerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/DebugInfoManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/profiling/RuntimeProfiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/regalloc/LivenessAnalysis.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/regalloc/LiveIntervalAnalysis.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/regalloc/LinearScanAllocator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/regalloc/RegAllocRewriter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/abi/ABIAnalysis.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/asm/Assembler.cpp
)

target_sources(fyra PRIVATE ${CODEGEN_SOURCES})
