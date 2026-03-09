# Makefile for building and testing the fyra project with GCC
# Replaces CMake build system with direct GCC compilation

# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g -MMD -MP
INCLUDES = -Iinclude -Isrc
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin
TEST_DIR = $(BUILD_DIR)/tests

# Library source files
LIB_SOURCES = $(shell find src -name "*.cpp")

# Test source files
TEST_SOURCES = $(shell find tests -name "*.cpp")

# Object files
LIB_OBJECTS = $(LIB_SOURCES:%.cpp=$(OBJ_DIR)/%.o)

# Library and executables
FYRA_LIB = $(BUILD_DIR)/libfyra.a
FYRA_COMPILER = $(BIN_DIR)/fyra_compiler

# Test names for individual targets
TEST_NAMES = \
    parser codegen add sub mul div windows aarch64 riscv64 wasm32 ssa float \
    functions control_flow comprehensive copy_elimination wasm_inmemory \
    inmemory_aarch64_exec benchmark_suite sqlite_clone http_server \
    simple_wasm fibonacci_wasm

TEST_EXECUTABLES = \
    $(TEST_DIR)/test_parser \
    $(TEST_DIR)/test_codegen \
    $(TEST_DIR)/test_add \
    $(TEST_DIR)/test_sub \
    $(TEST_DIR)/test_mul \
    $(TEST_DIR)/test_div \
    $(TEST_DIR)/test_windows \
    $(TEST_DIR)/test_aarch64 \
    $(TEST_DIR)/test_riscv64 \
    $(TEST_DIR)/test_wasm32 \
    $(TEST_DIR)/test_ssa \
    $(TEST_DIR)/test_float \
    $(TEST_DIR)/test_functions \
    $(TEST_DIR)/test_control_flow \
    $(TEST_DIR)/test_comprehensive \
    $(TEST_DIR)/test_copy_elimination \
    $(TEST_DIR)/test_wasm_inmemory \
    $(TEST_DIR)/test_inmemory_aarch64_exec \
    $(TEST_DIR)/test_benchmark_suite \
    $(TEST_DIR)/test_sqlite_clone \
    $(TEST_DIR)/test_http_server \
    $(TEST_DIR)/test_simple_wasm \
    $(TEST_DIR)/test_fibonacci_wasm

# Default target
.PHONY: all
all: $(FYRA_COMPILER) tests

# Create directories
$(OBJ_DIR) $(BIN_DIR) $(TEST_DIR):
	mkdir -p $@

# Compile library source files
$(OBJ_DIR)/src/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Create static library
$(FYRA_LIB): $(LIB_OBJECTS) | $(BUILD_DIR)
	ar rcs $@ $^

# Include dependency files
-include $(LIB_OBJECTS:.o=.d)
-include $(TEST_EXECUTABLES:%=%.d)

# Build main compiler executable
$(FYRA_COMPILER): main.cpp $(FYRA_LIB) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -L$(BUILD_DIR) -lfyra -o $@

