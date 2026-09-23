option(FYRA_ENABLE_EXTENDED_TESTS "Enable extended integration tests that are currently unstable in CI" OFF)

add_executable(test_parser test_parser.cpp)
target_link_libraries(test_parser PRIVATE fyra)

add_test(NAME ParserTest COMMAND test_parser)
set_tests_properties(ParserTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_codegen test_codegen.cpp)
target_link_libraries(test_codegen PRIVATE fyra)

add_test(NAME CodeGenTest COMMAND test_codegen)
set_tests_properties(CodeGenTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_add test_add.cpp)
target_link_libraries(test_add PRIVATE fyra)

add_test(NAME AddTest COMMAND test_add)
set_tests_properties(AddTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_sub test_sub.cpp)
target_link_libraries(test_sub PRIVATE fyra)

add_test(NAME SubTest COMMAND test_sub)
set_tests_properties(SubTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_mul test_mul.cpp)
target_link_libraries(test_mul PRIVATE fyra)

add_test(NAME MulTest COMMAND test_mul)
set_tests_properties(MulTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_div test_div.cpp)
target_link_libraries(test_div PRIVATE fyra)

add_test(NAME DivTest COMMAND test_div)
set_tests_properties(DivTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_windows test_windows.cpp)
target_link_libraries(test_windows PRIVATE fyra)

