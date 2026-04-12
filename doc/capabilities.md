# Fyra Capability System

Fyra uses a portable capability-based external call system instead of OS-specific syscalls. This makes the IR platform-agnostic and allows the backend to map capabilities to the appropriate platform APIs.

## Extern Syntax

```fyra
extern <capability_name>(<args>) : <return_type>
```

Example:
```fyra
%res = extern io.write(1, %buf, 10) : w
```

## Supported Capabilities

### IO Capability
- `io.write(fd: w, buf: l, len: w) : w`: Write to a stream.
- `io.read(fd: w, buf: l, len: w) : w`: Read from a stream.

### Memory Capability
- `memory.alloc(size: l) : l`: Allocate memory.
- `memory.free(addr: l, size: l)`: Free memory.

### Process Capability
- `process.exit(code: w)`: Terminate the process.

### Randomness Capability
- `random.u64() : l`: Generate a 64-bit random number.

### Time Capability
- `time.now() : l`: Get the current system time.

## Backend Implementation

### Linux (SystemV x64)
- `io.write` -> `sys_write`
- `io.read` -> `sys_read`
- `process.exit` -> `sys_exit`
- `memory.alloc` -> `sys_mmap`
- `memory.free` -> `sys_munmap`
- `random.u64` -> `sys_getrandom`
- `time.now` -> `sys_clock_gettime`

### Windows (x64)
- `io.write` -> `WriteFile`
- `io.read` -> `ReadFile` (to be implemented)
- `process.exit` -> `ExitProcess`
- `memory.alloc` -> `VirtualAlloc`
- `memory.free` -> `VirtualFree`
- `random.u64` -> `BCryptGenRandom`
- `time.now` -> `GetSystemTimeAsFileTime`

### WebAssembly (Wasm32)
- `io.write` -> `wasi_unstable.fd_write`
- `io.read` -> `wasi_unstable.fd_read`
- `process.exit` -> `wasi_unstable.proc_exit`
- `memory.alloc` -> `memory.grow`
