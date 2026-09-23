set(FYRA_FACADE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/CompilerPipeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BackendBuilder.cpp
)

target_sources(fyra PRIVATE ${FYRA_FACADE_SOURCES})
