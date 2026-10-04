# Gate A correctness reproducers

`aliasing.c` and `aliasing.fyra` exercise no-alias, exact-alias, forward-overlap,
backward-overlap, and runtime-unknown alias shapes with guard elements included
in the checksum. The vector-enabled Fyra result currently differs from GCC,
Clang, and Fyra's vector-disabled result. The workload is intentionally kept
out of the canonical passing corpus until loop-versioning overlap semantics are
repaired; it is the remaining Gate A blocker.

`mixed_fp.c` and `mixed_fp.fyra` combine an integer induction/control path,
integer-to-FP conversion, FP accumulation/arithmetic, and FP comparison. The
vector-enabled checksum currently differs from the reference modes, so this is
also retained as a Gate A correctness reproducer rather than a canonical row.
