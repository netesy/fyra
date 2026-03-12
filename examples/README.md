# Fyra Examples

## `example_phpish_oop_inmemory`

A tiny PHP-like toy language frontend that:

1. Parses a minimal class-based snippet (`class`, method, `new`, method call).
2. Lowers it to Fyra IR via `ir::IRBuilder`.
3. Generates an ELF directly from in-memory machine-code sections using `CodeGen` + `ElfGenerator::generateFromCode`.
4. Executes the generated executable and validates the result.

This example does **not** invoke gcc/clang.

Build and run:

```bash
cmake -S . -B build
cmake --build build --target example_phpish_oop_inmemory
./build/examples/example_phpish_oop_inmemory
```

## Frontend validation test

For production-readiness checks with multiple toy-language programs, run:

```bash
cmake --build build --target test_phpish_frontend
./build/tests/test_phpish_frontend
```
