# Fyra Syscall Mapping (Backend-Only)

Fyra no longer exposes syscalls in IR. Syscalls are an implementation detail used by specific backends (for example Linux), behind capability dispatch.

## Important Boundary

- IR-facing API: `extern <capability>(...)`
- Backend-facing implementation: platform calls (syscalls, WinAPI, WASI imports, etc.)

In other words, syscall names and numbers are **not** portable Fyra surface area.

## How Backend Mapping Works

```text
Call { capability: "io.write", args: [...] }
→ dispatchExtern(capability, args)
→ target backend adapter
→ platform primitive (syscall/WinAPI/WASI/etc.)
```

## Backend Responsibilities

Each backend must:
1. Implement the full capability contract.
2. Map capability semantics to target primitives.
3. Preserve deterministic error mapping.
4. Keep calling conventions and ABI guarantees stable for that target.

## Why This Change Exists

Direct syscall exposure in IR creates non-portable coupling to:
- syscall numbers
- target-specific ABIs
- OS naming conventions

The capability-first model keeps the IR portable while still allowing low-level backends to use syscalls internally where appropriate.
