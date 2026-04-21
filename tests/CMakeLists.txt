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
    add_executable(test_inmemory_aarch64_exec test_inmemory_aarch64_exec.cpp)
    target_link_libraries(test_inmemory_aarch64_exec PRIVATE fyra)

    add_test(NAME AArch64InMemoryExecTest COMMAND test_inmemory_aarch64_exec)
    set_tests_properties(AArch64InMemoryExecTest PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

if(FYRA_ENABLE_EXTENDED_TESTS)
    add_executable(test_benchmark_suite test_benchmark_suite.cpp)
    target_link_libraries(test_benchmark_suite PRIVATE fyra)

    add_test(NAME BenchmarkSuiteTest COMMAND test_benchmark_suite)
    set_tests_properties(BenchmarkSuiteTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

if(FYRA_ENABLE_EXTENDED_TESTS)
    add_executable(test_sqlite_clone test_sqlite_clone.cpp)
    target_link_libraries(test_sqlite_clone PRIVATE fyra)

    add_test(NAME SqliteCloneTest COMMAND test_sqlite_clone)
    set_tests_properties(SqliteCloneTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

add_executable(test_http_server test_http_server.cpp)
target_link_libraries(test_http_server PRIVATE fyra)

add_test(NAME HttpServerTest COMMAND test_http_server)
set_tests_properties(HttpServerTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
if(FYRA_ENABLE_EXTENDED_TESTS)
    add_executable(test_phpish_frontend test_phpish_frontend.cpp)
    target_link_libraries(test_phpish_frontend PRIVATE fyra)

    add_test(NAME PhpishFrontendTest COMMAND test_phpish_frontend)
    set_tests_properties(PhpishFrontendTest PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

    add_executable(test_voilet_frontend test_voilet_frontend.cpp)
    target_link_libraries(test_voilet_frontend PRIVATE fyra)

    add_test(NAME VoiletFrontendTest COMMAND test_voilet_frontend)
    set_tests_properties(VoiletFrontendTest PROPERTIES WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

add_executable(test_pe_generator test_pe_generator.cpp)
target_link_libraries(test_pe_generator PRIVATE fyra)

add_test(NAME PEGeneratorTest COMMAND test_pe_generator)
set_tests_properties(PEGeneratorTest PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

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
