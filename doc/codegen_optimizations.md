# Fyra Codegen Optimization Notes

This document summarizes optimization behavior currently implemented in the compiler and code generator.

## Optimization levels

- `-O0`: disables optimization passes.
- `-O1`: runs conservative cleanup (SCCP, copy elimination, GVN, CFG simplification, DCE).
- `-O2` (default): runs fixed-point iterations of the optimization pipeline and enables aggressive loop optimizations where target support is stable.

## Instruction fusion

Fyra now performs a target-aware compare+branch fusion:

- Detects `cmp-result` immediately consumed by a conditional branch (`Br`/`Jnz`).
- Emits target-native compare + conditional jump directly, skipping temporary boolean materialization.

Currently implemented targets:

- `SystemV_x64` (textual + binary emission)
- `Windows_x64` (textual emission)

This reduces redundant instruction sequences such as:

1. compare
2. set condition into temporary value
3. compare temporary with zero
4. branch

into:

1. compare operands
2. branch on flags

## Notes

- Binary fusion is enabled for `SystemV_x64` integer compare-and-branch patterns.
- Other targets keep binary emission on the non-fused path for safety until dedicated encoders are added.

## Wasm32 textual output

- Wasm32 textual output now stays in WAT/module form and avoids non-Wasm directives such as `.text`, `.globl`, and assembly-style block labels.
- The Wasm path is emitted through the Wasm target flow directly, so generated `.wat` files remain Wasm-oriented.
