#!/bin/bash

# Test script for running Fyra-generated PE executables using Wine
# Usage: ./test_pe_wine.sh <input.fyra> [target]

INPUT_FILE=$1
TARGET=${2:-windows}
OUTPUT_EXE="test_output.exe"

if [ -z "$INPUT_FILE" ]; then
    echo "Usage: $0 <input.fyra> [target]"
    exit 1
fi

# Build the compiler
make build/bin/fyra_compiler || exit 1

# Compile to PE
./build/bin/fyra_compiler "$INPUT_FILE" -o "$OUTPUT_EXE" --target "$TARGET" --gen-exec --enhanced || exit 1

# Check if wine is installed
if command -v wine &> /dev/null; then
    echo "Running $OUTPUT_EXE with wine..."
    wine "$OUTPUT_EXE"
    EXIT_CODE=$?
    echo "Wine exited with code $EXIT_CODE"
else
    echo "Wine not found. Structural verification only:"
    objdump -h "$OUTPUT_EXE"
fi