# Test executable build rules (Pattern Rule)
$(TEST_DIR)/test_%: $(OBJ_DIR)/tests/test_%.o $(FYRA_LIB) | $(TEST_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -L$(BUILD_DIR) -lfyra -o $@

# Exceptions for tests with unusual names or subdirectories
$(TEST_DIR)/test_wasm_inmemory: $(OBJ_DIR)/tests/CodeGen/wasm_inmemory_test.o $(FYRA_LIB) | $(TEST_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -L$(BUILD_DIR) -lfyra -o $@

$(TEST_DIR)/test_simple_wasm: $(OBJ_DIR)/tests/simple_wasm_test.o $(FYRA_LIB) | $(TEST_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -L$(BUILD_DIR) -lfyra -o $@

$(TEST_DIR)/test_fibonacci_wasm: $(OBJ_DIR)/tests/fibonacci_wasm_test.o $(FYRA_LIB) | $(TEST_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -L$(BUILD_DIR) -lfyra -o $@

# Special case for comprehensive and copy elimination tests (need TEST_FILE definition)
$(OBJ_DIR)/tests/test_comprehensive.o: tests/test_comprehensive.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DTEST_FILE=\"tests/comprehensive.fyra\" -c $< -o $@

$(OBJ_DIR)/tests/test_copy_elimination.o: tests/test_copy_elimination.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DTEST_FILE=\"tests/copy_elimination.fyra\" -c $< -o $@

# Special case for wasm inmemory test
$(OBJ_DIR)/tests/CodeGen/wasm_inmemory_test.o: tests/CodeGen/wasm_inmemory_test.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DTEST_SOURCE_DIR=\".\" -c $< -o $@

# Build targets
.PHONY: build library compiler tests
build: $(FYRA_COMPILER)
library: $(FYRA_LIB)
compiler: $(FYRA_COMPILER)
tests: $(TEST_EXECUTABLES)

# Test targets
.PHONY: test run-tests
test: run-tests
run-tests: $(TEST_EXECUTABLES)
	@echo "Running all tests..."
	@failed=""; \
	for test in $(TEST_EXECUTABLES); do \
		echo "Running $$test..."; \
		$$test || failed="$$failed $$test"; \
	done; \
	if [ -n "$$failed" ]; then \
		echo "The following tests failed:$$failed"; \
		exit 1; \
	fi; \
	echo "All tests passed!"

# Clean targets
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

# Help target
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  all                   - Build the compiler and tests (default)"
	@echo "  build                 - Build the compiler"
	@echo "  library               - Build only the fyra library"
	@echo "  compiler              - Build only the compiler executable"
	@echo "  tests                 - Build all test executables"
	@echo "  test / run-tests      - Build and run all tests"
	@echo "  test-<name>           - Run specific test (e.g. make test-parser)"
	@echo "  clean                 - Clean all build files"
	@echo "  help                  - Show this help message"

# Individual test convenience targets
define TEST_RULE
test-$(1): $(TEST_DIR)/test_$(1)
	./$(TEST_DIR)/test_$(1)
endef

test-parser: $(TEST_DIR)/test_parser
	./$(TEST_DIR)/test_parser
test-codegen: $(TEST_DIR)/test_codegen
	./$(TEST_DIR)/test_codegen
test-add: $(TEST_DIR)/test_add
	./$(TEST_DIR)/test_add
test-sub: $(TEST_DIR)/test_sub
	./$(TEST_DIR)/test_sub
test-mul: $(TEST_DIR)/test_mul
	./$(TEST_DIR)/test_mul
test-div: $(TEST_DIR)/test_div
	./$(TEST_DIR)/test_div
test-windows: $(TEST_DIR)/test_windows
	./$(TEST_DIR)/test_windows
test-aarch64: $(TEST_DIR)/test_aarch64
	./$(TEST_DIR)/test_aarch64
test-riscv64: $(TEST_DIR)/test_riscv64
	./$(TEST_DIR)/test_riscv64
test-wasm32: $(TEST_DIR)/test_wasm32
	./$(TEST_DIR)/test_wasm32
test-ssa: $(TEST_DIR)/test_ssa
	./$(TEST_DIR)/test_ssa
test-float: $(TEST_DIR)/test_float
	./$(TEST_DIR)/test_float
test-functions: $(TEST_DIR)/test_functions
	./$(TEST_DIR)/test_functions
test-control-flow: $(TEST_DIR)/test_control_flow
	./$(TEST_DIR)/test_control_flow
test-comprehensive: $(TEST_DIR)/test_comprehensive
	./$(TEST_DIR)/test_comprehensive
test-copy-elimination: $(TEST_DIR)/test_copy_elimination
	./$(TEST_DIR)/test_copy_elimination
test-wasm_inmemory: $(TEST_DIR)/test_wasm_inmemory
	./$(TEST_DIR)/test_wasm_inmemory
test-inmemory_aarch64_exec: $(TEST_DIR)/test_inmemory_aarch64_exec
	./$(TEST_DIR)/test_inmemory_aarch64_exec
test-benchmark_suite: $(TEST_DIR)/test_benchmark_suite
	./$(TEST_DIR)/test_benchmark_suite
test-sqlite_clone: $(TEST_DIR)/test_sqlite_clone
	./$(TEST_DIR)/test_sqlite_clone
test-http_server: $(TEST_DIR)/test_http_server
	./$(TEST_DIR)/test_http_server
test-simple_wasm: $(TEST_DIR)/test_simple_wasm
	./$(TEST_DIR)/test_simple_wasm
test-fibonacci_wasm: $(TEST_DIR)/test_fibonacci_wasm
	./$(TEST_DIR)/test_fibonacci_wasm
