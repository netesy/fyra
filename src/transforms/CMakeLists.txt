set(TRANSFORMS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/CFGBuilder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DominatorTree.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DominanceFrontier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PhiInsertion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SSARenamer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Mem2Reg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CopyElimination.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GVN.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DeadInstructionElimination.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SCCP.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LoopInvariantCodeMotion.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ControlFlowSimplification.cpp
)

target_sources(fyra PRIVATE ${TRANSFORMS_SOURCES})
