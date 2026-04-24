set(IR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Type.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Value.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/User.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Use.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Constant.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Instruction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SIMDInstruction.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GlobalVariable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Parameter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PhiNode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BasicBlock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Function.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Module.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/IRContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FunctionType.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/IRBuilder.cpp
)

target_sources(fyra PRIVATE ${IR_SOURCES})
