cmake_minimum_required(VERSION 3.10)
project(fyra CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(include src)

# Add the library
add_library(fyra "")

# Add subdirectories for modular organization
add_subdirectory(src/ir)
add_subdirectory(src/parser)
add_subdirectory(src/transforms)
add_subdirectory(src/codegen)
add_subdirectory(src/target)

# --- Compiler Executable ---
add_executable(fyra_compiler main.cpp)
target_link_libraries(fyra_compiler fyra)

# --- Testing ---
enable_testing()
add_subdirectory(tests)

# --- Examples ---
add_subdirectory(examples)
