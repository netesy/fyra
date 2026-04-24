set(TARGET_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/core/TargetInfo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/CompositeTargetInfo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/ArchitectureInfo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/TargetDescriptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/TargetResolver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/architecture/x64/X64Architecture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/architecture/aarch64/AArch64Architecture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/architecture/riscv64/RiscV64Architecture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/architecture/wasm32/Wasm32Architecture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/os/linux/LinuxOS.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/os/windows/WindowsOS.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/os/macos/MacOSOS.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/os/wasi/WASIOS.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/capabilities/CapabilityRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/artifact/apk/APKArtifact.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/artifact/apk/APKPackager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/artifact/apk/JavaStubGen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/artifact/apk/ManifestGen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/artifact/executable/binary_writer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/artifact/executable/elf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/artifact/executable/macho.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/artifact/executable/pe.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/artifact/executable/platform_utils.cpp
)

target_sources(fyra PRIVATE ${TARGET_SOURCES})