add_test(NAME WindowsTest COMMAND test_windows)
set_tests_properties(WindowsTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# add_executable(test_windows_arm64 test_windows_arm64.cpp)
# target_link_libraries(test_windows_arm64 PRIVATE fyra)

# add_test(NAME WindowsArm64Test COMMAND test_windows_arm64)
# set_tests_properties(WindowsArm64Test PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# add_executable(test_windows_amd64 test_windows_amd64.cpp)
# target_link_libraries(test_windows_amd64 PRIVATE fyra)

# add_test(NAME WindowsAmd64Test COMMAND test_windows_amd64)
# set_tests_properties(WindowsAmd64Test PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_aarch64 test_aarch64.cpp)
target_link_libraries(test_aarch64 PRIVATE fyra)

add_test(NAME AArch64Test COMMAND test_aarch64)
set_tests_properties(AArch64Test PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_riscv64 test_riscv64.cpp)
target_link_libraries(test_riscv64 PRIVATE fyra)

add_test(NAME RiscV64Test COMMAND test_riscv64)
set_tests_properties(RiscV64Test PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

if(FYRA_ENABLE_EXTENDED_TESTS)
    add_executable(test_wasm32 test_wasm32.cpp)
    target_link_libraries(test_wasm32 PRIVATE fyra)

    add_test(NAME Wasm32Test COMMAND test_wasm32)
    set_tests_properties(Wasm32Test PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

add_executable(test_ssa test_ssa.cpp)
target_link_libraries(test_ssa PRIVATE fyra)

add_test(NAME SSATest COMMAND test_ssa)
set_tests_properties(SSATest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_float test_float.cpp)
target_link_libraries(test_float PRIVATE fyra)

add_test(NAME FloatTest COMMAND test_float)
set_tests_properties(FloatTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_functions test_functions.cpp)
target_link_libraries(test_functions PRIVATE fyra)

add_test(NAME FunctionsTest COMMAND test_functions)
set_tests_properties(FunctionsTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_control_flow test_control_flow.cpp)
target_link_libraries(test_control_flow PRIVATE fyra)

add_test(NAME ControlFlowTest COMMAND test_control_flow)
set_tests_properties(ControlFlowTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_comprehensive test_comprehensive.cpp)
target_link_libraries(test_comprehensive PRIVATE fyra)
target_compile_definitions(test_comprehensive PRIVATE TEST_FILE="${CMAKE_CURRENT_SOURCE_DIR}/comprehensive.fyra")

add_test(NAME ComprehensiveParserTest COMMAND test_comprehensive)
set_tests_properties(ComprehensiveParserTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

if(FYRA_ENABLE_EXTENDED_TESTS)
    add_executable(test_copy_elimination test_copy_elimination.cpp)
    target_link_libraries(test_copy_elimination PRIVATE fyra)
    target_compile_definitions(test_copy_elimination PRIVATE TEST_FILE="${CMAKE_CURRENT_SOURCE_DIR}/copy_elimination.fyra")

    add_test(NAME CopyEliminationTest COMMAND test_copy_elimination)
endif()

add_executable(test_wasm_inmemory CodeGen/wasm_inmemory_test.cpp)
target_link_libraries(test_wasm_inmemory PRIVATE fyra)
target_compile_definitions(test_wasm_inmemory PRIVATE TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

add_test(NAME WasmInMemoryTest COMMAND test_wasm_inmemory)
set_tests_properties(WasmInMemoryTest PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

if(FYRA_ENABLE_EXTENDED_TESTS)
    add_executable(fibonacci_wasm_test fibonacci_wasm_test.cpp)
    target_link_libraries(fibonacci_wasm_test PRIVATE fyra)

    add_test(NAME FibonacciWasmTest COMMAND fibonacci_wasm_test)
    set_tests_properties(FibonacciWasmTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

add_executable(simple_wasm_test simple_wasm_test.cpp)
target_link_libraries(simple_wasm_test PRIVATE fyra)

add_test(NAME SimpleWasmTest COMMAND simple_wasm_test)
set_tests_properties(SimpleWasmTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_wasm_execution test_wasm_execution.cpp)
target_link_libraries(test_wasm_execution PRIVATE fyra)

add_test(NAME WasmExecutionTest COMMAND test_wasm_execution)
set_tests_properties(WasmExecutionTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# add_executable(test_linux_exec test_linux_exec.cpp)
# target_link_libraries(test_linux_exec PRIVATE fyra)

# add_test(NAME LinuxExecTest COMMAND test_linux_exec)
# set_tests_properties(LinuxExecTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# add_executable(test_inmemory_linux_exec test_inmemory_linux_exec.cpp)
# target_link_libraries(test_inmemory_linux_exec PRIVATE fyra)

# add_test(NAME LinuxInMemoryExecTest COMMAND test_inmemory_linux_exec)
# set_tests_properties(LinuxInMemoryExecTest PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# add_executable(test_inmemory_riscv_exec test_inmemory_riscv_exec.cpp)
# target_link_libraries(test_inmemory_riscv_exec PRIVATE fyra)

# add_test(NAME RiscVInMemoryExecTest COMMAND test_inmemory_riscv_exec)
# set_tests_properties(RiscVInMemoryExecTest PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

if(FYRA_ENABLE_EXTENDED_TESTS)
    add_executable(test_benchmark_suite test_benchmark_suite.cpp)
    target_link_libraries(test_benchmark_suite PRIVATE fyra)

    add_test(NAME BenchmarkSuiteTest COMMAND test_benchmark_suite)
    set_tests_properties(BenchmarkSuiteTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

add_executable(test_http_server test_http_server.cpp)
target_link_libraries(test_http_server PRIVATE fyra)

add_test(NAME HttpServerTest COMMAND test_http_server)
set_tests_properties(HttpServerTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
if(FYRA_ENABLE_EXTENDED_TESTS)
    add_executable(test_sqlite_clone test_sqlite_clone.cpp)
    target_link_libraries(test_sqlite_clone PRIVATE fyra)
    add_test(NAME SqliteCloneTest COMMAND test_sqlite_clone)
    set_tests_properties(SqliteCloneTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_executable(test_phpish_frontend test_phpish_frontend.cpp)
    target_link_libraries(test_phpish_frontend PRIVATE fyra)
    add_test(NAME PhpishFrontendTest COMMAND test_phpish_frontend)
    set_tests_properties(PhpishFrontendTest PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

    add_executable(test_voilet_frontend test_voilet_frontend.cpp)
    target_link_libraries(test_voilet_frontend PRIVATE fyra)

    add_test(NAME VoiletFrontendTest COMMAND test_voilet_frontend)
    set_tests_properties(VoiletFrontendTest PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

add_executable(test_elf_image test_elf_image.cpp)
target_link_libraries(test_elf_image PRIVATE fyra)

add_test(NAME ELFImageTest COMMAND test_elf_image)
set_tests_properties(ELFImageTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_extern test_extern.cpp)
target_link_libraries(test_extern PRIVATE fyra)

add_test(NAME ExternTest COMMAND test_extern)
set_tests_properties(ExternTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_capability_compliance test_capability_compliance.cpp)
target_link_libraries(test_capability_compliance PRIVATE fyra)

add_test(NAME CapabilityComplianceTest COMMAND test_capability_compliance)
set_tests_properties(CapabilityComplianceTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_windows_capability_compliance test_windows_capability_compliance.cpp)
target_link_libraries(test_windows_capability_compliance PRIVATE fyra)

add_test(NAME WindowsCapabilityComplianceTest COMMAND test_windows_capability_compliance)
set_tests_properties(WindowsCapabilityComplianceTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_windows_aarch64_capability_compliance test_windows_aarch64_capability_compliance.cpp)
target_link_libraries(test_windows_aarch64_capability_compliance PRIVATE fyra)

add_test(NAME WindowsAArch64CapabilityComplianceTest COMMAND test_windows_aarch64_capability_compliance)
set_tests_properties(WindowsAArch64CapabilityComplianceTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_aarch64_capability_compliance test_aarch64_capability_compliance.cpp)
target_link_libraries(test_aarch64_capability_compliance PRIVATE fyra)

add_test(NAME AArch64CapabilityComplianceTest COMMAND test_aarch64_capability_compliance)
set_tests_properties(AArch64CapabilityComplianceTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_macos_aarch64_capability_compliance test_macos_aarch64_capability_compliance.cpp)
target_link_libraries(test_macos_aarch64_capability_compliance PRIVATE fyra)

add_test(NAME MacOSAArch64CapabilityComplianceTest COMMAND test_macos_aarch64_capability_compliance)
set_tests_properties(MacOSAArch64CapabilityComplianceTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_sqlite_emulation test_sqlite_emulation.cpp)
target_link_libraries(test_sqlite_emulation PRIVATE fyra)
add_test(NAME SqliteEmulationTest COMMAND test_sqlite_emulation)

add_executable(test_portable_capabilities test_portable_capabilities.cpp)
target_link_libraries(test_portable_capabilities PRIVATE fyra)
add_test(NAME PortableCapabilitiesTest COMMAND test_portable_capabilities)

add_executable(test_licm test_licm.cpp)
target_link_libraries(test_licm PRIVATE fyra)
add_test(NAME LICMTest COMMAND test_licm)

add_executable(test_macho_image test_macho_image.cpp)
target_link_libraries(test_macho_image PRIVATE fyra)
add_test(NAME MachOImageTest COMMAND test_macho_image)

add_executable(test_pe_import test_pe_import.cpp)
target_link_libraries(test_pe_import PRIVATE fyra)
add_test(NAME PeImportTest COMMAND test_pe_import)

add_executable(test_pe_reloc test_pe_reloc.cpp)
target_link_libraries(test_pe_reloc PRIVATE fyra)
add_test(NAME PeRelocTest COMMAND test_pe_reloc)

add_executable(test_pe_import_complete test_pe_import_complete.cpp)
target_link_libraries(test_pe_import_complete PRIVATE fyra)
add_test(NAME PeImportCompleteTest COMMAND test_pe_import_complete)
set_tests_properties(LICMTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_inliner test_inliner.cpp)
target_link_libraries(test_inliner PRIVATE fyra)
add_test(NAME InlinerTest COMMAND test_inliner)
set_tests_properties(InlinerTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_gvn test_gvn.cpp)
target_link_libraries(test_gvn PRIVATE fyra)
add_test(NAME GVNTest COMMAND test_gvn)
set_tests_properties(GVNTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_composition test_composition.cpp)
target_link_libraries(test_composition PRIVATE fyra)
add_test(NAME CompositionTest COMMAND test_composition)
set_tests_properties(CompositionTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_scev test_scev.cpp)
target_link_libraries(test_scev PRIVATE fyra)
add_test(NAME SCEVTest COMMAND test_scev)
set_tests_properties(SCEVTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_closed_form_reduction test_closed_form_reduction.cpp)
target_link_libraries(test_closed_form_reduction PRIVATE fyra)
add_test(NAME ClosedFormReductionTest COMMAND test_closed_form_reduction)
set_tests_properties(ClosedFormReductionTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_sccp_interprocedural test_sccp_interprocedural.cpp)
target_link_libraries(test_sccp_interprocedural PRIVATE fyra)
add_test(NAME SCCPInterproceduralTest COMMAND test_sccp_interprocedural)

add_executable(test_div_strength_reduction test_div_strength_reduction.cpp)
target_link_libraries(test_div_strength_reduction PRIVATE fyra)
add_test(NAME DivisionStrengthReductionTest COMMAND test_div_strength_reduction)
set_tests_properties(SCCPInterproceduralTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_x64_constant_shifts test_x64_constant_shifts.cpp)
target_link_libraries(test_x64_constant_shifts PRIVATE fyra)
add_test(NAME X64ConstantShiftsTest COMMAND test_x64_constant_shifts)

add_executable(test_loop_unroll test_loop_unroll.cpp)
target_link_libraries(test_loop_unroll PRIVATE fyra)
add_test(NAME LoopUnrollTest COMMAND test_loop_unroll)
set_tests_properties(LoopUnrollTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_simd_stubs test_simd_stubs.cpp)
target_link_libraries(test_simd_stubs PRIVATE fyra)
add_test(NAME SIMDStubsTest COMMAND test_simd_stubs)
set_tests_properties(SIMDStubsTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_wasm_simd_proof test_wasm_simd_proof.cpp)
target_link_libraries(test_wasm_simd_proof PRIVATE fyra)
add_test(NAME WasmSIMDProofTest COMMAND test_wasm_simd_proof)
set_tests_properties(WasmSIMDProofTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_simd_exec test_simd_exec.cpp)
target_link_libraries(test_simd_exec PRIVATE fyra)
add_test(NAME SIMDTest COMMAND test_simd_exec)
set_tests_properties(SIMDTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_vshuffle_prerequisites test_vshuffle_prerequisites.cpp)
target_link_libraries(test_vshuffle_prerequisites PRIVATE fyra)
add_test(NAME VShufflePrerequisitesTest COMMAND test_vshuffle_prerequisites)

add_executable(test_vector_folding_and_spill test_vector_folding_and_spill.cpp)
target_link_libraries(test_vector_folding_and_spill PRIVATE fyra)
add_test(NAME VectorFoldingAndSpillTest COMMAND test_vector_folding_and_spill)
set_tests_properties(VectorFoldingAndSpillTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_fma_gather_egraph test_fma_gather_egraph.cpp)
target_link_libraries(test_fma_gather_egraph PRIVATE fyra)
add_test(NAME FMAGatherEGraphTest COMMAND test_fma_gather_egraph)
set_tests_properties(FMAGatherEGraphTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_loop_vectorizer test_loop_vectorizer.cpp)
target_link_libraries(test_loop_vectorizer PRIVATE fyra)
add_test(NAME LoopVectorizerTest COMMAND test_loop_vectorizer)
set_tests_properties(LoopVectorizerTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_auto_vec_array test_auto_vec_array.cpp)
target_link_libraries(test_auto_vec_array PRIVATE fyra)
add_test(NAME AutoVecArrayTest COMMAND test_auto_vec_array)
set_tests_properties(AutoVecArrayTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_auto_vec_widening test_auto_vec_widening.cpp)
target_link_libraries(test_auto_vec_widening PRIVATE fyra)
add_test(NAME AutoVecWideningTest COMMAND test_auto_vec_widening)
set_tests_properties(AutoVecWideningTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_auto_vec_properties test_auto_vec_properties.cpp)
target_link_libraries(test_auto_vec_properties PRIVATE fyra)
add_test(NAME AutoVecPropertiesTest COMMAND test_auto_vec_properties)
set_tests_properties(AutoVecPropertiesTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_auto_vec_predication test_auto_vec_predication.cpp)
target_link_libraries(test_auto_vec_predication PRIVATE fyra)
add_test(NAME AutoVecPredicationTest COMMAND test_auto_vec_predication)
set_tests_properties(AutoVecPredicationTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_archive test_archive.cpp)
target_link_libraries(test_archive PRIVATE fyra)
add_test(NAME ArchiveTest COMMAND test_archive)
set_tests_properties(ArchiveTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_object test_object.cpp)
target_link_libraries(test_object PRIVATE fyra)
add_test(NAME ObjectTest COMMAND test_object)
set_tests_properties(ObjectTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_linker test_linker.cpp)
target_link_libraries(test_linker PRIVATE fyra)
add_test(NAME LinkerTest COMMAND test_linker)
set_tests_properties(LinkerTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_auto_vec_float test_auto_vec_float.cpp)
target_link_libraries(test_auto_vec_float PRIVATE fyra)
add_test(NAME AutoVecFloatTest COMMAND test_auto_vec_float)
set_tests_properties(AutoVecFloatTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_auto_vec_reduction test_auto_vec_reduction.cpp)
target_link_libraries(test_auto_vec_reduction PRIVATE fyra)
add_test(NAME AutoVecReductionTest COMMAND test_auto_vec_reduction)
set_tests_properties(AutoVecReductionTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_auto_vec_alias test_auto_vec_alias.cpp)
target_link_libraries(test_auto_vec_alias PRIVATE fyra)
add_test(NAME AutoVecAliasTest COMMAND test_auto_vec_alias)
set_tests_properties(AutoVecAliasTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_auto_vec_slp test_auto_vec_slp.cpp)
target_link_libraries(test_auto_vec_slp PRIVATE fyra)
add_test(NAME AutoVecSLPTest COMMAND test_auto_vec_slp)
set_tests_properties(AutoVecSLPTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
